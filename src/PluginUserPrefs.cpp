#include "PluginUserPrefs.h"

#include "StringUtil.h"

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

    if (hasName && hasManufacturer && hasVersion) {
        record.status = ScanStatus::Recognized;
    } else if (hasName || hasManufacturer || hasVersion) {
        record.status = ScanStatus::PartiallyRecognized;
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

ManualOverride ManualOverrideFromRecord(const PluginRecord& record) {
    ManualOverride overrideRule;
    overrideRule.fileName = record.fileName;
    overrideRule.type = ToDisplayText(record.pluginType);
    overrideRule.pathContains = record.filePath;
    overrideRule.manufacturer = record.manufacturer;
    overrideRule.pluginName = record.pluginName;
    overrideRule.category = record.category;
    overrideRule.notes = L"Created from manual edit in VST Plugin Scanner";
    return overrideRule;
}

std::wstring ManualOverridesJson(const std::vector<ManualOverride>& overrides) {
    std::wstringstream stream;
    stream << L"[\r\n";
    for (std::size_t i = 0; i < overrides.size(); ++i) {
        const auto& overrideRule = overrides[i];
        stream << L"    {\r\n";
        stream << L"      \"match\": {\r\n";
        stream << L"        \"fileName\": \"" << JsonEscape(overrideRule.fileName) << L"\",\r\n";
        stream << L"        \"type\": \"" << JsonEscape(overrideRule.type) << L"\",\r\n";
        stream << L"        \"pathContains\": \"" << JsonEscape(overrideRule.pathContains) << L"\"\r\n";
        stream << L"      },\r\n";
        stream << L"      \"set\": {\r\n";
        stream << L"        \"manufacturer\": \"" << JsonEscape(overrideRule.manufacturer) << L"\",\r\n";
        stream << L"        \"pluginName\": \"" << JsonEscape(overrideRule.pluginName) << L"\",\r\n";
        stream << L"        \"category\": \"" << JsonEscape(overrideRule.category) << L"\"\r\n";
        stream << L"      },\r\n";
        stream << L"      \"notes\": \"" << JsonEscape(overrideRule.notes.empty() ? L"Created from manual edit in VST Plugin Scanner" : overrideRule.notes) << L"\"\r\n";
        stream << L"    }" << (i + 1 < overrides.size() ? L"," : L"") << L"\r\n";
    }
    stream << L"  ]";
    return stream.str();
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
    std::vector<ManualOverride> manualEdits;
    for (const auto& record : records) {
        if (record.manuallyEdited) {
            manualEdits.push_back(ManualOverrideFromRecord(record));
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
        result.errorMessage = L"plugin_rules_userprefs.json ist defekt. manualOverrides wurden nicht gespeichert. " + parseError;
        return result;
    }

    UserPrefs prefs;
    LoadRulesArray(root, L"manualOverrides", prefs);
    std::vector<ManualOverride> merged = prefs.manualOverrides;
    for (const auto& edit : manualEdits) {
        const std::wstring key = ManualOverrideKey(edit);
        auto existing = std::find_if(merged.begin(), merged.end(), [&](const ManualOverride& candidate) {
            return ManualOverrideKey(candidate) == key;
        });
        if (existing == merged.end()) {
            merged.push_back(edit);
        } else {
            *existing = edit;
        }
    }

    const std::wstring manualOverridesProperty =
        L"\"manualOverrides\": " + ManualOverridesJson(merged);

    std::wstring updatedText;
    std::size_t propertyStart = 0;
    std::size_t propertyEnd = 0;
    if (FindTopLevelMemberRange(text, L"manualOverrides", propertyStart, propertyEnd)) {
        updatedText = text.substr(0, propertyStart) +
            manualOverridesProperty +
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
        updatedText += L"\r\n  " + manualOverridesProperty + L"\r\n";
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
