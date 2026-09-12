#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vst3scanner {

struct InventoryRecord {
    std::string cid;
    std::string name;
    std::string vendor;
    std::string version;
    bool versionMissing = true;
    std::string sdkVersion;
    std::vector<std::string> subCategories;
    std::string modulePath;
    std::string protocolStatus;
    std::string diagnostic;
    std::uint64_t probeDurationMs = 0;
    std::size_t duplicateCount = 1;
    bool duplicate = false;
    bool fromCache = false;
};

struct ScanIssue {
    std::string modulePath;
    std::string status;
    std::string diagnostic;
    bool retried = false;
    bool timedOut = false;
};

struct ParsedProbeResult {
    bool valid = false;
    std::string error;
    std::string protocolStatus;
    std::string modulePath;
    std::string factoryVendor;
    std::uint64_t probeDurationMs = 0;
    std::vector<InventoryRecord> audioPlugins;
    std::string diagnostic;
};

struct ProbeCacheEntry {
    std::string key;
    std::string probeJson;
};

struct ParsedProbeCache {
    bool valid = false;
    std::string error;
    std::vector<ProbeCacheEntry> entries;
};

[[nodiscard]] ParsedProbeResult ParseProbeResultJson(std::string_view json);
[[nodiscard]] ParsedProbeCache ParseProbeCacheJson(std::string_view json);
[[nodiscard]] std::string SerializeProbeCacheJson(
    const std::vector<ProbeCacheEntry>& entries);
void MarkCidDuplicates(std::vector<InventoryRecord>& records);
[[nodiscard]] std::string SerializeInventoryCsv(
    const std::vector<InventoryRecord>& records,
    const std::vector<ScanIssue>& issues);
[[nodiscard]] std::string SerializeInventoryJson(
    const std::vector<InventoryRecord>& records,
    const std::vector<ScanIssue>& issues);

}  // namespace vst3scanner
