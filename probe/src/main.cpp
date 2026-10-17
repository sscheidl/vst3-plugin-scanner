#include "JsonProtocol.h"
#include "Vst3FactoryReader.h"
#include "ProbeEntry.h"
#include <windows.h>

#include "public.sdk/source/vst/utility/stringconvert.h"

#include <exception>
#include <algorithm>
#include <limits>
#include <string>

namespace {

int ExitCode(vst3scanner::ProbeStatus status) {
    using vst3scanner::ProbeStatus;
    switch (status) {
        case ProbeStatus::Ok:
        case ProbeStatus::Partial:
            return 0;
        case ProbeStatus::ProtocolError:
            return 2;
        case ProbeStatus::NotVst3:
        case ProbeStatus::AccessError:
            return 3;
        case ProbeStatus::WrongArchitecture:
        case ProbeStatus::LoadError:
            return 4;
        case ProbeStatus::FactoryMissing:
        case ProbeStatus::FactoryError:
            return 5;
        case ProbeStatus::NoClasses:
            return 6;
        case ProbeStatus::Timeout:
        case ProbeStatus::Crashed:
            return 7;
    }
    return 2;
}

bool WriteOutput(DWORD stream, const std::string& text) {
    const HANDLE handle = GetStdHandle(stream);
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto length = static_cast<DWORD>(std::min<std::size_t>(
            text.size() - offset, (std::numeric_limits<DWORD>::max)()));
        DWORD written = 0;
        if (!WriteFile(handle, text.data() + offset, length, &written, nullptr) || written == 0)
            return false;
        offset += written;
    }
    return true;
}

bool EmitResult(const vst3scanner::ProbeResult& result) {
    // GUI-subsystem child processes have inherited Win32 handles but no CRT console.
    const bool written = WriteOutput(STD_OUTPUT_HANDLE,
                                    vst3scanner::SerializeProbeResult(result) + '\n');
    if (!result.diagnostic.empty()) {
        WriteOutput(STD_ERROR_HANDLE, result.diagnostic + '\n');
    }
    return written;
}

}  // namespace

int vst3scanner::RunMetadataProbe(int argumentCount, wchar_t* arguments[]) {
    vst3scanner::ProbeResult result;
    std::string modulePath;
    try {
        if (argumentCount != 2) {
            result.status = vst3scanner::ProbeStatus::ProtocolError;
            result.diagnostic = "Usage: Vst3MetadataProbe.exe <module.vst3>";
            EmitResult(result);
            return ExitCode(result.status);
        }

        modulePath = Steinberg::Vst::StringConvert::convert(Steinberg::wscast(arguments[1]));
        result.module.path = modulePath;
        result = vst3scanner::ProbeVst3Module(modulePath);
    } catch (const std::exception& error) {
        result.status = vst3scanner::ProbeStatus::ProtocolError;
        result.module.path = modulePath;
        result.diagnostic = std::string("Unhandled probe exception: ") + error.what();
    } catch (...) {
        result.status = vst3scanner::ProbeStatus::ProtocolError;
        result.module.path = modulePath;
        result.diagnostic = "Unhandled non-standard probe exception.";
    }

    if (!EmitResult(result)) return 2;
    return ExitCode(result.status);
}

#ifndef VST3_EMBEDDED_PROBE
int wmain(int argumentCount, wchar_t* arguments[]) {
    return vst3scanner::RunMetadataProbe(argumentCount, arguments);
}
#endif
