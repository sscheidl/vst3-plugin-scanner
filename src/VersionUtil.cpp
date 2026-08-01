#include "VersionUtil.h"

#include "StringUtil.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <vector>

namespace {

struct ParsedVersion {
    std::vector<unsigned long long> parts;
    std::wstring suffix;
};

bool IsVersionSeparator(wchar_t c) {
    return c == L'.' || c == L',' || c == L'_';
}

bool ParseUnsigned(const std::wstring& text,
                   std::size_t& index,
                   unsigned long long& value) {
    if (index >= text.size() || !std::iswdigit(text[index])) {
        return false;
    }

    value = 0;
    while (index < text.size() && std::iswdigit(text[index])) {
        const unsigned digit = static_cast<unsigned>(text[index] - L'0');
        if (value > (std::numeric_limits<unsigned long long>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
        ++index;
    }
    return true;
}

std::optional<ParsedVersion> ParseAt(const std::wstring& text,
                                     std::size_t start,
                                     std::size_t& end,
                                     bool requireMultipleParts) {
    if (start >= text.size()) {
        return std::nullopt;
    }

    std::size_t index = start;
    if ((text[index] == L'v' || text[index] == L'V') &&
        index + 1 < text.size() && std::iswdigit(text[index + 1])) {
        ++index;
    }

    ParsedVersion parsed;
    unsigned long long part = 0;
    if (!ParseUnsigned(text, index, part)) {
        return std::nullopt;
    }
    parsed.parts.push_back(part);

    while (index < text.size()) {
        std::size_t separator = index;
        while (separator < text.size() && std::iswspace(text[separator])) {
            ++separator;
        }
        if (separator >= text.size() || !IsVersionSeparator(text[separator])) {
            break;
        }
        ++separator;
        while (separator < text.size() && std::iswspace(text[separator])) {
            ++separator;
        }

        std::size_t next = separator;
        if (!ParseUnsigned(text, next, part)) {
            break;
        }
        parsed.parts.push_back(part);
        index = next;
    }

    if (requireMultipleParts && parsed.parts.size() < 2) {
        return std::nullopt;
    }

    if (index < text.size() && text[index] == L'-') {
        const std::size_t suffixStart = index;
        ++index;
        while (index < text.size()) {
            const wchar_t c = text[index];
            if (!std::iswalnum(c) && c != L'.' && c != L'-') {
                break;
            }
            ++index;
        }
        if (index > suffixStart + 1) {
            parsed.suffix = ToLower(text.substr(suffixStart + 1, index - suffixStart - 1));
        }
    }

    end = index;
    return parsed;
}

std::wstring ParsedToString(const ParsedVersion& version) {
    std::wstring result;
    for (std::size_t i = 0; i < version.parts.size(); ++i) {
        if (i > 0) {
            result.push_back(L'.');
        }
        result += std::to_wstring(version.parts[i]);
    }
    if (!version.suffix.empty()) {
        result += L"-" + version.suffix;
    }
    return result;
}

std::optional<ParsedVersion> ParseComparable(const std::wstring& value) {
    const std::wstring normalized = NormalizeVersionString(value);
    if (normalized.empty()) {
        return std::nullopt;
    }

    std::size_t end = 0;
    const auto parsed = ParseAt(normalized, 0, end, false);
    if (!parsed || end != normalized.size()) {
        return std::nullopt;
    }
    return parsed;
}

int CompareSuffixNaturally(const std::wstring& left, const std::wstring& right) {
    std::size_t leftIndex = 0;
    std::size_t rightIndex = 0;
    while (leftIndex < left.size() && rightIndex < right.size()) {
        const bool leftIsDigit = std::iswdigit(left[leftIndex]) != 0;
        const bool rightIsDigit = std::iswdigit(right[rightIndex]) != 0;
        if (leftIsDigit != rightIsDigit) {
            return leftIsDigit ? -1 : 1;
        }

        std::size_t leftEnd = leftIndex;
        while (leftEnd < left.size() && (std::iswdigit(left[leftEnd]) != 0) == leftIsDigit) {
            ++leftEnd;
        }
        std::size_t rightEnd = rightIndex;
        while (rightEnd < right.size() && (std::iswdigit(right[rightEnd]) != 0) == rightIsDigit) {
            ++rightEnd;
        }

        if (leftIsDigit) {
            while (leftIndex + 1 < leftEnd && left[leftIndex] == L'0') {
                ++leftIndex;
            }
            while (rightIndex + 1 < rightEnd && right[rightIndex] == L'0') {
                ++rightIndex;
            }
            const std::size_t leftLength = leftEnd - leftIndex;
            const std::size_t rightLength = rightEnd - rightIndex;
            if (leftLength != rightLength) {
                return leftLength < rightLength ? -1 : 1;
            }
        }

        const std::wstring leftPart = left.substr(leftIndex, leftEnd - leftIndex);
        const std::wstring rightPart = right.substr(rightIndex, rightEnd - rightIndex);
        const int comparison = leftPart.compare(rightPart);
        if (comparison != 0) {
            return comparison < 0 ? -1 : 1;
        }
        leftIndex = leftEnd;
        rightIndex = rightEnd;
    }
    return leftIndex == left.size() && rightIndex == right.size()
        ? 0
        : (leftIndex == left.size() ? -1 : 1);
}

} // namespace

std::wstring ExtractVersionFromText(const std::wstring& value) {
    std::wstring text = value;
    const std::size_t nullPosition = text.find(L'\0');
    if (nullPosition != std::wstring::npos) {
        text.resize(nullPosition);
    }

    for (std::size_t start = 0; start < text.size(); ++start) {
        const bool startsWithDigit = std::iswdigit(text[start]) != 0;
        const bool startsWithV = (text[start] == L'v' || text[start] == L'V') &&
            start + 1 < text.size() && std::iswdigit(text[start + 1]);
        if (!startsWithDigit && !startsWithV) {
            continue;
        }
        if (start > 0 && std::iswalnum(text[start - 1])) {
            continue;
        }

        std::size_t end = start;
        const auto parsed = ParseAt(text, start, end, true);
        if (parsed) {
            return ParsedToString(*parsed);
        }
    }
    return {};
}

std::wstring NormalizeVersionString(const std::wstring& value) {
    std::wstring text = value;
    const std::size_t nullPosition = text.find(L'\0');
    if (nullPosition != std::wstring::npos) {
        text.resize(nullPosition);
    }
    text = Trim(text);
    if (text.empty()) {
        return {};
    }

    const std::wstring lowered = ToLower(text);
    const std::wstring prefixes[] = { L"version", L"ver." };
    for (const auto& prefix : prefixes) {
        if (lowered.rfind(prefix, 0) == 0) {
            text = Trim(text.substr(prefix.size()));
            break;
        }
    }

    std::size_t end = 0;
    if (const auto parsed = ParseAt(text, 0, end, false)) {
        const std::wstring remainder = Trim(text.substr(end));
        if (remainder.empty() || remainder.front() == L'(' || remainder.front() == L'[') {
            return ParsedToString(*parsed);
        }
    }

    const std::wstring extracted = ExtractVersionFromText(text);
    return extracted.empty() ? text : extracted;
}

std::optional<int> CompareVersionStrings(const std::wstring& left,
                                         const std::wstring& right) {
    const auto parsedLeft = ParseComparable(left);
    const auto parsedRight = ParseComparable(right);
    if (!parsedLeft || !parsedRight) {
        return std::nullopt;
    }

    const std::size_t partCount = std::max(parsedLeft->parts.size(), parsedRight->parts.size());
    for (std::size_t i = 0; i < partCount; ++i) {
        const auto leftPart = i < parsedLeft->parts.size() ? parsedLeft->parts[i] : 0;
        const auto rightPart = i < parsedRight->parts.size() ? parsedRight->parts[i] : 0;
        if (leftPart < rightPart) {
            return -1;
        }
        if (leftPart > rightPart) {
            return 1;
        }
    }

    if (parsedLeft->suffix.empty() != parsedRight->suffix.empty()) {
        return parsedLeft->suffix.empty() ? 1 : -1;
    }
    return CompareSuffixNaturally(parsedLeft->suffix, parsedRight->suffix);
}
