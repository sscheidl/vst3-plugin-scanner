#include "DuplicateDetector.h"

#include "StringUtil.h"

#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace {

std::wstring BuildDuplicateKey(const PluginRecord& record) {
    const std::wstring manufacturer = NormalizePluginKey(record.manufacturer);
    const std::wstring pluginName = NormalizePluginKey(record.pluginName);
    const std::wstring fileStem = NormalizePluginKey(std::filesystem::path(record.fileName).stem().wstring());

    if (!manufacturer.empty() && !pluginName.empty()) {
        return manufacturer + L"|" + pluginName;
    }
    if (!pluginName.empty()) {
        return L"name|" + pluginName;
    }
    if (!fileStem.empty()) {
        return L"file|" + fileStem;
    }
    return {};
}

} // namespace

void DuplicateDetector::MarkDuplicates(std::vector<PluginRecord>& records) const {
    for (auto& record : records) {
        record.isPossibleDuplicate = false;
        record.duplicateGroupId = 0;
    }

    std::map<std::wstring, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const std::wstring key = BuildDuplicateKey(records[i]);
        if (!key.empty()) {
            groups[key].push_back(i);
        }
    }

    int nextGroupId = 1;
    for (const auto& [key, indexes] : groups) {
        std::set<PluginType> types;
        for (const std::size_t index : indexes) {
            types.insert(records[index].pluginType);
        }

        if (indexes.size() < 2 || types.size() < 2) {
            continue;
        }

        for (const std::size_t index : indexes) {
            records[index].isPossibleDuplicate = true;
            records[index].duplicateGroupId = nextGroupId;
        }
        ++nextGroupId;
    }
}
