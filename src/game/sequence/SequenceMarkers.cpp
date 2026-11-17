#include "SequenceMarkers.h"

#include "JsonUtils.h"

#include <fstream>
#include <cmath>
#include <nlohmann/json.hpp>

namespace sequence
{
    SequenceMarker MarkerFromJson(const nlohmann::json& value)
    {
        SequenceMarker marker;
        if (!value.is_object()) return marker;
        marker.id = value.value("id", "");
        marker.x = value.value("x", 0.0f);
        marker.y = value.value("y", 0.0f);
        marker.type = value.value("type", "sequence");
        marker.z = value.value("z", marker.type == "camera" ? 1.6f : 0.0f);
        marker.angleDegrees = value.value("angle_degrees", 0.0f);
        marker.pitch = value.value("pitch", 0.0f);
        if (!std::isfinite(marker.x) || !std::isfinite(marker.y) || !std::isfinite(marker.z) ||
            !std::isfinite(marker.angleDegrees) || !std::isfinite(marker.pitch)) marker.id.clear();
        return marker;
    }

    nlohmann::json MarkerToJson(const SequenceMarker& marker)
    {
        return {{"id", marker.id}, {"x", marker.x}, {"y", marker.y}, {"z", marker.z},
            {"type", marker.type}, {"angle_degrees", marker.angleDegrees}, {"pitch", marker.pitch}};
    }

    std::string UniqueMarkerId(const std::vector<SequenceMarker>& markers, const std::string& base)
    {
        const std::string name = base.empty() ? "marker" : base;
        if (!FindMarker(markers, name)) return name;
        for (int suffix = 2; ; ++suffix)
        {
            const auto candidate = name + "_" + std::to_string(suffix);
            if (!FindMarker(markers, candidate)) return candidate;
        }
    }

    void RenameMarkerReferences(nlohmann::json& document, const std::string& oldId, const std::string& newId)
    {
        if (document.is_object())
            for (auto it = document.begin(); it != document.end(); ++it)
            {
                if ((it.key() == "marker" || it.key() == "target_marker") && it.value() == oldId)
                    it.value() = newId;
                else RenameMarkerReferences(it.value(), oldId, newId);
            }
        else if (document.is_array())
            for (auto& item : document) RenameMarkerReferences(item, oldId, newId);
    }

    namespace
    {
        void RenameActions(std::vector<ActionDef>& actions, const std::string& oldId, const std::string& newId)
        {
            for (auto& action : actions)
            {
                RenameMarkerReferences(action.data, oldId, newId);
                RenameActions(action.children, oldId, newId);
            }
        }
    }

    void RenameMarkerReferences(SequenceDefinition& definition, const std::string& oldId, const std::string& newId)
    {
        for (auto& node : definition.nodes) RenameActions(node.actions, oldId, newId);
        RenameActions(definition.onSkipActions, oldId, newId);
    }

    std::filesystem::path MarkerPathForMap(const std::filesystem::path& mapPath)
    {
        std::filesystem::path p = mapPath;
        p.replace_extension(".sequence_markers.json");
        return p;
    }

    bool LoadMarkersForMap(const std::filesystem::path& mapPath, std::vector<SequenceMarker>& out, std::string* outError)
    {
        out.clear();
        const auto path = MarkerPathForMap(mapPath);
        if (!std::filesystem::exists(path))
            return true;

        nlohmann::json root;
        std::string err;
        if (!jsonutils::LoadJsonFileSafe(path.string(), root, err))
        {
            if (outError) *outError = err;
            return false;
        }

        const auto& arr = root.is_array() ? root : root.value("markers", nlohmann::json::array());
        if (!arr.is_array())
        {
            if (outError) *outError = "markers must be an array: " + path.string();
            return false;
        }

        for (const auto& item : arr)
        {
            if (!item.is_object())
                continue;
            SequenceMarker marker = MarkerFromJson(item);
            if (!marker.id.empty())
                out.push_back(std::move(marker));
        }
        return true;
    }

    bool SaveMarkersForMap(const std::filesystem::path& mapPath, const std::vector<SequenceMarker>& markers, std::string* outError)
    {
        const auto path = MarkerPathForMap(mapPath);
        try
        {
            nlohmann::json root;
            root["markers"] = nlohmann::json::array();
            for (const auto& marker : markers)
            {
                root["markers"].push_back(MarkerToJson(marker));
            }

            std::ofstream f(path, std::ios::binary);
            if (!f)
            {
                if (outError) *outError = "Cannot write marker file: " + path.string();
                return false;
            }
            f << root.dump(4) << "\n";
            return true;
        }
        catch (const std::exception& e)
        {
            if (outError) *outError = e.what();
            return false;
        }
    }

    const SequenceMarker* FindMarker(const std::vector<SequenceMarker>& markers, const std::string& id)
    {
        for (const auto& marker : markers)
        {
            if (marker.id == id)
                return &marker;
        }
        return nullptr;
    }
}
