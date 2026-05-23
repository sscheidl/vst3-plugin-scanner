#include "ReportWriter.h"

#include "StringUtil.h"

#include <fstream>
#include <sstream>

namespace {

bool WriteUtf8File(const std::filesystem::path& path, const std::wstring& content, bool withBom, std::wstring& errorMessage) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        errorMessage = L"Ausgabedatei konnte nicht geoeffnet werden: " + path.wstring();
        return false;
    }
    if (withBom) {
        const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        stream.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    }
    const std::string bytes = WideToUtf8(content);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return stream.good();
}

std::wstring DuplicateText(const PluginRecord& record) {
    if (!record.isPossibleDuplicate) {
        return L"Nein";
    }
    return L"Ja (Gruppe " + std::to_wstring(record.duplicateGroupId) + L")";
}

} // namespace

std::wstring DefaultExtensionForFormat(ReportFormat format) {
    switch (format) {
    case ReportFormat::Csv:
        return L".csv";
    case ReportFormat::Txt:
        return L".txt";
    case ReportFormat::Html:
    default:
        return L".html";
    }
}

bool ReportWriter::Write(const std::filesystem::path& outputPath,
                         ReportFormat format,
                         const std::vector<PluginRecord>& records,
                         const ScanSummary& summary,
                         std::wstring& errorMessage) const {
    switch (format) {
    case ReportFormat::Csv:
        return WriteCsv(outputPath, records, summary, errorMessage);
    case ReportFormat::Txt:
        return WriteTxt(outputPath, records, summary, errorMessage);
    case ReportFormat::Html:
    default:
        return WriteHtml(outputPath, records, summary, errorMessage);
    }
}

bool ReportWriter::WriteHtml(const std::filesystem::path& outputPath,
                             const std::vector<PluginRecord>& records,
                             const ScanSummary& summary,
                             std::wstring& errorMessage) const {
    std::wstringstream html;
    html << L"<!doctype html>\n<html lang=\"de\">\n<head>\n<meta charset=\"utf-8\">\n";
    html << L"<title>VST Plugin Scan Report</title>\n";
    html << L"<style>\n";
    html << L"body{font-family:Segoe UI,Arial,sans-serif;margin:24px;background:#f6f7f9;color:#1f2328;}";
    html << L"h1{font-size:24px;margin:0 0 16px;} .summary{background:#fff;border:1px solid #d8dee4;padding:16px;margin-bottom:18px;}";
    html << L"table{border-collapse:collapse;width:100%;background:#fff;border:1px solid #d8dee4;} th,td{border-bottom:1px solid #d8dee4;padding:8px;text-align:left;vertical-align:top;font-size:13px;}";
    html << L"th{background:#eef2f6;position:sticky;top:0;} tr.dup{background:#fff4ce;} .warn{color:#9a6700;} .err{color:#b42318;} code{font-family:Consolas,monospace;font-size:12px;}";
    html << L"</style>\n</head>\n<body>\n";
    html << L"<h1>VST Plugin Scan Report</h1>\n";
    html << L"<div class=\"summary\"><strong>Zusammenfassung</strong><br>";
    html << L"Scanzeitpunkt: " << HtmlEscape(summary.scanTimestamp) << L"<br>";
    html << L"Anzahl VST2: " << summary.vst2Count << L"<br>";
    html << L"Anzahl VST3: " << summary.vst3Count << L"<br>";
    html << L"Anzahl CLAP: " << summary.clapCount << L"<br>";
    html << L"Anzahl AAX: " << summary.aaxCount << L"<br>";
    html << L"Dubletten-Gruppen: " << summary.duplicateCount << L"<br>";
    html << L"Dubletten-Eintraege: " << summary.duplicateEntryCount << L"<br>";
    html << L"VST2-Dubletten loeschbar: " << summary.vst2DuplicateCandidateCount << L"<br>";
    html << L"Fehler/Warnungen: " << summary.warningCount << L"<br>";
    html << L"Gescannte Pfade: " << HtmlEscape(JoinPathList(summary.scannedPaths)) << L"</div>\n";

    html << L"<table><thead><tr>";
    html << L"<th>Hersteller</th><th>Pluginname</th><th>Kategorie</th><th>Version</th><th>Typ</th><th>Pfad</th>";
    html << L"<th>Dateigroesse</th><th>Aenderungsdatum</th><th>Dublette</th><th>Status</th>";
    html << L"</tr></thead><tbody>\n";

    for (const auto& record : records) {
        html << L"<tr" << (record.isPossibleDuplicate ? L" class=\"dup\"" : L"") << L">";
        html << L"<td>" << HtmlEscape(record.manufacturer) << L"</td>";
        html << L"<td>" << HtmlEscape(record.pluginName) << L"</td>";
        html << L"<td>" << HtmlEscape(record.category) << L"</td>";
        html << L"<td>" << HtmlEscape(record.version) << L"</td>";
        html << L"<td>" << ToDisplayText(record.pluginType) << L"</td>";
        html << L"<td><code>" << HtmlEscape(record.filePath) << L"</code></td>";
        html << L"<td>" << record.fileSize << L"</td>";
        html << L"<td>" << HtmlEscape(record.modifiedDate) << L"</td>";
        html << L"<td>" << HtmlEscape(DuplicateText(record)) << L"</td>";
        html << L"<td>" << HtmlEscape(ToDisplayText(record.status));
        if (!record.warningMessage.empty()) {
            html << L"<br><span class=\"warn\">" << HtmlEscape(record.warningMessage) << L"</span>";
        }
        html << L"</td></tr>\n";
    }

    html << L"</tbody></table>\n</body>\n</html>\n";
    return WriteUtf8File(outputPath, html.str(), true, errorMessage);
}

bool ReportWriter::WriteCsv(const std::filesystem::path& outputPath,
                            const std::vector<PluginRecord>& records,
                            const ScanSummary&,
                            std::wstring& errorMessage) const {
    std::wstringstream csv;
    csv << L"Hersteller;Pluginname;Kategorie;Version;Typ;Pfad;Dateiname;Dateigroesse;Aenderungsdatum;Dublette;Status;Warnung\n";
    for (const auto& record : records) {
        csv << CsvEscape(record.manufacturer) << L";"
            << CsvEscape(record.pluginName) << L";"
            << CsvEscape(record.category) << L";"
            << CsvEscape(record.version) << L";"
            << ToDisplayText(record.pluginType) << L";"
            << CsvEscape(record.filePath) << L";"
            << CsvEscape(record.fileName) << L";"
            << record.fileSize << L";"
            << CsvEscape(record.modifiedDate) << L";"
            << CsvEscape(DuplicateText(record)) << L";"
            << CsvEscape(ToDisplayText(record.status)) << L";"
            << CsvEscape(record.warningMessage) << L"\n";
    }
    return WriteUtf8File(outputPath, csv.str(), true, errorMessage);
}

bool ReportWriter::WriteTxt(const std::filesystem::path& outputPath,
                            const std::vector<PluginRecord>& records,
                            const ScanSummary& summary,
                            std::wstring& errorMessage) const {
    std::wstringstream text;
    text << L"VST Plugin Scan Report\n";
    text << L"======================\n\n";
    text << L"Scanzeitpunkt: " << summary.scanTimestamp << L"\n";
    text << L"Anzahl VST2: " << summary.vst2Count << L"\n";
    text << L"Anzahl VST3: " << summary.vst3Count << L"\n";
    text << L"Anzahl CLAP: " << summary.clapCount << L"\n";
    text << L"Anzahl AAX: " << summary.aaxCount << L"\n";
    text << L"Dubletten-Gruppen: " << summary.duplicateCount << L"\n";
    text << L"Dubletten-Eintraege: " << summary.duplicateEntryCount << L"\n";
    text << L"VST2-Dubletten loeschbar: " << summary.vst2DuplicateCandidateCount << L"\n";
    text << L"Fehler/Warnungen: " << summary.warningCount << L"\n";
    text << L"Gescannte Pfade: " << JoinPathList(summary.scannedPaths) << L"\n\n";

    for (const auto& record : records) {
        text << L"- " << record.pluginName << L" [" << ToDisplayText(record.pluginType) << L"]\n";
        text << L"  Hersteller: " << record.manufacturer << L"\n";
        text << L"  Kategorie: " << record.category << L"\n";
        text << L"  Version: " << record.version << L"\n";
        text << L"  Pfad: " << record.filePath << L"\n";
        text << L"  Dateiname: " << record.fileName << L"\n";
        text << L"  Dateigroesse: " << record.fileSize << L"\n";
        text << L"  Aenderungsdatum: " << record.modifiedDate << L"\n";
        text << L"  Dublette: " << DuplicateText(record) << L"\n";
        text << L"  Status: " << ToDisplayText(record.status) << L"\n";
        if (!record.warningMessage.empty()) {
            text << L"  Warnung: " << record.warningMessage << L"\n";
        }
        text << L"\n";
    }

    return WriteUtf8File(outputPath, text.str(), true, errorMessage);
}
