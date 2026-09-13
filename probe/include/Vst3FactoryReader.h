#pragma once

#include "ProbeModel.h"

#include <string>

namespace vst3scanner {

// The caller passes a UTF-8 path. This function loads no plugin instances and
// invokes only the module entry points and factory metadata interfaces.
[[nodiscard]] ProbeResult ProbeVst3Module(const std::string& modulePathUtf8);

}  // namespace vst3scanner
