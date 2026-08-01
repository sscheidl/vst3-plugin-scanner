#pragma once

#include "PluginRecord.h"
#include "Vst3SdkProbe.h"

#include <filesystem>
#include <memory>
#include <string>

class MetadataReader {
public:
    MetadataReader();
    ~MetadataReader();
    MetadataReader(MetadataReader&&) noexcept;
    MetadataReader& operator=(MetadataReader&&) noexcept;
    MetadataReader(const MetadataReader&) = delete;
    MetadataReader& operator=(const MetadataReader&) = delete;

    PluginRecord ReadPlugin(const std::filesystem::path& pluginPath, PluginType type) const;

private:
    struct VersionInfo {
        std::wstring fileDescription;
        std::wstring productName;
        std::wstring companyName;
        std::wstring fileVersion;
        std::wstring productVersion;
        std::wstring fixedFileVersion;
        std::wstring fixedProductVersion;
    };

    VersionInfo ReadWindowsVersionInfo(const std::filesystem::path& filePath) const;
    VersionInfo ReadVst3ModuleInfo(const std::filesystem::path& pluginPath) const;
    std::filesystem::path ResolveMetadataBinary(const std::filesystem::path& pluginPath, PluginType type) const;

    std::unique_ptr<IVst3SdkProbe> vst3SdkProbe_;
};
