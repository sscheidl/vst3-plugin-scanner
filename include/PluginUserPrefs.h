#pragma once

#include "PluginRecord.h"

#include <filesystem>
#include <string>
#include <vector>

struct PluginUserPrefsResult {
    bool rulesFileFound = false;
    bool rulesLoaded = false;
    std::size_t appliedCount = 0;
    std::size_t manualOverrideCount = 0;
    std::wstring warningMessage;
};

struct SaveManualOverridesResult {
    bool success = false;
    std::size_t savedCount = 0;
    std::filesystem::path backupPath;
    std::wstring errorMessage;
};

std::filesystem::path PluginUserPrefsPathNextToExe();
PluginUserPrefsResult ApplyPluginUserPrefs(const std::filesystem::path& rulesPath,
                                           std::vector<PluginRecord>& records);
SaveManualOverridesResult SaveManualOverrides(const std::filesystem::path& rulesPath,
                                               const std::vector<PluginRecord>& records);
