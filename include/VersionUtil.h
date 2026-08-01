#pragma once

#include <optional>
#include <string>

// Returns a conservative, display-ready version. Non-version text is preserved.
[[nodiscard]] std::wstring NormalizeVersionString(const std::wstring& value);

// Extracts a version-like token with at least two numeric components.
[[nodiscard]] std::wstring ExtractVersionFromText(const std::wstring& value);

// Returns -1, 0 or 1. std::nullopt means that at least one value is not comparable.
[[nodiscard]] std::optional<int> CompareVersionStrings(const std::wstring& left,
                                                       const std::wstring& right);
