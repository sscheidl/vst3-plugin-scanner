#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

struct Vst3SdkProbeResult {
    bool succeeded = false;
    std::wstring moduleName;
    std::wstring vendor;
    std::wstring version;
    std::wstring category;
    std::wstring diagnostic;
};

// Implementations must isolate all plug-in loading in a separate process. The GUI
// scanner process must never load or initialize a third-party plug-in binary.
class IVst3SdkProbe {
public:
    virtual ~IVst3SdkProbe() = default;
    [[nodiscard]] virtual bool IsAvailable() const noexcept = 0;
    [[nodiscard]] virtual Vst3SdkProbeResult Probe(
        const std::filesystem::path& bundlePath,
        std::chrono::milliseconds timeout) const = 0;
};

[[nodiscard]] std::unique_ptr<IVst3SdkProbe> CreateVst3SdkProbe();
