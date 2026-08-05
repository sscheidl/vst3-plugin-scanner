#pragma once

#include "ProbeModel.h"

#include <string>
#include <string_view>

namespace vst3scanner {

[[nodiscard]] std::string SanitizeUtf8(std::string_view value);
[[nodiscard]] std::string JsonEscape(std::string_view value);
[[nodiscard]] std::string SerializeProbeResult(const ProbeResult& result);

}  // namespace vst3scanner
