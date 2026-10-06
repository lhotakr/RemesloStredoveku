#pragma once

#include "SequenceRuntime.h"

#include <filesystem>
#include <string>
#include <vector>

namespace sequence
{
    struct SequenceMarker
    {
        std::string id;
        float x = 0.0f;
        float y = 0.0f;
        std::string type = "sequence";
    };

    bool LoadMarkersForMap(const std::filesystem::path& mapPath, std::vector<SequenceMarker>& out, std::string* outError = nullptr);
    bool SaveMarkersForMap(const std::filesystem::path& mapPath, const std::vector<SequenceMarker>& markers, std::string* outError = nullptr);
    std::filesystem::path MarkerPathForMap(const std::filesystem::path& mapPath);
    const SequenceMarker* FindMarker(const std::vector<SequenceMarker>& markers, const std::string& id);
}
