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
        // Height above the local floor; legacy XY markers remain at ground level.
        float z = 0.0f;
        float angleDegrees = 0.0f;
        float pitch = 0.0f;
    };

    SequenceMarker MarkerFromJson(const nlohmann::json& value);
    nlohmann::json MarkerToJson(const SequenceMarker& marker);
    std::string UniqueMarkerId(const std::vector<SequenceMarker>& markers, const std::string& base);
    void RenameMarkerReferences(nlohmann::json& document, const std::string& oldId, const std::string& newId);
    void RenameMarkerReferences(SequenceDefinition& definition, const std::string& oldId, const std::string& newId);

    bool LoadMarkersForMap(const std::filesystem::path& mapPath, std::vector<SequenceMarker>& out, std::string* outError = nullptr);
    bool SaveMarkersForMap(const std::filesystem::path& mapPath, const std::vector<SequenceMarker>& markers, std::string* outError = nullptr);
    std::filesystem::path MarkerPathForMap(const std::filesystem::path& mapPath);
    const SequenceMarker* FindMarker(const std::vector<SequenceMarker>& markers, const std::string& id);
}
