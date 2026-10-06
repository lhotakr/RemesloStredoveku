#include "SequenceMarkers.h"

#include "JsonUtils.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace sequence
{
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
            SequenceMarker marker;
            marker.id = item.value("id", "");
            marker.x = item.value("x", 0.0f);
            marker.y = item.value("y", 0.0f);
            marker.type = item.value("type", "sequence");
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
                root["markers"].push_back({
                    {"id", marker.id},
                    {"x", marker.x},
                    {"y", marker.y},
                    {"type", marker.type.empty() ? "sequence" : marker.type}
                });
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
