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

[[nodiscard]] std::string CsvEscape(std::string_view value) {
    if (value.find_first_of(";\",\r\n") == std::string_view::npos) {
        return std::string(value);
    }
    std::string result = "\"";
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
    if (schemaVersion == nullptr || !schemaVersion->IsInt() || schemaVersion->GetInt() != 1) {
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
    if (!ReadString(*module, "path", result.modulePath, result.error) ||
        !ReadString(*module, "factoryVendor", result.factoryVendor, result.error)) {
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
        if (!ReadString(pluginClass, "cid", cid, result.error) ||
            !ReadString(pluginClass, "category", category, result.error) ||
            !ReadString(pluginClass, "name", name, result.error) ||
            !ReadString(pluginClass, "vendor", vendor, result.error) ||
            !ReadString(pluginClass, "version", version, result.error) ||
            !ReadString(pluginClass, "sdkVersion", sdkVersion, result.error) ||
            !ReadBoolean(pluginClass, "isAudioPlugin", isAudioPlugin, result.error) ||
            !ReadString(pluginClass, "diagnostic", diagnostic, result.error)) {
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

        InventoryRecord record;
        record.cid = Uppercase(std::move(cid));
        record.name = std::move(name);
        record.vendor = vendor.empty() ? result.factoryVendor : std::move(vendor);
        record.version = std::move(version);
        record.sdkVersion = std::move(sdkVersion);
        record.subCategories = std::move(parsedSubCategories);
        record.modulePath = result.modulePath;
        record.protocolStatus = result.protocolStatus;
        record.diagnostic = std::move(diagnostic);
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
    if (schemaVersion == nullptr || !schemaVersion->IsInt() || schemaVersion->GetInt() != 1) {
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
    output << "{\"schemaVersion\":1,\"entries\":[";
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
    output << "Hersteller;Plugin;Version;Kategorie;CID;Modulpfad;Dublette;Cache;Status;Diagnose\r\n";
    for (const auto& record : records) {
        output << CsvEscape(record.vendor) << ';' << CsvEscape(record.name) << ';'
               << CsvEscape(record.version) << ';' << CsvEscape(Join(record.subCategories, '|'))
               << ';' << CsvEscape(record.cid) << ';' << CsvEscape(record.modulePath) << ';'
               << (record.duplicate ? "ja (" + std::to_string(record.duplicateCount) + ')' : "nein")
               << ';' << (record.fromCache ? "ja" : "nein") << ';'
               << CsvEscape(record.protocolStatus) << ';' << CsvEscape(record.diagnostic) << "\r\n";
    }
    for (const auto& issue : issues) {
        output << ";;;;;" << CsvEscape(issue.modulePath) << ";nein;nein;"
               << CsvEscape(issue.status) << ';' << CsvEscape(issue.diagnostic) << "\r\n";
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
