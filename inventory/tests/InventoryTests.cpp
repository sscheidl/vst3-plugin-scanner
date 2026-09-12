#include "InventoryModel.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

std::string ValidProbeJson() {
    return R"({
        "schemaVersion": 2,
        "status": "ok",
        "module": {
            "path": "C:\\Plugins\\Shell.vst3",
            "name": "Shell",
            "isBundle": false,
            "factoryVendor": "Factory Vendor",
            "factoryUrl": "https://example.invalid",
            "factoryEmail": "support@example.invalid",
            "factoryFlags": 0,
            "classCount": 2,
            "probeDurationMs": 42
        },
        "classes": [
            {
                "index": 0,
                "cid": "00112233445566778899AABBCCDDEEFF",
                "category": "Audio Module Class",
                "name": "Compressor",
                "vendor": "",
                "version": "2.4.1",
                "sdkVersion": "VST 3.8.0",
                "subCategories": ["Fx", "Dynamics"],
                "classFlags": 0,
                "cardinality": 2147483647,
                "factoryInterface": 3,
                "isAudioPlugin": true,
                "versionMissing": false,
                "diagnostic": ""
            },
            {
                "index": 1,
                "cid": "FFEEDDCCBBAA99887766554433221100",
                "category": "Component Controller Class",
                "name": "Compressor Controller",
                "vendor": "Factory Vendor",
                "version": "2.4.1",
                "sdkVersion": "VST 3.8.0",
                "subCategories": [],
                "classFlags": 0,
                "cardinality": 1,
                "factoryInterface": 3,
                "isAudioPlugin": false,
                "versionMissing": false,
                "diagnostic": ""
            }
        ],
        "diagnostic": ""
    })";
}

std::string WaveShellProbeJson() {
    return R"({
        "schemaVersion": 2,
        "status": "ok",
        "module": {
            "path": "C:\\VST3\\WaveShell1-VST3 17.1_x64.vst3",
            "name": "WaveShell1-VST3 17.1_x64",
            "isBundle": false,
            "factoryVendor": "Waves",
            "factoryUrl": "https://www.waves.com",
            "factoryEmail": "",
            "factoryFlags": 17,
            "classCount": 3,
            "probeDurationMs": 12610
        },
        "classes": [
            {"index":0,"cid":"00112233445566778899AABBCCDDEE01","category":"Audio Module Class",
             "name":"COSMOS Sample Finder Stereo","vendor":"Waves","version":"17.1.42.50",
             "sdkVersion":"VST 3.6.9","subCategories":["Instrument","Waves"],
             "classFlags":1,"cardinality":2147483647,"factoryInterface":3,
             "isAudioPlugin":true,"versionMissing":false,"diagnostic":""},
            {"index":1,"cid":"00112233445566778899AABBCCDDEE02","category":"Audio Module Class",
             "name":"OVox Instrument Stereo","vendor":"Waves","version":"17.1.42.51",
             "sdkVersion":"VST 3.6.9","subCategories":["Instrument","Waves"],
             "classFlags":1,"cardinality":2147483647,"factoryInterface":3,
             "isAudioPlugin":true,"versionMissing":false,"diagnostic":""},
            {"index":2,"cid":"00112233445566778899AABBCCDDEE03","category":"Audio Module Class",
             "name":"OVox Stereo","vendor":"Waves","version":"17.1.42.51",
             "sdkVersion":"VST 3.6.9","subCategories":["Fx","Modulation"],
             "classFlags":1,"cardinality":2147483647,"factoryInterface":3,
             "isAudioPlugin":true,"versionMissing":false,"diagnostic":""}
        ],
        "diagnostic": ""
    })";
}

void TestStrictParsingAndAudioFilter() {
    const auto parsed = vst3scanner::ParseProbeResultJson(ValidProbeJson());
    Check(parsed.valid, "valid schema 2 JSON must parse");
    Check(parsed.audioPlugins.size() == 1, "controller classes must not enter inventory");
    Check(parsed.audioPlugins[0].vendor == "Factory Vendor",
          "empty class vendor must use authoritative factory vendor");
    Check(parsed.audioPlugins[0].version == "2.4.1", "raw version must be retained");
    Check(!parsed.audioPlugins[0].versionMissing,
          "a non-empty factory version must be marked as reported");

    auto missingVersion = ValidProbeJson();
    const auto version = missingVersion.find("\"version\": \"2.4.1\"");
    missingVersion.replace(version, std::string("\"version\": \"2.4.1\"").size(),
                           "\"version\": \"\"");
    const auto marker = missingVersion.find("\"versionMissing\": false");
    missingVersion.replace(marker, std::string("\"versionMissing\": false").size(),
                           "\"versionMissing\": true");
    const auto parsedMissing = vst3scanner::ParseProbeResultJson(missingVersion);
    Check(parsedMissing.valid && parsedMissing.audioPlugins[0].versionMissing,
          "an empty factory version must remain visible as missing");
    Check(vst3scanner::SerializeInventoryCsv(parsedMissing.audioPlugins, {})
                  .find(";Not reported;") != std::string::npos,
          "CSV must label an omitted factory version without inventing one");
}

void TestProtocolErrors() {
    Check(!vst3scanner::ParseProbeResultJson("").valid,
          "empty stdout must be a protocol error");
    Check(!vst3scanner::ParseProbeResultJson("{broken").valid,
          "malformed JSON must be a protocol error");
    auto wrongSchema = ValidProbeJson();
    const auto position = wrongSchema.find("\"schemaVersion\": 2");
    wrongSchema.replace(position, std::string("\"schemaVersion\": 2").size(),
                        "\"schemaVersion\": 99");
    Check(!vst3scanner::ParseProbeResultJson(wrongSchema).valid,
          "unknown schema versions must be rejected");
    auto unknownStatus = ValidProbeJson();
    const auto statusPosition = unknownStatus.find("\"status\": \"ok\"");
    unknownStatus.replace(statusPosition, std::string("\"status\": \"ok\"").size(),
                          "\"status\": \"future_status\"");
    Check(!vst3scanner::ParseProbeResultJson(unknownStatus).valid,
          "unknown protocol status values must be rejected");

    auto invalidUtf8 = ValidProbeJson();
    const auto namePosition = invalidUtf8.find("Compressor");
    invalidUtf8[namePosition] = static_cast<char>(0xFF);
    Check(!vst3scanner::ParseProbeResultJson(invalidUtf8).valid,
          "invalid UTF-8 must be rejected by the protocol parser");

    auto inconsistentVersion = ValidProbeJson();
    const auto versionMissingPosition = inconsistentVersion.find("\"versionMissing\": false");
    inconsistentVersion.replace(versionMissingPosition,
                                std::string("\"versionMissing\": false").size(),
                                "\"versionMissing\": true");
    Check(!vst3scanner::ParseProbeResultJson(inconsistentVersion).valid,
          "versionMissing must agree with the raw factory version");

    auto wrongClassCount = ValidProbeJson();
    const auto classCountPosition = wrongClassCount.find("\"classCount\": 2");
    wrongClassCount.replace(classCountPosition, std::string("\"classCount\": 2").size(),
                            "\"classCount\": 1");
    Check(!vst3scanner::ParseProbeResultJson(wrongClassCount).valid,
          "module classCount must match the classes array");
}

void TestCidDuplicateDetection() {
    vst3scanner::InventoryRecord first;
    first.cid = "00112233445566778899AABBCCDDEEFF";
    first.name = "Old shell";
    first.modulePath = "C:\\VST3\\OldShell.vst3";
    vst3scanner::InventoryRecord second = first;
    second.cid = "00112233445566778899aabbccddeeff";
    second.name = "New shell";
    second.modulePath = "C:\\VST3\\NewShell.vst3";
    vst3scanner::InventoryRecord unique;
    unique.cid = "FFEEDDCCBBAA99887766554433221100";
    unique.modulePath = "C:\\VST3\\Unique.vst3";

    auto repeatedInSameModule = first;
    repeatedInSameModule.name = "Repeated factory entry";
    std::vector records{first, second, unique, repeatedInSameModule};
    vst3scanner::MarkCidDuplicates(records);
    Check(records[0].duplicate && records[0].duplicateCount == 2,
          "same CID in multiple modules must be marked");
    Check(records[1].duplicate && records[1].duplicateCount == 2,
          "CID comparison must be case-insensitive");
    Check(!records[2].duplicate, "unique CIDs must not be marked");
    Check(records[3].duplicateCount == 2,
          "duplicate count must represent distinct module paths, not class rows");
}

void TestExports() {
    auto parsed = vst3scanner::ParseProbeResultJson(ValidProbeJson());
    vst3scanner::MarkCidDuplicates(parsed.audioPlugins);
    const auto csv = vst3scanner::SerializeInventoryCsv(parsed.audioPlugins, {});
    const auto json = vst3scanner::SerializeInventoryJson(parsed.audioPlugins, {});
    Check(csv.find("Plugin;Vendor;Version;Version source;SDK version;Category;Module;Module path;") == 0,
          "CSV columns must match the visible inventory order");
    Check(csv.find("Compressor;Factory Vendor;2.4.1;VST3 factory;VST 3.8.0") !=
              std::string::npos,
          "CSV must contain harmonized inventory values");
    Check(csv.find(";CID;") == std::string::npos,
          "CID must remain hidden from the user-facing CSV table");
    Check(json.find("\"version\":\"2.4.1\"") != std::string::npos,
          "JSON must contain raw installed version");
    Check(json.find("\"versionMissing\":false") != std::string::npos,
          "JSON must expose whether the factory reported a version");
    Check(json.find("\"probeDurationMs\":42") != std::string::npos,
          "JSON must contain probe duration");
    Check(json.find("\"diagnostic\":\"\"") != std::string::npos,
          "JSON must preserve per-plugin diagnostics");
}

void TestCsvFormulaInjectionGuard() {
    vst3scanner::InventoryRecord record;
    record.cid = "00112233445566778899AABBCCDDEEFF";
    record.name = "=HYPERLINK(\"https://example.invalid\")";
    record.vendor = "+Untrusted vendor";
    record.version = "-1+1";
    record.sdkVersion = "@SUM(1,1)";
    record.modulePath = "C:\\VST3\\Safe.vst3";
    record.protocolStatus = "ok";

    const auto csv = vst3scanner::SerializeInventoryCsv({record}, {});
    Check(csv.find("\"'=HYPERLINK(\"\"https://example.invalid\"\")\"") != std::string::npos,
          "CSV must neutralize formula-like plug-in names");
    Check(csv.find("\"'+Untrusted vendor\"") != std::string::npos,
          "CSV must neutralize formula-like vendor names");
    Check(csv.find("\"'-1+1\"") != std::string::npos,
          "CSV must neutralize formula-like version values");
    Check(csv.find("\"'@SUM(1,1)\"") != std::string::npos,
          "CSV must neutralize formula-like SDK values");
}

void TestWaveShellExpansion() {
    auto parsed = vst3scanner::ParseProbeResultJson(WaveShellProbeJson());
    Check(parsed.valid, "WaveShell probe JSON must parse");
    Check(parsed.audioPlugins.size() == 3,
          "every WaveShell audio class must become a separate inventory row");
    Check(parsed.audioPlugins[0].name == "COSMOS Sample Finder Stereo" &&
              parsed.audioPlugins[1].name == "OVox Instrument Stereo" &&
              parsed.audioPlugins[2].name == "OVox Stereo",
          "WaveShell class names must remain visible without shell heuristics");
    Check(parsed.audioPlugins[0].modulePath == parsed.audioPlugins[2].modulePath,
          "expanded WaveShell classes must retain their common module path");

    const auto csv = vst3scanner::SerializeInventoryCsv(parsed.audioPlugins, {});
    Check(csv.find("WaveShell1-VST3 17.1_x64.vst3") != std::string::npos,
          "CSV must identify the WaveShell module for every expanded class");
}

void TestDuplicateFactoryClassHandling() {
    auto duplicate = WaveShellProbeJson();
    const auto duplicateCid = duplicate.rfind("00112233445566778899AABBCCDDEE03");
    duplicate.replace(duplicateCid, 32, "00112233445566778899AABBCCDDEE01");
    Check(!vst3scanner::ParseProbeResultJson(duplicate).valid,
          "an OK response must not silently contain duplicate audio CIDs");

    const auto status = duplicate.find("\"status\": \"ok\"");
    duplicate.replace(status, std::string("\"status\": \"ok\"").size(),
                      "\"status\": \"partial\"");
    const auto parsed = vst3scanner::ParseProbeResultJson(duplicate);
    Check(parsed.valid && parsed.audioPlugins.size() == 2,
          "a partial factory response must retain one row per unique audio CID");
}

void TestProbeCacheFormat() {
    const std::vector<vst3scanner::ProbeCacheEntry> entries = {
        {"0123456789abcdef", ValidProbeJson()},
    };
    const auto serialized = vst3scanner::SerializeProbeCacheJson(entries);
    const auto parsed = vst3scanner::ParseProbeCacheJson(serialized);
    Check(parsed.valid && parsed.entries.size() == 1,
          "serialized cache must parse as one entry");
    Check(parsed.entries[0].key == "0123456789ABCDEF",
          "cache keys must be normalized to uppercase");
    Check(vst3scanner::ParseProbeResultJson(parsed.entries[0].probeJson).valid,
          "embedded probe JSON must survive cache serialization");

    auto legacyCache = serialized;
    const auto schemaPosition = legacyCache.find("\"schemaVersion\":2");
    legacyCache.replace(schemaPosition, std::string("\"schemaVersion\":2").size(),
                        "\"schemaVersion\":1");
    Check(!vst3scanner::ParseProbeCacheJson(legacyCache).valid,
          "legacy metadata-only cache files must be rebuilt for content hashing");

    Check(!vst3scanner::ParseProbeCacheJson("{broken").valid,
          "malformed cache JSON must be rejected");
    const std::vector<vst3scanner::ProbeCacheEntry> duplicateEntries = {
        {"0123456789ABCDEF", ValidProbeJson()},
        {"0123456789abcdef", ValidProbeJson()},
    };
    Check(!vst3scanner::ParseProbeCacheJson(
               vst3scanner::SerializeProbeCacheJson(duplicateEntries)).valid,
          "duplicate cache keys must be rejected");

    const std::string invalidEmbedded =
        R"({"schemaVersion":2,"entries":[{"key":"0123456789ABCDEF","probeJson":"{broken"}]})";
    Check(!vst3scanner::ParseProbeCacheJson(invalidEmbedded).valid,
          "invalid embedded probe JSON must reject the complete cache");
}

}  // namespace

int main() {
    TestStrictParsingAndAudioFilter();
    TestProtocolErrors();
    TestCidDuplicateDetection();
    TestExports();
    TestCsvFormulaInjectionGuard();
    TestWaveShellExpansion();
    TestDuplicateFactoryClassHandling();
    TestProbeCacheFormat();
    if (failures == 0) std::cout << "All VST3 inventory tests passed.\n";
    return failures == 0 ? 0 : 1;
}
