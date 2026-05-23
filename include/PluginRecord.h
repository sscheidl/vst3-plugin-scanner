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

struct PluginRecord {
    std::wstring manufacturer;
    std::wstring pluginName;
    std::wstring category;
    std::wstring version;
    PluginType pluginType = PluginType::Unknown;
    std::wstring filePath;
    std::wstring fileName;
    std::uintmax_t fileSize = 0;
    std::wstring modifiedDate;
    int duplicateGroupId = 0;
    bool isPossibleDuplicate = false;
    ScanStatus status = ScanStatus::Unknown;
    std::wstring warningMessage;
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
