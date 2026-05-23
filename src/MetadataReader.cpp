#include "MetadataReader.h"

#include "StringUtil.h"

#include <Windows.h>

#include <cstdint>
#include <fstream>
#include <system_error>
#include <vector>

namespace {

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
    std::wstring raw(static_cast<wchar_t*>(value));
    return Trim(raw);
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

std::wstring ExtractJsonString(const std::wstring& text, const std::wstring& key) {
    std::size_t index = 0;
    while (index < text.size()) {
        if (text[index] != L'"') {
            ++index;
            continue;
        }

        std::size_t keyStart = index;
        std::wstring parsedKey;
        if (!ParseJsonStringAt(text, keyStart, parsedKey)) {
            ++index;
            continue;
        }
        SkipJsonWhitespace(text, keyStart);
        if (keyStart >= text.size() || text[keyStart] != L':') {
            index = keyStart;
            continue;
        }
        ++keyStart;
        SkipJsonWhitespace(text, keyStart);
        if (parsedKey == key && keyStart < text.size() && text[keyStart] == L'"') {
            std::wstring parsedValue;
            if (ParseJsonStringAt(text, keyStart, parsedValue)) {
                return Trim(parsedValue);
            }
            return {};
        }
        index = keyStart;
    }
    return {};
}

std::wstring ReadTextFileUtf8(const std::filesystem::path& path) {
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
    std::filesystem::path current = pluginPath;
    if (ToLower(current.extension().wstring()) == L".vst3" ||
        ToLower(current.extension().wstring()) == L".aaxplugin") {
        current = current.parent_path();
    } else {
        current = current.parent_path();
    }

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

} // namespace

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
        record.warningMessage = L"Pfad konnte nicht gelesen werden: " + Utf8ToWide(ec.message());
        return record;
    }

    if (isDirectory) {
        record.fileSize = DirectorySize(pluginPath);
    } else {
        record.fileSize = std::filesystem::file_size(pluginPath, ec);
        if (ec) {
            record.fileSize = 0;
            record.warningMessage = L"Dateigroesse konnte nicht gelesen werden: " + Utf8ToWide(ec.message());
        }
    }

    const auto modified = std::filesystem::last_write_time(pluginPath, ec);
    if (!ec) {
        record.modifiedDate = FormatFileTime(modified);
    } else if (record.warningMessage.empty()) {
        record.warningMessage = L"Aenderungsdatum konnte nicht gelesen werden: " + Utf8ToWide(ec.message());
    }

    VersionInfo info;
    if (type == PluginType::Vst3) {
        info = ReadVst3ModuleInfo(pluginPath);
    }

    const std::filesystem::path metadataBinary = ResolveMetadataBinary(pluginPath, type);
    VersionInfo windowsInfo = ReadWindowsVersionInfo(metadataBinary);

    if (info.productName.empty()) {
        info.productName = windowsInfo.productName.empty() ? windowsInfo.fileDescription : windowsInfo.productName;
    }
    if (info.companyName.empty()) {
        info.companyName = windowsInfo.companyName;
    }
    if (info.productVersion.empty()) {
        info.productVersion = windowsInfo.productVersion.empty() ? windowsInfo.fileVersion : windowsInfo.productVersion;
    }

    if (!info.productName.empty()) {
        record.pluginName = info.productName;
    }
    record.manufacturer = info.companyName;
    if (Trim(record.manufacturer).empty()) {
        record.manufacturer = InferManufacturer(pluginPath, record.pluginName, record.fileName);
    }
    record.version = info.productVersion;
    record.category = InferPluginCategory(record.pluginName, record.fileName);

    const bool hasName = !Trim(record.pluginName).empty();
    const bool hasManufacturer = !Trim(record.manufacturer).empty();
    const bool hasVersion = !Trim(record.version).empty();

    if (hasName && hasManufacturer && hasVersion) {
        record.status = ScanStatus::Recognized;
    } else if (hasName || hasManufacturer || hasVersion) {
        record.status = ScanStatus::PartiallyRecognized;
        if (record.warningMessage.empty()) {
            record.warningMessage = L"Nicht alle Metadaten konnten zuverlaessig ermittelt werden.";
        }
    } else {
        record.status = ScanStatus::Unknown;
        record.pluginName = StemName(pluginPath);
        if (record.warningMessage.empty()) {
            record.warningMessage = L"Keine Windows-Versioninformationen gefunden.";
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

    struct Translation {
        WORD language;
        WORD codePage;
    };

    Translation* translations = nullptr;
    UINT translationSize = 0;
    WORD language = 0x0409;
    WORD codePage = 0x04B0;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                       reinterpret_cast<LPVOID*>(&translations), &translationSize) &&
        translations != nullptr && translationSize >= sizeof(Translation)) {
        language = translations[0].language;
        codePage = translations[0].codePage;
    }

    result.fileDescription = GetVersionString(data.data(), language, codePage, L"FileDescription");
    result.productName = GetVersionString(data.data(), language, codePage, L"ProductName");
    result.companyName = GetVersionString(data.data(), language, codePage, L"CompanyName");
    result.fileVersion = GetVersionString(data.data(), language, codePage, L"FileVersion");
    result.productVersion = GetVersionString(data.data(), language, codePage, L"ProductVersion");
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
        result.productName = ExtractJsonString(text, L"Name");
        result.companyName = ExtractJsonString(text, L"Vendor");
        result.productVersion = ExtractJsonString(text, L"Version");
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
        if (std::filesystem::exists(contents, ec) && !ec) {
            std::filesystem::recursive_directory_iterator iterator(
                contents,
                std::filesystem::directory_options::skip_permission_denied,
                ec);
            std::filesystem::recursive_directory_iterator end;
            while (!ec && iterator != end) {
                if (iterator->is_regular_file(ec) && !ec && ToLower(iterator->path().extension().wstring()) == L".vst3") {
                    return iterator->path();
                }
                iterator.increment(ec);
                if (ec) {
                    ec.clear();
                }
            }
        }

        if (std::filesystem::exists(pluginPath, ec) && !ec) {
            for (const auto& entry : std::filesystem::directory_iterator(pluginPath, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (!ec && entry.is_regular_file() && ToLower(entry.path().extension().wstring()) == L".vst3") {
                    return entry.path();
                }
            }
        }
        return pluginPath;
    }

    return pluginPath;
}
