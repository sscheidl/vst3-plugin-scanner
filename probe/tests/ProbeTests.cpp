#include "JsonProtocol.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/utility/uid.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void TestCidFormatting() {
    const VST3::UID uid(0x00112233U, 0x44556677U, 0x8899AABBU, 0xCCDDEEFFU, false);
    const auto value = uid.toString(false);
    Check(value == "00112233445566778899AABBCCDDEEFF", "CID must be 32 uppercase hex digits");
}

void TestUnicodeConversion() {
    const std::string original = "Gr\xC3\xB6\xC3\x9F\x65";
    const auto utf16 = Steinberg::Vst::StringConvert::convert(original);
    Check(Steinberg::Vst::StringConvert::convert(utf16) == original,
          "UTF-8 and UTF-16 conversion must round-trip");
}

void TestAudioPluginClassification() {
    Check(vst3scanner::IsAudioPluginCategory("Audio Module Class"),
          "the exact VST3 audio class category must be recognized");
    Check(!vst3scanner::IsAudioPluginCategory("Component Controller Class"),
          "non-audio factory classes must remain visible but not be marked as audio plugins");
}

void TestJsonEscapingAndUtf8Repair() {
    Check(vst3scanner::JsonEscape("a\"b\\c\n") == "a\\\"b\\\\c\\n",
          "JSON special characters must be escaped");
    const std::string invalid("bad\xFF", 4);
    Check(vst3scanner::SanitizeUtf8(invalid) == "bad\xEF\xBF\xBD",
          "invalid UTF-8 must be replaced so stdout remains valid JSON");
    const std::string overlong("\xE0\x80\x80", 3);
    Check(vst3scanner::SanitizeUtf8(overlong) ==
              "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD",
          "overlong UTF-8 encodings must be rejected");
}

void TestProtocolWithMultipleClassesAndMissingVersion() {
    vst3scanner::ProbeResult result;
    result.status = vst3scanner::ProbeStatus::Ok;
    result.module.path = "C:\\Test\\Shell.vst3";
    result.module.classCount = 2;

    vst3scanner::Vst3ClassData first;
    first.index = 0;
    first.cid = "00112233445566778899AABBCCDDEEFF";
    first.name = "Compressor";
    first.classVersionRaw = "2.4.1";
    first.isAudioPlugin = true;
    result.classes.push_back(first);

    vst3scanner::Vst3ClassData second;
    second.index = 1;
    second.cid = "FFEEDDCCBBAA99887766554433221100";
    second.name = "Analyzer";
    result.classes.push_back(second);

    const auto json = vst3scanner::SerializeProbeResult(result);
    Check(json.find("\"classCount\":2") != std::string::npos,
          "module class count must be serialized");
    Check(json.find("\"version\":\"2.4.1\"") != std::string::npos,
          "raw class version must be serialized unchanged");
    Check(json.find("\"versionMissing\":true") != std::string::npos,
          "empty versions must remain visible");
    Check(json.find("FFEEDDCCBBAA99887766554433221100") != std::string::npos,
          "all classes in a multi-class module must be serialized");
}

}  // namespace

int main() {
    TestCidFormatting();
    TestUnicodeConversion();
    TestAudioPluginClassification();
    TestJsonEscapingAndUtf8Repair();
    TestProtocolWithMultipleClassesAndMissingVersion();

    if (failures == 0) {
        std::cout << "All VST3 probe tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
