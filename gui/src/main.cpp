#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>

#include "InventoryModel.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"Vst3ProbeGuiWindow";
constexpr wchar_t kWindowTitle[] = L"VST3 Plugin Scanner 2.3.0";
constexpr UINT kProbeDoneMessage = WM_APP + 1;
constexpr UINT kProgressMessage = WM_APP + 3;
constexpr DWORD kInitialProbeTimeoutMs = 15'000;
constexpr DWORD kRetryProbeTimeoutMs = 30'000;
constexpr DWORD kStoppedExitCode = 1223;
constexpr std::size_t kMaximumCapturedBytes = 8U * 1024U * 1024U;
constexpr std::size_t kMaximumCacheBytes = 64U * 1024U * 1024U;

enum ControlId : int {
    PathEdit = 1001,
    FileButton,
    RunButton,
    StopButton,
    OutputEdit,
    StatusLabel,
    ScanFolderButton = 1008,
    ExportCsvButton,
    ExportJsonButton,
    CacheCheckbox,
};

enum class ScanMode {
    SingleModule,
    Folder,
};

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() { Reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            Reset(std::exchange(other.handle_, nullptr));
        }
        return *this;
    }

    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    void Reset(HANDLE replacement = nullptr) noexcept {
        if (*this) {
            CloseHandle(handle_);
        }
        handle_ = replacement;
    }

private:
    HANDLE handle_ = nullptr;
};

struct ProbeRunResult {
    std::wstring status;
    std::wstring output;
    std::string jsonOutput;
    std::string stderrOutput;
    std::string protocolStatus;
    DWORD exitCode = 0;
    bool timedOut = false;
    bool stopped = false;
    bool retried = false;
    bool fromCache = false;
    bool processCompleted = false;
    bool terminationIncomplete = false;
    std::size_t cacheHits = 0;
    std::vector<vst3scanner::InventoryRecord> inventory;
    std::vector<vst3scanner::ScanIssue> issues;
};

struct ProbeCache {
    std::unordered_map<std::string, std::string> entries;
    bool dirty = false;
};

enum class CacheLoadResult {
    Missing,
    Loaded,
    Invalid,
};

struct AppState {
    HWND pathLabel = nullptr;
    HWND pathEdit = nullptr;
    HWND fileButton = nullptr;
    HWND scanFolderButton = nullptr;
    HWND runButton = nullptr;
    HWND stopButton = nullptr;
    HWND outputLabel = nullptr;
    HWND cacheCheckbox = nullptr;
    HWND resultList = nullptr;
    HWND exportCsvButton = nullptr;
    HWND exportJsonButton = nullptr;
    HWND statusLabel = nullptr;
    HFONT uiFont = nullptr;
    UINT dpi = 96;
    ScanMode scanMode = ScanMode::SingleModule;

    std::thread worker;
    std::atomic_bool stopRequested = false;
    std::atomic_bool closing = false;
    std::mutex processMutex;
    HANDLE activeJob = nullptr;
    std::mutex resultMutex;
    ProbeRunResult result;
    std::wstring progressStatus;
    std::vector<vst3scanner::InventoryRecord> displayedInventory;
    std::vector<vst3scanner::ScanIssue> displayedIssues;
    int sortColumn = -1;
    bool sortAscending = true;
};

class ActiveJobRegistration {
public:
    ActiveJobRegistration(AppState& state, HANDLE job) : state_(state), job_(job) {
        std::lock_guard lock(state_.processMutex);
        state_.activeJob = job_;
    }

    ~ActiveJobRegistration() {
        std::lock_guard lock(state_.processMutex);
        if (state_.activeJob == job_) {
            state_.activeJob = nullptr;
        }
    }

    ActiveJobRegistration(const ActiveJobRegistration&) = delete;
    ActiveJobRegistration& operator=(const ActiveJobRegistration&) = delete;

private:
    AppState& state_;
    HANDLE job_;
};

[[nodiscard]] AppState* GetState(HWND window) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

[[nodiscard]] int Scale(const AppState& state, int value) {
    return MulDiv(value, static_cast<int>(state.dpi), 96);
}

[[nodiscard]] std::wstring FormatSystemError(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);
    std::wstring message = length != 0 && buffer != nullptr ? std::wstring(buffer, length)
                                                              : L"Unbekannter Windows-Fehler";
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }
    return message;
}

[[nodiscard]] bool IsCrashExitCode(DWORD exitCode) noexcept {
    return (exitCode & 0xC0000000U) == 0xC0000000U;
}

[[nodiscard]] std::wstring FormatExitCode(DWORD exitCode) {
    std::wostringstream output;
    output << L"0x" << std::uppercase << std::hex << std::setfill(L'0') << std::setw(8)
           << exitCode;
    return output.str();
}

[[nodiscard]] std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                     static_cast<int>(value.size()), nullptr, 0);
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (length == 0) {
        flags = 0;
        length = MultiByteToWideChar(CP_UTF8, flags, value.data(),
                                     static_cast<int>(value.size()), nullptr, 0);
    }
    if (length == 0) {
        return L"[UTF-8-Ausgabe konnte nicht dekodiert werden]";
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, flags, value.data(), static_cast<int>(value.size()),
                        result.data(), length);
    return result;
}

[[nodiscard]] std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

[[nodiscard]] std::string PrettyPrintJson(const std::string& json) {
    std::string result;
    result.reserve(json.size() + json.size() / 4U);
    int indentation = 0;
    bool inString = false;
    bool escaped = false;

    const auto appendIndent = [&]() {
        result.append(static_cast<std::size_t>(std::max(indentation, 0)) * 2U, ' ');
    };

    for (const char character : json) {
        if (inString) {
            result.push_back(character);
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }

        switch (character) {
            case '"':
                inString = true;
                result.push_back(character);
                break;
            case '{':
            case '[':
                result.push_back(character);
                result.append("\r\n");
                ++indentation;
                appendIndent();
                break;
            case '}':
            case ']':
                result.append("\r\n");
                --indentation;
                appendIndent();
                result.push_back(character);
                break;
            case ',':
                result.push_back(character);
                result.append("\r\n");
                appendIndent();
                break;
            case ':':
                result.append(": ");
                break;
            case '\r':
            case '\n':
            case '\t':
                break;
            default:
                result.push_back(character);
                break;
        }
    }
    return result;
}

[[nodiscard]] std::wstring QuoteCommandLineArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(character);
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}

[[nodiscard]] std::filesystem::path ExecutableDirectory() {
    std::wstring path(512, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size() - 1U) {
            path.resize(length);
            return std::filesystem::path(path).parent_path();
        }
        path.resize(path.size() * 2U);
    }
}

struct PipeCapture {
    std::string text;
    DWORD error = ERROR_SUCCESS;
    bool truncated = false;
    bool captureEnabled = true;
    bool closed = false;
};

void DrainPipe(HANDLE pipe, PipeCapture& capture) {
    char buffer[4096];
    while (!capture.closed) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            const auto error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA) {
                capture.closed = true;
            } else if (capture.error == ERROR_SUCCESS) {
                capture.error = error;
            }
            return;
        }
        if (available == 0) return;

        DWORD bytesRead = 0;
        const DWORD requested = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
        if (!ReadFile(pipe, buffer, requested, &bytesRead, nullptr) || bytesRead == 0) {
            const auto error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA) {
                capture.closed = true;
            } else if (capture.error == ERROR_SUCCESS) {
                capture.error = error;
            }
            return;
        }
        if (capture.captureEnabled) {
            const auto remaining = kMaximumCapturedBytes > capture.text.size()
                                       ? kMaximumCapturedBytes - capture.text.size()
                                       : 0U;
            const auto toCopy = std::min<std::size_t>(bytesRead, remaining);
            try {
                capture.text.append(buffer, toCopy);
            } catch (...) {
                capture.captureEnabled = false;
                capture.truncated = true;
            }
            capture.truncated = capture.truncated || toCopy != bytesRead;
        }
    }
}

void RequestStop(AppState& state) {
    state.stopRequested.store(true);
    std::lock_guard lock(state.processMutex);
    if (state.activeJob != nullptr) {
        TerminateJobObject(state.activeJob, kStoppedExitCode);
    }
}

[[nodiscard]] ProbeRunResult RunProbeAttempt(AppState& state,
                                             const std::wstring& pluginPath,
                                             DWORD timeoutMs) {
    ProbeRunResult result;
    const auto probePath = ExecutableDirectory() / L"Vst3MetadataProbe.exe";
    std::error_code probePathError;
    if (probePath.empty() || !std::filesystem::exists(probePath, probePathError) ||
        probePathError) {
        result.status = L"Probe nicht gefunden";
        result.output = L"Vst3MetadataProbe.exe muss im selben Verzeichnis wie die GUI liegen.";
        return result;
    }

    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE stdoutReadRaw = nullptr;
    HANDLE stdoutWriteRaw = nullptr;
    HANDLE stderrReadRaw = nullptr;
    HANDLE stderrWriteRaw = nullptr;
    if (!CreatePipe(&stdoutReadRaw, &stdoutWriteRaw, &security, 0) ||
        !CreatePipe(&stderrReadRaw, &stderrWriteRaw, &security, 0)) {
        const auto error = GetLastError();
        if (stdoutReadRaw != nullptr) CloseHandle(stdoutReadRaw);
        if (stdoutWriteRaw != nullptr) CloseHandle(stdoutWriteRaw);
        if (stderrReadRaw != nullptr) CloseHandle(stderrReadRaw);
        if (stderrWriteRaw != nullptr) CloseHandle(stderrWriteRaw);
        result.status = L"Pipe-Fehler";
        result.output = FormatSystemError(error);
        return result;
    }

    UniqueHandle stdoutRead(stdoutReadRaw);
    UniqueHandle stdoutWrite(stdoutWriteRaw);
    UniqueHandle stderrRead(stderrReadRaw);
    UniqueHandle stderrWrite(stderrWriteRaw);
    if (!SetHandleInformation(stdoutRead.Get(), HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(stderrRead.Get(), HANDLE_FLAG_INHERIT, 0)) {
        result.status = L"Pipe-Fehler";
        result.output = FormatSystemError(GetLastError());
        return result;
    }

    UniqueHandle nullInput(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) {
        result.status = L"Job-Object-Fehler";
        result.output = FormatSystemError(GetLastError());
        return result;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
    jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &jobLimits,
                                 sizeof(jobLimits))) {
        result.status = L"Job-Object-Fehler";
        result.output = FormatSystemError(GetLastError());
        return result;
    }

    std::wstring commandLine = QuoteCommandLineArgument(probePath.wstring()) + L" " +
                               QuoteCommandLineArgument(pluginPath);

    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    if (attributeBytes == 0) {
        result.status = L"Prozessisolierung konnte nicht vorbereitet werden";
        result.output = FormatSystemError(GetLastError());
        return result;
    }
    std::vector<unsigned char> attributeStorage(attributeBytes);
    auto* attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        attributeStorage.data());
    if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeBytes)) {
        result.status = L"Prozessisolierung konnte nicht vorbereitet werden";
        result.output = FormatSystemError(GetLastError());
        return result;
    }

    std::vector<HANDLE> inheritedHandles{stdoutWrite.Get(), stderrWrite.Get()};
    if (nullInput) inheritedHandles.push_back(nullInput.Get());
    if (!UpdateProcThreadAttribute(
            attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inheritedHandles.data(), inheritedHandles.size() * sizeof(HANDLE), nullptr, nullptr)) {
        const auto error = GetLastError();
        DeleteProcThreadAttributeList(attributeList);
        result.status = L"Prozessisolierung konnte nicht eingerichtet werden";
        result.output = FormatSystemError(error);
        return result;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nullInput ? nullInput.Get() : nullptr;
    startup.StartupInfo.hStdOutput = stdoutWrite.Get();
    startup.StartupInfo.hStdError = stderrWrite.Get();
    startup.lpAttributeList = attributeList;

    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(
        nullptr, commandLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
        nullptr, nullptr, &startup.StartupInfo, &processInfo);
    const auto createError = created ? ERROR_SUCCESS : GetLastError();
    DeleteProcThreadAttributeList(attributeList);
    if (!created) {
        result.status = L"Probe konnte nicht gestartet werden";
        result.output = FormatSystemError(createError);
        return result;
    }

    UniqueHandle process(processInfo.hProcess);
    UniqueHandle processThread(processInfo.hThread);
    if (!AssignProcessToJobObject(job.Get(), process.Get())) {
        const auto error = GetLastError();
        TerminateProcess(process.Get(), kStoppedExitCode);
        WaitForSingleObject(process.Get(), 5'000);
        result.status = L"Probe konnte nicht isoliert werden";
        result.output = FormatSystemError(error);
        return result;
    }

    ActiveJobRegistration activeJob(state, job.Get());
    if (state.stopRequested.load()) {
        TerminateJobObject(job.Get(), kStoppedExitCode);
    }
    if (ResumeThread(processThread.Get()) == static_cast<DWORD>(-1)) {
        const auto error = GetLastError();
        TerminateJobObject(job.Get(), error);
        WaitForSingleObject(process.Get(), 5'000);
        result.status = L"Probe-Thread konnte nicht gestartet werden";
        result.output = FormatSystemError(error);
        return result;
    }
    processThread.Reset();
    stdoutWrite.Reset();
    stderrWrite.Reset();
    nullInput.Reset();

    PipeCapture stdoutCapture;
    PipeCapture stderrCapture;
    bool timedOut = false;
    bool waitFailed = false;
    bool terminationIncomplete = false;
    DWORD waitError = ERROR_SUCCESS;
    DWORD processControlError = ERROR_SUCCESS;
    DWORD waitResult = WAIT_TIMEOUT;
    const auto startedWaiting = GetTickCount64();

    const auto terminateJob = [&](DWORD exitCode) {
        if (!TerminateJobObject(job.Get(), exitCode) && processControlError == ERROR_SUCCESS) {
            processControlError = GetLastError();
        }
    };

    while (true) {
        DrainPipe(stdoutRead.Get(), stdoutCapture);
        DrainPipe(stderrRead.Get(), stderrCapture);
        waitResult = WaitForSingleObject(process.Get(), 25);
        if (waitResult == WAIT_OBJECT_0) break;
        if (waitResult == WAIT_FAILED) {
            waitFailed = true;
            waitError = GetLastError();
            terminateJob(waitError);
            break;
        }
        if (state.stopRequested.load()) {
            terminateJob(kStoppedExitCode);
            break;
        }
        if (GetTickCount64() - startedWaiting >= timeoutMs) {
            timedOut = true;
            terminateJob(WAIT_TIMEOUT);
            break;
        }
    }

    bool processExited = waitResult == WAIT_OBJECT_0;
    if (!processExited) {
        const auto terminationDeadline = GetTickCount64() + 5'000;
        while (GetTickCount64() < terminationDeadline) {
            DrainPipe(stdoutRead.Get(), stdoutCapture);
            DrainPipe(stderrRead.Get(), stderrCapture);
            waitResult = WaitForSingleObject(process.Get(), 25);
            if (waitResult == WAIT_OBJECT_0) {
                processExited = true;
                break;
            }
            if (waitResult == WAIT_FAILED) {
                if (waitError == ERROR_SUCCESS) waitError = GetLastError();
                waitFailed = true;
                break;
            }
        }
        if (!processExited && WaitForSingleObject(process.Get(), 0) == WAIT_OBJECT_0) {
            processExited = true;
        }
        terminationIncomplete = !processExited;
    } else {
        // Kill possible descendants after the direct probe process has exited.
        terminateJob(ERROR_SUCCESS);
    }

    DrainPipe(stdoutRead.Get(), stdoutCapture);
    DrainPipe(stderrRead.Get(), stderrCapture);
    stdoutRead.Reset();
    stderrRead.Reset();

    const bool exitCodeAvailable = processExited &&
                                   GetExitCodeProcess(process.Get(), &result.exitCode) != FALSE;
    result.processCompleted = exitCodeAvailable && !waitFailed && !terminationIncomplete;
    result.terminationIncomplete = terminationIncomplete;
    result.jsonOutput = stdoutCapture.text;
    result.stderrOutput = stderrCapture.text;
    const bool stopped = state.stopRequested.load() && !timedOut;
    result.timedOut = timedOut;
    result.stopped = stopped;
    if (terminationIncomplete) {
        result.protocolStatus = timedOut ? "timeout" : "protocol_error";
        result.status = L"Probe-Prozess konnte nicht vollständig beendet werden";
        result.output = L"Der Scan wurde nach fünf Sekunden fortgesetzt; der isolierte Job wird beim Schließen des Handles erneut beendet.";
    } else if (timedOut) {
        result.protocolStatus = "timeout";
        result.status = L"Timeout nach " + std::to_wstring(timeoutMs / 1'000) + L" Sekunden";
    } else if (waitFailed) {
        result.status = L"Warten auf Probe fehlgeschlagen: " + FormatSystemError(waitError);
    } else if (stopped) {
        result.status = L"Prüfung abgebrochen";
    } else if (!exitCodeAvailable) {
        result.protocolStatus = "protocol_error";
        result.status = L"Exitcode der Probe konnte nicht gelesen werden";
        result.output = FormatSystemError(GetLastError());
    } else {
        const auto parsed = vst3scanner::ParseProbeResultJson(stdoutCapture.text);
        result.protocolStatus = parsed.valid ? parsed.protocolStatus : "protocol_error";
        if (!parsed.valid && IsCrashExitCode(result.exitCode)) {
            result.protocolStatus = "crashed";
            result.status = L"Probe abgestürzt, Exitcode " + FormatExitCode(result.exitCode);
            result.output = L"Der isolierte Probe-Prozess wurde durch eine Windows-Ausnahme beendet.";
        } else if (parsed.valid && result.exitCode != 0 &&
                   (parsed.protocolStatus == "ok" || parsed.protocolStatus == "partial")) {
            result.protocolStatus = IsCrashExitCode(result.exitCode) ? "crashed" : "protocol_error";
            result.status = L"Widersprüchliches Probe-Ergebnis, Exitcode " +
                            FormatExitCode(result.exitCode);
            result.output = L"Die Probe meldete Erfolg, wurde aber nicht erfolgreich beendet.";
        } else {
            result.status = L"Probe beendet, Exitcode " + std::to_wstring(result.exitCode) +
                            L", Status: " + Utf8ToWide(result.protocolStatus);
            if (!parsed.valid) {
                result.output = L"Ungültige Probe-Antwort: " + Utf8ToWide(parsed.error);
            }
        }
    }

    if (!stdoutCapture.text.empty() && result.output.empty()) {
        result.output = Utf8ToWide(PrettyPrintJson(stdoutCapture.text));
    } else if (result.output.empty()) {
        result.output = L"Die Probe hat keine JSON-Ausgabe geliefert.";
    }
    if (!stderrCapture.text.empty()) {
        result.output.append(L"\r\n\r\n--- Diagnose (stderr) ---\r\n");
        result.output.append(Utf8ToWide(stderrCapture.text));
    }
    if (processControlError != ERROR_SUCCESS) {
        result.output.append(L"\r\n\r\n[Job-Steuerung fehlgeschlagen: ")
            .append(FormatSystemError(processControlError)).append(L"]\r\n");
    }
    if (stdoutCapture.error != ERROR_SUCCESS || stderrCapture.error != ERROR_SUCCESS) {
        const auto pipeError = stdoutCapture.error != ERROR_SUCCESS
                                   ? stdoutCapture.error
                                   : stderrCapture.error;
        result.output.append(L"\r\n\r\n[Pipe-Ausgabe konnte nicht vollständig gelesen werden: ")
            .append(FormatSystemError(pipeError)).append(L"]\r\n");
    }
    if (stdoutCapture.truncated || stderrCapture.truncated) {
        result.output.append(L"\r\n\r\n[Ausgabe wurde bei 8 MiB gekürzt.]\r\n");
    }
    return result;
}

[[nodiscard]] std::wstring ReadWindowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

[[nodiscard]] bool HasVst3Extension(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    return extension == L".vst3";
}

void PublishProgress(HWND window, AppState& state, std::wstring message) {
    {
        std::lock_guard lock(state.resultMutex);
        state.progressStatus = std::move(message);
    }
    if (!state.closing.load()) {
        PostMessageW(window, kProgressMessage, 0, 0);
    }
}

[[nodiscard]] ProbeRunResult RunProbeWithRetry(HWND window,
                                               AppState& state,
                                               const std::wstring& pluginPath,
                                               const std::wstring& progressLabel) {
    const auto label = progressLabel.empty() ? L"Probe" : progressLabel;
    PublishProgress(window, state, label + L" - erster Versuch: maximal 15 Sekunden ...");
    auto result = RunProbeAttempt(state, pluginPath, kInitialProbeTimeoutMs);
    if (!result.timedOut || state.stopRequested.load()) {
        return result;
    }

    PublishProgress(window, state, label + L" - Timeout, Wiederholung: maximal 30 Sekunden ...");
    auto retry = RunProbeAttempt(state, pluginPath, kRetryProbeTimeoutMs);
    retry.retried = true;
    retry.output.insert(0, L"[Erster Versuch nach 15 Sekunden abgebrochen; einmal mit 30 Sekunden wiederholt.]\r\n\r\n");
    if (retry.timedOut) {
        retry.status = L"Timeout nach Wiederholung (15 + 30 Sekunden)";
    } else if (!retry.stopped) {
        retry.status.append(L" (nach Wiederholung)");
    }
    return retry;
}

[[nodiscard]] std::wstring NormalizedPathKey(const std::filesystem::path& path) {
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error) {
        error.clear();
        normalized = std::filesystem::absolute(path, error);
    }
    std::wstring key = (error ? path : normalized).wstring();
    std::transform(key.begin(), key.end(), key.begin(), towlower);
    return key;
}

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
}

template <typename Value>
void HashValue(std::uint64_t& hash, const Value& value) {
    HashBytes(hash, &value, sizeof(value));
}

[[nodiscard]] bool ShouldHashFileContents(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    auto filename = path.filename().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    std::transform(filename.begin(), filename.end(), filename.begin(), towlower);
    return extension == L".vst3" || extension == L".dll" || filename == L"moduleinfo.json";
}

[[nodiscard]] bool HashFileContents(std::uint64_t& hash,
                                    const std::filesystem::path& path,
                                    const std::atomic_bool& stopRequested) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::vector<char> buffer(64U * 1024U);
    while (input) {
        if (stopRequested.load()) return false;
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytesRead = input.gcount();
        if (bytesRead > 0) {
            HashBytes(hash, buffer.data(), static_cast<std::size_t>(bytesRead));
        }
    }
    return input.eof() && !input.bad();
}

[[nodiscard]] std::string CacheKey(const std::filesystem::path& modulePath,
                                   const std::atomic_bool& stopRequested) {
    struct FileState {
        std::filesystem::path sourcePath;
        std::wstring path;
        std::uintmax_t size = 0;
        std::int64_t modified = 0;
        bool hashContents = false;
    };

    std::vector<FileState> files;
    std::error_code error;
    if (std::filesystem::is_regular_file(modulePath, error) && !error) {
        const auto modified = std::filesystem::last_write_time(modulePath, error);
        if (error) return {};
        const auto size = std::filesystem::file_size(modulePath, error);
        if (error) return {};
        files.push_back({modulePath, NormalizedPathKey(modulePath), size,
                         static_cast<std::int64_t>(modified.time_since_epoch().count()), true});
    } else {
        error.clear();
        if (!std::filesystem::is_directory(modulePath, error) || error) return {};
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator iterator(modulePath, options, error);
        const std::filesystem::recursive_directory_iterator end;
        if (error) return {};
        while (iterator != end) {
            if (stopRequested.load()) return {};
            std::error_code typeError;
            if (iterator->is_regular_file(typeError) && !typeError) {
                const auto size = iterator->file_size(typeError);
                if (typeError) return {};
                const auto modified = iterator->last_write_time(typeError);
                if (typeError) return {};
                auto relative = std::filesystem::relative(iterator->path(), modulePath, typeError);
                if (typeError) return {};
                auto relativeKey = relative.wstring();
                std::transform(relativeKey.begin(), relativeKey.end(), relativeKey.begin(), towlower);
                files.push_back({iterator->path(), std::move(relativeKey), size,
                                 static_cast<std::int64_t>(modified.time_since_epoch().count()),
                                 ShouldHashFileContents(iterator->path())});
            }
            iterator.increment(error);
            if (error) return {};
        }
    }

    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return left.path < right.path;
    });
    std::uint64_t hash = 14695981039346656037ULL;
    static constexpr std::string_view algorithmMarker = "vst3-cache-content-v2";
    HashBytes(hash, algorithmMarker.data(), algorithmMarker.size());
    const auto root = NormalizedPathKey(modulePath);
    HashBytes(hash, root.data(), root.size() * sizeof(wchar_t));
    for (const auto& file : files) {
        HashBytes(hash, file.path.data(), file.path.size() * sizeof(wchar_t));
        HashValue(hash, file.size);
        HashValue(hash, file.modified);
        if (file.hashContents && !HashFileContents(hash, file.sourcePath, stopRequested)) return {};
    }
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

[[nodiscard]] std::filesystem::path CacheFilePath() {
    std::wstring executablePath(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(),
                                            static_cast<DWORD>(executablePath.size()));
    if (length == 0 || length >= executablePath.size()) return {};
    executablePath.resize(length);
    return std::filesystem::path(executablePath).parent_path() / L"vst3_scanner_cache.json";
}

[[nodiscard]] CacheLoadResult LoadProbeCache(ProbeCache& cache) {
    cache.entries.clear();
    cache.dirty = false;
    const auto path = CacheFilePath();
    if (path.empty()) return CacheLoadResult::Invalid;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error == std::errc::no_such_file_or_directory) return CacheLoadResult::Missing;
    if (error || size == 0 || size > kMaximumCacheBytes) return CacheLoadResult::Invalid;

    std::ifstream input(path, std::ios::binary);
    if (!input) return CacheLoadResult::Invalid;
    std::string json(static_cast<std::size_t>(size), '\0');
    input.read(json.data(), static_cast<std::streamsize>(json.size()));
    if (!input) return CacheLoadResult::Invalid;
    const auto parsed = vst3scanner::ParseProbeCacheJson(json);
    if (!parsed.valid) return CacheLoadResult::Invalid;
    for (const auto& entry : parsed.entries) {
        cache.entries.emplace(entry.key, entry.probeJson);
    }
    return CacheLoadResult::Loaded;
}

[[nodiscard]] bool TryLoadCachedProbe(const std::filesystem::path& modulePath,
                                      std::string_view key,
                                      const ProbeCache& cache,
                                      ProbeRunResult& result) {
    const auto entry = cache.entries.find(std::string(key));
    if (key.empty() || entry == cache.entries.end()) return false;
    const auto parsed = vst3scanner::ParseProbeResultJson(entry->second);
    if (!parsed.valid || (parsed.protocolStatus != "ok" && parsed.protocolStatus != "partial") ||
        NormalizedPathKey(Utf8ToWide(parsed.modulePath)) != NormalizedPathKey(modulePath)) {
        return false;
    }

    result.exitCode = 0;
    result.processCompleted = true;
    result.protocolStatus = parsed.protocolStatus;
    result.status = L"Cachetreffer, Status: " + Utf8ToWide(parsed.protocolStatus);
    result.jsonOutput = entry->second;
    result.fromCache = true;
    return true;
}

[[nodiscard]] bool PutCachedProbe(const std::filesystem::path& modulePath,
                                  std::string_view key,
                                  std::string_view json,
                                  ProbeCache& cache) {
    if (key.empty() || json.empty()) return false;
    const auto parsed = vst3scanner::ParseProbeResultJson(json);
    if (!parsed.valid ||
        NormalizedPathKey(Utf8ToWide(parsed.modulePath)) != NormalizedPathKey(modulePath)) {
        return false;
    }
    cache.entries[std::string(key)] = std::string(json);
    cache.dirty = true;
    return true;
}

[[nodiscard]] bool SaveProbeCache(const ProbeCache& cache) {
    if (!cache.dirty) return true;
    std::vector<vst3scanner::ProbeCacheEntry> entries;
    entries.reserve(cache.entries.size());
    for (const auto& [key, probeJson] : cache.entries) {
        entries.push_back({key, probeJson});
    }
    const auto json = vst3scanner::SerializeProbeCacheJson(entries);
    if (json.empty() || json.size() > kMaximumCacheBytes) return false;
    const auto cacheFile = CacheFilePath();
    if (cacheFile.empty()) return false;
    const auto temporary = cacheFile.wstring() + L".tmp";
    {
        std::ofstream output(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(json.data(), static_cast<std::streamsize>(json.size()));
        if (!output) {
            DeleteFileW(temporary.c_str());
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), cacheFile.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

[[nodiscard]] std::vector<std::filesystem::path> FindVst3Candidates(
    const std::filesystem::path& root,
    const AppState& state,
    std::vector<std::wstring>& warnings) {
    std::vector<std::filesystem::path> candidates;
    std::set<std::wstring> seen;
    const auto addCandidate = [&](const std::filesystem::path& candidate) {
        auto key = NormalizedPathKey(candidate);
        if (seen.insert(std::move(key)).second) {
            candidates.push_back(candidate);
        }
    };

    std::error_code error;
    if (std::filesystem::is_regular_file(root, error)) {
        if (!error && HasVst3Extension(root)) addCandidate(root);
        return candidates;
    }
    error.clear();
    if (!std::filesystem::is_directory(root, error) || error) {
        warnings.push_back(L"Scanpfad ist kein lesbares Verzeichnis: " + root.wstring());
        return candidates;
    }
    if (HasVst3Extension(root)) {
        addCandidate(root);
        return candidates;
    }

    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iterator(root, options, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        warnings.push_back(L"Scanordner konnte nicht vollständig geöffnet werden: " +
                           FormatSystemError(error.value()));
        error.clear();
    }

    while (iterator != end && !state.stopRequested.load()) {
        const auto current = iterator->path();
        const DWORD attributes = GetFileAttributesW(current.c_str());
        const bool isReparsePoint = attributes != INVALID_FILE_ATTRIBUTES &&
                                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        std::error_code typeError;
        if (isReparsePoint) {
            iterator.disable_recursion_pending();
            if (HasVst3Extension(current)) addCandidate(current);
        } else if (iterator->is_directory(typeError)) {
            if (HasVst3Extension(current)) {
                addCandidate(current);
                iterator.disable_recursion_pending();
            }
        } else if (!typeError && iterator->is_regular_file(typeError) &&
                   !typeError && HasVst3Extension(current)) {
            addCandidate(current);
        }
        if (typeError) {
            warnings.push_back(L"Zugriff übersprungen: " + current.wstring());
        }

        iterator.increment(error);
        if (error) {
            warnings.push_back(L"Dateisystemfehler beim Scan: " + FormatSystemError(error.value()));
            error.clear();
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return NormalizedPathKey(left) < NormalizedPathKey(right);
    });
    return candidates;
}

[[nodiscard]] ProbeRunResult RunFolderScan(HWND window,
                                           AppState& state,
                                           const std::filesystem::path& root,
                                           bool useCache) {
    ProbeRunResult result;
    PublishProgress(window, state, L"Suche VST3-Module ...");
    std::vector<std::wstring> warnings;
    const auto candidates = FindVst3Candidates(root, state, warnings);
    ProbeCache cache;
    const auto cacheLoadResult = useCache ? LoadProbeCache(cache) : CacheLoadResult::Missing;

    std::size_t completed = 0;
    std::size_t successful = 0;
    std::size_t failed = 0;
    std::size_t timeouts = 0;
    std::size_t retries = 0;
    std::size_t cacheHits = 0;
    bool cacheWriteFailed = false;
    bool cacheFingerprintFailed = false;

    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (state.stopRequested.load()) break;
        const auto progressLabel = L"[" + std::to_wstring(index + 1) + L"/" +
                                   std::to_wstring(candidates.size()) + L"] " +
                                   candidates[index].filename().wstring();
        ProbeRunResult current;
        const auto cacheKey = useCache ? CacheKey(candidates[index], state.stopRequested)
                                       : std::string{};
        if (state.stopRequested.load()) break;
        if (useCache && cacheKey.empty()) cacheFingerprintFailed = true;
        if (useCache && !cacheKey.empty() &&
            TryLoadCachedProbe(candidates[index], cacheKey, cache, current)) {
            ++cacheHits;
            PublishProgress(window, state, progressLabel + L" - Cachetreffer");
        } else {
            current = RunProbeWithRetry(window, state, candidates[index].wstring(), progressLabel);
        }
        if (current.stopped) break;
        ++completed;
        if (current.retried) ++retries;
        if (current.timedOut) ++timeouts;
        auto parsed = vst3scanner::ParseProbeResultJson(current.jsonOutput);
        const bool succeeded = parsed.valid && current.processCompleted &&
                               current.exitCode == 0 && !current.timedOut &&
                               (parsed.protocolStatus == "ok" ||
                                parsed.protocolStatus == "partial");
        if (succeeded) {
            ++successful;
            if (useCache && !current.fromCache) {
                cacheWriteFailed = !PutCachedProbe(candidates[index], cacheKey,
                                                   current.jsonOutput, cache) ||
                                   cacheWriteFailed;
            }
            for (auto& plugin : parsed.audioPlugins) {
                plugin.fromCache = current.fromCache;
                result.inventory.push_back(std::move(plugin));
            }
        } else {
            ++failed;
            vst3scanner::ScanIssue issue;
            issue.modulePath = WideToUtf8(candidates[index].wstring());
            issue.status = !current.protocolStatus.empty()
                               ? current.protocolStatus
                               : (parsed.valid ? parsed.protocolStatus : "protocol_error");
            issue.diagnostic = current.terminationIncomplete
                                   ? WideToUtf8(current.output)
                                   : (parsed.valid ? parsed.diagnostic : parsed.error);
            if (issue.diagnostic.empty()) issue.diagnostic = current.stderrOutput;
            if (issue.diagnostic.empty()) issue.diagnostic = WideToUtf8(current.output);
            issue.retried = current.retried;
            issue.timedOut = current.timedOut;
            result.issues.push_back(std::move(issue));
        }
    }

    for (const auto& warning : warnings) {
        vst3scanner::ScanIssue issue;
        issue.modulePath = WideToUtf8(root.wstring());
        issue.status = "filesystem_warning";
        issue.diagnostic = WideToUtf8(warning);
        result.issues.push_back(std::move(issue));
    }
    vst3scanner::MarkCidDuplicates(result.inventory);
    std::set<std::string> duplicateCids;
    for (const auto& plugin : result.inventory) {
        if (plugin.duplicate) duplicateCids.insert(plugin.cid);
    }
    const bool stopped = state.stopRequested.load();
    if (useCache && !stopped && cache.dirty && !SaveProbeCache(cache)) {
        cacheWriteFailed = true;
    }
    result.stopped = stopped;
    result.cacheHits = cacheHits;
    result.status = stopped ? L"Batchscan abgebrochen: " : L"Batchscan beendet: ";
    result.status.append(std::to_wstring(completed)).append(L"/").append(
        std::to_wstring(candidates.size()));
    result.status.append(L", Plugins: ").append(std::to_wstring(result.inventory.size()));
    result.status.append(L", Module OK: ").append(std::to_wstring(successful));
    result.status.append(L", Fehler: ").append(std::to_wstring(failed));
    if (useCache) {
        result.status.append(L", Cache: ").append(std::to_wstring(cacheHits));
        if (cacheLoadResult == CacheLoadResult::Invalid) {
            result.status.append(L" (Datei war ungültig)");
        }
        if (cacheFingerprintFailed) result.status.append(L" (Fingerprint fehlgeschlagen)");
        if (cacheWriteFailed) result.status.append(L" (Schreiben fehlgeschlagen)");
    } else {
        result.status.append(L", Cache: aus");
    }
    result.status.append(L", CID-Konflikte: ").append(std::to_wstring(duplicateCids.size()));
    result.status.append(L", Wiederholungen: ").append(std::to_wstring(retries));
    result.status.append(L", Timeouts: ").append(std::to_wstring(timeouts));
    return result;
}
[[nodiscard]] bool SelectPath(HWND owner, bool selectFolder, std::wstring& selectedPath,
                              const wchar_t* folderTitle = nullptr) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return false;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (selectFolder) {
        options |= FOS_PICKFOLDERS;
        dialog->SetTitle(folderTitle != nullptr ? folderTitle : L"Ordner auswählen");
    } else {
        options |= FOS_FILEMUSTEXIST;
        const COMDLG_FILTERSPEC filters[] = {
            {L"VST3-Module (*.vst3)", L"*.vst3"},
            {L"Alle Dateien", L"*.*"},
        };
        dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
        dialog->SetDefaultExtension(L"vst3");
        dialog->SetTitle(L"VST3-Datei auswählen");
    }
    dialog->SetOptions(options);

    bool selected = false;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                selectedPath = path;
                CoTaskMemFree(path);
                selected = true;
            }
            item->Release();
        }
    }
    dialog->Release();
    return selected;
}

[[nodiscard]] bool SelectExportPath(HWND owner,
                                    bool json,
                                    std::filesystem::path& selectedPath) {
    IFileSaveDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return false;
    }
    const COMDLG_FILTERSPEC filters[] = {
        {json ? L"JSON-Dateien (*.json)" : L"CSV-Dateien (*.csv)",
         json ? L"*.json" : L"*.csv"},
        {L"Alle Dateien", L"*.*"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetDefaultExtension(json ? L"json" : L"csv");
    dialog->SetFileName(json ? L"vst3_inventory.json" : L"vst3_inventory.csv");
    dialog->SetTitle(json ? L"VST3-Inventar als JSON exportieren"
                          : L"VST3-Inventar als CSV exportieren");

    bool selected = false;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                selectedPath = path;
                CoTaskMemFree(path);
                selected = true;
            }
            item->Release();
        }
    }
    dialog->Release();
    return selected;
}

[[nodiscard]] std::wstring JoinedCategories(const std::vector<std::string>& categories) {
    std::wstring result;
    for (std::size_t index = 0; index < categories.size(); ++index) {
        if (index != 0) result.append(L" | ");
        result.append(Utf8ToWide(categories[index]));
    }
    return result;
}

[[nodiscard]] std::string ModuleFileName(std::string_view path) {
    const auto separator = path.find_last_of("/\\");
    return std::string(separator == std::string_view::npos ? path : path.substr(separator + 1U));
}

void SetListCell(HWND list, int row, int column, const std::wstring& value) {
    ListView_SetItemText(list, row, column, const_cast<wchar_t*>(value.c_str()));
}

void PopulateResultsList(AppState& state) {
    ListView_DeleteAllItems(state.resultList);
    int row = 0;
    for (const auto& record : state.displayedInventory) {
        const auto name = Utf8ToWide(record.name);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.pszText = const_cast<wchar_t*>(name.c_str());
        ListView_InsertItem(state.resultList, &item);
        SetListCell(state.resultList, row, 1, Utf8ToWide(record.vendor));
        SetListCell(state.resultList, row, 2, Utf8ToWide(record.version));
        SetListCell(state.resultList, row, 3, Utf8ToWide(record.sdkVersion));
        SetListCell(state.resultList, row, 4, JoinedCategories(record.subCategories));
        SetListCell(state.resultList, row, 5, Utf8ToWide(ModuleFileName(record.modulePath)));
        SetListCell(state.resultList, row, 6, Utf8ToWide(record.modulePath));
        SetListCell(state.resultList, row, 7,
                    record.duplicate ? L"Ja (" + std::to_wstring(record.duplicateCount) + L")"
                                     : L"Nein");
        SetListCell(state.resultList, row, 8, record.fromCache ? L"Ja" : L"Nein");
        SetListCell(state.resultList, row, 9, Utf8ToWide(record.protocolStatus));
        SetListCell(state.resultList, row, 10, std::to_wstring(record.probeDurationMs));
        SetListCell(state.resultList, row, 11, Utf8ToWide(record.diagnostic));
        ++row;
    }
    for (const auto& issue : state.displayedIssues) {
        const auto filename = std::filesystem::path(Utf8ToWide(issue.modulePath)).filename().wstring();
        const auto name = L"[Problem] " + filename;
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.pszText = const_cast<wchar_t*>(name.c_str());
        ListView_InsertItem(state.resultList, &item);
        SetListCell(state.resultList, row, 5, filename);
        SetListCell(state.resultList, row, 6, Utf8ToWide(issue.modulePath));
        SetListCell(state.resultList, row, 7, L"Nein");
        SetListCell(state.resultList, row, 8, L"Nein");
        SetListCell(state.resultList, row, 9, Utf8ToWide(issue.status));
        SetListCell(state.resultList, row, 11, Utf8ToWide(issue.diagnostic));
        ++row;
    }
}

[[nodiscard]] std::string SortValue(const vst3scanner::InventoryRecord& record, int column) {
    switch (column) {
        case 0: return record.name;
        case 1: return record.vendor;
        case 2: return record.version;
        case 3: return record.sdkVersion;
        case 4: {
            std::string value;
            for (const auto& category : record.subCategories) value.append(category).push_back('|');
            return value;
        }
        case 5: return ModuleFileName(record.modulePath);
        case 6: return record.modulePath;
        case 7: return record.duplicate ? std::to_string(record.duplicateCount) : "0";
        case 8: return record.fromCache ? "1" : "0";
        case 9: return record.protocolStatus;
        case 10: return std::to_string(record.probeDurationMs);
        case 11: return record.diagnostic;
        default: return record.name;
    }
}

void SortInventory(AppState& state, int column) {
    if (state.sortColumn == column) {
        state.sortAscending = !state.sortAscending;
    } else {
        state.sortColumn = column;
        state.sortAscending = true;
    }
    const bool ascending = state.sortAscending;
    std::stable_sort(state.displayedInventory.begin(), state.displayedInventory.end(),
                     [column, ascending](const auto& left, const auto& right) {
                         if (column == 10 && left.probeDurationMs != right.probeDurationMs) {
                             return ascending ? left.probeDurationMs < right.probeDurationMs
                                              : left.probeDurationMs > right.probeDurationMs;
                         }
                         const auto leftValue = SortValue(left, column);
                         const auto rightValue = SortValue(right, column);
                         const int comparison = _stricmp(leftValue.c_str(), rightValue.c_str());
                         return ascending ? comparison < 0 : comparison > 0;
                     });
    PopulateResultsList(state);
}

void ExportInventory(HWND window, const AppState& state, bool json) {
    if (state.displayedInventory.empty() && state.displayedIssues.empty()) return;
    std::filesystem::path path;
    if (!SelectExportPath(window, json, path)) return;
    const auto content = json
                             ? vst3scanner::SerializeInventoryJson(state.displayedInventory,
                                                                  state.displayedIssues)
                             : vst3scanner::SerializeInventoryCsv(state.displayedInventory,
                                                                 state.displayedIssues);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        MessageBoxW(window, L"Die Exportdatei konnte nicht geöffnet werden.", kWindowTitle,
                    MB_OK | MB_ICONERROR);
        return;
    }
    if (!json) {
        static constexpr unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        output.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) {
        MessageBoxW(window, L"Die Exportdatei konnte nicht vollständig geschrieben werden.",
                    kWindowTitle, MB_OK | MB_ICONERROR);
        return;
    }
    MessageBoxW(window, L"Inventar wurde erfolgreich exportiert.", kWindowTitle,
                MB_OK | MB_ICONINFORMATION);
}

void SetRunning(AppState& state, bool running) {
    EnableWindow(state.pathEdit, !running);
    EnableWindow(state.fileButton, !running);
    EnableWindow(state.scanFolderButton, !running);
    EnableWindow(state.runButton, !running);
    EnableWindow(state.cacheCheckbox, !running);
    EnableWindow(state.stopButton, running);
    const bool hasResults = !state.displayedInventory.empty() || !state.displayedIssues.empty();
    EnableWindow(state.exportCsvButton, !running && hasResults);
    EnableWindow(state.exportJsonButton, !running && hasResults);
}

void UpdateFonts(HWND window, AppState& state) {
    if (state.uiFont != nullptr) DeleteObject(state.uiFont);
    const int uiHeight = -MulDiv(10, static_cast<int>(state.dpi), 72);
    state.uiFont = CreateFontW(uiHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    EnumChildWindows(
        window,
        [](HWND child, LPARAM parameter) -> BOOL {
            const auto* currentState = reinterpret_cast<AppState*>(parameter);
            SendMessageW(child, WM_SETFONT,
                         reinterpret_cast<WPARAM>(currentState->uiFont), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&state));
}

void LayoutControls(HWND window, AppState& state) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int margin = Scale(state, 16);
    const int gap = Scale(state, 8);
    const int rowHeight = Scale(state, 30);
    const int labelHeight = Scale(state, 18);
    const int fileWidth = Scale(state, 96);
    const int scanWidth = Scale(state, 104);
    const int runWidth = Scale(state, 82);
    const int stopWidth = Scale(state, 70);
    const int top = margin + labelHeight + Scale(state, 6);
    const int fixedWidth = fileWidth + scanWidth + runWidth + stopWidth + gap * 4;
    const int editWidth = std::max(Scale(state, 160), width - margin * 2 - fixedWidth);

    MoveWindow(state.pathLabel, margin, margin, width - margin * 2, labelHeight, TRUE);
    int x = margin;
    MoveWindow(state.pathEdit, x, top, editWidth, rowHeight, TRUE);
    x += editWidth + gap;
    MoveWindow(state.fileButton, x, top, fileWidth, rowHeight, TRUE);
    x += fileWidth + gap;
    MoveWindow(state.scanFolderButton, x, top, scanWidth, rowHeight, TRUE);
    x += scanWidth + gap;
    MoveWindow(state.runButton, x, top, runWidth, rowHeight, TRUE);
    x += runWidth + gap;
    MoveWindow(state.stopButton, x, top, stopWidth, rowHeight, TRUE);

    const int outputLabelTop = top + rowHeight + Scale(state, 12);
    const int cacheWidth = Scale(state, 130);
    const int exportWidth = Scale(state, 104);
    MoveWindow(state.outputLabel, margin, outputLabelTop,
               width - margin * 2 - cacheWidth - exportWidth * 2 - gap * 3,
               rowHeight, TRUE);
    MoveWindow(state.cacheCheckbox,
               width - margin - exportWidth * 2 - gap * 2 - cacheWidth,
               outputLabelTop, cacheWidth, rowHeight, TRUE);
    MoveWindow(state.exportCsvButton, width - margin - exportWidth * 2 - gap,
               outputLabelTop, exportWidth, rowHeight, TRUE);
    MoveWindow(state.exportJsonButton, width - margin - exportWidth,
               outputLabelTop, exportWidth, rowHeight, TRUE);
    const int outputTop = outputLabelTop + rowHeight + Scale(state, 4);
    const int statusHeight = Scale(state, 24);
    const int outputHeight = std::max(Scale(state, 120), height - outputTop - statusHeight - margin * 2);
    MoveWindow(state.resultList, margin, outputTop, width - margin * 2, outputHeight, TRUE);
    MoveWindow(state.statusLabel, margin, outputTop + outputHeight + gap,
               width - margin * 2, statusHeight, TRUE);
}

void StartProbe(HWND window, AppState& state) {
    const auto selectedPath = ReadWindowText(state.pathEdit);
    if (selectedPath.empty()) {
        MessageBoxW(window, L"Bitte zuerst ein VST3-Modul oder einen Scanordner auswählen.",
                    kWindowTitle, MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::error_code error;
    const std::filesystem::path path(selectedPath);
    if (!std::filesystem::exists(path, error) || error) {
        MessageBoxW(window, L"Der ausgewählte Pfad ist nicht erreichbar.", kWindowTitle,
                    MB_OK | MB_ICONERROR);
        return;
    }
    const bool isDirectory = std::filesystem::is_directory(path, error) && !error;
    const auto mode = state.scanMode;
    const bool useCache = SendMessageW(state.cacheCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (mode == ScanMode::Folder && !isDirectory) {
        MessageBoxW(window, L"Für den Ordnerscan muss ein Verzeichnis ausgewählt werden.",
                    kWindowTitle, MB_OK | MB_ICONWARNING);
        return;
    }
    if (mode == ScanMode::SingleModule && !HasVst3Extension(path)) {
        MessageBoxW(window, L"Das ausgewählte Modul besitzt nicht die Endung .vst3.",
                    kWindowTitle, MB_OK | MB_ICONWARNING);
        return;
    }

    if (state.worker.joinable()) state.worker.join();
    state.stopRequested.store(false);
    state.displayedInventory.clear();
    state.displayedIssues.clear();
    ListView_DeleteAllItems(state.resultList);
    SetWindowTextW(state.statusLabel, mode == ScanMode::Folder
                                                ? L"Suche VST3-Module ..."
                                                : L"Probe läuft: maximal 15 Sekunden ...");
    SetRunning(state, true);

    try {
        state.worker = std::thread([window, &state, selectedPath, mode, useCache]() {
            ProbeRunResult runResult;
            try {
                if (mode == ScanMode::Folder) {
                    runResult = RunFolderScan(window, state, std::filesystem::path(selectedPath),
                                              useCache);
                } else {
                    const auto label = std::filesystem::path(selectedPath).filename().wstring();
                    ProbeCache cache;
                    const auto cacheLoadResult = useCache
                                                     ? LoadProbeCache(cache)
                                                     : CacheLoadResult::Missing;
                    const auto modulePath = std::filesystem::path(selectedPath);
                    const auto cacheKey = useCache
                                              ? CacheKey(modulePath, state.stopRequested)
                                              : std::string{};
                    if (state.stopRequested.load()) {
                        runResult.stopped = true;
                        runResult.status = L"Prüfung abgebrochen";
                    } else if (!useCache || cacheKey.empty() ||
                               !TryLoadCachedProbe(modulePath, cacheKey, cache, runResult)) {
                        runResult = RunProbeWithRetry(window, state, selectedPath, label);
                    }
                    auto parsed = vst3scanner::ParseProbeResultJson(runResult.jsonOutput);
                    const bool succeeded = parsed.valid && runResult.processCompleted &&
                                           runResult.exitCode == 0 &&
                                           !runResult.timedOut &&
                                           (parsed.protocolStatus == "ok" ||
                                            parsed.protocolStatus == "partial");
                    if (succeeded) {
                        if (useCache && !runResult.fromCache) {
                            if (cacheKey.empty()) {
                                runResult.status.append(L", Cache-Fingerprint fehlgeschlagen");
                            } else if (!PutCachedProbe(modulePath, cacheKey,
                                                      runResult.jsonOutput, cache) ||
                                       !SaveProbeCache(cache)) {
                                runResult.status.append(L", Cache konnte nicht geschrieben werden");
                            } else if (cacheLoadResult == CacheLoadResult::Invalid) {
                                runResult.status.append(L", Cachedatei wurde neu aufgebaut");
                            }
                        }
                        for (auto& plugin : parsed.audioPlugins) {
                            plugin.fromCache = runResult.fromCache;
                            runResult.inventory.push_back(std::move(plugin));
                        }
                        vst3scanner::MarkCidDuplicates(runResult.inventory);
                    } else if (!runResult.stopped) {
                        vst3scanner::ScanIssue issue;
                        issue.modulePath = WideToUtf8(selectedPath);
                        issue.status = !runResult.protocolStatus.empty()
                                           ? runResult.protocolStatus
                                           : (parsed.valid ? parsed.protocolStatus
                                                           : "protocol_error");
                        issue.diagnostic = runResult.terminationIncomplete
                                               ? WideToUtf8(runResult.output)
                                               : (parsed.valid ? parsed.diagnostic : parsed.error);
                        if (issue.diagnostic.empty()) issue.diagnostic = runResult.stderrOutput;
                        if (issue.diagnostic.empty()) issue.diagnostic = WideToUtf8(runResult.output);
                        issue.retried = runResult.retried;
                        issue.timedOut = runResult.timedOut;
                        runResult.issues.push_back(std::move(issue));
                    }
                }
            } catch (const std::exception& exception) {
                runResult.status = L"Unerwarteter GUI-Worker-Fehler";
                runResult.output = Utf8ToWide(exception.what());
            } catch (...) {
                runResult.status = L"Unerwarteter GUI-Worker-Fehler";
                runResult.output = L"Nicht näher bestimmbarer Fehler.";
            }
            {
                std::lock_guard lock(state.resultMutex);
                state.result = std::move(runResult);
            }
            if (!state.closing.load()) PostMessageW(window, kProbeDoneMessage, 0, 0);
        });
    } catch (const std::exception& exception) {
        SetRunning(state, false);
        SetWindowTextW(state.statusLabel, L"Worker-Thread konnte nicht gestartet werden.");
        MessageBoxW(window, Utf8ToWide(exception.what()).c_str(), kWindowTitle,
                    MB_OK | MB_ICONERROR);
    }
}
LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = GetState(window);
    switch (message) {
        case WM_NCCREATE: {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE: {
            state = GetState(window);
            state->dpi = GetDpiForWindow(window);
            state->pathLabel = CreateWindowExW(
                0, L"STATIC", L"VST3-Modul oder Scanordner:", WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            state->pathEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(PathEdit), nullptr, nullptr);
            state->fileButton = CreateWindowExW(
                0, L"BUTTON", L"VST3-Datei", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(FileButton), nullptr, nullptr);
            state->scanFolderButton = CreateWindowExW(
                0, L"BUTTON", L"Scan-Ordner", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(ScanFolderButton), nullptr, nullptr);
            state->runButton = CreateWindowExW(
                0, L"BUTTON", L"Prüfen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(RunButton), nullptr, nullptr);
            state->stopButton = CreateWindowExW(
                0, L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(StopButton), nullptr, nullptr);
            state->outputLabel = CreateWindowExW(
                0, L"STATIC", L"VST3-Inventar (Spaltenkopf anklicken zum Sortieren):",
                WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            state->cacheCheckbox = CreateWindowExW(
                0, L"BUTTON", L"Cache aktivieren",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(CacheCheckbox), nullptr, nullptr);
            SendMessageW(state->cacheCheckbox, BM_SETCHECK, BST_UNCHECKED, 0);
            state->exportCsvButton = CreateWindowExW(
                0, L"BUTTON", L"Export CSV", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(ExportCsvButton), nullptr, nullptr);
            state->exportJsonButton = CreateWindowExW(
                0, L"BUTTON", L"Export JSON", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(ExportJsonButton), nullptr, nullptr);
            state->resultList = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(OutputEdit), nullptr, nullptr);
            state->statusLabel = CreateWindowExW(
                0, L"STATIC", L"Bereit", WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(StatusLabel), nullptr, nullptr);
            if (state->pathLabel == nullptr || state->pathEdit == nullptr ||
                state->fileButton == nullptr || state->scanFolderButton == nullptr ||
                state->runButton == nullptr || state->stopButton == nullptr ||
                state->outputLabel == nullptr || state->cacheCheckbox == nullptr ||
                state->exportCsvButton == nullptr || state->exportJsonButton == nullptr ||
                state->resultList == nullptr || state->statusLabel == nullptr) {
                MessageBoxW(window, L"Die Benutzeroberfläche konnte nicht vollständig erstellt werden.",
                            kWindowTitle, MB_OK | MB_ICONERROR);
                return -1;
            }
            SendMessageW(state->pathEdit, EM_SETLIMITTEXT, 32767, 0);
            ListView_SetExtendedListViewStyle(
                state->resultList,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            const struct {
                const wchar_t* title;
                int width;
            } columns[] = {
                {L"Plugin", 180}, {L"Hersteller", 140}, {L"Version", 100},
                {L"SDK-Version", 100}, {L"Kategorie", 140}, {L"Modul", 220},
                {L"Modulpfad", 360}, {L"Dublette", 80}, {L"Cache", 60},
                {L"Status", 110}, {L"Dauer (ms)", 90}, {L"Diagnose", 300},
            };
            for (int index = 0; index < static_cast<int>(std::size(columns)); ++index) {
                LVCOLUMNW column{};
                column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                column.pszText = const_cast<wchar_t*>(columns[index].title);
                column.cx = Scale(*state, columns[index].width);
                column.iSubItem = index;
                ListView_InsertColumn(state->resultList, index, &column);
            }

            UpdateFonts(window, *state);
            SetRunning(*state, false);
            LayoutControls(window, *state);
            return 0;
        }
        case WM_SIZE:
            if (state != nullptr) LayoutControls(window, *state);
            return 0;
        case WM_DPICHANGED:
            if (state != nullptr) {
                state->dpi = HIWORD(wParam);
                const auto* suggested = reinterpret_cast<RECT*>(lParam);
                SetWindowPos(window, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                UpdateFonts(window, *state);
                LayoutControls(window, *state);
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi = state != nullptr ? state->dpi : 96;
            info->ptMinTrackSize.x = MulDiv(860, static_cast<int>(dpi), 96);
            info->ptMinTrackSize.y = MulDiv(500, static_cast<int>(dpi), 96);
            return 0;
        }
        case WM_COMMAND:
            if (state == nullptr) break;
            switch (LOWORD(wParam)) {
                case FileButton: {
                    std::wstring selected;
                    if (SelectPath(window, false, selected)) {
                        state->scanMode = ScanMode::SingleModule;
                        SetWindowTextW(state->pathEdit, selected.c_str());
                        SetWindowTextW(state->statusLabel, L"Einzelmodul ausgewählt");
                    }
                    return 0;
                }
                case ScanFolderButton: {
                    std::wstring selected;
                    if (SelectPath(window, true, selected, L"VST3-Scanordner auswählen")) {
                        state->scanMode = ScanMode::Folder;
                        SetWindowTextW(state->pathEdit, selected.c_str());
                        SetWindowTextW(state->statusLabel, L"Scanordner ausgewählt");
                    }
                    return 0;
                }
                case RunButton:
                    StartProbe(window, *state);
                    return 0;
                case ExportCsvButton:
                    ExportInventory(window, *state, false);
                    return 0;
                case ExportJsonButton:
                    ExportInventory(window, *state, true);
                    return 0;
                case StopButton:
                    SetWindowTextW(state->statusLabel, L"Vorgang wird beendet ...");
                    EnableWindow(state->stopButton, FALSE);
                    RequestStop(*state);
                    return 0;
            }
            break;
        case WM_NOTIFY:
            if (state != nullptr) {
                const auto* header = reinterpret_cast<NMHDR*>(lParam);
                if (header->hwndFrom == state->resultList && header->code == LVN_COLUMNCLICK) {
                    const auto* notification = reinterpret_cast<NMLISTVIEW*>(lParam);
                    SortInventory(*state, notification->iSubItem);
                    return 0;
                }
            }
            break;
        case kProgressMessage:
            if (state != nullptr) {
                std::wstring progress;
                {
                    std::lock_guard lock(state->resultMutex);
                    progress = state->progressStatus;
                }
                SetWindowTextW(state->statusLabel, progress.c_str());
            }
            return 0;
        case kProbeDoneMessage:
            if (state != nullptr) {
                if (state->worker.joinable()) state->worker.join();
                ProbeRunResult result;
                {
                    std::lock_guard lock(state->resultMutex);
                    result = state->result;
                }
                SetWindowTextW(state->statusLabel, result.status.c_str());
                state->displayedInventory = std::move(result.inventory);
                state->displayedIssues = std::move(result.issues);
                PopulateResultsList(*state);
                SetRunning(*state, false);
            }
            return 0;
        case WM_CLOSE:
            if (state != nullptr) {
                state->closing.store(true);
                RequestStop(*state);
                if (state->worker.joinable()) state->worker.join();
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (state != nullptr) {
                state->closing.store(true);
                RequestStop(*state);
                if (state->worker.joinable()) state->worker.join();
                if (state->uiFont != nullptr) DeleteObject(state->uiFont);
                state->uiFont = nullptr;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX commonControls{sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&commonControls);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                       COINIT_DISABLE_OLE1DDE);
    if (FAILED(comResult)) {
        MessageBoxW(nullptr, L"Die Windows-Dateiauswahl konnte nicht initialisiert werden.",
                    kWindowTitle, MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIconSm = windowClass.hIcon;
    if (RegisterClassExW(&windowClass) == 0) {
        CoUninitialize();
        return 1;
    }

    AppState state;
    const UINT dpi = GetDpiForSystem();
    RECT desired{0, 0, MulDiv(960, static_cast<int>(dpi), 96),
                 MulDiv(680, static_cast<int>(dpi), 96)};
    AdjustWindowRectExForDpi(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    HWND window = CreateWindowExW(
        0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, desired.right - desired.left, desired.bottom - desired.top,
        nullptr, nullptr, instance, &state);
    if (window == nullptr) {
        CoUninitialize();
        return 1;
    }

    SetWindowTextW(window, kWindowTitle);
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
