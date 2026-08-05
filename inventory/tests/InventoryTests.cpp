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
            "factoryVendor": "Factory Vendor",
            "probeDurationMs": 42
        },
        "classes": [
            {
                "cid": "00112233445566778899AABBCCDDEEFF",
                "category": "Audio Module Class",
                "name": "Compressor",
                "vendor": "",
                "version": "2.4.1",
                "sdkVersion": "VST 3.8.0",
                "subCategories": ["Fx", "Dynamics"],
                "isAudioPlugin": true,
                "diagnostic": ""
            },
            {
                "cid": "FFEEDDCCBBAA99887766554433221100",
                "category": "Component Controller Class",
                "name": "Compressor Controller",
                "vendor": "Factory Vendor",
                "version": "2.4.1",
                "sdkVersion": "VST 3.8.0",
                "subCategories": [],
                "isAudioPlugin": false,
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
            "factoryVendor": "Waves",
            "probeDurationMs": 12610
        },
        "classes": [
            {"cid":"00112233445566778899AABBCCDDEE01","category":"Audio Module Class",
             "name":"COSMOS Sample Finder Stereo","vendor":"Waves","version":"17.1.42.50",
             "sdkVersion":"VST 3.6.9","subCategories":["Instrument","Waves"],
             "isAudioPlugin":true,"diagnostic":""},
            {"cid":"00112233445566778899AABBCCDDEE02","category":"Audio Module Class",
             "name":"OVox Instrument Stereo","vendor":"Waves","version":"17.1.42.51",
             "sdkVersion":"VST 3.6.9","subCategories":["Instrument","Waves"],
             "isAudioPlugin":true,"diagnostic":""},
            {"cid":"00112233445566778899AABBCCDDEE03","category":"Audio Module Class",
             "name":"OVox Stereo","vendor":"Waves","version":"17.1.42.51",
             "sdkVersion":"VST 3.6.9","subCategories":["Fx","Modulation"],
             "isAudioPlugin":true,"diagnostic":""}
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
    Check(csv.find("Plugin;Hersteller;Version;SDK-Version;Kategorie;Modul;Modulpfad;") == 0,
          "CSV columns must match the visible inventory order");
    Check(csv.find("Compressor;Factory Vendor;2.4.1;VST 3.8.0") != std::string::npos,
          "CSV must contain harmonized inventory values");
    Check(csv.find(";CID;") == std::string::npos,
          "CID must remain hidden from the user-facing CSV table");
    Check(json.find("\"version\":\"2.4.1\"") != std::string::npos,
          "JSON must contain raw installed version");
    Check(json.find("\"probeDurationMs\":42") != std::string::npos,
          "JSON must contain probe duration");
    Check(json.find("\"diagnostic\":\"\"") != std::string::npos,
          "JSON must preserve per-plugin diagnostics");
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
    TestWaveShellExpansion();
    TestProbeCacheFormat();
    if (failures == 0) std::cout << "All VST3 inventory tests passed.\n";
    return failures == 0 ? 0 : 1;
}
