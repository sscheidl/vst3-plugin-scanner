#include "InventoryModel.h"

#include "JsonProtocol.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace vst3scanner {
namespace {

using JsonValue = rapidjson::Value;
constexpr rapidjson::SizeType kMaximumProtocolClasses = 100000U;

[[nodiscard]] const JsonValue* Member(const JsonValue& object, const char* name) {
    if (!object.IsObject()) return nullptr;
    const auto iterator = object.FindMember(name);
    return iterator == object.MemberEnd() ? nullptr : &iterator->value;
}

[[nodiscard]] bool ReadString(const JsonValue& object,
                              const char* name,
                              std::string& output,
                              std::string& error) {
    const auto* value = Member(object, name);
    if (value == nullptr || !value->IsString()) {
        error = std::string("Missing or invalid string field: ") + name;
        return false;
    }
    output.assign(value->GetString(), value->GetStringLength());
    return true;
}

[[nodiscard]] bool ReadBoolean(const JsonValue& object,
                               const char* name,
                               bool& output,
                               std::string& error) {
    const auto* value = Member(object, name);
    if (value == nullptr || !value->IsBool()) {
        error = std::string("Missing or invalid boolean field: ") + name;
        return false;
    }
    output = value->GetBool();
    return true;
}

[[nodiscard]] bool ReadInteger(const JsonValue& object,
                               const char* name,
                               std::int32_t& output,
                               std::string& error) {
    const auto* value = Member(object, name);
    if (value == nullptr || !value->IsInt()) {
        error = std::string("Missing or invalid integer field: ") + name;
        return false;
    }
    output = value->GetInt();
    return true;
}

[[nodiscard]] bool ReadUnsigned(const JsonValue& object,
                                const char* name,
                                std::uint32_t& output,
                                std::string& error) {
    const auto* value = Member(object, name);
    if (value == nullptr || !value->IsUint()) {
        error = std::string("Missing or invalid unsigned field: ") + name;
        return false;
    }
    output = value->GetUint();
    return true;
}

[[nodiscard]] bool IsValidCid(std::string_view cid) {
    return cid.size() == 32U && std::all_of(cid.begin(), cid.end(), [](unsigned char value) {
        return std::isxdigit(value) != 0;
    });
}

[[nodiscard]] bool IsValidCacheKey(std::string_view key) {
    return key.size() == 16U && std::all_of(key.begin(), key.end(), [](unsigned char value) {
        return std::isxdigit(value) != 0;
    });
}

[[nodiscard]] std::string Uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

[[nodiscard]] bool IsKnownProtocolStatus(std::string_view status) {
    static constexpr std::string_view values[] = {
        "ok",          "partial",       "not_vst3",     "wrong_architecture",
        "load_error",  "factory_missing", "factory_error", "no_classes",
        "timeout",     "crashed",       "access_error", "protocol_error",
    };
    return std::find(std::begin(values), std::end(values), status) != std::end(values);
}

[[nodiscard]] std::string Join(const std::vector<std::string>& values, char separator) {
    std::string result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) result.push_back(separator);
        result.append(values[index]);
    }
    return result;
}

[[nodiscard]] std::string FileNameFromPath(std::string_view path) {
    const auto separator = path.find_last_of("/\\");
    return std::string(separator == std::string_view::npos ? path : path.substr(separator + 1U));
}

[[nodiscard]] std::string CsvEscape(std::string_view value) {
    // Prevent spreadsheet applications from interpreting untrusted plug-in metadata
    // as a formula when the CSV is opened. Tabs and carriage returns are included
    // because some spreadsheet importers treat them as formula prefixes.
    const bool needsFormulaGuard = !value.empty() &&
        (value.front() == '=' || value.front() == '+' || value.front() == '-' ||
         value.front() == '@' || value.front() == '\t' || value.front() == '\r');
    if (!needsFormulaGuard && value.find_first_of(";\",\r\n\t") == std::string_view::npos) {
        return std::string(value);
    }
    std::string result = "\"";
    if (needsFormulaGuard) result.push_back('\'');
    for (const char character : value) {
        if (character == '\"') result.push_back('\"');
        result.push_back(character);
    }
    result.push_back('\"');
    return result;
}

void WriteJsonString(std::ostringstream& output, std::string_view value) {
    output << '\"' << JsonEscape(value) << '\"';
}

}  // namespace

ParsedProbeResult ParseProbeResultJson(std::string_view json) {
    ParsedProbeResult result;
    if (json.empty()) {
        result.error = "Probe returned no JSON output.";
        return result;
    }

    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    if (document.HasParseError()) {
        result.error = "Invalid JSON at offset " + std::to_string(document.GetErrorOffset()) +
                       ": " + rapidjson::GetParseError_En(document.GetParseError());
        return result;
    }
    if (!document.IsObject()) {
        result.error = "Protocol root must be a JSON object.";
        return result;
    }

    const auto* schemaVersion = Member(document, "schemaVersion");
    if (schemaVersion == nullptr || !schemaVersion->IsInt() || schemaVersion->GetInt() != 2) {
        result.error = "Unsupported or missing schemaVersion.";
        return result;
    }
    if (!ReadString(document, "status", result.protocolStatus, result.error)) return result;
    if (!IsKnownProtocolStatus(result.protocolStatus)) {
        result.error = "Unknown protocol status: " + result.protocolStatus;
        return result;
    }

    const auto* module = Member(document, "module");
    if (module == nullptr || !module->IsObject()) {
        result.error = "Missing or invalid module object.";
        return result;
    }
    std::string moduleName;
    std::string factoryUrl;
    std::string factoryEmail;
    bool isBundle = false;
    std::int32_t factoryFlags = 0;
    std::int32_t classCount = 0;
    if (!ReadString(*module, "path", result.modulePath, result.error) ||
        !ReadString(*module, "name", moduleName, result.error) ||
        !ReadBoolean(*module, "isBundle", isBundle, result.error) ||
        !ReadString(*module, "factoryVendor", result.factoryVendor, result.error) ||
        !ReadString(*module, "factoryUrl", factoryUrl, result.error) ||
        !ReadString(*module, "factoryEmail", factoryEmail, result.error) ||
        !ReadInteger(*module, "factoryFlags", factoryFlags, result.error) ||
        !ReadInteger(*module, "classCount", classCount, result.error)) {
        return result;
    }
    if (classCount < 0 || static_cast<rapidjson::SizeType>(classCount) > kMaximumProtocolClasses) {
        result.error = "module.classCount is outside the supported range.";
        return result;
    }
    const auto* duration = Member(*module, "probeDurationMs");
    if (duration == nullptr || !duration->IsUint64()) {
        result.error = "Missing or invalid module.probeDurationMs.";
        return result;
    }
    result.probeDurationMs = duration->GetUint64();
    if (!ReadString(document, "diagnostic", result.diagnostic, result.error)) return result;

    const auto* classes = Member(document, "classes");
    if (classes == nullptr || !classes->IsArray() ||
        classes->Size() > kMaximumProtocolClasses) {
        result.error = "Missing or invalid classes array.";
        return result;
    }
    if (classes->Size() != static_cast<rapidjson::SizeType>(classCount)) {
        result.error = "module.classCount does not match the classes array.";
        return result;
    }

    std::unordered_set<std::int32_t> classIndexes;
    std::unordered_set<std::string> audioCids;
    for (const auto& pluginClass : classes->GetArray()) {
        if (!pluginClass.IsObject()) {
            result.error = "Every classes entry must be an object.";
            return result;
        }

        std::string cid;
        std::string category;
        std::string name;
        std::string vendor;
        std::string version;
        std::string sdkVersion;
        std::string diagnostic;
        bool isAudioPlugin = false;
        bool versionMissing = false;
        std::int32_t index = -1;
        std::int32_t cardinality = 0;
        std::int32_t factoryInterface = 0;
        std::uint32_t classFlags = 0;
        if (!ReadInteger(pluginClass, "index", index, result.error) ||
            !ReadString(pluginClass, "cid", cid, result.error) ||
            !ReadString(pluginClass, "category", category, result.error) ||
            !ReadString(pluginClass, "name", name, result.error) ||
            !ReadString(pluginClass, "vendor", vendor, result.error) ||
            !ReadString(pluginClass, "version", version, result.error) ||
            !ReadString(pluginClass, "sdkVersion", sdkVersion, result.error) ||
            !ReadUnsigned(pluginClass, "classFlags", classFlags, result.error) ||
            !ReadInteger(pluginClass, "cardinality", cardinality, result.error) ||
            !ReadInteger(pluginClass, "factoryInterface", factoryInterface, result.error) ||
            !ReadBoolean(pluginClass, "isAudioPlugin", isAudioPlugin, result.error) ||
            !ReadBoolean(pluginClass, "versionMissing", versionMissing, result.error) ||
            !ReadString(pluginClass, "diagnostic", diagnostic, result.error)) {
            return result;
        }

        if (index < 0 || index >= classCount || !classIndexes.insert(index).second) {
            result.error = "Class index is outside the module range or duplicated.";
            return result;
        }
        if (factoryInterface < 0 || factoryInterface > 3) {
            result.error = "factoryInterface is outside the supported range.";
            return result;
        }
        if (versionMissing != version.empty()) {
            result.error = "Inconsistent version/versionMissing fields.";
            return result;
        }

        const bool categoryIsAudio = category == "Audio Module Class";
        if (categoryIsAudio != isAudioPlugin) {
            result.error = "Inconsistent isAudioPlugin/category fields.";
            return result;
        }

        const auto* subCategories = Member(pluginClass, "subCategories");
        if (subCategories == nullptr || !subCategories->IsArray()) {
            result.error = "Missing or invalid subCategories array.";
            return result;
        }
        std::vector<std::string> parsedSubCategories;
        for (const auto& item : subCategories->GetArray()) {
            if (!item.IsString()) {
                result.error = "subCategories entries must be strings.";
                return result;
            }
            parsedSubCategories.emplace_back(item.GetString(), item.GetStringLength());
        }

        if (!isAudioPlugin) continue;
        if (!IsValidCid(cid)) {
            result.error = "Audio class contains an invalid CID: " + cid;
            return result;
        }

        cid = Uppercase(std::move(cid));
        if (!audioCids.insert(cid).second) {
            if (result.protocolStatus != "partial") {
                result.error = "Duplicate audio class CID in a non-partial probe response: " + cid;
                return result;
            }
            continue;
        }

        InventoryRecord record;
        record.cid = std::move(cid);
        record.name = std::move(name);
        record.vendor = vendor.empty() ? result.factoryVendor : std::move(vendor);
        record.version = std::move(version);
        record.versionMissing = versionMissing;
        record.sdkVersion = std::move(sdkVersion);
        record.subCategories = std::move(parsedSubCategories);
        record.modulePath = result.modulePath;
        record.protocolStatus = result.protocolStatus;
        record.diagnostic = diagnostic.empty() ? result.diagnostic : std::move(diagnostic);
        record.probeDurationMs = result.probeDurationMs;
        result.audioPlugins.push_back(std::move(record));
    }

    result.valid = true;
    return result;
}

ParsedProbeCache ParseProbeCacheJson(std::string_view json) {
    ParsedProbeCache result;
    if (json.empty()) {
        result.error = "Cache file is empty.";
        return result;
    }

    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    if (document.HasParseError()) {
        result.error = "Invalid cache JSON at offset " +
                       std::to_string(document.GetErrorOffset()) + ": " +
                       rapidjson::GetParseError_En(document.GetParseError());
        return result;
    }
    if (!document.IsObject()) {
        result.error = "Cache root must be a JSON object.";
        return result;
    }
    const auto* schemaVersion = Member(document, "schemaVersion");
    if (schemaVersion == nullptr || !schemaVersion->IsInt() || schemaVersion->GetInt() != 2) {
        result.error = "Unsupported or missing cache schemaVersion.";
        return result;
    }
    const auto* entries = Member(document, "entries");
    if (entries == nullptr || !entries->IsArray() || entries->Size() > 100000U) {
        result.error = "Missing, invalid, or excessive cache entries array.";
        return result;
    }

    std::unordered_set<std::string> keys;
    for (const auto& item : entries->GetArray()) {
        ProbeCacheEntry entry;
        if (!item.IsObject() || !ReadString(item, "key", entry.key, result.error) ||
            !ReadString(item, "probeJson", entry.probeJson, result.error)) {
            return result;
        }
        entry.key = Uppercase(std::move(entry.key));
        if (!IsValidCacheKey(entry.key)) {
            result.error = "Cache entry contains an invalid key.";
            return result;
        }
        if (!keys.insert(entry.key).second) {
            result.error = "Cache contains duplicate keys.";
            return result;
        }
        const auto parsedProbe = ParseProbeResultJson(entry.probeJson);
        if (!parsedProbe.valid ||
            (parsedProbe.protocolStatus != "ok" && parsedProbe.protocolStatus != "partial")) {
            result.error = "Cache entry contains an invalid probe response.";
            return result;
        }
        result.entries.push_back(std::move(entry));
    }
    result.valid = true;
    return result;
}

std::string SerializeProbeCacheJson(const std::vector<ProbeCacheEntry>& entries) {
    auto sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        return left.key < right.key;
    });
    std::ostringstream output;
    output << "{\"schemaVersion\":2,\"entries\":[";
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (index != 0) output << ',';
        output << "{\"key\":";
        WriteJsonString(output, Uppercase(sorted[index].key));
        output << ",\"probeJson\":";
        WriteJsonString(output, sorted[index].probeJson);
        output << '}';
    }
    output << "]}";
    return output.str();
}

void MarkCidDuplicates(std::vector<InventoryRecord>& records) {
    std::unordered_map<std::string, std::unordered_set<std::string>> locations;
    for (const auto& record : records) {
        locations[Uppercase(record.cid)].insert(record.modulePath);
    }
    for (auto& record : records) {
        record.duplicateCount = locations[Uppercase(record.cid)].size();
        record.duplicate = record.duplicateCount > 1U;
    }
}

std::string SerializeInventoryCsv(const std::vector<InventoryRecord>& records,
                                  const std::vector<ScanIssue>& issues) {
    std::ostringstream output;
    output << "Plugin;Vendor;Version;Version source;SDK version;Category;Module;Module path;"
              "Duplicate;Cache;Status;Duration (ms);Diagnostic\r\n";
    for (const auto& record : records) {
        output << CsvEscape(record.name) << ';' << CsvEscape(record.vendor) << ';'
               << CsvEscape(record.version) << ';'
               << (record.versionMissing ? "Not reported" : "VST3 factory") << ';'
               << CsvEscape(record.sdkVersion) << ';'
               << CsvEscape(Join(record.subCategories, '|')) << ';'
               << CsvEscape(FileNameFromPath(record.modulePath)) << ';'
               << CsvEscape(record.modulePath) << ';'
               << (record.duplicate ? "Yes (" + std::to_string(record.duplicateCount) + ')' : "No")
               << ';' << (record.fromCache ? "Yes" : "No") << ';'
               << CsvEscape(record.protocolStatus) << ';' << record.probeDurationMs << ';'
               << CsvEscape(record.diagnostic) << "\r\n";
    }
    for (const auto& issue : issues) {
        const auto module = FileNameFromPath(issue.modulePath);
        output << CsvEscape("[Problem] " + module) << ";;;;;;" << CsvEscape(module) << ';'
               << CsvEscape(issue.modulePath) << ";No;No;" << CsvEscape(issue.status)
               << ";;" << CsvEscape(issue.diagnostic) << "\r\n";
    }
    return output.str();
}

std::string SerializeInventoryJson(const std::vector<InventoryRecord>& records,
                                   const std::vector<ScanIssue>& issues) {
    std::ostringstream output;
    output << "{\"schemaVersion\":1,\"plugins\":[";
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (index != 0) output << ',';
        const auto& record = records[index];
        output << "{\"cid\":"; WriteJsonString(output, record.cid);
        output << ",\"name\":"; WriteJsonString(output, record.name);
        output << ",\"vendor\":"; WriteJsonString(output, record.vendor);
        output << ",\"version\":"; WriteJsonString(output, record.version);
        output << ",\"versionMissing\":" << (record.versionMissing ? "true" : "false");
        output << ",\"sdkVersion\":"; WriteJsonString(output, record.sdkVersion);
        output << ",\"subCategories\":[";
        for (std::size_t category = 0; category < record.subCategories.size(); ++category) {
            if (category != 0) output << ',';
            WriteJsonString(output, record.subCategories[category]);
        }
        output << "],\"modulePath\":"; WriteJsonString(output, record.modulePath);
        output << ",\"duplicate\":" << (record.duplicate ? "true" : "false")
               << ",\"duplicateCount\":" << record.duplicateCount
               << ",\"fromCache\":" << (record.fromCache ? "true" : "false")
               << ",\"probeDurationMs\":" << record.probeDurationMs
               << ",\"status\":"; WriteJsonString(output, record.protocolStatus);
        output << ",\"diagnostic\":"; WriteJsonString(output, record.diagnostic);
        output << '}';
    }
    output << "],\"issues\":[";
    for (std::size_t index = 0; index < issues.size(); ++index) {
        if (index != 0) output << ',';
        const auto& issue = issues[index];
        output << "{\"modulePath\":"; WriteJsonString(output, issue.modulePath);
        output << ",\"status\":"; WriteJsonString(output, issue.status);
        output << ",\"diagnostic\":"; WriteJsonString(output, issue.diagnostic);
        output << ",\"retried\":" << (issue.retried ? "true" : "false")
               << ",\"timedOut\":" << (issue.timedOut ? "true" : "false") << '}';
    }
    output << "]}";
    return output.str();
}

}  // namespace vst3scanner
