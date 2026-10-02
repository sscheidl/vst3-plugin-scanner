#include "MetadataReader.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Expect(bool condition, const char* testName) {
    if (!condition) {
        std::cerr << "FAILED " << testName << '\n';
        ++failures;
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
            (L"VstPluginScannerTests-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_, ec);
    }

    ~TemporaryDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& Path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    TemporaryDirectory temporary;
    MetadataReader reader;

    const auto bundle = temporary.Path() / L"Fixture.vst3";
    const auto resources = bundle / L"Contents" / L"Resources";
    std::filesystem::create_directories(resources);
    {
        std::ofstream moduleInfo(resources / L"moduleinfo.json", std::ios::binary);
        moduleInfo << R"json({
            "Classes": [{"Version": "99.0.0", "Vendor": "Wrong Vendor"}],
            "Factory Info": {"Vendor": "Correct Vendor",},
            "Version": "Version 1, 2, 3, 4",
            "Name": "Fixture Plugin",
        })json";
    }

    const PluginRecord moduleRecord = reader.ReadPlugin(bundle, PluginType::Vst3);
    Expect(moduleRecord.pluginName == L"Fixture Plugin", "module name");
    Expect(moduleRecord.manufacturer == L"Correct Vendor", "factory vendor");
    Expect(moduleRecord.version == L"1.2.3.4", "top-level module version");
    Expect(moduleRecord.versionSource == VersionSource::Vst3ModuleInfo, "module version source");
    Expect(moduleRecord.metadataFromModuleInfo, "module metadata marker");

    const auto filenameBundle = temporary.Path() / L"Fallback Plugin 4.5.6.vst3";
    std::filesystem::create_directories(filenameBundle);
    const PluginRecord filenameRecord = reader.ReadPlugin(filenameBundle, PluginType::Vst3);
    Expect(filenameRecord.version == L"4.5.6", "filename version fallback");
    Expect(filenameRecord.versionSource == VersionSource::FileName, "filename version source");

    if (argc > 1) {
        const PluginRecord executableRecord = reader.ReadPlugin(argv[1], PluginType::Vst2);
        Expect(executableRecord.version == L"1.2.0.0", "Windows version resource");
        Expect(executableRecord.versionSource == VersionSource::WindowsProductVersion,
               "Windows version source");
    } else {
        Expect(false, "scanner executable argument");
    }

    if (failures == 0) {
        std::cout << "All MetadataReader tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
