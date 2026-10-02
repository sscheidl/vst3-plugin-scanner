#include "ReportWriter.h"

#include "StringUtil.h"

#include <fstream>
#include <set>
#include <sstream>

namespace {

bool WriteUtf8File(const std::filesystem::path& path, const std::wstring& content, bool withBom, std::wstring& errorMessage) {
    // WideToUtf8 returns an empty string when the input contains unpaired
    // surrogates, which can come from broken file names. Detect that before the
    // file is touched, so a failed conversion can never produce an empty report
    // that still reports success.
    const std::string bytes = WideToUtf8(content);
    if (bytes.empty() && !content.empty()) {
        errorMessage = L"Report contains characters that cannot be encoded as UTF-8: " + path.wstring();
        return false;
    }

    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        errorMessage = L"Output file could not be opened: " + path.wstring();
        return false;
    }
    if (withBom) {
        const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        stream.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    }
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    stream.close();
    if (!stream.good()) {
        errorMessage = L"Output file could not be written completely: " + path.wstring();
        return false;
    }
    return true;
}

std::wstring DuplicateText(const PluginRecord& record) {
    if (!record.isPossibleDuplicate) {
        return L"No";
    }
    return L"Yes (group " + std::to_wstring(record.duplicateGroupId) + L")";
}

std::wstring DateSortValue(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (wchar_t c : value) {
        if (c >= L'0' && c <= L'9') {
            result.push_back(c);
        }
    }
    return result;
}

void WriteSortableHeader(std::wstringstream& html,
                         const wchar_t* title,
                         int column,
                         const wchar_t* sortType) {
    html << L"<th data-column=\"" << column << L"\" data-sort-type=\"" << sortType << L"\">";
    html << L"<button type=\"button\" class=\"sort-button\">" << title << L"<span class=\"sort-indicator\" aria-hidden=\"true\"></span></button>";
    html << L"</th>";
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
    html << L"<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
    html << L"<title>VST Plugin Scan Report</title>\n";
    html << L"<style>\n";
    html << L"body{font-family:Segoe UI,Arial,sans-serif;margin:24px;background:#f6f7f9;color:#1f2328;}";
    html << L"h1{font-size:24px;margin:0 0 16px;}.summary{background:#fff;border:1px solid #d8dee4;padding:16px;margin-bottom:14px;line-height:1.45;}";
    html << L".controls{display:flex;flex-wrap:wrap;gap:12px;align-items:end;background:#fff;border:1px solid #d8dee4;padding:12px;margin-bottom:14px;}";
    html << L".control{display:flex;flex-direction:column;gap:4px;font-size:12px;color:#57606a;}input,select{font:inherit;font-size:13px;padding:6px 8px;border:1px solid #d0d7de;background:#fff;color:#1f2328;}";
    html << L"#searchBox{min-width:260px;}.result-count{font-size:13px;color:#57606a;margin-left:auto;padding-bottom:7px;}";
    html << L"table{border-collapse:collapse;width:100%;background:#fff;border:1px solid #d8dee4;}th,td{border-bottom:1px solid #d8dee4;padding:8px;text-align:left;vertical-align:top;font-size:13px;}";
    html << L"th{background:#eef2f6;position:sticky;top:0;z-index:1;}tbody tr:nth-child(even){background:#fbfcfd;}tbody tr.dup{background:#fff7d6;}tbody tr.dup:nth-child(even){background:#fff2bd;}";
    html << L".sort-button{all:unset;box-sizing:border-box;display:flex;align-items:center;gap:5px;width:100%;cursor:pointer;font-weight:600;}.sort-button:focus{outline:2px solid #0969da;outline-offset:2px;}";
    html << L".sort-indicator{display:inline-block;min-width:1em;color:#57606a;}.warn{color:#9a6700;}code{font-family:Consolas,monospace;font-size:12px;white-space:pre-wrap;overflow-wrap:anywhere;}";
    html << L"</style>\n</head>\n<body>\n";
    html << L"<h1>VST Plugin Scan Report</h1>\n";
    html << L"<div class=\"summary\"><strong>Summary</strong><br>";
    html << L"Scan time: " << HtmlEscape(summary.scanTimestamp) << L"<br>";
    html << L"VST2 count: " << summary.vst2Count << L"<br>";
    html << L"VST3 count: " << summary.vst3Count << L"<br>";
    html << L"CLAP count: " << summary.clapCount << L"<br>";
    html << L"AAX count: " << summary.aaxCount << L"<br>";
    html << L"Duplicate groups: " << summary.duplicateCount << L"<br>";
    html << L"Duplicate entries: " << summary.duplicateEntryCount << L"<br>";
    html << L"Deletable VST2 duplicates: " << summary.vst2DuplicateCandidateCount << L"<br>";
    html << L"Version detected reliably: " << summary.versionDetectedCount << L"<br>";
    html << L"Version guessed from file name: " << summary.versionHeuristicCount << L"<br>";
    html << L"Version missing: " << summary.versionMissingCount << L"<br>";
    html << L"Errors/warnings: " << summary.warningCount << L"<br>";
    html << L"Scanned paths: " << HtmlEscape(JoinPathList(summary.scannedPaths)) << L"</div>\n";

    std::set<std::wstring> categories;
    for (const auto& record : records) {
        const std::wstring category = Trim(record.category);
        if (!category.empty()) {
            categories.insert(category);
        }
    }

    html << L"<div class=\"controls\">";
    html << L"<label class=\"control\">Search<input id=\"searchBox\" type=\"search\" placeholder=\"Search all visible columns\"></label>";
    html << L"<label class=\"control\">Type<select id=\"typeFilter\"><option value=\"\">All</option><option value=\"VST2\">VST2</option><option value=\"VST3\">VST3</option><option value=\"CLAP\">CLAP</option><option value=\"AAX\">AAX</option></select></label>";
    html << L"<label class=\"control\">Category<select id=\"categoryFilter\"><option value=\"\">All</option>";
    for (const auto& category : categories) {
        html << L"<option value=\"" << HtmlEscape(category) << L"\">" << HtmlEscape(category) << L"</option>";
    }
    html << L"</select></label>";
    html << L"<label class=\"control\">Duplicates<select id=\"duplicateFilter\"><option value=\"\">All</option><option value=\"1\">Duplicates only</option><option value=\"0\">No duplicates</option></select></label>";
    html << L"<span id=\"resultCount\" class=\"result-count\"></span></div>\n";

    html << L"<table id=\"pluginTable\"><thead><tr>";
    WriteSortableHeader(html, L"Manufacturer", 0, L"text");
    WriteSortableHeader(html, L"Plug-in name", 1, L"text");
    WriteSortableHeader(html, L"Category", 2, L"text");
    WriteSortableHeader(html, L"Version", 3, L"text");
    WriteSortableHeader(html, L"Version source", 4, L"text");
    WriteSortableHeader(html, L"Type", 5, L"text");
    WriteSortableHeader(html, L"Path", 6, L"text");
    WriteSortableHeader(html, L"File size", 7, L"number");
    WriteSortableHeader(html, L"Modified", 8, L"number");
    WriteSortableHeader(html, L"Duplicate", 9, L"number");
    WriteSortableHeader(html, L"Status", 10, L"text");
    html << L"</tr></thead><tbody>\n";

    for (const auto& record : records) {
        html << L"<tr"
             << (record.isPossibleDuplicate ? L" class=\"dup\"" : L"")
             << L" data-type=\"" << ToDisplayText(record.pluginType) << L"\""
             << L" data-category=\"" << HtmlEscape(record.category) << L"\""
             << L" data-duplicate=\"" << (record.isPossibleDuplicate ? L"1" : L"0") << L"\">";
        html << L"<td>" << HtmlEscape(record.manufacturer) << L"</td>";
        html << L"<td>" << HtmlEscape(record.pluginName) << L"</td>";
        html << L"<td>" << HtmlEscape(record.category) << L"</td>";
        html << L"<td>" << HtmlEscape(record.version) << L"</td>";
        html << L"<td>" << HtmlEscape(ToDisplayText(record.versionSource)) << L"</td>";
        html << L"<td>" << ToDisplayText(record.pluginType) << L"</td>";
        html << L"<td><code>" << HtmlEscape(record.filePath) << L"</code></td>";
        html << L"<td data-sort=\"" << record.fileSize << L"\">" << HtmlEscape(FormatFileSize(record.fileSize)) << L"</td>";
        html << L"<td data-sort=\"" << HtmlEscape(DateSortValue(record.modifiedDate)) << L"\">" << HtmlEscape(record.modifiedDate) << L"</td>";
        html << L"<td data-sort=\"" << (record.isPossibleDuplicate ? L"1" : L"0") << L"\">" << HtmlEscape(DuplicateText(record)) << L"</td>";
        html << L"<td>" << HtmlEscape(ToDisplayText(record));
        if (!record.warningMessage.empty()) {
            html << L"<br><span class=\"warn\">" << HtmlEscape(record.warningMessage) << L"</span>";
        }
        html << L"</td></tr>\n";
    }

    html << L"</tbody></table>\n";
    html << L"<script>\n";
    html << L"(function(){\n";
    html << L"var table=document.getElementById('pluginTable');\n";
    html << L"var tbody=table.tBodies[0];\n";
    html << L"var rows=Array.prototype.slice.call(tbody.rows);\n";
    html << L"var search=document.getElementById('searchBox');\n";
    html << L"var typeFilter=document.getElementById('typeFilter');\n";
    html << L"var categoryFilter=document.getElementById('categoryFilter');\n";
    html << L"var duplicateFilter=document.getElementById('duplicateFilter');\n";
    html << L"var resultCount=document.getElementById('resultCount');\n";
    html << L"var sortColumn=-1;\n";
    html << L"var sortDirection=1;\n";
    html << L"function text(row){return row.textContent.toLowerCase();}\n";
    html << L"function value(row,column,type){var cell=row.cells[column];var raw=cell.getAttribute('data-sort')||cell.textContent;if(type==='number'){var n=Number(raw);return isNaN(n)?0:n;}return raw.toLowerCase();}\n";
    html << L"function compare(a,b,column,type){var av=value(a,column,type);var bv=value(b,column,type);if(type==='number'){return (av-bv)*sortDirection;}return av.localeCompare(bv,undefined,{numeric:true,sensitivity:'base'})*sortDirection;}\n";
    html << L"function updateIndicators(active){Array.prototype.forEach.call(table.tHead.querySelectorAll('.sort-indicator'),function(el){el.textContent='';});if(active){active.querySelector('.sort-indicator').innerHTML=sortDirection===1?'&uarr;':'&darr;';}}\n";
    html << L"function applyFilters(){var query=search.value.trim().toLowerCase();var type=typeFilter.value;var category=categoryFilter.value;var duplicate=duplicateFilter.value;var visible=0;rows.forEach(function(row){var ok=(!query||text(row).indexOf(query)!==-1)&&(!type||row.getAttribute('data-type')===type)&&(!category||row.getAttribute('data-category')===category)&&(!duplicate||row.getAttribute('data-duplicate')===duplicate);row.style.display=ok?'':'none';if(ok){visible++;}});resultCount.textContent=visible+' of '+rows.length+' entries';}\n";
    html << L"Array.prototype.forEach.call(table.tHead.querySelectorAll('th[data-column]'),function(header){header.addEventListener('click',function(){var column=Number(header.getAttribute('data-column'));var type=header.getAttribute('data-sort-type');if(sortColumn===column){sortDirection*=-1;}else{sortColumn=column;sortDirection=1;}rows.sort(function(a,b){return compare(a,b,column,type);});rows.forEach(function(row){tbody.appendChild(row);});updateIndicators(header);applyFilters();});});\n";
    html << L"[search,typeFilter,categoryFilter,duplicateFilter].forEach(function(control){control.addEventListener('input',applyFilters);control.addEventListener('change',applyFilters);});\n";
    html << L"applyFilters();\n";
    html << L"}());\n";
    html << L"</script>\n</body>\n</html>\n";
    return WriteUtf8File(outputPath, html.str(), true, errorMessage);
}

bool ReportWriter::WriteCsv(const std::filesystem::path& outputPath,
                            const std::vector<PluginRecord>& records,
                            const ScanSummary&,
                            std::wstring& errorMessage) const {
    std::wstringstream csv;
    csv << L"Manufacturer;PluginName;Category;Version;VersionSource;Type;Path;FileName;FileSize;Modified;Duplicate;Status;Warning\n";
    for (const auto& record : records) {
        csv << CsvEscape(record.manufacturer) << L";"
            << CsvEscape(record.pluginName) << L";"
            << CsvEscape(record.category) << L";"
            << CsvEscape(record.version) << L";"
            << CsvEscape(ToDisplayText(record.versionSource)) << L";"
            << ToDisplayText(record.pluginType) << L";"
            << CsvEscape(record.filePath) << L";"
            << CsvEscape(record.fileName) << L";"
            << record.fileSize << L";"
            << CsvEscape(record.modifiedDate) << L";"
            << CsvEscape(DuplicateText(record)) << L";"
            << CsvEscape(ToDisplayText(record)) << L";"
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
    text << L"Scan time: " << summary.scanTimestamp << L"\n";
    text << L"VST2 count: " << summary.vst2Count << L"\n";
    text << L"VST3 count: " << summary.vst3Count << L"\n";
    text << L"CLAP count: " << summary.clapCount << L"\n";
    text << L"AAX count: " << summary.aaxCount << L"\n";
    text << L"Duplicate groups: " << summary.duplicateCount << L"\n";
    text << L"Duplicate entries: " << summary.duplicateEntryCount << L"\n";
    text << L"Deletable VST2 duplicates: " << summary.vst2DuplicateCandidateCount << L"\n";
    text << L"Version detected reliably: " << summary.versionDetectedCount << L"\n";
    text << L"Version guessed from file name: " << summary.versionHeuristicCount << L"\n";
    text << L"Version missing: " << summary.versionMissingCount << L"\n";
    text << L"Errors/warnings: " << summary.warningCount << L"\n";
    text << L"Scanned paths: " << JoinPathList(summary.scannedPaths) << L"\n\n";

    for (const auto& record : records) {
        text << L"- " << record.pluginName << L" [" << ToDisplayText(record.pluginType) << L"]\n";
        text << L"  Manufacturer: " << record.manufacturer << L"\n";
        text << L"  Category: " << record.category << L"\n";
        text << L"  Version: " << record.version << L"\n";
        text << L"  Version source: " << ToDisplayText(record.versionSource) << L"\n";
        text << L"  Path: " << record.filePath << L"\n";
        text << L"  File name: " << record.fileName << L"\n";
        text << L"  File size: " << FormatFileSize(record.fileSize) << L"\n";
        text << L"  Modified: " << record.modifiedDate << L"\n";
        text << L"  Duplicate: " << DuplicateText(record) << L"\n";
        text << L"  Status: " << ToDisplayText(record) << L"\n";
        if (!record.warningMessage.empty()) {
            text << L"  Warning: " << record.warningMessage << L"\n";
        }
        text << L"\n";
    }

    return WriteUtf8File(outputPath, text.str(), true, errorMessage);
}
