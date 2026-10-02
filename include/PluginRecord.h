#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class PluginType {
    Unknown,
    Vst2,
    Vst3,
    Clap,
    Aax
};

enum class ScanStatus {
    Recognized,
    PartiallyRecognized,
    Unknown,
    AccessError
};

enum class VersionSource {
    Unknown,
    Vst3ModuleInfo,
    Vst3SdkProbe,
    WindowsProductVersion,
    WindowsFileVersion,
    WindowsFixedFileInfo,
    FileName,
    UserRule,
    ManualEdit
};

// Warnings are identified by code, never by their display text. Logic in other
// translation units must compare codes so that wording stays a pure UI concern.
enum class WarningCode {
    None,
    PathNotReadable,
    FileSizeNotReadable,
    ModifiedDateNotReadable,
    HeuristicVersionFromFileName,
    IncompleteMetadata,
    NoWindowsVersionInfo,
    ScanError
};

struct PluginRecord {
    std::wstring manufacturer;
    std::wstring pluginName;
    std::wstring category;
    std::wstring version;
    VersionSource versionSource = VersionSource::Unknown;
    PluginType pluginType = PluginType::Unknown;
    std::wstring filePath;
    std::wstring fileName;
    std::uintmax_t fileSize = 0;
    std::wstring modifiedDate;
    int duplicateGroupId = 0;
    bool isPossibleDuplicate = false;
    ScanStatus status = ScanStatus::Unknown;
    WarningCode warningCode = WarningCode::None;
    std::wstring warningMessage;
    bool metadataFromModuleInfo = false;
    bool metadataFromJson = false;
    bool metadataFromManualOverrides = false;
    bool manuallyEdited = false;
    bool versionManuallyEdited = false;
};

struct ScanSummary {
    std::size_t vst2Count = 0;
    std::size_t vst3Count = 0;
    std::size_t clapCount = 0;
    std::size_t aaxCount = 0;
    std::size_t duplicateCount = 0; // Number of duplicate groups.
    std::size_t duplicateEntryCount = 0;
    std::size_t vst2DuplicateCandidateCount = 0;
    std::size_t warningCount = 0;
    std::size_t versionDetectedCount = 0;
    std::size_t versionHeuristicCount = 0;
    std::size_t versionMissingCount = 0;
    std::vector<std::wstring> scannedPaths;
    std::wstring scanTimestamp;
};

[[nodiscard]] inline const wchar_t* ToDisplayText(PluginType type) {
    switch (type) {
    case PluginType::Unknown:
        return L"Unknown";
    case PluginType::Vst2:
        return L"VST2";
    case PluginType::Vst3:
        return L"VST3";
    case PluginType::Clap:
        return L"CLAP";
    case PluginType::Aax:
        return L"AAX";
    default:
        return L"Unknown";
    }
}

[[nodiscard]] inline const wchar_t* ToDisplayText(ScanStatus status) {
    switch (status) {
    case ScanStatus::Recognized:
        return L"recognized";
    case ScanStatus::PartiallyRecognized:
        return L"metadata partially recognized";
    case ScanStatus::AccessError:
        return L"access error";
    case ScanStatus::Unknown:
    default:
        return L"unknown";
    }
}

[[nodiscard]] inline const wchar_t* ToDisplayText(VersionSource source) {
    switch (source) {
    case VersionSource::Vst3ModuleInfo:
        return L"VST3 moduleinfo.json";
    case VersionSource::Vst3SdkProbe:
        return L"VST3 SDK probe";
    case VersionSource::WindowsProductVersion:
        return L"Windows ProductVersion";
    case VersionSource::WindowsFileVersion:
        return L"Windows FileVersion";
    case VersionSource::WindowsFixedFileInfo:
        return L"Windows FixedFileInfo";
    case VersionSource::FileName:
        return L"file name (heuristic)";
    case VersionSource::UserRule:
        return L"user rule";
    case VersionSource::ManualEdit:
        return L"manual edit";
    case VersionSource::Unknown:
    default:
        return L"unknown";
    }
}

[[nodiscard]] inline const wchar_t* ToDisplayText(WarningCode code) {
    switch (code) {
    case WarningCode::PathNotReadable:
        return L"Path could not be read";
    case WarningCode::FileSizeNotReadable:
        return L"File size could not be read";
    case WarningCode::ModifiedDateNotReadable:
        return L"Modified date could not be read";
    case WarningCode::HeuristicVersionFromFileName:
        return L"Version number was only guessed from the file name";
    case WarningCode::IncompleteMetadata:
        return L"Not all metadata could be determined reliably";
    case WarningCode::NoWindowsVersionInfo:
        return L"No Windows version information found";
    case WarningCode::ScanError:
        return L"Scan error";
    case WarningCode::None:
    default:
        return L"";
    }
}

inline void SetWarning(PluginRecord& record, WarningCode code, const std::wstring& detail = {}) {
    record.warningCode = code;
    if (code == WarningCode::None) {
        record.warningMessage.clear();
        return;
    }
    record.warningMessage = ToDisplayText(code);
    if (!detail.empty()) {
        record.warningMessage += L": " + detail;
    }
}

inline void ClearWarning(PluginRecord& record) {
    SetWarning(record, WarningCode::None);
}

[[nodiscard]] inline std::wstring ToDisplayText(const PluginRecord& record) {
    std::wstring text = ToDisplayText(record.status);
    if (record.metadataFromModuleInfo) {
        text += L" | VST3 moduleinfo.json";
    }
    if (record.metadataFromJson) {
        text += L" | data from JSON";
    }
    if (record.metadataFromManualOverrides) {
        text += L" | data from manualOverrides";
    }
    if (record.manuallyEdited) {
        text += L" | manually edited";
    }
    return text;
}
