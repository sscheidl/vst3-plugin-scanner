#pragma once

#include "PluginRecord.h"

#include <filesystem>
#include <string>

class MetadataReader {
public:
    PluginRecord ReadPlugin(const std::filesystem::path& pluginPath, PluginType type) const;

private:
    struct VersionInfo {
        std::wstring fileDescription;
        std::wstring productName;
        std::wstring companyName;
        std::wstring fileVersion;
        std::wstring productVersion;
    };

    VersionInfo ReadWindowsVersionInfo(const std::filesystem::path& filePath) const;
    VersionInfo ReadVst3ModuleInfo(const std::filesystem::path& pluginPath) const;
    std::filesystem::path ResolveMetadataBinary(const std::filesystem::path& pluginPath, PluginType type) const;
};

