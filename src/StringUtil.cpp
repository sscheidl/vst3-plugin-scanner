#include "StringUtil.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cwctype>
#include <iomanip>
#include <sstream>

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
    return result;
}

std::wstring Trim(const std::wstring& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t c) {
        return std::iswspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t c) {
        return std::iswspace(c) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return std::wstring(first, last);
}

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

std::wstring NormalizePluginKey(const std::wstring& value) {
    std::wstring lowered = ToLower(value);
    std::wstring result;
    result.reserve(lowered.size());
    for (wchar_t c : lowered) {
        if (std::iswalnum(c)) {
            result.push_back(c);
        }
    }
    return result;
}

std::wstring HtmlEscape(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (wchar_t c : value) {
        switch (c) {
        case L'&':
            result += L"&amp;";
            break;
        case L'<':
            result += L"&lt;";
            break;
        case L'>':
            result += L"&gt;";
            break;
        case L'"':
            result += L"&quot;";
            break;
        case L'\'':
            result += L"&#39;";
            break;
        default:
            result.push_back(c);
            break;
        }
    }
    return result;
}

std::wstring CsvEscape(const std::wstring& value) {
    bool needsQuotes = false;
    for (wchar_t c : value) {
        if (c == L';' || c == L',' || c == L'"' || c == L'\r' || c == L'\n') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return value;
    }

    std::wstring result = L"\"";
    for (wchar_t c : value) {
        if (c == L'"') {
            result += L"\"\"";
        } else {
            result.push_back(c);
        }
    }
    result += L"\"";
    return result;
}

std::wstring FormatFileTime(const std::filesystem::file_time_type& value) {
    const auto systemTimePoint = std::chrono::clock_cast<std::chrono::system_clock>(value);
    const std::time_t time = std::chrono::system_clock::to_time_t(systemTimePoint);
    std::tm localTime{};
    localtime_s(&localTime, &time);

    std::wstringstream stream;
    stream << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::wstring CurrentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &time);

    std::wstringstream stream;
    stream << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::wstring JoinPathList(const std::vector<std::wstring>& paths) {
    std::wstring result;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) {
            result += L", ";
        }
        result += paths[i];
    }
    return result;
}
