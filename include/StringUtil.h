#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::wstring Trim(const std::wstring& value);
std::wstring ToLower(std::wstring value);
std::wstring NormalizePluginKey(const std::wstring& value);
std::wstring HtmlEscape(const std::wstring& value);
std::wstring CsvEscape(const std::wstring& value);
std::wstring FormatFileTime(const std::filesystem::file_time_type& value);
std::wstring FormatFileSize(std::uintmax_t bytes);
std::wstring CurrentTimestamp();
std::wstring JoinPathList(const std::vector<std::wstring>& paths);
