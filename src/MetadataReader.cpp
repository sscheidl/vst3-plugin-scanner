#include "MetadataReader.h"

#include "StringUtil.h"
#include "VersionUtil.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <optional>
#include <system_error>
#include <vector>

namespace {

struct Translation {
    WORD language;
    WORD codePage;
};

std::wstring GetVersionString(void* data, WORD language, WORD codePage, const wchar_t* key) {
    std::wstring block = L"\\StringFileInfo\\";
    wchar_t langCode[16]{};
    swprintf_s(langCode, L"%04x%04x", language, codePage);
    block += langCode;
    block += L"\\";
    block += key;

    LPVOID value = nullptr;
    UINT size = 0;
    if (!VerQueryValueW(data, block.c_str(), &value, &size) || value == nullptr || size == 0) {
        return {};
    }
    const auto* text = static_cast<const wchar_t*>(value);
    std::wstring raw(text, text + size);
    const std::size_t terminator = raw.find(L'\0');
    if (terminator != std::wstring::npos) {
        raw.resize(terminator);
    }
    return Trim(raw);
}

void AddTranslation(std::vector<Translation>& translations, WORD language, WORD codePage) {
    const auto exists = std::any_of(translations.begin(), translations.end(), [&](const Translation& item) {
        return item.language == language && item.codePage == codePage;
    });
    if (!exists) {
        translations.push_back({ language, codePage });
    }
}

std::wstring GetVersionString(void* data,
                              const std::vector<Translation>& translations,
                              const wchar_t* key) {
    for (const auto& translation : translations) {
        const std::wstring value = GetVersionString(data, translation.language, translation.codePage, key);
        if (!value.empty()) {
            return value;
        }
    }
    return {};
}

std::wstring FormatFixedVersion(DWORD mostSignificant, DWORD leastSignificant) {
    if (mostSignificant == 0 && leastSignificant == 0) {
        return {};
    }
    return std::to_wstring(HIWORD(mostSignificant)) + L"." +
        std::to_wstring(LOWORD(mostSignificant)) + L"." +
        std::to_wstring(HIWORD(leastSignificant)) + L"." +
        std::to_wstring(LOWORD(leastSignificant));
}

bool IsJsonWhitespace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

void SkipJsonWhitespace(const std::wstring& text, std::size_t& index) {
    while (index < text.size() && IsJsonWhitespace(text[index])) {
        ++index;
    }
}

int HexValue(wchar_t c) {
    if (c >= L'0' && c <= L'9') {
        return c - L'0';
    }
    if (c >= L'a' && c <= L'f') {
        return 10 + c - L'a';
    }
    if (c >= L'A' && c <= L'F') {
        return 10 + c - L'A';
    }
    return -1;
}

bool ParseJsonStringAt(const std::wstring& text, std::size_t& index, std::wstring& value) {
    if (index >= text.size() || text[index] != L'"') {
        return false;
    }
    ++index;

    std::wstring result;
    while (index < text.size()) {
        wchar_t c = text[index++];
        if (c == L'"') {
            value = result;
            return true;
        }
        if (c != L'\\') {
            result.push_back(c);
            continue;
        }
        if (index >= text.size()) {
            return false;
        }

        wchar_t escaped = text[index++];
        switch (escaped) {
        case L'"':
        case L'\\':
        case L'/':
            result.push_back(escaped);
            break;
        case L'b':
            result.push_back(L'\b');
            break;
        case L'f':
            result.push_back(L'\f');
            break;
        case L'n':
            result.push_back(L'\n');
            break;
        case L'r':
            result.push_back(L'\r');
            break;
        case L't':
            result.push_back(L'\t');
            break;
        case L'u': {
            if (index + 4 > text.size()) {
                return false;
            }
            int codepoint = 0;
            for (int i = 0; i < 4; ++i) {
                const int hex = HexValue(text[index++]);
                if (hex < 0) {
                    return false;
                }
                codepoint = (codepoint << 4) | hex;
            }
            result.push_back(static_cast<wchar_t>(codepoint));
            break;
        }
        default:
            return false;
        }
    }
    return false;
}

void SkipJsonTrivia(const std::wstring& text, std::size_t& index) {
    while (index < text.size()) {
        SkipJsonWhitespace(text, index);
        if (index + 1 >= text.size() || text[index] != L'/') {
            return;
        }
        if (text[index + 1] == L'/') {
            index += 2;
            while (index < text.size() && text[index] != L'\n') {
                ++index;
            }
            continue;
        }
        if (text[index + 1] == L'*') {
            index += 2;
            while (index + 1 < text.size() && !(text[index] == L'*' && text[index + 1] == L'/')) {
                ++index;
            }
            if (index + 1 < text.size()) {
                index += 2;
            }
            continue;
        }
        return;
    }
}

std::optional<std::size_t> FindDirectMemberValue(const std::wstring& objectText,
                                                  const std::wstring& key) {
    int objectDepth = 0;
    int arrayDepth = 0;
    std::size_t index = 0;
    while (index < objectText.size()) {
        SkipJsonTrivia(objectText, index);
        if (index >= objectText.size()) {
            break;
        }

        const wchar_t c = objectText[index];
        if (c == L'{') {
            ++objectDepth;
            ++index;
            continue;
        }
        if (c == L'}') {
            --objectDepth;
            ++index;
            continue;
        }
        if (c == L'[') {
            ++arrayDepth;
            ++index;
            continue;
        }
        if (c == L']') {
            --arrayDepth;
            ++index;
            continue;
        }
        if (c != L'"') {
            ++index;
            continue;
        }

        const int keyObjectDepth = objectDepth;
        const int keyArrayDepth = arrayDepth;
        std::wstring parsedKey;
        if (!ParseJsonStringAt(objectText, index, parsedKey)) {
            return std::nullopt;
        }
        if (keyObjectDepth != 1 || keyArrayDepth != 0) {
            continue;
        }

        std::size_t valueStart = index;
        SkipJsonTrivia(objectText, valueStart);
        if (valueStart >= objectText.size() || objectText[valueStart] != L':') {
            continue;
        }
        ++valueStart;
        SkipJsonTrivia(objectText, valueStart);
        if (parsedKey == key) {
            return valueStart;
        }
    }
    return std::nullopt;
}

std::wstring ExtractDirectStringMember(const std::wstring& objectText,
                                       const std::wstring& key) {
    const auto valueStart = FindDirectMemberValue(objectText, key);
    if (!valueStart || *valueStart >= objectText.size() || objectText[*valueStart] != L'"') {
        return {};
    }
    std::size_t index = *valueStart;
    std::wstring value;
    return ParseJsonStringAt(objectText, index, value) ? Trim(value) : std::wstring{};
}

std::wstring ExtractDirectObjectMember(const std::wstring& objectText,
                                       const std::wstring& key) {
    const auto valueStart = FindDirectMemberValue(objectText, key);
    if (!valueStart || *valueStart >= objectText.size() || objectText[*valueStart] != L'{') {
        return {};
    }

    int depth = 0;
    std::size_t index = *valueStart;
    const std::size_t start = index;
    while (index < objectText.size()) {
        if (objectText[index] == L'"') {
            std::wstring ignored;
            if (!ParseJsonStringAt(objectText, index, ignored)) {
                return {};
            }
            continue;
        }
        if (index + 1 < objectText.size() && objectText[index] == L'/' &&
            (objectText[index + 1] == L'/' || objectText[index + 1] == L'*')) {
            SkipJsonTrivia(objectText, index);
            continue;
        }
        if (objectText[index] == L'{') {
            ++depth;
        } else if (objectText[index] == L'}') {
            --depth;
            if (depth == 0) {
                return objectText.substr(start, index - start + 1);
            }
        }
        ++index;
    }
    return {};
}

std::wstring ReadTextFileUtf8(const std::filesystem::path& path) {
    constexpr std::uintmax_t maxMetadataFileSize = 4 * 1024 * 1024;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > maxMetadataFileSize) {
        return {};
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return Utf8ToWide(bytes);
}

std::wstring StemName(const std::filesystem::path& path) {
    if (ToLower(path.extension().wstring()) == L".aaxplugin") {
        return path.stem().wstring();
    }
    if (path.has_stem()) {
        return path.stem().wstring();
    }
    return path.filename().wstring();
}

std::wstring InferPluginCategory(const std::wstring& pluginName, const std::wstring& fileName) {
    const std::wstring haystack = ToLower(pluginName + L" " + fileName);

    const std::vector<std::pair<std::wstring, std::wstring>> rules = {
        {L"reverb", L"Reverb"},
        {L"verb", L"Reverb"},
        {L"delay", L"Delay"},
        {L"echo", L"Delay"},
        {L"compress", L"Compressor"},
        {L"comp", L"Compressor"},
        {L"limiter", L"Limiter"},
        {L"clipper", L"Limiter"},
        {L" eq", L"EQ"},
        {L"equalizer", L"EQ"},
        {L"filter", L"Filter"},
        {L"chorus", L"Modulation"},
        {L"flanger", L"Modulation"},
        {L"phaser", L"Modulation"},
        {L"satur", L"Saturation"},
        {L"distort", L"Distortion"},
        {L"drive", L"Distortion"},
        {L"synth", L"Instrument"},
        {L"piano", L"Instrument"},
        {L"organ", L"Instrument"},
        {L"drum", L"Instrument"},
        {L"sampler", L"Instrument"},
        {L"vocal", L"Vocal"},
        {L"tuner", L"Utility"},
        {L"meter", L"Metering"},
        {L"analyzer", L"Metering"},
    };

    for (const auto& [token, category] : rules) {
        if (haystack.find(token) != std::wstring::npos) {
            return category;
        }
    }
    return L"Unknown";
}

bool IsGenericVendorFolder(const std::wstring& folderName) {
    const std::wstring normalized = ToLower(folderName);
    const std::vector<std::wstring> genericNames = {
        L"",
        L"vst",
        L"vst2",
        L"vst3",
        L"vstplugins",
        L"plugins",
        L"plug-ins",
        L"common files",
        L"program files",
        L"program files (x86)",
        L"steinberg",
        L"audio",
        L"contents",
        L"resources",
        L"x86_64-win",
        L"x64-win",
        L"arm64-win",
        L"x64",
    };

    for (const auto& generic : genericNames) {
        if (normalized == generic) {
            return true;
        }
    }
    return false;
}

std::wstring MatchKnownVendor(const std::wstring& pluginName, const std::wstring& fileName) {
    const std::wstring haystack = ToLower(pluginName + L" " + fileName);
    const std::vector<std::pair<std::wstring, std::wstring>> vendors = {
        {L"fabfilter", L"FabFilter"},
        {L"arturia", L"Arturia"},
        {L"valhalla", L"Valhalla DSP"},
        {L"soundtoys", L"SoundToys"},
        {L"waveshell", L"Waves Audio"},
        {L"waves ", L"Waves Audio"},
        {L"izotope", L"iZotope"},
        {L"plugin alliance", L"Plugin Alliance"},
        {L"brainworx", L"Plugin Alliance"},
        {L"bx_", L"Plugin Alliance"},
        {L"softube", L"Softube"},
        {L"uhe", L"u-he"},
        {L"u-he", L"u-he"},
        {L"xfer", L"Xfer Records"},
        {L"serum", L"Xfer Records"},
        {L"native instruments", L"Native Instruments"},
        {L"kontakt", L"Native Instruments"},
        {L"replika", L"Native Instruments"},
        {L"eventide", L"Eventide"},
        {L"sonible", L"sonible"},
        {L"voxengo", L"Voxengo"},
        {L"ujam", L"UJAM"},
        {L"cableguys", L"Cableguys"},
        {L"dawesome", L"Dawesome"},
        {L"denise", L"Denise Audio"},
        {L"baby audio", L"BABY Audio"},
        {L"black rooster", L"Black Rooster Audio"},
        {L"melodyne", L"Celemony Software GmbH"},
    };

    for (const auto& [token, vendor] : vendors) {
        if (haystack.find(token) != std::wstring::npos) {
            return vendor;
        }
    }
    return {};
}

std::wstring InferManufacturerFromPath(const std::filesystem::path& pluginPath) {
    std::filesystem::path current = pluginPath.parent_path();

    for (int depth = 0; depth < 3 && !current.empty(); ++depth) {
        const std::wstring folder = current.filename().wstring();
        if (!IsGenericVendorFolder(folder)) {
            return folder;
        }
        current = current.parent_path();
    }
    return {};
}

std::wstring InferManufacturer(const std::filesystem::path& pluginPath,
                               const std::wstring& pluginName,
                               const std::wstring& fileName) {
    std::wstring vendor = MatchKnownVendor(pluginName, fileName);
    if (!vendor.empty()) {
        return vendor;
    }

    vendor = InferManufacturerFromPath(pluginPath);
    if (!vendor.empty()) {
        return vendor;
    }
    return {};
}

std::uintmax_t DirectorySize(const std::filesystem::path& path) {
    std::uintmax_t total = 0;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator iterator(
        path,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    std::filesystem::recursive_directory_iterator end;

    while (!ec && iterator != end) {
        if (iterator->is_regular_file(ec) && !ec) {
            total += iterator->file_size(ec);
            if (ec) {
                ec.clear();
            }
        }
        iterator.increment(ec);
        if (ec) {
            ec.clear();
        }
    }
    return total;
}

std::filesystem::path FindBinaryInDirectory(const std::filesystem::path& directory,
                                            const std::wstring& extension,
                                            const std::wstring& preferredStem) {
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec) {
        return {};
    }

    std::vector<std::filesystem::path> matches;
    std::filesystem::directory_iterator iterator(
        directory, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::directory_iterator end;
    while (!ec && iterator != end) {
        if (iterator->is_regular_file(ec) && !ec &&
            ToLower(iterator->path().extension().wstring()) == extension) {
            matches.push_back(iterator->path());
        }
        iterator.increment(ec);
    }

    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        return ToLower(left.wstring()) < ToLower(right.wstring());
    });
    const std::wstring normalizedStem = ToLower(preferredStem);
    const auto preferred = std::find_if(matches.begin(), matches.end(), [&](const auto& path) {
        return ToLower(path.stem().wstring()) == normalizedStem;
    });
    return preferred != matches.end() ? *preferred : (matches.empty() ? std::filesystem::path{} : matches.front());
}

} // namespace

MetadataReader::MetadataReader()
    : vst3SdkProbe_(CreateVst3SdkProbe()) {
}

MetadataReader::~MetadataReader() = default;
MetadataReader::MetadataReader(MetadataReader&&) noexcept = default;
MetadataReader& MetadataReader::operator=(MetadataReader&&) noexcept = default;

PluginRecord MetadataReader::ReadPlugin(const std::filesystem::path& pluginPath, PluginType type) const {
    PluginRecord record;
    record.pluginType = type;
    record.filePath = pluginPath.wstring();
    record.fileName = pluginPath.filename().wstring();
    record.pluginName = StemName(pluginPath);

    std::error_code ec;
    const bool isDirectory = std::filesystem::is_directory(pluginPath, ec);
    if (ec) {
        record.status = ScanStatus::AccessError;
        SetWarning(record, WarningCode::PathNotReadable, Utf8ToWide(ec.message()));
        return record;
    }

    if (isDirectory) {
        record.fileSize = DirectorySize(pluginPath);
    } else {
        record.fileSize = std::filesystem::file_size(pluginPath, ec);
        if (ec) {
            record.fileSize = 0;
            SetWarning(record, WarningCode::FileSizeNotReadable, Utf8ToWide(ec.message()));
        }
    }

    const auto modified = std::filesystem::last_write_time(pluginPath, ec);
    if (!ec) {
        record.modifiedDate = FormatFileTime(modified);
    } else if (record.warningCode == WarningCode::None) {
        SetWarning(record, WarningCode::ModifiedDateNotReadable, Utf8ToWide(ec.message()));
    }

    Vst3SdkProbeResult sdkInfo;
    VersionInfo moduleInfo;
    if (type == PluginType::Vst3) {
        if (vst3SdkProbe_ && vst3SdkProbe_->IsAvailable()) {
            sdkInfo = vst3SdkProbe_->Probe(pluginPath, std::chrono::seconds(10));
        }
        moduleInfo = ReadVst3ModuleInfo(pluginPath);
        record.metadataFromModuleInfo = !moduleInfo.productName.empty() ||
            !moduleInfo.companyName.empty() || !moduleInfo.productVersion.empty();
    }

    const std::filesystem::path metadataBinary = ResolveMetadataBinary(pluginPath, type);
    const VersionInfo windowsInfo = ReadWindowsVersionInfo(metadataBinary);

    const std::wstring productName = sdkInfo.succeeded && !sdkInfo.moduleName.empty()
        ? sdkInfo.moduleName
        : !moduleInfo.productName.empty()
        ? moduleInfo.productName
        : (!windowsInfo.productName.empty() ? windowsInfo.productName : windowsInfo.fileDescription);
    const std::wstring companyName = sdkInfo.succeeded && !sdkInfo.vendor.empty()
        ? sdkInfo.vendor
        : !moduleInfo.companyName.empty()
        ? moduleInfo.companyName
        : windowsInfo.companyName;

    if (!productName.empty()) {
        record.pluginName = productName;
    }
    record.manufacturer = companyName;
    if (Trim(record.manufacturer).empty()) {
        record.manufacturer = InferManufacturer(pluginPath, record.pluginName, record.fileName);
    }

    const auto assignVersion = [&](const std::wstring& value, VersionSource source) {
        if (!record.version.empty() || Trim(value).empty()) {
            return;
        }
        record.version = NormalizeVersionString(value);
        if (!record.version.empty()) {
            record.versionSource = source;
        }
    };
    if (sdkInfo.succeeded) {
        assignVersion(sdkInfo.version, VersionSource::Vst3SdkProbe);
    }
    assignVersion(moduleInfo.productVersion, VersionSource::Vst3ModuleInfo);
    assignVersion(windowsInfo.productVersion, VersionSource::WindowsProductVersion);
    assignVersion(windowsInfo.fileVersion, VersionSource::WindowsFileVersion);
    assignVersion(windowsInfo.fixedProductVersion, VersionSource::WindowsFixedFileInfo);
    assignVersion(windowsInfo.fixedFileVersion, VersionSource::WindowsFixedFileInfo);
    assignVersion(ExtractVersionFromText(StemName(pluginPath)), VersionSource::FileName);

    record.category = sdkInfo.succeeded && !Trim(sdkInfo.category).empty()
        ? sdkInfo.category
        : InferPluginCategory(record.pluginName, record.fileName);

    const bool hasName = !Trim(record.pluginName).empty();
    const bool hasManufacturer = !Trim(record.manufacturer).empty();
    const bool hasVersion = !Trim(record.version).empty();
    const bool hasReliableVersion = hasVersion && record.versionSource != VersionSource::FileName;

    if (hasName && hasManufacturer && hasReliableVersion) {
        record.status = ScanStatus::Recognized;
    } else if (hasName || hasManufacturer || hasVersion) {
        record.status = ScanStatus::PartiallyRecognized;
        if (record.warningCode == WarningCode::None) {
            SetWarning(record, record.versionSource == VersionSource::FileName
                ? WarningCode::HeuristicVersionFromFileName
                : WarningCode::IncompleteMetadata);
        }
    } else {
        record.status = ScanStatus::Unknown;
        record.pluginName = StemName(pluginPath);
        if (record.warningCode == WarningCode::None) {
            SetWarning(record, WarningCode::NoWindowsVersionInfo);
        }
    }

    return record;
}

MetadataReader::VersionInfo MetadataReader::ReadWindowsVersionInfo(const std::filesystem::path& filePath) const {
    VersionInfo result;
    if (filePath.empty()) {
        return result;
    }

    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(filePath.c_str(), &handle);
    if (size == 0) {
        return result;
    }

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(filePath.c_str(), 0, size, data.data())) {
        return result;
    }

    Translation* translations = nullptr;
    UINT translationSize = 0;
    std::vector<Translation> translationCandidates;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                       reinterpret_cast<LPVOID*>(&translations), &translationSize) &&
        translations != nullptr && translationSize >= sizeof(Translation)) {
        const std::size_t count = translationSize / sizeof(Translation);
        for (std::size_t i = 0; i < count; ++i) {
            AddTranslation(translationCandidates, translations[i].language, translations[i].codePage);
        }
    }
    AddTranslation(translationCandidates, 0x0409, 0x04B0); // en-US, Unicode
    AddTranslation(translationCandidates, 0x0000, 0x04B0); // language-neutral, Unicode

    result.fileDescription = GetVersionString(data.data(), translationCandidates, L"FileDescription");
    result.productName = GetVersionString(data.data(), translationCandidates, L"ProductName");
    result.companyName = GetVersionString(data.data(), translationCandidates, L"CompanyName");
    result.fileVersion = GetVersionString(data.data(), translationCandidates, L"FileVersion");
    result.productVersion = GetVersionString(data.data(), translationCandidates, L"ProductVersion");

    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT fixedInfoSize = 0;
    if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&fixedInfo), &fixedInfoSize) &&
        fixedInfo != nullptr && fixedInfoSize >= sizeof(VS_FIXEDFILEINFO) &&
        fixedInfo->dwSignature == VS_FFI_SIGNATURE) {
        result.fixedFileVersion = FormatFixedVersion(
            fixedInfo->dwFileVersionMS, fixedInfo->dwFileVersionLS);
        result.fixedProductVersion = FormatFixedVersion(
            fixedInfo->dwProductVersionMS, fixedInfo->dwProductVersionLS);
    }
    return result;
}

MetadataReader::VersionInfo MetadataReader::ReadVst3ModuleInfo(const std::filesystem::path& pluginPath) const {
    VersionInfo result;
    std::error_code directoryError;
    if (!std::filesystem::is_directory(pluginPath, directoryError) || directoryError) {
        return result;
    }

    const std::filesystem::path candidates[] = {
        pluginPath / L"Contents" / L"Resources" / L"moduleinfo.json",
        pluginPath / L"Resources" / L"moduleinfo.json",
        pluginPath / L"moduleinfo.json",
    };

    for (const auto& candidate : candidates) {
        std::error_code existsError;
        if (!std::filesystem::exists(candidate, existsError) || existsError) {
            continue;
        }
        const std::wstring text = ReadTextFileUtf8(candidate);
        if (text.empty()) {
            continue;
        }
        result.productName = ExtractDirectStringMember(text, L"Name");
        result.productVersion = ExtractDirectStringMember(text, L"Version");
        const std::wstring factoryInfo = ExtractDirectObjectMember(text, L"Factory Info");
        result.companyName = ExtractDirectStringMember(factoryInfo, L"Vendor");
        return result;
    }
    return result;
}

std::filesystem::path MetadataReader::ResolveMetadataBinary(const std::filesystem::path& pluginPath, PluginType type) const {
    std::error_code ec;
    if (type == PluginType::Vst2 || type == PluginType::Clap || !std::filesystem::is_directory(pluginPath, ec) || ec) {
        return pluginPath;
    }

    if (type == PluginType::Aax) {
        const std::filesystem::path contents = pluginPath / L"Contents";
        if (std::filesystem::exists(contents, ec) && !ec) {
            std::filesystem::recursive_directory_iterator iterator(
                contents,
                std::filesystem::directory_options::skip_permission_denied,
                ec);
            std::filesystem::recursive_directory_iterator end;
            while (!ec && iterator != end) {
                if (iterator->is_regular_file(ec) && !ec && ToLower(iterator->path().extension().wstring()) == L".aaxplugin") {
                    return iterator->path();
                }
                iterator.increment(ec);
                if (ec) {
                    ec.clear();
                }
            }
        }
        return pluginPath;
    }

    if (type == PluginType::Vst3) {
        const std::filesystem::path contents = pluginPath / L"Contents";
        const std::wstring bundleStem = pluginPath.stem().wstring();
        const std::array<const wchar_t*, 6> architectureDirectories = {
            L"x86_64-win",
            L"x64-win",
            L"arm64ec-win",
            L"arm64-win",
            L"x86-win",
            L"arm-win",
        };
        for (const wchar_t* architecture : architectureDirectories) {
            const auto binary = FindBinaryInDirectory(contents / architecture, L".vst3", bundleStem);
            if (!binary.empty()) {
                return binary;
            }
        }

        const auto directBinary = FindBinaryInDirectory(pluginPath, L".vst3", bundleStem);
        if (!directBinary.empty()) {
            return directBinary;
        }

        // Unknown future architectures are inspected only after all official layouts.
        if (std::filesystem::exists(contents, ec) && !ec) {
            std::vector<std::filesystem::path> fallbackBinaries;
            std::filesystem::recursive_directory_iterator iterator(
                contents, std::filesystem::directory_options::skip_permission_denied, ec);
            const std::filesystem::recursive_directory_iterator end;
            while (!ec && iterator != end) {
                if (iterator->is_regular_file(ec) && !ec &&
                    ToLower(iterator->path().extension().wstring()) == L".vst3") {
                    fallbackBinaries.push_back(iterator->path());
                }
                iterator.increment(ec);
            }
            std::sort(fallbackBinaries.begin(), fallbackBinaries.end(), [](const auto& left, const auto& right) {
                return ToLower(left.wstring()) < ToLower(right.wstring());
            });
            if (!fallbackBinaries.empty()) {
                return fallbackBinaries.front();
            }
        }
        return pluginPath;
    }

    return pluginPath;
}
