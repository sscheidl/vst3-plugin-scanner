#pragma once

#include "PluginRecord.h"

#include <vector>

class DuplicateDetector {
public:
    void MarkDuplicates(std::vector<PluginRecord>& records) const;
};

