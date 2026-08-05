#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vst3scanner {

enum class ProbeStatus {
    Ok,
    Partial,
    NotVst3,
    WrongArchitecture,
    LoadError,
    FactoryMissing,
    FactoryError,
    NoClasses,
    Timeout,
    Crashed,
    AccessError,
    ProtocolError,
};

struct Vst3ClassData {
    std::int32_t index = -1;
    std::string cid;
    std::int32_t cardinality = 0;
    std::string category;
    std::string name;
    std::uint32_t classFlags = 0;
    std::vector<std::string> subCategories;
    std::string vendor;
    std::string classVersionRaw;
    std::string sdkVersionRaw;
    std::int32_t factoryInterface = 0;
    bool isAudioPlugin = false;
    std::string diagnostic;
};

struct Vst3ModuleData {
    std::string path;
    std::string name;
    bool isBundle = false;
    std::string factoryVendor;
    std::string factoryUrl;
    std::string factoryEmail;
    std::int32_t factoryFlags = 0;
    std::int32_t classCount = 0;
    std::uint64_t probeDurationMs = 0;
};

struct ProbeResult {
    std::int32_t schemaVersion = 2;
    ProbeStatus status = ProbeStatus::ProtocolError;
    Vst3ModuleData module;
    std::vector<Vst3ClassData> classes;
    std::string diagnostic;
};

inline constexpr std::string_view kAudioModuleCategory = "Audio Module Class";

[[nodiscard]] constexpr bool IsAudioPluginCategory(std::string_view category) noexcept {
    return category == kAudioModuleCategory;
}

[[nodiscard]] const char* ToProtocolString(ProbeStatus status) noexcept;

}  // namespace vst3scanner
