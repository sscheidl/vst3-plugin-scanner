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
        "schemaVersion": 1,
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

void TestStrictParsingAndAudioFilter() {
    const auto parsed = vst3scanner::ParseProbeResultJson(ValidProbeJson());
    Check(parsed.valid, "valid schema 1 JSON must parse");
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
    const auto position = wrongSchema.find("\"schemaVersion\": 1");
    wrongSchema.replace(position, std::string("\"schemaVersion\": 1").size(),
                        "\"schemaVersion\": 2");
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
    Check(csv.find("Factory Vendor;Compressor;2.4.1") != std::string::npos,
          "CSV must contain inventory values");
    Check(json.find("\"version\":\"2.4.1\"") != std::string::npos,
          "JSON must contain raw installed version");
    Check(json.find("\"probeDurationMs\":42") != std::string::npos,
          "JSON must contain probe duration");
    Check(json.find("\"diagnostic\":\"\"") != std::string::npos,
          "JSON must preserve per-plugin diagnostics");
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
        R"({"schemaVersion":1,"entries":[{"key":"0123456789ABCDEF","probeJson":"{broken"}]})";
    Check(!vst3scanner::ParseProbeCacheJson(invalidEmbedded).valid,
          "invalid embedded probe JSON must reject the complete cache");
}

}  // namespace

int main() {
    TestStrictParsingAndAudioFilter();
    TestProtocolErrors();
    TestCidDuplicateDetection();
    TestExports();
    TestProbeCacheFormat();
    if (failures == 0) std::cout << "All VST3 inventory tests passed.\n";
    return failures == 0 ? 0 : 1;
}
