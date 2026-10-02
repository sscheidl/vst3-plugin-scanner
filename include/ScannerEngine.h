#pragma once

#include "PluginRecord.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

struct ScanOptions {
    std::wstring vst2Path;
    std::wstring vst3Path;
    std::wstring clapPath;
    std::wstring aaxPath;
    std::wstring customPath;
    // Rules file the scan must apply. The GUI resolves this once so that editing,
    // saving and applying always operate on the very same file. Empty falls back
    // to the file next to the executable.
    std::wstring rulesPath;
};

struct ScanResult {
    std::vector<PluginRecord> records;
    ScanSummary summary;
};

ScanSummary BuildSummary(const std::vector<PluginRecord>& records,
                         const std::vector<std::wstring>& scannedPaths = {},
                         const std::wstring& scanTimestamp = {});

struct ScanProgress {
    std::size_t current = 0;
    std::size_t total = 0;
    std::wstring message;
};

class ScannerEngine {
public:
    using ProgressCallback = std::function<void(const ScanProgress&)>;
    using LogCallback = std::function<void(const std::wstring&)>;

    ScanResult Scan(const ScanOptions& options,
                    const std::atomic_bool& stopRequested,
                    ProgressCallback onProgress,
                    LogCallback onLog);
};
