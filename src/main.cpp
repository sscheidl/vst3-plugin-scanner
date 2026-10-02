#include "ReportWriter.h"
#include "ScannerEngine.h"
#include "StringUtil.h"
#include "DuplicateDetector.h"
#include "PluginUserPrefs.h"
#include "VersionUtil.h"

#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <ShlObj.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <atomic>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <cwchar>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Version.lib")

namespace {

constexpr int IDC_VST2_PATH = 1001;
constexpr int IDC_VST3_PATH = 1002;
constexpr int IDC_CUSTOM_PATH = 1003;
constexpr int IDC_OUTPUT_FILE = 1004;
constexpr int IDC_FORMAT = 1005;
constexpr int IDC_START = 1006;
constexpr int IDC_STOP = 1007;
constexpr int IDC_PROGRESS = 1008;
constexpr int IDC_STATUS = 1009;
constexpr int IDC_LOG = 1010;
constexpr int IDC_BROWSE_VST2 = 1011;
constexpr int IDC_BROWSE_VST3 = 1012;
constexpr int IDC_BROWSE_CUSTOM = 1013;
constexpr int IDC_BROWSE_OUTPUT = 1014;
constexpr int IDC_CLAP_PATH = 1015;
constexpr int IDC_AAX_PATH = 1016;
constexpr int IDC_BROWSE_CLAP = 1017;
constexpr int IDC_BROWSE_AAX = 1018;
constexpr int IDC_RESULTS = 1019;
constexpr int IDC_EXPORT = 1020;
constexpr int IDC_SUMMARY = 1021;
constexpr int IDC_CLEAN_VST2_DUP = 1022;
constexpr int IDC_CLEAN_CLAP = 1023;
constexpr int IDC_CLEAN_AAX = 1024;
constexpr int IDC_EDIT_JSON = 1025;
constexpr int IDC_SAVE_OVERRIDES = 1026;
constexpr int IDC_INLINE_EDIT = 1027;

constexpr UINT IDM_OPEN_IN_EXPLORER = 40001;
constexpr UINT IDM_DELETE_SELECTED = 40002;

constexpr wchar_t APP_VERSION[] = L"1.2.0.0";

constexpr UINT WM_SCAN_PROGRESS = WM_APP + 1;
constexpr UINT WM_SCAN_LOG = WM_APP + 2;
constexpr UINT WM_SCAN_DONE = WM_APP + 3;
// Destroying the inline editor from inside its own WM_KILLFOCUS handler is
// fragile, so the window is only hidden there and destroyed from the message
// loop through this message.
constexpr UINT WM_DESTROY_INLINE_EDIT = WM_APP + 4;

struct ProgressMessage {
    std::size_t current = 0;
    std::size_t total = 0;
    std::wstring message;
};

struct DoneMessage {
    bool stopped = false;
    std::wstring fatalError;
    ScanSummary summary;
    std::vector<PluginRecord> records;
};

template <typename T>
void PostOwnedMessage(HWND window, UINT messageId, std::unique_ptr<T> message) {
    if (PostMessageW(window, messageId, 0, reinterpret_cast<LPARAM>(message.get()))) {
        message.release();
    }
}

struct AppState {
    HWND window = nullptr;
    HWND progress = nullptr;
    HWND status = nullptr;
    HWND log = nullptr;
    HWND results = nullptr;
    HWND startButton = nullptr;
    HWND stopButton = nullptr;
    HWND exportButton = nullptr;
    HWND editJsonButton = nullptr;
    HWND saveOverridesButton = nullptr;
    HWND cleanVst2DuplicatesButton = nullptr;
    HWND cleanClapButton = nullptr;
    HWND cleanAaxButton = nullptr;
    HWND summaryLabel = nullptr;
    HWND formatCombo = nullptr;
    std::thread worker;
    std::atomic_bool stopRequested = false;
    std::vector<PluginRecord> records;
    ScanSummary summary;
    int sortColumn = -1;
    bool sortAscending = true;
    bool running = false;
    HWND inlineEdit = nullptr;
    int editRow = -1;
    int editColumn = -1;
    HFONT uiFont = nullptr;
};

HFONT CreateUiFont() {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        return nullptr;
    }
    return CreateFontIndirectW(&metrics.lfMessageFont);
}

BOOL CALLBACK ApplyFontToChild(HWND child, LPARAM font) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
    return TRUE;
}

std::wstring GetWindowTextString(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length + 1), L'\0');
    if (length > 0) {
        GetWindowTextW(control, text.data(), length + 1);
    }
    text.resize(static_cast<std::size_t>(length));
    return text;
}

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

void SetControlText(HWND parent, int id, const std::wstring& text) {
    SetWindowTextW(GetDlgItem(parent, id), text.c_str());
}

void AppendLog(HWND log, const std::wstring& line) {
    const int length = GetWindowTextLengthW(log);
    SendMessageW(log, EM_SETSEL, length, length);
    std::wstring text = line + L"\r\n";
    SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

std::wstring SummaryText(const ScanSummary& summary) {
    return L"VST2: " + std::to_wstring(summary.vst2Count) +
        L" | VST3: " + std::to_wstring(summary.vst3Count) +
        L" | CLAP: " + std::to_wstring(summary.clapCount) +
        L" | AAX: " + std::to_wstring(summary.aaxCount) +
        L" | duplicate groups: " + std::to_wstring(summary.duplicateCount) +
        L" | entries: " + std::to_wstring(summary.duplicateEntryCount) +
        L" | VST2 deletable: " + std::to_wstring(summary.vst2DuplicateCandidateCount) +
        L" | version reliable: " + std::to_wstring(summary.versionDetectedCount) +
        L" | heuristic: " + std::to_wstring(summary.versionHeuristicCount) +
        L" | missing: " + std::to_wstring(summary.versionMissingCount);
}

void UpdateSummaryLabel(AppState& state) {
    if (state.summaryLabel) {
        SetWindowTextW(state.summaryLabel, SummaryText(state.summary).c_str());
    }
}

bool HasManualEdits(const AppState& state) {
    return std::any_of(state.records.begin(), state.records.end(), [](const PluginRecord& record) {
        return record.manuallyEdited;
    });
}

bool IsEditableColumn(int column) {
    return column == 1 || column == 2 || column == 3 || column == 4;
}

void InsertColumn(HWND list, int index, const wchar_t* title, int width) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<wchar_t*>(title);
    column.cx = width;
    column.iSubItem = index;
    ListView_InsertColumn(list, index, &column);
}

void SetListText(HWND list, int row, int column, const std::wstring& text) {
    ListView_SetItemText(list, row, column, const_cast<wchar_t*>(text.c_str()));
}

std::wstring FileSizeText(std::uintmax_t size) {
    return FormatFileSize(size);
}

void PopulateResultsList(HWND list, const std::vector<PluginRecord>& records) {
    ListView_DeleteAllItems(list);
    for (int i = 0; i < static_cast<int>(records.size()); ++i) {
        const auto& record = records[static_cast<std::size_t>(i)];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = const_cast<wchar_t*>(ToDisplayText(record.pluginType));
        ListView_InsertItem(list, &item);

        SetListText(list, i, 1, record.manufacturer);
        SetListText(list, i, 2, record.pluginName);
        SetListText(list, i, 3, record.category);
        SetListText(list, i, 4, record.version);
        SetListText(list, i, 5, ToDisplayText(record.versionSource));
        SetListText(list, i, 6, FileSizeText(record.fileSize));
        SetListText(list, i, 7, record.isPossibleDuplicate ? L"Yes" : L"No");
        SetListText(list, i, 8, ToDisplayText(record));
        SetListText(list, i, 9, record.filePath);
    }
}

std::wstring SortText(const PluginRecord& record, int column) {
    switch (column) {
    case 0:
        return ToDisplayText(record.pluginType);
    case 1:
        return record.manufacturer;
    case 2:
        return record.pluginName;
    case 3:
        return record.category;
    case 4:
        return record.version;
    case 5:
        return ToDisplayText(record.versionSource);
    case 6:
        return std::to_wstring(record.fileSize);
    case 7:
        return record.isPossibleDuplicate ? L"Yes" : L"No";
    case 8:
        return ToDisplayText(record);
    case 9:
        return record.filePath;
    default:
        return {};
    }
}

void SortRecords(AppState& state, int column) {
    if (state.sortColumn == column) {
        state.sortAscending = !state.sortAscending;
    } else {
        state.sortColumn = column;
        state.sortAscending = true;
    }

    std::sort(state.records.begin(), state.records.end(), [&](const PluginRecord& left, const PluginRecord& right) {
        int cmp = 0;
        if (column == 4) {
            const auto versionComparison = CompareVersionStrings(left.version, right.version);
            if (versionComparison) {
                cmp = *versionComparison;
            } else {
                cmp = _wcsicmp(left.version.c_str(), right.version.c_str());
            }
        } else if (column == 6) {
            if (left.fileSize < right.fileSize) {
                cmp = -1;
            } else if (left.fileSize > right.fileSize) {
                cmp = 1;
            }
        } else {
            cmp = _wcsicmp(SortText(left, column).c_str(), SortText(right, column).c_str());
        }
        if (cmp == 0) {
            cmp = _wcsicmp(left.pluginName.c_str(), right.pluginName.c_str());
        }
        return state.sortAscending ? cmp < 0 : cmp > 0;
    });
    PopulateResultsList(state.results, state.records);
}

void FinishInlineEdit(AppState& state, bool commit);

LRESULT CALLBACK InlineEditProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    auto* state = reinterpret_cast<AppState*>(refData);
    switch (message) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            FinishInlineEdit(*state, true);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            FinishInlineEdit(*state, false);
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        if (state && state->inlineEdit == edit) {
            FinishInlineEdit(*state, true);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(edit, InlineEditProc, 1);
        break;
    default:
        break;
    }
    return DefSubclassProc(edit, message, wParam, lParam);
}

void UpdateEditedRecordField(PluginRecord& record, int column, const std::wstring& value) {
    if (column == 1) {
        record.manufacturer = value;
    } else if (column == 2) {
        record.pluginName = value;
    } else if (column == 3) {
        record.category = value;
    } else if (column == 4) {
        record.version = NormalizeVersionString(value);
    }
}

std::wstring EditedRecordField(const PluginRecord& record, int column) {
    if (column == 1) {
        return record.manufacturer;
    }
    if (column == 2) {
        return record.pluginName;
    }
    if (column == 3) {
        return record.category;
    }
    if (column == 4) {
        return record.version;
    }
    return {};
}

void FinishInlineEdit(AppState& state, bool commit) {
    HWND edit = state.inlineEdit;
    if (!edit) {
        return;
    }

    const int row = state.editRow;
    const int column = state.editColumn;
    state.inlineEdit = nullptr;
    state.editRow = -1;
    state.editColumn = -1;

    if (commit &&
        row >= 0 &&
        column >= 0 &&
        row < static_cast<int>(state.records.size()) &&
        IsEditableColumn(column)) {
        const std::wstring value = Trim(GetWindowTextString(edit));
        PluginRecord& record = state.records[static_cast<std::size_t>(row)];
        const std::wstring oldValue = EditedRecordField(record, column);
        if (value != oldValue) {
            UpdateEditedRecordField(record, column, value);
            record.manuallyEdited = true;
            if (column == 4) {
                record.versionManuallyEdited = true;
                record.versionSource = VersionSource::ManualEdit;
            }
            record.metadataFromManualOverrides = false;

            SetListText(state.results, row, 1, record.manufacturer);
            SetListText(state.results, row, 2, record.pluginName);
            SetListText(state.results, row, 3, record.category);
            SetListText(state.results, row, 4, record.version);
            SetListText(state.results, row, 5, ToDisplayText(record.versionSource));
            SetListText(state.results, row, 8, ToDisplayText(record));
            EnableWindow(state.saveOverridesButton, TRUE);
            SetWindowTextW(state.status, L"Manual change applied.");
        }
    }

    // The editor may currently be inside its own window procedure (WM_KILLFOCUS),
    // so it is detached and hidden here and destroyed later from the message loop.
    RemoveWindowSubclass(edit, InlineEditProc, 1);
    ShowWindow(edit, SW_HIDE);
    if (!PostMessageW(state.window, WM_DESTROY_INLINE_EDIT, 0, reinterpret_cast<LPARAM>(edit))) {
        DestroyWindow(edit);
    }
}

void StartInlineEdit(AppState& state, int row, int column) {
    if (!IsEditableColumn(column) ||
        row < 0 ||
        row >= static_cast<int>(state.records.size()) ||
        state.running) {
        return;
    }

    FinishInlineEdit(state, true);

    RECT rect{};
    if (!ListView_GetSubItemRect(state.results, row, column, LVIR_BOUNDS, &rect)) {
        return;
    }

    const std::wstring value = EditedRecordField(state.records[static_cast<std::size_t>(row)], column);
    state.inlineEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        value.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        state.results,
        ControlId(IDC_INLINE_EDIT),
        nullptr,
        nullptr);
    if (!state.inlineEdit) {
        return;
    }

    state.editRow = row;
    state.editColumn = column;
    if (state.uiFont) {
        SendMessageW(state.inlineEdit, WM_SETFONT, reinterpret_cast<WPARAM>(state.uiFont), TRUE);
    }
    SetWindowSubclass(state.inlineEdit, InlineEditProc, 1, reinterpret_cast<DWORD_PTR>(&state));
    SendMessageW(state.inlineEdit, EM_SETSEL, 0, -1);
    SetFocus(state.inlineEdit);
}

HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, nullptr, nullptr);
}

HWND CreateEdit(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           x, y, w, h, parent, ControlId(id), nullptr, nullptr);
}

HWND CreateButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                           x, y, w, h, parent, ControlId(id), nullptr, nullptr);
}

std::wstring BrowseForFolder(HWND owner) {
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    info.lpszTitle = L"Select folder";

    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (!item) {
        return {};
    }

    wchar_t path[MAX_PATH]{};
    SHGetPathFromIDListW(item, path);
    CoTaskMemFree(item);
    return path;
}

std::wstring BrowseForOutputFile(HWND owner, ReportFormat format) {
    wchar_t fileName[MAX_PATH] = L"vst_plugin_report";
    std::wstring extension = DefaultExtensionForFormat(format);
    wcscat_s(fileName, extension.c_str());

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"HTML Report (*.html)\0*.html\0CSV Report (*.csv)\0*.csv\0Text Report (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrDefExt = extension.c_str() + 1;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) {
        return {};
    }
    return fileName;
}

std::filesystem::path ExeDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (length == 0) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

void AddUniqueJsonCandidate(std::vector<std::filesystem::path>& candidates,
                            const std::filesystem::path& directory) {
    if (directory.empty()) {
        return;
    }

    const std::filesystem::path candidate = directory / L"plugin_rules_userprefs.json";
    const std::wstring key = ToLower(candidate.wstring());
    for (const auto& existing : candidates) {
        if (ToLower(existing.wstring()) == key) {
            return;
        }
    }
    candidates.push_back(candidate);
}

std::vector<std::filesystem::path> JsonRuleCandidates() {
    std::vector<std::filesystem::path> candidates;

    const std::filesystem::path exeDir = ExeDirectory();
    AddUniqueJsonCandidate(candidates, exeDir);

    std::error_code ec;
    AddUniqueJsonCandidate(candidates, std::filesystem::current_path(ec));

    const std::filesystem::path configName = exeDir.filename();
    const std::filesystem::path platformName = exeDir.parent_path().filename();
    if ((ToLower(configName.wstring()) == L"debug" || ToLower(configName.wstring()) == L"release") &&
        !platformName.empty()) {
        AddUniqueJsonCandidate(candidates, exeDir.parent_path().parent_path());
    }

    return candidates;
}

std::filesystem::path FindJsonRulesFile() {
    std::error_code ec;
    for (const auto& candidate : JsonRuleCandidates()) {
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
        ec.clear();
    }
    return {};
}

bool WriteJsonTemplate(const std::filesystem::path& path) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream <<
        "{\r\n"
        "  \"schemaVersion\": \"0.3-userprefs\",\r\n"
        "  \"vendorAliases\": {},\r\n"
        "  \"vendorRules\": [],\r\n"
        "  \"pluginRules\": []\r\n"
        "}\r\n";
    return stream.good();
}

std::filesystem::path CreateJsonTemplateFile() {
    const auto candidates = JsonRuleCandidates();
    if (!candidates.empty() && WriteJsonTemplate(candidates.front())) {
        return candidates.front();
    }

    std::error_code ec;
    const std::filesystem::path fallback =
        std::filesystem::current_path(ec) / L"plugin_rules_userprefs.json";
    if (!ec && WriteJsonTemplate(fallback)) {
        return fallback;
    }
    return {};
}

std::wstring QuoteArgument(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

std::filesystem::path FindEditorExecutable() {
    const std::filesystem::path candidates[] = {
        L"C:\\Program Files\\Notepad++\\notepad++.exe",
        L"C:\\Program Files (x86)\\Notepad++\\notepad++.exe",
    };

    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
        ec.clear();
    }

    wchar_t pathBuffer[MAX_PATH]{};
    const DWORD pathLength = SearchPathW(nullptr, L"notepad++.exe", nullptr, MAX_PATH, pathBuffer, nullptr);
    if (pathLength > 0 && pathLength < MAX_PATH) {
        return pathBuffer;
    }

    return L"notepad.exe";
}

bool LaunchEditor(const std::filesystem::path& editor,
                  const std::filesystem::path& jsonPath,
                  HWND owner) {
    const std::wstring parameters = QuoteArgument(jsonPath);
    const HINSTANCE result = ShellExecuteW(
        owner,
        L"open",
        editor.c_str(),
        parameters.c_str(),
        nullptr,
        SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

ReportFormat SelectedFormat(HWND combo) {
    const LRESULT selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selected == 1) {
        return ReportFormat::Csv;
    }
    if (selected == 2) {
        return ReportFormat::Txt;
    }
    return ReportFormat::Html;
}

std::wstring EnsureOutputExtension(std::wstring path, ReportFormat format) {
    if (Trim(path).empty()) {
        path = std::wstring(L"vst_plugin_report") + DefaultExtensionForFormat(format);
    }
    std::filesystem::path fsPath(path);
    if (!fsPath.has_extension()) {
        fsPath += DefaultExtensionForFormat(format);
    }
    return fsPath.wstring();
}

void SetRunningState(AppState& state, bool running) {
    state.running = running;
    EnableWindow(state.startButton, running ? FALSE : TRUE);
    EnableWindow(state.stopButton, running ? TRUE : FALSE);
    EnableWindow(state.exportButton, (!running && !state.records.empty()) ? TRUE : FALSE);
    EnableWindow(state.editJsonButton, TRUE);
    EnableWindow(state.saveOverridesButton, (!running && HasManualEdits(state)) ? TRUE : FALSE);
    EnableWindow(state.cleanVst2DuplicatesButton, (!running && !state.records.empty()) ? TRUE : FALSE);
    EnableWindow(state.cleanClapButton, (!running && !state.records.empty()) ? TRUE : FALSE);
    EnableWindow(state.cleanAaxButton, (!running && !state.records.empty()) ? TRUE : FALSE);
}

void LayoutControls(HWND window, AppState& state) {
    RECT rect{};
    GetClientRect(window, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int margin = 16;
    const int browseWidth = 90;
    const int labelWidth = 110;
    const int rowHeight = 26;
    const int editX = margin + labelWidth + 4;
    const int browseX = width - margin - browseWidth;
    const int editWidth = std::max(120, browseX - editX - 10);

    const int rows[][2] = {
        {IDC_VST2_PATH, IDC_BROWSE_VST2},
        {IDC_VST3_PATH, IDC_BROWSE_VST3},
        {IDC_CLAP_PATH, IDC_BROWSE_CLAP},
        {IDC_AAX_PATH, IDC_BROWSE_AAX},
        {IDC_CUSTOM_PATH, IDC_BROWSE_CUSTOM},
    };
    const int yBase = 16;
    for (int i = 0; i < 5; ++i) {
        const int y = yBase + i * 36;
        MoveWindow(GetDlgItem(window, rows[i][0]), editX, y, editWidth, 24, TRUE);
        MoveWindow(GetDlgItem(window, rows[i][1]), browseX, y - 1, browseWidth, rowHeight, TRUE);
    }

    const int outputY = 196;
    MoveWindow(state.formatCombo, editX, outputY, 160, 120, TRUE);
    const int outputLabelX = editX + 180;
    const int editJsonWidth = 90;
    const int editJsonX = browseX - editJsonWidth - 10;
    MoveWindow(GetDlgItem(window, IDC_OUTPUT_FILE), outputLabelX + 100, outputY, std::max(160, editJsonX - outputLabelX - 110), 24, TRUE);
    MoveWindow(state.editJsonButton, editJsonX, outputY - 1, editJsonWidth, rowHeight, TRUE);
    MoveWindow(GetDlgItem(window, IDC_BROWSE_OUTPUT), browseX, outputY - 1, browseWidth, rowHeight, TRUE);

    const int actionY = 236;
    MoveWindow(state.startButton, margin, actionY, 110, 32, TRUE);
    MoveWindow(state.stopButton, margin + 120, actionY, 110, 32, TRUE);
    MoveWindow(state.exportButton, margin + 240, actionY, 110, 32, TRUE);
    MoveWindow(state.saveOverridesButton, margin + 360, actionY, 145, 32, TRUE);
    MoveWindow(state.cleanVst2DuplicatesButton, margin + 515, actionY, 120, 32, TRUE);
    MoveWindow(state.cleanClapButton, margin + 645, actionY, 85, 32, TRUE);
    MoveWindow(state.cleanAaxButton, margin + 740, actionY, 85, 32, TRUE);
    MoveWindow(state.progress, margin + 835, actionY + 5, std::max(0, width - margin * 2 - 835), 22, TRUE);
    MoveWindow(state.summaryLabel, margin, 276, width - margin * 2, 24, TRUE);
    MoveWindow(state.status, margin, 300, width - margin * 2, 24, TRUE);

    const int resultsY = 330;
    const int logHeight = 100;
    const int logY = std::max(resultsY + 140, height - margin - logHeight);
    MoveWindow(state.results, margin, resultsY, width - margin * 2, std::max(120, logY - resultsY - 10), TRUE);
    MoveWindow(state.log, margin, logY, width - margin * 2, logHeight, TRUE);
}

std::filesystem::path JsonRulesSavePath();

void StartScan(AppState& state) {
    if (state.running) {
        return;
    }
    if (state.worker.joinable()) {
        state.worker.join();
    }

    // The scan must apply exactly the file that "Edit JSON" and "Save overrides"
    // operate on, otherwise a run started from a different working directory reads
    // a different rules file than the GUI writes.
    const std::filesystem::path rulesPath = JsonRulesSavePath();
    const ScanOptions options{
        GetWindowTextString(GetDlgItem(state.window, IDC_VST2_PATH)),
        GetWindowTextString(GetDlgItem(state.window, IDC_VST3_PATH)),
        GetWindowTextString(GetDlgItem(state.window, IDC_CLAP_PATH)),
        GetWindowTextString(GetDlgItem(state.window, IDC_AAX_PATH)),
        GetWindowTextString(GetDlgItem(state.window, IDC_CUSTOM_PATH)),
        rulesPath.wstring(),
    };

    SendMessageW(state.progress, PBM_SETPOS, 0, 0);
    FinishInlineEdit(state, false);
    ListView_DeleteAllItems(state.results);
    state.records.clear();
    state.summary = ScanSummary{};
    state.sortColumn = -1;
    UpdateSummaryLabel(state);
    EnableWindow(state.exportButton, FALSE);
    EnableWindow(state.saveOverridesButton, FALSE);
    SetWindowTextW(state.status, L"Starting scan...");
    SetWindowTextW(state.log, L"");
    state.stopRequested.store(false);
    SetRunningState(state, true);

    HWND window = state.window;
    std::atomic_bool* stopFlag = &state.stopRequested;
    try {
        state.worker = std::thread([window, stopFlag, options]() {
            auto done = std::make_unique<DoneMessage>();
            try {
                ScannerEngine engine;
                ScanResult result = engine.Scan(
                    options,
                    *stopFlag,
                    [window](const ScanProgress& progress) {
                        PostOwnedMessage(window, WM_SCAN_PROGRESS,
                            std::make_unique<ProgressMessage>(ProgressMessage{
                                progress.current, progress.total, progress.message }));
                    },
                    [window](const std::wstring& line) {
                        PostOwnedMessage(window, WM_SCAN_LOG, std::make_unique<std::wstring>(line));
                    });
                done->summary = std::move(result.summary);
                done->records = std::move(result.records);
            } catch (const std::exception& ex) {
                done->fatalError = L"Unexpected scan error: " + Utf8ToWide(ex.what());
            } catch (...) {
                done->fatalError = L"Unexpected unknown scan error.";
            }
            done->stopped = stopFlag->load();
            PostOwnedMessage(window, WM_SCAN_DONE, std::move(done));
        });
    } catch (const std::system_error& ex) {
        SetRunningState(state, false);
        const std::wstring error = L"Scan thread could not be started: " + Utf8ToWide(ex.what());
        SetWindowTextW(state.status, L"Scan could not be started.");
        AppendLog(state.log, error);
        MessageBoxW(state.window, error.c_str(), L"Scan error", MB_OK | MB_ICONERROR);
    }
}

void ExportResults(AppState& state) {
    if (state.running || state.records.empty()) {
        return;
    }
    FinishInlineEdit(state, true);

    const ReportFormat format = SelectedFormat(state.formatCombo);
    const std::wstring outputPath = EnsureOutputExtension(
        GetWindowTextString(GetDlgItem(state.window, IDC_OUTPUT_FILE)),
        format);
    SetControlText(state.window, IDC_OUTPUT_FILE, outputPath);

    ReportWriter writer;
    std::wstring error;
    if (writer.Write(outputPath, format, state.records, state.summary, error)) {
        SetWindowTextW(state.status, L"Export complete.");
        AppendLog(state.log, L"Export: " + outputPath);
    } else {
        SetWindowTextW(state.status, L"Export failed.");
        AppendLog(state.log, L"Error: " + error);
        MessageBoxW(state.window, error.c_str(), L"Export failed", MB_OK | MB_ICONERROR);
    }
}

void EditJsonRules(AppState& state) {
    std::filesystem::path jsonPath = FindJsonRulesFile();
    bool created = false;

    if (jsonPath.empty()) {
        const int answer = MessageBoxW(
            state.window,
            L"plugin_rules_userprefs.json was not found. Create a new template?",
            L"Edit JSON",
            MB_YESNO | MB_ICONQUESTION);
        if (answer != IDYES) {
            return;
        }

        jsonPath = CreateJsonTemplateFile();
        if (jsonPath.empty()) {
            SetWindowTextW(state.status, L"JSON template could not be created.");
            AppendLog(state.log, L"editor launch failed: plugin_rules_userprefs.json could not be created");
            return;
        }
        created = true;
        AppendLog(state.log, L"plugin_rules_userprefs.json created: " + jsonPath.wstring());
    }

    const std::filesystem::path editor = FindEditorExecutable();
    if (LaunchEditor(editor, jsonPath, state.window) ||
        (ToLower(editor.filename().wstring()) != L"notepad.exe" &&
         LaunchEditor(L"notepad.exe", jsonPath, state.window))) {
        SetWindowTextW(state.status, created ? L"JSON template created and opened." : L"JSON file opened.");
        AppendLog(state.log, L"plugin_rules_userprefs.json opened: " + jsonPath.wstring());
        return;
    }

    SetWindowTextW(state.status, L"Editor could not be started.");
    AppendLog(state.log, L"editor launch failed: " + jsonPath.wstring());
}

std::filesystem::path JsonRulesSavePath() {
    std::filesystem::path jsonPath = FindJsonRulesFile();
    if (!jsonPath.empty()) {
        return jsonPath;
    }

    const auto candidates = JsonRuleCandidates();
    if (!candidates.empty()) {
        return candidates.front();
    }
    return std::filesystem::path(L"plugin_rules_userprefs.json");
}

void SaveOverrides(AppState& state) {
    FinishInlineEdit(state, true);
    if (!HasManualEdits(state)) {
        SetWindowTextW(state.status, L"No manual overrides to save.");
        AppendLog(state.log, L"No manual overrides to save.");
        return;
    }

    const std::filesystem::path jsonPath = JsonRulesSavePath();
    const SaveManualOverridesResult result = SaveManualOverrides(jsonPath, state.records);
    if (!result.success) {
        SetWindowTextW(state.status, L"Overrides could not be saved.");
        AppendLog(state.log, L"Saving overrides failed: " + result.errorMessage);
        MessageBoxW(state.window, result.errorMessage.c_str(), L"Save overrides", MB_OK | MB_ICONERROR);
        return;
    }

    SetWindowTextW(state.status, L"Overrides saved.");
    AppendLog(state.log, L"Overrides saved: " + jsonPath.wstring());
    if (!result.backupPath.empty()) {
        AppendLog(state.log, L"Backup: " + result.backupPath.wstring());
    }
}

// SHFileOperation with FOF_ALLOWUNDO silently falls back to a permanent delete
// when an item cannot be recycled (UNC paths, disabled Recycle Bin, items larger
// than the bin quota). IFileOperation with FOFX_RECYCLEONDELETE fails instead of
// destroying the file, which is what the documented behaviour promises.
bool MovePathToRecycleBin(HWND owner, const std::wstring& path, std::wstring& error) {
    IFileOperation* operation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    if (FAILED(hr) || !operation) {
        error = L"Delete operation could not be created: " + path;
        return false;
    }

    IShellItem* item = nullptr;
    hr = operation->SetOperationFlags(static_cast<DWORD>(
        FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT |
        FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD));
    if (SUCCEEDED(hr)) {
        hr = operation->SetOwnerWindow(owner);
    }
    if (SUCCEEDED(hr)) {
        hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
    }
    if (SUCCEEDED(hr)) {
        hr = operation->DeleteItem(item, nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = operation->PerformOperations();
    }

    BOOL aborted = FALSE;
    bool succeeded = SUCCEEDED(hr) &&
        SUCCEEDED(operation->GetAnyOperationsAborted(&aborted)) && !aborted;

    if (item) {
        item->Release();
    }
    operation->Release();

    if (!succeeded) {
        error = L"Path could not be moved to the Recycle Bin: " + path;
    }
    return succeeded;
}

void RefreshAfterDeletion(AppState& state) {
    DuplicateDetector detector;
    detector.MarkDuplicates(state.records);
    state.summary = BuildSummary(state.records, state.summary.scannedPaths, state.summary.scanTimestamp);
    PopulateResultsList(state.results, state.records);
    UpdateSummaryLabel(state);
    SetRunningState(state, false);
}

void OpenSelectedInExplorer(AppState& state) {
    const int selected = ListView_GetNextItem(state.results, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= static_cast<int>(state.records.size())) {
        return;
    }
    const std::wstring args = L"/select,\"" + state.records[static_cast<std::size_t>(selected)].filePath + L"\"";
    ShellExecuteW(state.window, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void DeleteSelectedRecord(AppState& state) {
    const int selected = ListView_GetNextItem(state.results, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= static_cast<int>(state.records.size())) {
        return;
    }

    const auto index = static_cast<std::size_t>(selected);
    const std::wstring message =
        L"Move this entry to the Recycle Bin?\n\n" +
        state.records[index].filePath;
    if (MessageBoxW(state.window, message.c_str(), L"Delete file", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    std::wstring error;
    if (MovePathToRecycleBin(state.window, state.records[index].filePath, error)) {
        AppendLog(state.log, L"Deleted: " + state.records[index].filePath);
        state.records.erase(state.records.begin() + static_cast<std::ptrdiff_t>(index));
        RefreshAfterDeletion(state);
    } else {
        AppendLog(state.log, L"Error: " + error);
        MessageBoxW(state.window, error.c_str(), L"Delete failed", MB_OK | MB_ICONERROR);
    }
}

template <typename Predicate>
void DeleteMatchingRecords(AppState& state, const std::wstring& label, Predicate predicate) {
    std::vector<std::size_t> indexes;
    for (std::size_t i = 0; i < state.records.size(); ++i) {
        if (predicate(state.records[i])) {
            indexes.push_back(i);
        }
    }

    if (indexes.empty()) {
        MessageBoxW(state.window, L"No matching entries found.", label.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring message =
        L"Move " + std::to_wstring(indexes.size()) +
        L" entries to the Recycle Bin?\n\nVST3 files are not deleted by this action.";
    if (MessageBoxW(state.window, message.c_str(), label.c_str(), MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    std::size_t deleted = 0;
    for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) {
        std::wstring error;
        const auto index = *it;
        if (MovePathToRecycleBin(state.window, state.records[index].filePath, error)) {
            AppendLog(state.log, L"Deleted: " + state.records[index].filePath);
            state.records.erase(state.records.begin() + static_cast<std::ptrdiff_t>(index));
            ++deleted;
        } else {
            AppendLog(state.log, L"Error: " + error);
        }
    }

    RefreshAfterDeletion(state);
    SetWindowTextW(state.status, (label + L": " + std::to_wstring(deleted) + L" entries deleted.").c_str());
}

void DeleteVst2Duplicates(AppState& state) {
    DeleteMatchingRecords(state, L"Delete VST2 duplicates", [](const PluginRecord& record) {
        return record.pluginType == PluginType::Vst2 && record.isPossibleDuplicate;
    });
}

void DeleteClapRecords(AppState& state) {
    DeleteMatchingRecords(state, L"Delete CLAP", [](const PluginRecord& record) {
        return record.pluginType == PluginType::Clap;
    });
}

void DeleteAaxRecords(AppState& state) {
    DeleteMatchingRecords(state, L"Delete AAX", [](const PluginRecord& record) {
        return record.pluginType == PluginType::Aax;
    });
}

void StopScan(AppState& state) {
    if (!state.running) {
        return;
    }
    state.stopRequested.store(true);
    SetWindowTextW(state.status, L"Stop requested...");
    AppendLog(state.log, L"Stop requested. The current file access is finished cleanly first.");
}

void CreateMainControls(HWND window, AppState& state) {
    CreateLabel(window, L"VST2 path", 16, 18, 110, 22);
    CreateEdit(window, IDC_VST2_PATH, L"C:\\Program Files\\Vstplugins", 130, 16, 600, 24);
    CreateButton(window, IDC_BROWSE_VST2, L"Browse", 740, 15, 90, 26);

    CreateLabel(window, L"VST3 path", 16, 54, 110, 22);
    CreateEdit(window, IDC_VST3_PATH, L"C:\\Program Files\\Common Files\\VST3", 130, 52, 600, 24);
    CreateButton(window, IDC_BROWSE_VST3, L"Browse", 740, 51, 90, 26);

    CreateLabel(window, L"CLAP path", 16, 90, 110, 22);
    CreateEdit(window, IDC_CLAP_PATH, L"C:\\Program Files\\Common Files\\CLAP", 130, 88, 600, 24);
    CreateButton(window, IDC_BROWSE_CLAP, L"Browse", 740, 87, 90, 26);

    CreateLabel(window, L"AAX path", 16, 126, 110, 22);
    CreateEdit(window, IDC_AAX_PATH, L"C:\\Program Files\\Common Files\\Avid\\Audio\\Plug-Ins", 130, 124, 600, 24);
    CreateButton(window, IDC_BROWSE_AAX, L"Browse", 740, 123, 90, 26);

    CreateLabel(window, L"Custom path", 16, 162, 110, 22);
    CreateEdit(window, IDC_CUSTOM_PATH, L"", 130, 160, 600, 24);
    CreateButton(window, IDC_BROWSE_CUSTOM, L"Browse", 740, 159, 90, 26);

    CreateLabel(window, L"Output format", 16, 200, 110, 22);
    state.formatCombo = CreateWindowExW(0, WC_COMBOBOXW, L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                        130, 196, 160, 120, window, ControlId(IDC_FORMAT), nullptr, nullptr);
    SendMessageW(state.formatCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"HTML"));
    SendMessageW(state.formatCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"CSV"));
    SendMessageW(state.formatCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TXT"));
    SendMessageW(state.formatCombo, CB_SETCURSEL, 0, 0);

    CreateLabel(window, L"Output file", 310, 200, 100, 22);
    CreateEdit(window, IDC_OUTPUT_FILE, L"vst_plugin_report.html", 410, 196, 320, 24);
    state.editJsonButton = CreateButton(window, IDC_EDIT_JSON, L"Edit JSON", 640, 195, 90, 26);
    CreateButton(window, IDC_BROWSE_OUTPUT, L"Browse", 740, 195, 90, 26);
    state.summaryLabel = CreateLabel(window, L"VST2: 0 | VST3: 0 | CLAP: 0 | AAX: 0 | duplicate groups: 0 | entries: 0 | VST2 deletable: 0", 16, 276, 760, 22);

    state.startButton = CreateButton(window, IDC_START, L"Start Scan", 16, 236, 110, 32);
    state.stopButton = CreateButton(window, IDC_STOP, L"Stop Scan", 136, 236, 110, 32);
    state.exportButton = CreateButton(window, IDC_EXPORT, L"Export", 256, 236, 110, 32);
    // Creation coordinates mirror LayoutControls so that the window is correct even
    // before the first layout pass.
    state.saveOverridesButton = CreateButton(window, IDC_SAVE_OVERRIDES, L"Save overrides", 376, 236, 145, 32);
    state.cleanVst2DuplicatesButton = CreateButton(window, IDC_CLEAN_VST2_DUP, L"Del VST2 Dup", 531, 236, 120, 32);
    state.cleanClapButton = CreateButton(window, IDC_CLEAN_CLAP, L"Del CLAP", 661, 236, 85, 32);
    state.cleanAaxButton = CreateButton(window, IDC_CLEAN_AAX, L"Del AAX", 756, 236, 85, 32);
    EnableWindow(state.stopButton, FALSE);
    EnableWindow(state.exportButton, FALSE);
    EnableWindow(state.saveOverridesButton, FALSE);
    EnableWindow(state.cleanVst2DuplicatesButton, FALSE);
    EnableWindow(state.cleanClapButton, FALSE);
    EnableWindow(state.cleanAaxButton, FALSE);

    state.progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                     WS_CHILD | WS_VISIBLE,
                                     260, 241, 570, 22, window, ControlId(IDC_PROGRESS), nullptr, nullptr);
    SendMessageW(state.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    state.status = CreateWindowExW(0, L"STATIC", L"Ready.",
                                   WS_CHILD | WS_VISIBLE,
                                   16, 282, 814, 24, window, ControlId(IDC_STATUS), nullptr, nullptr);

    state.results = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
                                    16, 310, 814, 220, window, ControlId(IDC_RESULTS), nullptr, nullptr);
    ListView_SetExtendedListViewStyle(state.results, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    InsertColumn(state.results, 0, L"Type", 56);
    InsertColumn(state.results, 1, L"Manufacturer", 140);
    InsertColumn(state.results, 2, L"Plugin", 170);
    InsertColumn(state.results, 3, L"Category", 110);
    InsertColumn(state.results, 4, L"Version", 90);
    InsertColumn(state.results, 5, L"Version source", 165);
    InsertColumn(state.results, 6, L"Size", 90);
    InsertColumn(state.results, 7, L"Duplicate", 80);
    InsertColumn(state.results, 8, L"Status", 170);
    InsertColumn(state.results, 9, L"Path", 520);

    state.log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                16, 540, 814, 100, window, ControlId(IDC_LOG), nullptr, nullptr);
    SetWindowTextW(window, (std::wstring(L"Windows VST Plugin Scanner ") + APP_VERSION).c_str());
    state.uiFont = CreateUiFont();
    if (state.uiFont) {
        EnumChildWindows(window, ApplyFontToChild, reinterpret_cast<LPARAM>(state.uiFont));
    }
    LayoutControls(window, state);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<AppState*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        CreateMainControls(window, *state);
        return 0;
    }
    case WM_COMMAND:
        if (!state) {
            break;
        }
        switch (LOWORD(wParam)) {
        case IDC_START:
            StartScan(*state);
            return 0;
        case IDC_STOP:
            StopScan(*state);
            return 0;
        case IDC_EXPORT:
            ExportResults(*state);
            return 0;
        case IDC_EDIT_JSON:
            EditJsonRules(*state);
            return 0;
        case IDC_SAVE_OVERRIDES:
            SaveOverrides(*state);
            return 0;
        case IDC_CLEAN_VST2_DUP:
            DeleteVst2Duplicates(*state);
            return 0;
        case IDC_CLEAN_CLAP:
            DeleteClapRecords(*state);
            return 0;
        case IDC_CLEAN_AAX:
            DeleteAaxRecords(*state);
            return 0;
        case IDC_BROWSE_VST2:
        case IDC_BROWSE_VST3:
        case IDC_BROWSE_CLAP:
        case IDC_BROWSE_AAX:
        case IDC_BROWSE_CUSTOM: {
            const std::wstring folder = BrowseForFolder(window);
            if (!folder.empty()) {
                int target = IDC_CUSTOM_PATH;
                if (LOWORD(wParam) == IDC_BROWSE_VST2) {
                    target = IDC_VST2_PATH;
                } else if (LOWORD(wParam) == IDC_BROWSE_VST3) {
                    target = IDC_VST3_PATH;
                } else if (LOWORD(wParam) == IDC_BROWSE_CLAP) {
                    target = IDC_CLAP_PATH;
                } else if (LOWORD(wParam) == IDC_BROWSE_AAX) {
                    target = IDC_AAX_PATH;
                }
                SetControlText(window, target, folder);
            }
            return 0;
        }
        case IDC_BROWSE_OUTPUT: {
            const std::wstring file = BrowseForOutputFile(window, SelectedFormat(state->formatCombo));
            if (!file.empty()) {
                SetControlText(window, IDC_OUTPUT_FILE, file);
            }
            return 0;
        }
        case IDC_FORMAT:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                const ReportFormat format = SelectedFormat(state->formatCombo);
                SetControlText(window, IDC_OUTPUT_FILE, std::wstring(L"vst_plugin_report") + DefaultExtensionForFormat(format));
            }
            return 0;
        default:
            break;
        }
        break;
    case WM_CONTEXTMENU:
        if (state && reinterpret_cast<HWND>(wParam) == state->results) {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (point.x == -1 && point.y == -1) {
                const int selected = ListView_GetNextItem(state->results, -1, LVNI_SELECTED);
                if (selected < 0) {
                    return 0;
                }
                RECT itemRect{};
                ListView_GetItemRect(state->results, selected, &itemRect, LVIR_BOUNDS);
                point.x = itemRect.left;
                point.y = itemRect.bottom;
                ClientToScreen(state->results, &point);
            }

            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, IDM_OPEN_IN_EXPLORER, L"Show target path in Explorer");
            AppendMenuW(menu, MF_STRING, IDM_DELETE_SELECTED, L"Delete file/bundle");
            const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
            DestroyMenu(menu);

            if (command == IDM_OPEN_IN_EXPLORER) {
                OpenSelectedInExplorer(*state);
            } else if (command == IDM_DELETE_SELECTED) {
                DeleteSelectedRecord(*state);
            }
            return 0;
        }
        break;
    case WM_NOTIFY:
        if (state) {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header && header->idFrom == IDC_RESULTS && header->code == LVN_COLUMNCLICK) {
                FinishInlineEdit(*state, true);
                auto* listInfo = reinterpret_cast<NMLISTVIEW*>(lParam);
                SortRecords(*state, listInfo->iSubItem);
                return 0;
            }
            if (header && header->idFrom == IDC_RESULTS && header->code == NM_DBLCLK) {
                auto* itemInfo = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                StartInlineEdit(*state, itemInfo->iItem, itemInfo->iSubItem);
                return 0;
            }
        }
        break;
    case WM_SCAN_PROGRESS: {
        std::unique_ptr<ProgressMessage> progress(reinterpret_cast<ProgressMessage*>(lParam));
        if (state && progress) {
            int percent = 0;
            if (progress->total > 0) {
                percent = static_cast<int>((progress->current * 100) / progress->total);
            }
            SendMessageW(state->progress, PBM_SETPOS, percent, 0);
            SetWindowTextW(state->status, progress->message.c_str());
        }
        return 0;
    }
    case WM_SCAN_LOG: {
        std::unique_ptr<std::wstring> line(reinterpret_cast<std::wstring*>(lParam));
        if (state && line) {
            AppendLog(state->log, *line);
        }
        return 0;
    }
    case WM_SCAN_DONE: {
        std::unique_ptr<DoneMessage> done(reinterpret_cast<DoneMessage*>(lParam));
        if (state && done) {
            if (state->worker.joinable()) {
                state->worker.join();
            }
            SetRunningState(*state, false);
            state->summary = std::move(done->summary);
            state->records = std::move(done->records);
            PopulateResultsList(state->results, state->records);
            UpdateSummaryLabel(*state);
            if (!done->fatalError.empty()) {
                SetWindowTextW(state->status, L"Scan finished with an error.");
                AppendLog(state->log, done->fatalError);
                MessageBoxW(window, done->fatalError.c_str(), L"Scan error", MB_OK | MB_ICONERROR);
            } else if (done->stopped) {
                SetWindowTextW(state->status, L"Scan cancelled. Results can still be exported.");
                AppendLog(state->log, L"Scan cancelled.");
            } else {
                SendMessageW(state->progress, PBM_SETPOS, 0, 0);
                SetWindowTextW(state->status, L"Scan complete. Click Export to write a report.");
                AppendLog(state->log, L"Scan complete.");
            }
            EnableWindow(state->exportButton, state->records.empty() ? FALSE : TRUE);
        }
        return 0;
    }
    case WM_SIZE:
        if (state && state->results) {
            LayoutControls(window, *state);
        }
        return 0;
    case WM_CLOSE:
        if (state) {
            FinishInlineEdit(*state, true);
        }
        if (state && state->running) {
            state->stopRequested.store(true);
            MessageBoxW(window, L"A scan is still running. Please wait until the worker has finished.", L"Scan running", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY_INLINE_EDIT: {
        auto edit = reinterpret_cast<HWND>(lParam);
        // Handle values can be recycled, so confirm this really is our editor
        // before destroying anything.
        if (state && edit && IsWindow(edit) && GetParent(edit) == state->results &&
            GetDlgCtrlID(edit) == IDC_INLINE_EDIT) {
            DestroyWindow(edit);
        }
        return 0;
    }
    case WM_DESTROY:
        if (state) {
            state->stopRequested.store(true);
            if (state->worker.joinable()) {
                state->worker.join();
            }
            // The worker allocates every notification on the heap. Anything still
            // queued after the join would never be dispatched, so free it here.
            MSG pending{};
            while (PeekMessageW(&pending, window, WM_SCAN_PROGRESS, WM_SCAN_DONE, PM_REMOVE)) {
                if (pending.message == WM_SCAN_PROGRESS) {
                    delete reinterpret_cast<ProgressMessage*>(pending.lParam);
                } else if (pending.message == WM_SCAN_LOG) {
                    delete reinterpret_cast<std::wstring*>(pending.lParam);
                } else if (pending.message == WM_SCAN_DONE) {
                    delete reinterpret_cast<DoneMessage*>(pending.lParam);
                }
            }
            if (state->uiFont) {
                DeleteObject(state->uiFont);
                state->uiFont = nullptr;
            }
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);

    OleInitialize(nullptr);

    const wchar_t className[] = L"VstPluginScannerWindow";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = className;
    RegisterClassExW(&windowClass);

    AppState state;
    const std::wstring windowTitle = L"Windows VST Plugin Scanner " + std::wstring(APP_VERSION);
    HWND window = CreateWindowExW(
        0,
        className,
        windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        870,
        710,
        nullptr,
        nullptr,
        instance,
        &state);

    if (!window) {
        OleUninitialize();
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    OleUninitialize();
    return static_cast<int>(message.wParam);
}
