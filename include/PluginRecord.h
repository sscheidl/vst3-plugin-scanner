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
        return L"erfolgreich erkannt";
    case ScanStatus::PartiallyRecognized:
        return L"Metadaten teilweise erkannt";
    case ScanStatus::AccessError:
        return L"Fehler beim Zugriff";
    case ScanStatus::Unknown:
    default:
        return L"unbekannt";
    }
}

[[nodiscard]] inline const wchar_t* ToDisplayText(VersionSource source) {
    switch (source) {
    case VersionSource::Vst3ModuleInfo:
        return L"VST3 moduleinfo.json";
    case VersionSource::Vst3SdkProbe:
        return L"VST3 SDK-Probe";
    case VersionSource::WindowsProductVersion:
        return L"Windows ProductVersion";
    case VersionSource::WindowsFileVersion:
        return L"Windows FileVersion";
    case VersionSource::WindowsFixedFileInfo:
        return L"Windows FixedFileInfo";
    case VersionSource::FileName:
        return L"Dateiname (Heuristik)";
    case VersionSource::UserRule:
        return L"Benutzerregel";
    case VersionSource::ManualEdit:
        return L"Manuelle Eingabe";
    case VersionSource::Unknown:
    default:
        return L"Unbekannt";
    }
}

[[nodiscard]] inline std::wstring ToDisplayText(const PluginRecord& record) {
    std::wstring text = ToDisplayText(record.status);
    if (record.metadataFromModuleInfo) {
        text += L" | VST3 moduleinfo.json";
    }
    if (record.metadataFromJson) {
        text += L" | Daten aus JSON";
    }
    if (record.metadataFromManualOverrides) {
        text += L" | Daten aus manualOverrides";
    }
    if (record.manuallyEdited) {
        text += L" | Manuell editiert";
    }
    return text;
}
