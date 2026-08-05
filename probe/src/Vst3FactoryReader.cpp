#include "Vst3FactoryReader.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "public.sdk/source/vst/hosting/module.h"

#include <chrono>
#include <filesystem>

namespace vst3scanner {
namespace {

using Clock = std::chrono::steady_clock;
constexpr Steinberg::int32 kMaximumFactoryClasses = 100000;

void AppendDiagnostic(std::string& target, const std::string& message) {
    if (message.empty()) {
        return;
    }
    if (!target.empty()) {
        target.append("; ");
    }
    target.append(message);
}

Vst3ClassData ConvertClassInfo(
    std::int32_t index,
    std::int32_t factoryInterface,
    const VST3::Hosting::ClassInfo& source) {
    Vst3ClassData result;
    result.index = index;
    result.cid = source.ID().toString();
    result.cardinality = source.cardinality();
    result.category = source.category();
    result.name = source.name();
    result.classFlags = source.classFlags();
    result.subCategories = source.subCategories();
    result.vendor = source.vendor();
    result.classVersionRaw = source.version();
    result.sdkVersionRaw = source.sdkVersion();
    result.factoryInterface = factoryInterface;
    result.isAudioPlugin = IsAudioPluginCategory(result.category);
    return result;
}

}  // namespace

ProbeResult ProbeVst3Module(const std::string& modulePathUtf8) {
    const auto started = Clock::now();
    ProbeResult result;
    result.module.path = modulePathUtf8;

    const auto finish = [&](ProbeStatus status, const std::string& diagnostic = {}) {
        result.status = status;
        AppendDiagnostic(result.diagnostic, diagnostic);
        result.module.probeDurationMs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
        return result;
    };

    if (modulePathUtf8.empty()) {
        return finish(ProbeStatus::ProtocolError, "The module path is empty.");
    }

    std::string loadError;
    auto module = VST3::Hosting::Module::create(modulePathUtf8, loadError);
    if (!module) {
        return finish(ProbeStatus::LoadError,
                      loadError.empty() ? "The VST3 module could not be loaded." : loadError);
    }

    result.module.name = module->getName();
    result.module.isBundle = module->isBundle();

    const auto& factoryPointer = module->getFactory().get();
    if (!factoryPointer) {
        return finish(ProbeStatus::FactoryMissing, "GetPluginFactory returned no factory.");
    }

    bool partial = false;
    Steinberg::PFactoryInfo factoryInfo{};
    if (factoryPointer->getFactoryInfo(&factoryInfo) == Steinberg::kResultOk) {
        const VST3::Hosting::FactoryInfo converted(std::move(factoryInfo));
        result.module.factoryVendor = converted.vendor();
        result.module.factoryUrl = converted.url();
        result.module.factoryEmail = converted.email();
        result.module.factoryFlags = converted.flags();
    } else {
        partial = true;
        AppendDiagnostic(result.diagnostic, "IPluginFactory::getFactoryInfo failed.");
    }

    const auto classCount = factoryPointer->countClasses();
    if (classCount < 0) {
        return finish(ProbeStatus::FactoryError,
                      "IPluginFactory::countClasses returned a negative value.");
    }
    if (classCount > kMaximumFactoryClasses) {
        return finish(ProbeStatus::FactoryError,
                      "IPluginFactory::countClasses exceeded the safety limit.");
    }
    result.module.classCount = classCount;
    if (classCount == 0) {
        return finish(ProbeStatus::NoClasses, "The factory exports no classes.");
    }

    const auto factory3 = Steinberg::U::cast<Steinberg::IPluginFactory3>(factoryPointer);
    const auto factory2 = Steinberg::U::cast<Steinberg::IPluginFactory2>(factoryPointer);
    result.classes.reserve(static_cast<std::size_t>(classCount));

    for (Steinberg::int32 index = 0; index < classCount; ++index) {
        if (factory3) {
            Steinberg::PClassInfoW info{};
            if (factory3->getClassInfoUnicode(index, &info) == Steinberg::kResultOk) {
                result.classes.push_back(ConvertClassInfo(
                    index, 3, VST3::Hosting::ClassInfo(info)));
                continue;
            }
        }
        if (factory2) {
            Steinberg::PClassInfo2 info{};
            if (factory2->getClassInfo2(index, &info) == Steinberg::kResultOk) {
                result.classes.push_back(ConvertClassInfo(
                    index, 2, VST3::Hosting::ClassInfo(info)));
                continue;
            }
        }

        Steinberg::PClassInfo info{};
        if (factoryPointer->getClassInfo(index, &info) == Steinberg::kResultOk) {
            result.classes.push_back(ConvertClassInfo(
                index, 1, VST3::Hosting::ClassInfo(info)));
            continue;
        }

        partial = true;
        Vst3ClassData failedClass;
        failedClass.index = index;
        failedClass.diagnostic = "All supported class-info queries failed for this index.";
        result.classes.push_back(std::move(failedClass));
        AppendDiagnostic(result.diagnostic,
                         "Class metadata could not be read at index " + std::to_string(index) + '.');
    }

    return finish(partial ? ProbeStatus::Partial : ProbeStatus::Ok);
}

}  // namespace vst3scanner
