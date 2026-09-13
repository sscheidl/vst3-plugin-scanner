#include "JsonProtocol.h"
#include "Vst3FactoryReader.h"

#include "public.sdk/source/vst/utility/stringconvert.h"

#include <fcntl.h>
#include <io.h>

#include <exception>
#include <iostream>
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

void EmitResult(const vst3scanner::ProbeResult& result) {
    std::cout << vst3scanner::SerializeProbeResult(result) << '\n';
    if (!result.diagnostic.empty()) {
        std::cerr << result.diagnostic << '\n';
    }
}

}  // namespace

int wmain(int argumentCount, wchar_t* arguments[]) {
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);

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

    EmitResult(result);
    return ExitCode(result.status);
}
