#pragma once

#include "PluginRecord.h"

#include <filesystem>
#include <string>
#include <vector>

enum class ReportFormat {
    Html,
    Csv,
    Txt
};

class ReportWriter {
public:
    bool Write(const std::filesystem::path& outputPath,
               ReportFormat format,
               const std::vector<PluginRecord>& records,
               const ScanSummary& summary,
               std::wstring& errorMessage) const;

private:
    bool WriteHtml(const std::filesystem::path& outputPath,
                   const std::vector<PluginRecord>& records,
                   const ScanSummary& summary,
                   std::wstring& errorMessage) const;
    bool WriteCsv(const std::filesystem::path& outputPath,
                  const std::vector<PluginRecord>& records,
                  const ScanSummary& summary,
                  std::wstring& errorMessage) const;
    bool WriteTxt(const std::filesystem::path& outputPath,
                  const std::vector<PluginRecord>& records,
                  const ScanSummary& summary,
                  std::wstring& errorMessage) const;
};

std::wstring DefaultExtensionForFormat(ReportFormat format);

