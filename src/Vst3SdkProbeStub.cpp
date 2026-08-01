#include "Vst3SdkProbe.h"

namespace {

class DisabledVst3SdkProbe final : public IVst3SdkProbe {
public:
    [[nodiscard]] bool IsAvailable() const noexcept override {
        return false;
    }

    [[nodiscard]] Vst3SdkProbeResult Probe(
        const std::filesystem::path&,
        std::chrono::milliseconds) const override {
        return {};
    }
};

} // namespace

std::unique_ptr<IVst3SdkProbe> CreateVst3SdkProbe() {
    return std::make_unique<DisabledVst3SdkProbe>();
}
