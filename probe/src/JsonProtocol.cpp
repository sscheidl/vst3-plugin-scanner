#include "JsonProtocol.h"

#include <iomanip>
#include <sstream>

namespace vst3scanner {
namespace {

constexpr std::string_view kReplacementCharacter = "\xEF\xBF\xBD";

void WriteString(std::ostringstream& output, std::string_view value) {
    output << '\"' << JsonEscape(value) << '\"';
}

void WriteStringArray(std::ostringstream& output, const std::vector<std::string>& values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        WriteString(output, values[index]);
    }
    output << ']';
}

bool IsContinuation(unsigned char value) {
    return (value & 0xC0U) == 0x80U;
}

}  // namespace

const char* ToProtocolString(ProbeStatus status) noexcept {
    switch (status) {
        case ProbeStatus::Ok: return "ok";
        case ProbeStatus::Partial: return "partial";
        case ProbeStatus::NotVst3: return "not_vst3";
        case ProbeStatus::WrongArchitecture: return "wrong_architecture";
        case ProbeStatus::LoadError: return "load_error";
        case ProbeStatus::FactoryMissing: return "factory_missing";
        case ProbeStatus::FactoryError: return "factory_error";
        case ProbeStatus::NoClasses: return "no_classes";
        case ProbeStatus::Timeout: return "timeout";
        case ProbeStatus::Crashed: return "crashed";
        case ProbeStatus::AccessError: return "access_error";
        case ProbeStatus::ProtocolError: return "protocol_error";
    }
    return "protocol_error";
}

std::string SanitizeUtf8(std::string_view value) {
    std::string result;
    result.reserve(value.size());

    for (std::size_t index = 0; index < value.size();) {
        const auto lead = static_cast<unsigned char>(value[index]);
        if (lead <= 0x7FU) {
            result.push_back(static_cast<char>(lead));
            ++index;
            continue;
        }

        std::size_t length = 0;
        std::uint32_t codePoint = 0;
        if (lead >= 0xC2U && lead <= 0xDFU) {
            length = 2;
            codePoint = lead & 0x1FU;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            length = 3;
            codePoint = lead & 0x0FU;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            length = 4;
            codePoint = lead & 0x07U;
        }

        bool valid = length != 0 && index + length <= value.size();
        for (std::size_t offset = 1; valid && offset < length; ++offset) {
            const auto byte = static_cast<unsigned char>(value[index + offset]);
            valid = IsContinuation(byte);
            codePoint = (codePoint << 6U) | (byte & 0x3FU);
        }
        if (valid) {
            const std::uint32_t minimum = length == 2 ? 0x80U : (length == 3 ? 0x800U : 0x10000U);
            valid = codePoint >= minimum && codePoint <= 0x10FFFFU &&
                    !(codePoint >= 0xD800U && codePoint <= 0xDFFFU);
        }

        if (!valid) {
            result.append(kReplacementCharacter);
            ++index;
            continue;
        }

        result.append(value.substr(index, length));
        index += length;
    }
    return result;
}

std::string JsonEscape(std::string_view value) {
    const auto validUtf8 = SanitizeUtf8(value);
    std::ostringstream output;
    for (const auto character : validUtf8) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
            case '\"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (byte < 0x20U) {
                    output << "\\u" << std::hex << std::uppercase << std::setw(4)
                           << std::setfill('0') << static_cast<unsigned int>(byte)
                           << std::dec << std::nouppercase;
                } else {
                    output << character;
                }
        }
    }
    return output.str();
}

std::string SerializeProbeResult(const ProbeResult& result) {
    std::ostringstream output;
    output << '{';
    output << "\"schemaVersion\":" << result.schemaVersion << ',';
    output << "\"status\":";
    WriteString(output, ToProtocolString(result.status));
    output << ",\"module\":{";
    output << "\"path\":"; WriteString(output, result.module.path);
    output << ",\"name\":"; WriteString(output, result.module.name);
    output << ",\"binaryPath\":"; WriteString(output, result.module.binaryPath);
    output << ",\"isBundle\":" << (result.module.isBundle ? "true" : "false");
    output << ",\"factoryVendor\":"; WriteString(output, result.module.factoryVendor);
    output << ",\"factoryUrl\":"; WriteString(output, result.module.factoryUrl);
    output << ",\"factoryEmail\":"; WriteString(output, result.module.factoryEmail);
    output << ",\"factoryFlags\":" << result.module.factoryFlags;
    output << ",\"classCount\":" << result.module.classCount;
    output << ",\"probeDurationMs\":" << result.module.probeDurationMs;
    output << "},\"classes\":[";

    for (std::size_t index = 0; index < result.classes.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        const auto& pluginClass = result.classes[index];
        output << '{';
        output << "\"index\":" << pluginClass.index;
        output << ",\"cid\":"; WriteString(output, pluginClass.cid);
        output << ",\"category\":"; WriteString(output, pluginClass.category);
        output << ",\"name\":"; WriteString(output, pluginClass.name);
        output << ",\"vendor\":"; WriteString(output, pluginClass.vendor);
        output << ",\"version\":"; WriteString(output, pluginClass.classVersionRaw);
        output << ",\"sdkVersion\":"; WriteString(output, pluginClass.sdkVersionRaw);
        output << ",\"subCategories\":"; WriteStringArray(output, pluginClass.subCategories);
        output << ",\"classFlags\":" << pluginClass.classFlags;
        output << ",\"cardinality\":" << pluginClass.cardinality;
        output << ",\"factoryInterface\":" << pluginClass.factoryInterface;
        output << ",\"isAudioPlugin\":" << (pluginClass.isAudioPlugin ? "true" : "false");
        output << ",\"versionMissing\":"
               << (pluginClass.classVersionRaw.empty() ? "true" : "false");
        output << ",\"diagnostic\":"; WriteString(output, pluginClass.diagnostic);
        output << '}';
    }

    output << "],\"diagnostic\":";
    WriteString(output, result.diagnostic);
    output << '}';
    return output.str();
}

}  // namespace vst3scanner
