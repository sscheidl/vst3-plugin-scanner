#include "PluginUserPrefs.h"

#include "StringUtil.h"
#include "VersionUtil.h"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <system_error>

namespace {

struct JsonValue {
    enum class Type {
        Null,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    std::wstring stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::wstring, JsonValue> objectValue;
};

class JsonParser {
public:
    explicit JsonParser(std::wstring text) : text_(std::move(text)) {
        if (!text_.empty() && text_[0] == 0xFEFF) {
            text_.erase(text_.begin());
        }
    }

    bool Parse(JsonValue& value, std::wstring& error) {
        index_ = 0;
        SkipWhitespace();
        if (!ParseValue(value)) {
            error = L"JSON konnte nicht gelesen werden.";
            return false;
        }
        SkipWhitespace();
        if (index_ != text_.size()) {
            error = L"JSON enthaelt unerwartete Daten.";
            return false;
        }
        return true;
    }

private:
    bool ParseValue(JsonValue& value) {
        SkipWhitespace();
        if (index_ >= text_.size()) {
            return false;
        }
        if (text_[index_] == L'"') {
            value.type = JsonValue::Type::String;
            return ParseString(value.stringValue);
        }
        if (text_[index_] == L'[') {
            return ParseArray(value);
        }
        if (text_[index_] == L'{') {
            return ParseObject(value);
        }
        return ParseLiteral(value);
    }

    bool ParseObject(JsonValue& value) {
        if (text_[index_] != L'{') {
            return false;
        }
        ++index_;
        value.type = JsonValue::Type::Object;
        value.objectValue.clear();
        SkipWhitespace();
        if (index_ < text_.size() && text_[index_] == L'}') {
            ++index_;
            return true;
        }
        while (index_ < text_.size()) {
            std::wstring key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (index_ >= text_.size() || text_[index_] != L':') {
                return false;
            }
            ++index_;

            JsonValue child;
            if (!ParseValue(child)) {
                return false;
            }
            value.objectValue[std::move(key)] = std::move(child);

            SkipWhitespace();
            if (index_ >= text_.size()) {
                return false;
            }
            if (text_[index_] == L'}') {
                ++index_;
                return true;
            }
            if (text_[index_] != L',') {
                return false;
            }
            ++index_;
            SkipWhitespace();
        }
        return false;
    }

    bool ParseArray(JsonValue& value) {
        if (text_[index_] != L'[') {
            return false;
        }
        ++index_;
        value.type = JsonValue::Type::Array;
        value.arrayValue.clear();
        SkipWhitespace();
        if (index_ < text_.size() && text_[index_] == L']') {
            ++index_;
            return true;
        }
        while (index_ < text_.size()) {
            JsonValue child;
            if (!ParseValue(child)) {
                return false;
            }
            value.arrayValue.push_back(std::move(child));

            SkipWhitespace();
            if (index_ >= text_.size()) {
                return false;
            }
            if (text_[index_] == L']') {
                ++index_;
                return true;
            }
            if (text_[index_] != L',') {
                return false;
            }
            ++index_;
            SkipWhitespace();
        }
        return false;
    }

    bool ParseLiteral(JsonValue& value) {
        const std::size_t start = index_;
        while (index_ < text_.size()) {
            const wchar_t c = text_[index_];
            if (c == L',' || c == L'}' || c == L']' || IsWhitespace(c)) {
                break;
            }
            ++index_;
        }
        if (index_ == start) {
            return false;
        }
        value.type = JsonValue::Type::Null;
        return true;
    }

    bool ParseString(std::wstring& value) {
        if (index_ >= text_.size() || text_[index_] != L'"') {
            return false;
        }
        ++index_;

        std::wstring result;
        while (index_ < text_.size()) {
            wchar_t c = text_[index_++];
            if (c == L'"') {
                value = std::move(result);
                return true;
            }
            if (c != L'\\') {
                result.push_back(c);
                continue;
            }
            if (index_ >= text_.size()) {
                return false;
            }

            const wchar_t escaped = text_[index_++];
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
            case L'u':
                if (!ParseUnicodeEscape(result)) {
                    return false;
                }
                break;
            default:
                return false;
            }
        }
        return false;
    }

    bool ParseUnicodeEscape(std::wstring& result) {
        if (index_ + 4 > text_.size()) {
            return false;
        }
        int codepoint = 0;
        for (int i = 0; i < 4; ++i) {
            const int hex = HexValue(text_[index_++]);
            if (hex < 0) {
                return false;
            }
            codepoint = (codepoint << 4) | hex;
        }
        result.push_back(static_cast<wchar_t>(codepoint));
        return true;
    }

    void SkipWhitespace() {
        while (index_ < text_.size() && IsWhitespace(text_[index_])) {
            ++index_;
        }
    }

    static bool IsWhitespace(wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    }

    static int HexValue(wchar_t c) {
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

    std::wstring text_;
    std::size_t index_ = 0;
};

struct PluginRule {
    std::wstring pluginName;
    std::wstring vendor;
    std::wstring category;
    std::wstring version;
    std::vector<std::wstring> matchTokens;
    std::vector<std::wstring> typesSeen;
};

struct VendorRule {
    std::wstring vendor;
    std::vector<std::wstring> matchTokens;
};

struct ManualOverride {
    std::wstring fileName;
    std::wstring type;
    std::wstring pathContains;
    std::wstring manufacturer;
    std::wstring pluginName;
    std::wstring category;
    std::wstring notes;
};

struct UserPrefs {
    std::vector<PluginRule> pluginRules;
    std::vector<VendorRule> vendorRules;
    std::vector<ManualOverride> manualOverrides;
    std::map<std::wstring, std::wstring> vendorAliases;
};

const JsonValue* FindMember(const JsonValue& object, const std::wstring& key) {
    if (object.type != JsonValue::Type::Object) {
        return nullptr;
    }
    const auto it = object.objectValue.find(key);
    if (it == object.objectValue.end()) {
        return nullptr;
    }
    return &it->second;
}

std::wstring GetStringMember(const JsonValue& object, const std::wstring& key) {
    const JsonValue* value = FindMember(object, key);
    if (!value || value->type != JsonValue::Type::String) {
        return {};
    }
    return Trim(value->stringValue);
}

std::vector<std::wstring> GetStringArrayMember(const JsonValue& object, const std::wstring& key) {
    std::vector<std::wstring> result;
    const JsonValue* value = FindMember(object, key);
    if (!value || value->type != JsonValue::Type::Array) {
        return result;
    }
    for (const auto& item : value->arrayValue) {
        if (item.type == JsonValue::Type::String) {
            const std::wstring text = Trim(item.stringValue);
            if (!text.empty()) {
                result.push_back(text);
            }
        }
    }
    return result;
}

std::wstring ReadUtf8File(const std::filesystem::path& path, bool& ok) {
    ok = false;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    ok = true;
    return Utf8ToWide(bytes);
}

bool IsBlacklistedManufacturer(const std::wstring& value) {
    const std::wstring normalized = ToLower(Trim(value));
    const std::vector<std::wstring> blacklist = {
        L"clap",
        L"vst",
        L"vst2",
        L"vst3",
        L"aax",
        L"au",
        L"audio unit",
        L"plugin",
        L"unknown",
    };
    return std::find(blacklist.begin(), blacklist.end(), normalized) != blacklist.end();
}

std::wstring NormalizeMatchToken(const std::wstring& value) {
    return NormalizePluginKey(value);
}

std::wstring StemWithoutPluginExtension(const std::wstring& fileName) {
    return std::filesystem::path(fileName).stem().wstring();
}

std::vector<std::wstring> MatchHaystacks(const PluginRecord& record) {
    std::vector<std::wstring> values = {
        record.pluginName,
        record.fileName,
        StemWithoutPluginExtension(record.fileName),
        record.filePath,
    };

    std::vector<std::wstring> normalized;
    for (const auto& value : values) {
        const std::wstring key = NormalizeMatchToken(value);
        if (!key.empty() && std::find(normalized.begin(), normalized.end(), key) == normalized.end()) {
            normalized.push_back(key);
        }
    }
    return normalized;
}

bool TokenMatchesRecord(const std::wstring& token, const std::vector<std::wstring>& haystacks) {
    const std::wstring normalizedToken = NormalizeMatchToken(token);
    if (normalizedToken.empty()) {
        return false;
    }
    for (const auto& haystack : haystacks) {
        if (haystack == normalizedToken || haystack.find(normalizedToken) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

bool TypeMatchesRecord(const PluginRule& rule, PluginType type) {
    if (rule.typesSeen.empty()) {
        return true;
    }
    const std::wstring displayType = ToLower(ToDisplayText(type));
    for (const auto& typeSeen : rule.typesSeen) {
        if (ToLower(Trim(typeSeen)) == displayType) {
            return true;
        }
    }
    return false;
}

const PluginRule* FindBestPluginRule(const UserPrefs& prefs, const PluginRecord& record) {
    const std::vector<std::wstring> haystacks = MatchHaystacks(record);
    const PluginRule* best = nullptr;
    std::size_t bestTokenLength = 0;

    for (const auto& rule : prefs.pluginRules) {
        if (!TypeMatchesRecord(rule, record.pluginType)) {
            continue;
        }
        for (const auto& token : rule.matchTokens) {
            const std::wstring normalizedToken = NormalizeMatchToken(token);
            if (normalizedToken.size() < bestTokenLength) {
                continue;
            }
            if (TokenMatchesRecord(token, haystacks)) {
                best = &rule;
                bestTokenLength = normalizedToken.size();
            }
        }
    }
    return best;
}

const VendorRule* FindBestVendorRule(const UserPrefs& prefs, const PluginRecord& record) {
    const std::vector<std::wstring> haystacks = MatchHaystacks(record);
    const VendorRule* best = nullptr;
    std::size_t bestTokenLength = 0;

    for (const auto& rule : prefs.vendorRules) {
        for (const auto& token : rule.matchTokens) {
            const std::wstring normalizedToken = NormalizeMatchToken(token);
            if (normalizedToken.size() < bestTokenLength) {
                continue;
            }
            if (TokenMatchesRecord(token, haystacks)) {
                best = &rule;
                bestTokenLength = normalizedToken.size();
            }
        }
    }
    return best;
}

void AddPluginRule(const JsonValue& item, UserPrefs& prefs) {
    if (item.type != JsonValue::Type::Object) {
        return;
    }

    PluginRule rule;
    rule.pluginName = GetStringMember(item, L"pluginName");
    rule.vendor = GetStringMember(item, L"vendor");
    rule.category = GetStringMember(item, L"category");
    rule.version = GetStringMember(item, L"version");
    rule.matchTokens = GetStringArrayMember(item, L"match");
    rule.typesSeen = GetStringArrayMember(item, L"typesSeen");
    if (rule.matchTokens.empty() && !rule.pluginName.empty()) {
        rule.matchTokens.push_back(rule.pluginName);
    }
    if (!rule.matchTokens.empty()) {
        prefs.pluginRules.push_back(std::move(rule));
    }
}

void AddVendorRule(const JsonValue& item, UserPrefs& prefs) {
    if (item.type != JsonValue::Type::Object) {
        return;
    }

    VendorRule rule;
    rule.vendor = GetStringMember(item, L"vendor");
    rule.matchTokens = GetStringArrayMember(item, L"tokens");
    const auto matchTokens = GetStringArrayMember(item, L"match");
    rule.matchTokens.insert(rule.matchTokens.end(), matchTokens.begin(), matchTokens.end());
    if (!rule.vendor.empty() && !rule.matchTokens.empty()) {
        prefs.vendorRules.push_back(std::move(rule));
    }
}

void AddManualOverride(const JsonValue& item, UserPrefs& prefs) {
    if (item.type != JsonValue::Type::Object) {
        return;
    }

    const JsonValue* match = FindMember(item, L"match");
    const JsonValue* set = FindMember(item, L"set");
    if (!match || !set || match->type != JsonValue::Type::Object || set->type != JsonValue::Type::Object) {
        return;
    }

    ManualOverride overrideRule;
    overrideRule.fileName = GetStringMember(*match, L"fileName");
    overrideRule.type = GetStringMember(*match, L"type");
    overrideRule.pathContains = GetStringMember(*match, L"pathContains");
    overrideRule.manufacturer = GetStringMember(*set, L"manufacturer");
    overrideRule.pluginName = GetStringMember(*set, L"pluginName");
    overrideRule.category = GetStringMember(*set, L"category");
    overrideRule.notes = GetStringMember(item, L"notes");
    if (!overrideRule.fileName.empty() && !overrideRule.type.empty()) {
        prefs.manualOverrides.push_back(std::move(overrideRule));
    }
}

void LoadRulesArray(const JsonValue& root, const std::wstring& key, UserPrefs& prefs) {
    const JsonValue* rules = FindMember(root, key);
    if (!rules || rules->type != JsonValue::Type::Array) {
        return;
    }
    for (const auto& item : rules->arrayValue) {
        if (key == L"pluginRules") {
            AddPluginRule(item, prefs);
        } else if (key == L"vendorRules") {
            AddVendorRule(item, prefs);
        } else if (key == L"manualOverrides") {
            AddManualOverride(item, prefs);
        }
    }
}

void LoadVendorAliases(const JsonValue& root, UserPrefs& prefs) {
    const JsonValue* aliases = FindMember(root, L"vendorAliases");
    if (!aliases || aliases->type != JsonValue::Type::Object) {
        return;
    }
    for (const auto& [key, value] : aliases->objectValue) {
        if (value.type == JsonValue::Type::String) {
            prefs.vendorAliases[ToLower(Trim(key))] = Trim(value.stringValue);
        }
    }
}

bool LoadUserPrefs(const std::filesystem::path& rulesPath, UserPrefs& prefs, std::wstring& warningMessage) {
    bool readOk = false;
    const std::wstring text = ReadUtf8File(rulesPath, readOk);
    if (!readOk) {
        warningMessage = L"plugin_rules_userprefs.json konnte nicht gelesen werden.";
        return false;
    }

    JsonValue root;
    JsonParser parser(text);
    std::wstring parseError;
    if (!parser.Parse(root, parseError) || root.type != JsonValue::Type::Object) {
        warningMessage = L"Warnung: plugin_rules_userprefs.json ist defekt. " + parseError;
        return false;
    }

    LoadVendorAliases(root, prefs);
    LoadRulesArray(root, L"vendorRules", prefs);
    LoadRulesArray(root, L"pluginRules", prefs);
    LoadRulesArray(root, L"manualOverrides", prefs);

    if (prefs.pluginRules.empty() &&
        prefs.vendorRules.empty() &&
        prefs.vendorAliases.empty() &&
        prefs.manualOverrides.empty()) {
        warningMessage = L"Warnung: plugin_rules_userprefs.json enthaelt keine verwertbaren Regeln.";
        return false;
    }
    return true;
}

bool AssignIfChanged(std::wstring& target, const std::wstring& value) {
    const std::wstring trimmed = Trim(value);
    if (trimmed.empty() || target == trimmed) {
        return false;
    }
    target = trimmed;
    return true;
}

std::optional<std::wstring> VendorAliasFor(const UserPrefs& prefs, const std::wstring& vendor) {
    const auto it = prefs.vendorAliases.find(ToLower(Trim(vendor)));
    if (it == prefs.vendorAliases.end() || Trim(it->second).empty()) {
        return std::nullopt;
    }
    return it->second;
}

std::wstring ManualOverrideKey(const std::wstring& fileName, const std::wstring& type) {
    return ToLower(Trim(fileName)) + L"\n" + ToLower(Trim(type));
}

std::wstring ManualOverrideKey(const ManualOverride& overrideRule) {
    return ManualOverrideKey(overrideRule.fileName, overrideRule.type);
}

std::wstring ManualOverrideKey(const PluginRecord& record) {
    return ManualOverrideKey(record.fileName, ToDisplayText(record.pluginType));
}

bool PathContainsMatches(const ManualOverride& overrideRule, const PluginRecord& record) {
    const std::wstring token = ToLower(Trim(overrideRule.pathContains));
    if (token.empty()) {
        return true;
    }
    return ToLower(record.filePath).find(token) != std::wstring::npos;
}

bool ManualOverrideMatches(const ManualOverride& overrideRule, const PluginRecord& record) {
    return ManualOverrideKey(overrideRule) == ManualOverrideKey(record) &&
        PathContainsMatches(overrideRule, record);
}

const ManualOverride* FindManualOverride(const UserPrefs& prefs, const PluginRecord& record) {
    for (const auto& overrideRule : prefs.manualOverrides) {
        if (ManualOverrideMatches(overrideRule, record)) {
            return &overrideRule;
        }
    }
    return nullptr;
}

void RecomputeMetadataStatus(PluginRecord& record) {
    if (record.status == ScanStatus::AccessError) {
        return;
    }

    const bool hasName = !Trim(record.pluginName).empty();
    const bool hasManufacturer = !Trim(record.manufacturer).empty();
    const bool hasVersion = !Trim(record.version).empty();
    const bool hasReliableVersion = hasVersion && record.versionSource != VersionSource::FileName;

    if (hasName && hasManufacturer && hasReliableVersion) {
        record.status = ScanStatus::Recognized;
        if (record.warningMessage == L"Nicht alle Metadaten konnten zuverlaessig ermittelt werden." ||
            record.warningMessage == L"Versionsnummer wurde nur heuristisch aus dem Dateinamen ermittelt." ||
            record.warningMessage == L"Keine Windows-Versioninformationen gefunden.") {
            record.warningMessage.clear();
        }
    } else if (hasName || hasManufacturer || hasVersion) {
        record.status = ScanStatus::PartiallyRecognized;
        if (record.versionSource == VersionSource::FileName && record.warningMessage.empty()) {
            record.warningMessage = L"Versionsnummer wurde nur heuristisch aus dem Dateinamen ermittelt.";
        }
    } else {
        record.status = ScanStatus::Unknown;
    }
}

bool ApplyManualOverrideToRecord(const ManualOverride& overrideRule, PluginRecord& record) {
    bool changed = false;
    changed = AssignIfChanged(record.manufacturer, overrideRule.manufacturer) || changed;
    changed = AssignIfChanged(record.pluginName, overrideRule.pluginName) || changed;
    changed = AssignIfChanged(record.category, overrideRule.category) || changed;

    record.metadataFromManualOverrides = true;
    RecomputeMetadataStatus(record);
    return changed;
}

bool ApplyPrefsToRecord(const UserPrefs& prefs, PluginRecord& record) {
    bool changedByJson = false;

    if (const auto alias = VendorAliasFor(prefs, record.manufacturer);
        alias && !IsBlacklistedManufacturer(record.manufacturer) && !IsBlacklistedManufacturer(*alias)) {
        changedByJson = AssignIfChanged(record.manufacturer, *alias) || changedByJson;
    }

    const PluginRule* pluginRule = FindBestPluginRule(prefs, record);
    if (pluginRule) {
        changedByJson = AssignIfChanged(record.pluginName, pluginRule->pluginName) || changedByJson;
        if (!IsBlacklistedManufacturer(pluginRule->vendor)) {
            changedByJson = AssignIfChanged(record.manufacturer, pluginRule->vendor) || changedByJson;
        }
        changedByJson = AssignIfChanged(record.category, pluginRule->category) || changedByJson;
        const bool versionChanged = AssignIfChanged(record.version, NormalizeVersionString(pluginRule->version));
        if (versionChanged) {
            record.versionSource = VersionSource::UserRule;
        }
        changedByJson = versionChanged || changedByJson;
    } else if (Trim(record.manufacturer).empty() || IsBlacklistedManufacturer(record.manufacturer)) {
        if (const VendorRule* vendorRule = FindBestVendorRule(prefs, record);
            vendorRule && !IsBlacklistedManufacturer(vendorRule->vendor)) {
            changedByJson = AssignIfChanged(record.manufacturer, vendorRule->vendor) || changedByJson;
        }
    }

    if (IsBlacklistedManufacturer(record.manufacturer)) {
        record.manufacturer.clear();
    }

    if (changedByJson) {
        record.metadataFromJson = true;
        RecomputeMetadataStatus(record);
    } else {
        RecomputeMetadataStatus(record);
    }

    if (const ManualOverride* overrideRule = FindManualOverride(prefs, record)) {
        const bool manualChanged = ApplyManualOverrideToRecord(*overrideRule, record);
        return changedByJson || manualChanged;
    }

    return changedByJson;
}

std::wstring JsonEscape(const std::wstring& value) {
    std::wstring result;
    for (wchar_t c : value) {
        switch (c) {
        case L'"':
            result += L"\\\"";
            break;
        case L'\\':
            result += L"\\\\";
            break;
        case L'\b':
            result += L"\\b";
            break;
        case L'\f':
            result += L"\\f";
            break;
        case L'\n':
            result += L"\\n";
            break;
        case L'\r':
            result += L"\\r";
            break;
        case L'\t':
            result += L"\\t";
            break;
        default:
            result.push_back(c);
            break;
        }
    }
    return result;
}

JsonValue MakeJsonString(const std::wstring& value) {
    JsonValue json;
    json.type = JsonValue::Type::String;
    json.stringValue = value;
    return json;
}

JsonValue MakeJsonStringArray(const std::vector<std::wstring>& values) {
    JsonValue json;
    json.type = JsonValue::Type::Array;
    for (const auto& value : values) {
        json.arrayValue.push_back(MakeJsonString(value));
    }
    return json;
}

bool HasObjectMember(const JsonValue& object, const std::wstring& key) {
    return object.type == JsonValue::Type::Object &&
        object.objectValue.find(key) != object.objectValue.end();
}

void SetObjectString(JsonValue& object, const std::wstring& key, const std::wstring& value) {
    object.type = JsonValue::Type::Object;
    object.objectValue[key] = MakeJsonString(value);
}

void SetObjectStringArray(JsonValue& object, const std::wstring& key, const std::vector<std::wstring>& values) {
    object.type = JsonValue::Type::Object;
    object.objectValue[key] = MakeJsonStringArray(values);
}

std::wstring JsonIndent(int level) {
    return std::wstring(static_cast<std::size_t>(level * 2), L' ');
}

std::wstring JsonValueToText(const JsonValue& value, int indentLevel);

std::wstring JsonArrayToText(const JsonValue& value, int indentLevel) {
    if (value.arrayValue.empty()) {
        return L"[]";
    }

    std::wstringstream stream;
    stream << L"[\r\n";
    for (std::size_t i = 0; i < value.arrayValue.size(); ++i) {
        stream << JsonIndent(indentLevel + 1)
               << JsonValueToText(value.arrayValue[i], indentLevel + 1)
               << (i + 1 < value.arrayValue.size() ? L"," : L"")
               << L"\r\n";
    }
    stream << JsonIndent(indentLevel) << L"]";
    return stream.str();
}

std::wstring JsonObjectToText(const JsonValue& value, int indentLevel) {
    if (value.objectValue.empty()) {
        return L"{}";
    }

    std::wstringstream stream;
    stream << L"{\r\n";
    std::size_t index = 0;
    for (const auto& [key, child] : value.objectValue) {
        stream << JsonIndent(indentLevel + 1)
               << L"\"" << JsonEscape(key) << L"\": "
               << JsonValueToText(child, indentLevel + 1)
               << (++index < value.objectValue.size() ? L"," : L"")
               << L"\r\n";
    }
    stream << JsonIndent(indentLevel) << L"}";
    return stream.str();
}

std::wstring JsonValueToText(const JsonValue& value, int indentLevel) {
    switch (value.type) {
    case JsonValue::Type::String:
        return L"\"" + JsonEscape(value.stringValue) + L"\"";
    case JsonValue::Type::Array:
        return JsonArrayToText(value, indentLevel);
    case JsonValue::Type::Object:
        return JsonObjectToText(value, indentLevel);
    case JsonValue::Type::Null:
    default:
        return L"null";
    }
}

std::vector<std::wstring> PluginRuleKeysInWriteOrder(const JsonValue& rule) {
    std::vector<std::wstring> keys;
    const std::vector<std::wstring> preferred = {
        L"pluginName",
        L"match",
        L"vendor",
        L"category",
        L"version",
        L"typesSeen",
    };

    for (const auto& key : preferred) {
        if (HasObjectMember(rule, key)) {
            keys.push_back(key);
        }
    }
    if (rule.type == JsonValue::Type::Object) {
        for (const auto& [key, _] : rule.objectValue) {
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(key);
            }
        }
    }
    return keys;
}

std::wstring PluginRuleJson(const JsonValue& rule, int indentLevel) {
    if (rule.type != JsonValue::Type::Object) {
        return JsonValueToText(rule, indentLevel);
    }

    const std::vector<std::wstring> keys = PluginRuleKeysInWriteOrder(rule);
    if (keys.empty()) {
        return L"{}";
    }

    std::wstringstream stream;
    stream << L"{\r\n";
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto it = rule.objectValue.find(keys[i]);
        if (it == rule.objectValue.end()) {
            continue;
        }
        stream << JsonIndent(indentLevel + 1)
               << L"\"" << JsonEscape(keys[i]) << L"\": "
               << JsonValueToText(it->second, indentLevel + 1)
               << (i + 1 < keys.size() ? L"," : L"")
               << L"\r\n";
    }
    stream << JsonIndent(indentLevel) << L"}";
    return stream.str();
}

std::wstring PluginRulesJson(const std::vector<JsonValue>& rules) {
    if (rules.empty()) {
        return L"[]";
    }

    std::wstringstream stream;
    stream << L"[\r\n";
    for (std::size_t i = 0; i < rules.size(); ++i) {
        stream << L"    " << PluginRuleJson(rules[i], 2)
               << (i + 1 < rules.size() ? L"," : L"")
               << L"\r\n";
    }
    stream << L"  ]";
    return stream.str();
}

bool StringVectorContainsNormalized(const std::vector<std::wstring>& values, const std::wstring& value) {
    const std::wstring normalized = ToLower(Trim(value));
    return std::any_of(values.begin(), values.end(), [&](const std::wstring& candidate) {
        return ToLower(Trim(candidate)) == normalized;
    });
}

std::wstring PreferredPluginRuleMatchToken(const PluginRecord& record) {
    std::wstring token = StemWithoutPluginExtension(record.fileName);
    if (Trim(token).empty()) {
        token = record.pluginName;
    }
    return Trim(token);
}

bool JsonPluginRuleTypeMatchesRecord(const JsonValue& rule, PluginType type) {
    const std::vector<std::wstring> typesSeen = GetStringArrayMember(rule, L"typesSeen");
    if (typesSeen.empty()) {
        return true;
    }
    return StringVectorContainsNormalized(typesSeen, ToDisplayText(type));
}

int FindPluginRuleIndexForRecord(const std::vector<JsonValue>& rules, const PluginRecord& record) {
    const std::vector<std::wstring> haystacks = MatchHaystacks(record);
    int bestIndex = -1;
    std::size_t bestTokenLength = 0;

    for (std::size_t i = 0; i < rules.size(); ++i) {
        const JsonValue& rule = rules[i];
        if (rule.type != JsonValue::Type::Object || !JsonPluginRuleTypeMatchesRecord(rule, record.pluginType)) {
            continue;
        }

        std::vector<std::wstring> tokens = GetStringArrayMember(rule, L"match");
        const std::wstring pluginName = GetStringMember(rule, L"pluginName");
        if (tokens.empty() && !pluginName.empty()) {
            tokens.push_back(pluginName);
        }

        for (const auto& token : tokens) {
            const std::wstring normalizedToken = NormalizeMatchToken(token);
            if (normalizedToken.size() < bestTokenLength) {
                continue;
            }
            if (TokenMatchesRecord(token, haystacks)) {
                bestIndex = static_cast<int>(i);
                bestTokenLength = normalizedToken.size();
            }
        }
    }

    return bestIndex;
}

void EnsurePluginRuleMatchToken(JsonValue& rule, const std::wstring& token) {
    if (Trim(token).empty()) {
        return;
    }

    std::vector<std::wstring> tokens = GetStringArrayMember(rule, L"match");
    const std::wstring normalizedToken = NormalizeMatchToken(token);
    const bool exists = std::any_of(tokens.begin(), tokens.end(), [&](const std::wstring& candidate) {
        return NormalizeMatchToken(candidate) == normalizedToken;
    });
    if (!exists) {
        tokens.push_back(token);
        SetObjectStringArray(rule, L"match", tokens);
    } else if (!HasObjectMember(rule, L"match")) {
        SetObjectStringArray(rule, L"match", tokens);
    }
}

void EnsurePluginRuleTypeSeen(JsonValue& rule, const std::wstring& type) {
    std::vector<std::wstring> typesSeen = GetStringArrayMember(rule, L"typesSeen");
    if (!StringVectorContainsNormalized(typesSeen, type)) {
        typesSeen.push_back(type);
    }
    SetObjectStringArray(rule, L"typesSeen", typesSeen);
}

void UpdatePluginRuleFromRecord(JsonValue& rule, const PluginRecord& record) {
    SetObjectString(rule, L"pluginName", record.pluginName);
    SetObjectString(rule, L"vendor", record.manufacturer);
    SetObjectString(rule, L"category", record.category);
    EnsurePluginRuleMatchToken(rule, PreferredPluginRuleMatchToken(record));
    EnsurePluginRuleTypeSeen(rule, ToDisplayText(record.pluginType));
    if (record.versionManuallyEdited || HasObjectMember(rule, L"version")) {
        SetObjectString(rule, L"version", record.version);
    }
}

JsonValue NewPluginRuleFromRecord(const PluginRecord& record) {
    JsonValue rule;
    rule.type = JsonValue::Type::Object;
    SetObjectString(rule, L"pluginName", record.pluginName);
    SetObjectStringArray(rule, L"match", { PreferredPluginRuleMatchToken(record) });
    SetObjectString(rule, L"vendor", record.manufacturer);
    SetObjectString(rule, L"category", record.category);
    if (record.versionManuallyEdited) {
        SetObjectString(rule, L"version", record.version);
    }
    SetObjectStringArray(rule, L"typesSeen", { ToDisplayText(record.pluginType) });
    return rule;
}

bool WriteUtf8File(const std::filesystem::path& path, const std::wstring& content, std::wstring& errorMessage) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        errorMessage = L"Datei konnte nicht geschrieben werden: " + path.wstring();
        return false;
    }
    const std::string bytes = WideToUtf8(content);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream.good()) {
        errorMessage = L"Datei konnte nicht vollstaendig geschrieben werden: " + path.wstring();
        return false;
    }
    return true;
}

std::wstring BackupTimestamp() {
    std::wstring timestamp = CurrentTimestamp();
    for (wchar_t& c : timestamp) {
        if (c == L' ') {
            c = L'_';
        } else if (c == L':') {
            c = L'\0';
        }
    }
    timestamp.erase(std::remove(timestamp.begin(), timestamp.end(), L'\0'), timestamp.end());
    return timestamp;
}

bool SkipJsonStringLiteral(const std::wstring& text, std::size_t& index) {
    if (index >= text.size() || text[index] != L'"') {
        return false;
    }
    ++index;
    while (index < text.size()) {
        const wchar_t c = text[index++];
        if (c == L'"') {
            return true;
        }
        if (c == L'\\') {
            if (index >= text.size()) {
                return false;
            }
            ++index;
        }
    }
    return false;
}

bool ParseJsonStringAtForWrite(const std::wstring& text, std::size_t& index, std::wstring& value) {
    if (index >= text.size() || text[index] != L'"') {
        return false;
    }
    ++index;

    std::wstring result;
    while (index < text.size()) {
        wchar_t c = text[index++];
        if (c == L'"') {
            value = std::move(result);
            return true;
        }
        if (c != L'\\') {
            result.push_back(c);
            continue;
        }
        if (index >= text.size()) {
            return false;
        }

        const wchar_t escaped = text[index++];
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
                const wchar_t hexChar = text[index++];
                int hex = -1;
                if (hexChar >= L'0' && hexChar <= L'9') {
                    hex = hexChar - L'0';
                } else if (hexChar >= L'a' && hexChar <= L'f') {
                    hex = 10 + hexChar - L'a';
                } else if (hexChar >= L'A' && hexChar <= L'F') {
                    hex = 10 + hexChar - L'A';
                }
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

void SkipJsonWhitespaceForWrite(const std::wstring& text, std::size_t& index) {
    while (index < text.size() &&
           (text[index] == L' ' || text[index] == L'\t' || text[index] == L'\r' || text[index] == L'\n')) {
        ++index;
    }
}

bool SkipJsonValue(const std::wstring& text, std::size_t& index) {
    SkipJsonWhitespaceForWrite(text, index);
    if (index >= text.size()) {
        return false;
    }

    if (text[index] == L'"') {
        return SkipJsonStringLiteral(text, index);
    }

    if (text[index] == L'{' || text[index] == L'[') {
        std::vector<wchar_t> closeStack;
        closeStack.push_back(text[index] == L'{' ? L'}' : L']');
        ++index;
        while (index < text.size() && !closeStack.empty()) {
            if (text[index] == L'"') {
                if (!SkipJsonStringLiteral(text, index)) {
                    return false;
                }
                continue;
            }
            if (text[index] == L'{') {
                closeStack.push_back(L'}');
            } else if (text[index] == L'[') {
                closeStack.push_back(L']');
            } else if (text[index] == closeStack.back()) {
                closeStack.pop_back();
            }
            ++index;
        }
        return closeStack.empty();
    }

    while (index < text.size() &&
           text[index] != L',' &&
           text[index] != L'}' &&
           text[index] != L']' &&
           text[index] != L'\r' &&
           text[index] != L'\n' &&
           text[index] != L'\t' &&
           text[index] != L' ') {
        ++index;
    }
    return true;
}

bool FindTopLevelMemberRange(const std::wstring& text,
                             const std::wstring& memberName,
                             std::size_t& propertyStart,
                             std::size_t& propertyEnd) {
    std::size_t index = 0;
    SkipJsonWhitespaceForWrite(text, index);
    if (index >= text.size() || text[index] != L'{') {
        return false;
    }
    ++index;

    while (index < text.size()) {
        SkipJsonWhitespaceForWrite(text, index);
        if (index < text.size() && text[index] == L',') {
            ++index;
            SkipJsonWhitespaceForWrite(text, index);
        }
        if (index >= text.size() || text[index] == L'}') {
            return false;
        }

        const std::size_t start = index;
        std::wstring key;
        std::size_t keyIndex = index;
        if (!ParseJsonStringAtForWrite(text, keyIndex, key)) {
            return false;
        }
        index = keyIndex;
        SkipJsonWhitespaceForWrite(text, index);
        if (index >= text.size() || text[index] != L':') {
            return false;
        }
        ++index;
        if (!SkipJsonValue(text, index)) {
            return false;
        }
        propertyEnd = index;
        if (key == memberName) {
            propertyStart = start;
            return true;
        }
    }
    return false;
}

} // namespace

std::filesystem::path PluginUserPrefsPathNextToExe() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (length == 0) {
        return std::filesystem::path(L"plugin_rules_userprefs.json");
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path() / L"plugin_rules_userprefs.json";
}

PluginUserPrefsResult ApplyPluginUserPrefs(const std::filesystem::path& rulesPath,
                                           std::vector<PluginRecord>& records) {
    PluginUserPrefsResult result;
    std::error_code ec;
    result.rulesFileFound = std::filesystem::exists(rulesPath, ec) && !ec;
    if (!result.rulesFileFound) {
        return result;
    }

    UserPrefs prefs;
    if (!LoadUserPrefs(rulesPath, prefs, result.warningMessage)) {
        return result;
    }
    result.rulesLoaded = true;

    for (auto& record : records) {
        const bool hadManualOverride = record.metadataFromManualOverrides;
        if (ApplyPrefsToRecord(prefs, record)) {
            ++result.appliedCount;
        }
        if (!hadManualOverride && record.metadataFromManualOverrides) {
            ++result.manualOverrideCount;
        }
    }

    return result;
}

SaveManualOverridesResult SaveManualOverrides(const std::filesystem::path& rulesPath,
                                               const std::vector<PluginRecord>& records) {
    SaveManualOverridesResult result;
    std::vector<PluginRecord> manualEdits;
    for (const auto& record : records) {
        if (record.manuallyEdited) {
            manualEdits.push_back(record);
        }
    }

    result.savedCount = manualEdits.size();
    if (manualEdits.empty()) {
        result.success = true;
        return result;
    }

    std::wstring text;
    bool readOk = false;
    std::error_code ec;
    const bool fileExists = std::filesystem::exists(rulesPath, ec) && !ec;
    if (fileExists) {
        text = ReadUtf8File(rulesPath, readOk);
        if (!readOk) {
            result.errorMessage = L"plugin_rules_userprefs.json konnte nicht gelesen werden.";
            return result;
        }
    } else {
        text =
            L"{\r\n"
            L"  \"schemaVersion\": \"0.3-userprefs\"\r\n"
            L"}\r\n";
        readOk = true;
    }

    JsonValue root;
    JsonParser parser(text);
    std::wstring parseError;
    if (!parser.Parse(root, parseError) || root.type != JsonValue::Type::Object) {
        result.errorMessage = L"plugin_rules_userprefs.json ist defekt. pluginRules wurden nicht gespeichert. " + parseError;
        return result;
    }

    std::vector<JsonValue> merged;
    if (const JsonValue* pluginRules = FindMember(root, L"pluginRules");
        pluginRules && pluginRules->type == JsonValue::Type::Array) {
        merged = pluginRules->arrayValue;
    }

    for (const auto& edit : manualEdits) {
        const int existingIndex = FindPluginRuleIndexForRecord(merged, edit);
        if (existingIndex < 0) {
            merged.push_back(NewPluginRuleFromRecord(edit));
        } else {
            UpdatePluginRuleFromRecord(merged[static_cast<std::size_t>(existingIndex)], edit);
        }
    }

    const std::wstring pluginRulesProperty =
        L"\"pluginRules\": " + PluginRulesJson(merged);

    std::wstring updatedText;
    std::size_t propertyStart = 0;
    std::size_t propertyEnd = 0;
    if (FindTopLevelMemberRange(text, L"pluginRules", propertyStart, propertyEnd)) {
        updatedText = text.substr(0, propertyStart) +
            pluginRulesProperty +
            text.substr(propertyEnd);
    } else {
        std::size_t insertAt = text.find_last_of(L'}');
        if (insertAt == std::wstring::npos) {
            result.errorMessage = L"plugin_rules_userprefs.json konnte nicht aktualisiert werden.";
            return result;
        }

        std::size_t previous = insertAt;
        while (previous > 0 &&
               (text[previous - 1] == L' ' ||
                text[previous - 1] == L'\t' ||
                text[previous - 1] == L'\r' ||
                text[previous - 1] == L'\n')) {
            --previous;
        }
        const bool hasExistingMembers = previous > 0 && text[previous - 1] != L'{';
        updatedText = text.substr(0, previous);
        if (hasExistingMembers) {
            updatedText += L",";
        }
        updatedText += L"\r\n  " + pluginRulesProperty + L"\r\n";
        updatedText += text.substr(insertAt);
    }

    if (fileExists) {
        result.backupPath = rulesPath.parent_path() /
            (rulesPath.stem().wstring() + L".backup_" + BackupTimestamp() + rulesPath.extension().wstring());
        if (!std::filesystem::copy_file(rulesPath, result.backupPath, std::filesystem::copy_options::overwrite_existing, ec) || ec) {
            result.errorMessage = L"Backup konnte nicht erstellt werden: " + result.backupPath.wstring();
            return result;
        }
    }

    if (!WriteUtf8File(rulesPath, updatedText, result.errorMessage)) {
        return result;
    }

    result.success = true;
    return result;
}
