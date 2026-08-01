#include "VersionUtil.h"

#include <iostream>
#include <optional>
#include <string>

namespace {

int failures = 0;

void ExpectEqual(const std::wstring& actual,
                 const std::wstring& expected,
                 const char* testName) {
    if (actual != expected) {
        std::wcerr << L"FAILED " << testName << L": expected '" << expected
                   << L"', got '" << actual << L"'\n";
        ++failures;
    }
}

void ExpectComparison(const std::wstring& left,
                      const std::wstring& right,
                      std::optional<int> expected,
                      const char* testName) {
    const auto actual = CompareVersionStrings(left, right);
    if (actual != expected) {
        std::cerr << "FAILED " << testName << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    ExpectEqual(NormalizeVersionString(L"  Version 1, 2, 3, 4  "), L"1.2.3.4", "comma normalization");
    ExpectEqual(NormalizeVersionString(L"v2.5.0-beta"), L"2.5.0-beta", "prefix and prerelease");
    ExpectEqual(NormalizeVersionString(L"1.4.2 (x64)"), L"1.4.2", "architecture suffix");
    ExpectEqual(ExtractVersionFromText(L"WaveShell1-VST3 15.5.79.262"), L"15.5.79.262", "filename extraction");
    ExpectEqual(ExtractVersionFromText(L"Plugin without version"), L"", "missing filename version");

    ExpectComparison(L"1.2", L"1.2.0", 0, "zero padded equality");
    ExpectComparison(L"1.10", L"1.9.9", 1, "numeric comparison");
    ExpectComparison(L"2.0-beta", L"2.0", -1, "prerelease ordering");
    ExpectComparison(L"2.0-rc10", L"2.0-rc2", 1, "natural prerelease ordering");
    ExpectComparison(L"unknown", L"1.0", std::nullopt, "invalid comparison");

    if (failures == 0) {
        std::cout << "All VersionUtil tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
