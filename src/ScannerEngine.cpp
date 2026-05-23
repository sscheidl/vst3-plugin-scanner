#include "ScannerEngine.h"

#include "DuplicateDetector.h"
#include "MetadataReader.h"
#include "PluginUserPrefs.h"
#include "StringUtil.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <optional>
#include <set>
#include <system_error>
#include <vector>

namespace {

struct Candidate {
    std::filesystem::path path;
    PluginType type;
};

bool HasExtension(const std::filesystem::path& path, const std::wstring& extension) {
    return ToLower(path.extension().wstring()) == extension;
}

template <typename T>
bool ReadStruct(const std::vector<unsigned char>& bytes, std::size_t offset, T& out) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
        return false;
    }
    std::memcpy(&out, bytes.data() + offset, sizeof(T));
    return true;
}

bool ReadNullTerminatedAscii(const std::vector<unsigned char>& bytes, std::size_t offset, std::string& out) {
    if (offset >= bytes.size()) {
        return false;
    }
    std::string result;
    for (std::size_t i = offset; i < bytes.size(); ++i) {
        const char c = static_cast<char>(bytes[i]);
        if (c == '\0') {
            out = result;
            return true;
        }
        result.push_back(c);
        if (result.size() > 512) {
            return false;
        }
    }
    return false;
}

std::optional<std::size_t> RvaToOffset(
    DWORD rva,
    const std::vector<IMAGE_SECTION_HEADER>& sections,
    std::size_t fileSize) {
    for (const auto& section : sections) {
        const DWORD start = section.VirtualAddress;
        const DWORD span = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
        const DWORD end = start + span;
        if (rva >= start && rva < end) {
            const std::size_t offset = static_cast<std::size_t>(section.PointerToRawData + (rva - start));
            if (offset < fileSize) {
                return offset;
            }
        }
    }
    return std::nullopt;
}

bool HasVst2EntrypointExport(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }

    std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());
    if (bytes.size() < sizeof(IMAGE_DOS_HEADER)) {
        return false;
    }

    IMAGE_DOS_HEADER dos{};
    if (!ReadStruct(bytes, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) {
        return false;
    }

    const std::size_t ntOffset = static_cast<std::size_t>(dos.e_lfanew);
    DWORD signature = 0;
    if (!ReadStruct(bytes, ntOffset, signature) || signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    IMAGE_FILE_HEADER fileHeader{};
    const std::size_t fileHeaderOffset = ntOffset + sizeof(DWORD);
    if (!ReadStruct(bytes, fileHeaderOffset, fileHeader)) {
        return false;
    }

    const std::size_t optionalOffset = fileHeaderOffset + sizeof(IMAGE_FILE_HEADER);
    WORD magic = 0;
    if (!ReadStruct(bytes, optionalOffset, magic)) {
        return false;
    }

    IMAGE_DATA_DIRECTORY exportDirectoryEntry{};
    if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        IMAGE_OPTIONAL_HEADER64 optionalHeader{};
        if (!ReadStruct(bytes, optionalOffset, optionalHeader) ||
            optionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
            return false;
        }
        exportDirectoryEntry = optionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    } else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        IMAGE_OPTIONAL_HEADER32 optionalHeader{};
        if (!ReadStruct(bytes, optionalOffset, optionalHeader) ||
            optionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
            return false;
        }
        exportDirectoryEntry = optionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    } else {
        return false;
    }

    if (exportDirectoryEntry.VirtualAddress == 0 || exportDirectoryEntry.Size == 0) {
        return false;
    }

    const std::size_t sectionOffset = optionalOffset + fileHeader.SizeOfOptionalHeader;
    std::vector<IMAGE_SECTION_HEADER> sections;
    sections.reserve(fileHeader.NumberOfSections);
    for (WORD i = 0; i < fileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!ReadStruct(bytes, sectionOffset + static_cast<std::size_t>(i) * sizeof(IMAGE_SECTION_HEADER), section)) {
            return false;
        }
        sections.push_back(section);
    }

    const auto exportOffset = RvaToOffset(exportDirectoryEntry.VirtualAddress, sections, bytes.size());
    if (!exportOffset) {
        return false;
    }

    IMAGE_EXPORT_DIRECTORY exportDirectory{};
    if (!ReadStruct(bytes, *exportOffset, exportDirectory) || exportDirectory.NumberOfNames == 0) {
        return false;
    }

    const auto namesOffset = RvaToOffset(exportDirectory.AddressOfNames, sections, bytes.size());
    if (!namesOffset) {
        return false;
    }

    for (DWORD i = 0; i < exportDirectory.NumberOfNames; ++i) {
        DWORD nameRva = 0;
        if (!ReadStruct(bytes, *namesOffset + static_cast<std::size_t>(i) * sizeof(DWORD), nameRva)) {
            return false;
        }
        const auto nameOffset = RvaToOffset(nameRva, sections, bytes.size());
        if (!nameOffset) {
            continue;
        }
        std::string exportName;
        if (ReadNullTerminatedAscii(bytes, *nameOffset, exportName) &&
            (exportName == "VSTPluginMain" || exportName == "main")) {
            return true;
        }
    }

    return false;
}

void AddUniquePath(std::vector<std::filesystem::path>& paths, const std::wstring& value) {
    const std::wstring trimmed = Trim(value);
    if (trimmed.empty()) {
        return;
    }

    std::filesystem::path path(trimmed);
    std::wstring key = ToLower(path.wstring());
    for (const auto& existing : paths) {
        if (ToLower(existing.wstring()) == key) {
            return;
        }
    }
    paths.push_back(path);
}

void CollectCandidatesFromPath(const std::filesystem::path& root,
                               std::vector<Candidate>& candidates,
                               std::vector<std::wstring>& scannedPaths,
                               const std::atomic_bool& stopRequested,
                               const ScannerEngine::LogCallback& onLog) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) {
        onLog(L"Ungueltiger oder nicht erreichbarer Pfad: " + root.wstring());
        return;
    }
    if (!std::filesystem::is_directory(root, ec) || ec) {
        onLog(L"Pfad ist kein Ordner: " + root.wstring());
        return;
    }

    scannedPaths.push_back(root.wstring());

    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    std::filesystem::recursive_directory_iterator end;

    while (!stopRequested.load() && iterator != end) {
        if (ec) {
            onLog(L"Zugriff beim Scannen uebersprungen: " + Utf8ToWide(ec.message()));
            ec.clear();
        }

        const auto path = iterator->path();
        bool descend = true;
        try {
            if (iterator->is_directory(ec) && !ec && HasExtension(path, L".vst3")) {
                candidates.push_back({ path, PluginType::Vst3 });
                descend = false;
            } else if (iterator->is_regular_file(ec) && !ec) {
                if (HasExtension(path, L".dll")) {
                    if (HasVst2EntrypointExport(path)) {
                        candidates.push_back({ path, PluginType::Vst2 });
                    }
                } else if (HasExtension(path, L".vst3")) {
                    candidates.push_back({ path, PluginType::Vst3 });
                } else if (HasExtension(path, L".clap")) {
                    candidates.push_back({ path, PluginType::Clap });
                }
            } else if (iterator->is_directory(ec) && !ec && HasExtension(path, L".aaxplugin")) {
                candidates.push_back({ path, PluginType::Aax });
                descend = false;
            }
        } catch (const std::filesystem::filesystem_error& ex) {
            onLog(L"Fehler beim Zugriff: " + path.wstring() + L" (" + Utf8ToWide(ex.what()) + L")");
        }

        if (!descend) {
            iterator.disable_recursion_pending();
        }
        iterator.increment(ec);
    }
}

} // namespace

ScanSummary BuildSummary(const std::vector<PluginRecord>& records,
                         const std::vector<std::wstring>& scannedPaths,
                         const std::wstring& scanTimestamp) {
    ScanSummary summary;
    summary.scannedPaths = scannedPaths;
    summary.scanTimestamp = scanTimestamp.empty() ? CurrentTimestamp() : scanTimestamp;

    std::set<int> duplicateGroups;

    for (const auto& record : records) {
        switch (record.pluginType) {
        case PluginType::Vst2:
            ++summary.vst2Count;
            break;
        case PluginType::Vst3:
            ++summary.vst3Count;
            break;
        case PluginType::Clap:
            ++summary.clapCount;
            break;
        case PluginType::Aax:
            ++summary.aaxCount;
            break;
        case PluginType::Unknown:
        default:
            break;
        }
        if (record.isPossibleDuplicate && record.duplicateGroupId > 0) {
            ++summary.duplicateEntryCount;
            duplicateGroups.insert(record.duplicateGroupId);
            if (record.pluginType == PluginType::Vst2) {
                ++summary.vst2DuplicateCandidateCount;
            }
        }
        if (record.status == ScanStatus::AccessError ||
            record.status == ScanStatus::PartiallyRecognized ||
            record.status == ScanStatus::Unknown ||
            !record.warningMessage.empty()) {
            ++summary.warningCount;
        }
    }
    summary.duplicateCount = duplicateGroups.size();
    return summary;
}

ScanResult ScannerEngine::Scan(const ScanOptions& options,
                               const std::atomic_bool& stopRequested,
                               ProgressCallback onProgress,
                               LogCallback onLog) {
    ScanResult result;
    result.summary.scanTimestamp = CurrentTimestamp();

    std::vector<std::filesystem::path> roots;
    AddUniquePath(roots, options.vst2Path);
    AddUniquePath(roots, options.vst3Path);
    AddUniquePath(roots, options.clapPath);
    AddUniquePath(roots, options.aaxPath);
    AddUniquePath(roots, options.customPath);

    onProgress({ 0, 0, L"Sammle Plugin-Dateien..." });

    std::vector<Candidate> candidates;
    for (const auto& root : roots) {
        if (stopRequested.load()) {
            break;
        }
        onLog(L"Scanne Pfad: " + root.wstring());
        CollectCandidatesFromPath(root, candidates, result.summary.scannedPaths, stopRequested, onLog);
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return ToLower(left.path.wstring()) < ToLower(right.path.wstring());
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return ToLower(left.path.wstring()) == ToLower(right.path.wstring()) && left.type == right.type;
    }), candidates.end());

    MetadataReader reader;
    const std::size_t total = candidates.size();
    result.records.reserve(total);

    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (stopRequested.load()) {
            onLog(L"Scan wurde abgebrochen.");
            break;
        }

        const auto& candidate = candidates[index];
        onProgress({ index + 1, total, L"Lese Metadaten: " + candidate.path.filename().wstring() });

        try {
            PluginRecord record = reader.ReadPlugin(candidate.path, candidate.type);
            result.records.push_back(std::move(record));
        } catch (const std::exception& ex) {
            PluginRecord failed;
            failed.pluginType = candidate.type;
            failed.filePath = candidate.path.wstring();
            failed.fileName = candidate.path.filename().wstring();
            failed.pluginName = candidate.path.stem().wstring();
            failed.status = ScanStatus::AccessError;
            failed.warningMessage = Utf8ToWide(ex.what());
            result.records.push_back(std::move(failed));
            ++result.summary.warningCount;
        }
    }

    DuplicateDetector detector;
    detector.MarkDuplicates(result.records);

    const PluginUserPrefsResult userPrefs = ApplyPluginUserPrefs(PluginUserPrefsPathNextToExe(), result.records);
    if (!userPrefs.warningMessage.empty()) {
        onLog(userPrefs.warningMessage);
    } else if (userPrefs.rulesLoaded) {
        onLog(L"plugin_rules_userprefs.json angewendet: " + std::to_wstring(userPrefs.appliedCount) + L" Eintraege aktualisiert.");
    }

    result.summary = BuildSummary(result.records, result.summary.scannedPaths, result.summary.scanTimestamp);

    onProgress({ result.records.size(), total, stopRequested.load() ? L"Scan abgebrochen." : L"Scan abgeschlossen." });
    return result;
}
