#pragma once

#include "InteriorMapData.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace interior
{
    namespace detail
    {
        using json = nlohmann::json;

        inline HistoricalMetadata ParseHistorical(const json& source)
        {
            HistoricalMetadata result;
            if (!source.is_object())
                return result;

            result.certainty = ParseHistoricalCertainty(source.value("certainty", "unknown"));
            result.phase = source.value("phase", std::string());
            result.activeFrom = source.value("active_from", 0);
            result.activeTo = source.value("active_to", 0);
            result.note = source.value("note", std::string());

            if (source.contains("sources") && source["sources"].is_array())
            {
                for (const auto& value : source["sources"])
                {
                    if (value.is_string())
                        result.sources.push_back(value.get<std::string>());
                }
            }
            return result;
        }

        inline RectI ParseRect(const json& source)
        {
            RectI rect;
            if (source.is_array() && source.size() >= 4)
            {
                rect.x = source[0].get<int>();
                rect.y = source[1].get<int>();
                rect.w = source[2].get<int>();
                rect.h = source[3].get<int>();
            }
            else if (source.is_object())
            {
                rect.x = source.value("x", 0);
                rect.y = source.value("y", 0);
                rect.w = source.value("w", 0);
                rect.h = source.value("h", 0);
            }
            return rect;
        }

        inline Vec2d ParseVec2(const json& source)
        {
            Vec2d value;
            if (source.is_array() && source.size() >= 2)
            {
                value.x = source[0].get<double>();
                value.y = source[1].get<double>();
            }
            else if (source.is_object())
            {
                value.x = source.value("x", 0.0);
                value.y = source.value("y", 0.0);
            }
            return value;
        }

        inline PlaneDef ParsePlane(const json& source, double fallbackHeight)
        {
            PlaneDef plane;
            plane.height = fallbackHeight;
            if (!source.is_object())
                return plane;

            plane.height = source.value("height", fallbackHeight);
            plane.slopeX = source.value("slope_x", 0.0);
            plane.slopeY = source.value("slope_y", 0.0);
            plane.originX = source.value("origin_x", 0.0);
            plane.originY = source.value("origin_y", 0.0);
            plane.materialId = source.value("material", std::string());
            return plane;
        }

        inline bool LoadJsonFile(const std::filesystem::path& path, json& out, std::string& error)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                error = "Cannot open JSON file: " + path.string();
                return false;
            }

            try
            {
                input >> out;
                return true;
            }
            catch (const std::exception& ex)
            {
                error = "JSON parse failed in " + path.string() + ": " + ex.what();
                return false;
            }
        }

        inline SectorDef SectorFromPolygonRoom(const PolygonRoomDef& room)
        {
            SectorDef sector;
            sector.id = room.id;
            sector.displayName = room.displayName;
            sector.roomId = room.id;
            sector.roomFunction = room.roomFunction;
            sector.floor = room.floor;
            sector.ceiling = room.ceiling;
            sector.boundaryMaterialId = room.boundaryMaterialId;
            sector.wallHeight = room.wallHeight;
            sector.ambient = room.ambient;
            sector.skyCeiling = room.skyCeiling;
            sector.npcAccess = room.npcAccess;
            sector.historical = room.historical;
            return sector;
        }
    }

    inline bool ParseCastleLocation(const std::string& value, CastleLocationRef& out)
    {
        constexpr const char* prefix = "castle:";
        if (value.rfind(prefix, 0) != 0)
            return false;

        const std::string payload = value.substr(7);
        const std::size_t slash = payload.find('/');
        if (slash == std::string::npos || slash == 0 || slash + 1 >= payload.size())
            return false;

        out.castleId = payload.substr(0, slash);
        out.mapId = payload.substr(slash + 1);
        return !out.castleId.empty() && !out.mapId.empty();
    }

    inline bool LoadCastleProject(
        const std::filesystem::path& projectRoot,
        const std::string& castleId,
        CastleProjectDef& out,
        std::string& error)
    {
        using namespace detail;
        const std::filesystem::path base = projectRoot / "data" / "castles" / castleId;

        json castleJson;
        if (!LoadJsonFile(base / "castle.json", castleJson, error))
            return false;

        out = CastleProjectDef{};
        out.schemaVersion = castleJson.value("schema_version", 1);
        out.id = castleJson.value("id", castleId);
        out.displayName = castleJson.value("display_name", out.id);
        out.defaultMap = castleJson.value("default_map", std::string());

        if (castleJson.contains("maps") && castleJson["maps"].is_array())
        {
            for (const auto& item : castleJson["maps"])
            {
                if (item.is_string())
                    out.maps.push_back(item.get<std::string>());
            }
        }

        json materialsJson;
        if (!LoadJsonFile(base / "materials.json", materialsJson, error))
            return false;

        if (!materialsJson.contains("materials") || !materialsJson["materials"].is_array())
        {
            error = "materials.json is missing the materials array.";
            return false;
        }

        for (const auto& source : materialsJson["materials"])
        {
            MaterialDef material;
            material.id = source.value("id", std::string());
            material.texturePath = source.value("texture", std::string());
            material.kind = source.value("kind", std::string("surface"));
            if (source.contains("historical"))
                material.historical = ParseHistorical(source["historical"]);

            if (!material.id.empty())
                out.materials[material.id] = std::move(material);
        }

        return true;
    }

    inline bool LoadCastleMap(
        const std::filesystem::path& projectRoot,
        const std::string& castleId,
        const std::string& mapId,
        InteriorMapDef& out,
        CastleProjectDef& project,
        std::string& error)
    {
        using namespace detail;
        if (!LoadCastleProject(projectRoot, castleId, project, error))
            return false;

        const std::filesystem::path base = projectRoot / "data" / "castles" / castleId;
        json root;
        if (!LoadJsonFile(base / "maps" / (mapId + ".map.json"), root, error))
            return false;

        out = InteriorMapDef{};
        out.schemaVersion = root.value("schema_version", 1);
        out.geometryMode = root.value("geometry_mode", out.schemaVersion >= 2 ? std::string("polygon") : std::string("raster"));
        out.castleId = castleId;
        out.id = root.value("id", mapId);
        out.displayName = root.value("display_name", out.id);
        out.mapKind = root.value("map_kind", std::string("interior_25d"));
        out.cellSize = root.value("cell_size", 1.0);
        out.fovDegrees = root.value("fov_degrees", 72.0);
        out.eyeHeight = root.value("eye_height", 0.56);
        out.doorScale = std::clamp(root.value("door_scale", 1.0), 0.25, 2.0);
        out.defaultSolidMaterialId = root.value("default_solid_material", std::string());
        out.defaultDoorMaterialId = root.value("default_door_material", std::string());
        out.defaultSectorId = root.value("default_sector", std::string());
        out.skyMaterialId = root.value("sky_material", std::string());

        if (root.contains("default_materials") && root["default_materials"].is_object())
        {
            const auto& defaults = root["default_materials"];
            if (out.defaultSolidMaterialId.empty())
                out.defaultSolidMaterialId = defaults.value("wall", std::string());
            if (out.defaultDoorMaterialId.empty())
                out.defaultDoorMaterialId = defaults.value("door", std::string());
            if (out.skyMaterialId.empty())
                out.skyMaterialId = defaults.value("sky", std::string());
        }

        if (root.contains("size") && root["size"].is_array() && root["size"].size() >= 2)
        {
            out.width = root["size"][0].get<int>();
            out.height = root["size"][1].get<int>();
        }

        if (root.contains("historical"))
            out.historical = ParseHistorical(root["historical"]);

        if (root.contains("sectors") && root["sectors"].is_array())
        {
            for (const auto& source : root["sectors"])
            {
                SectorDef sector;
                sector.id = source.value("id", std::string());
                sector.displayName = source.value("display_name", sector.id);
                sector.roomId = source.value("room_id", std::string());
                sector.roomFunction = source.value("room_function", std::string());
                sector.boundaryMaterialId = source.value("boundary_material", std::string());
                sector.wallHeight = source.value("wall_height", -1.0);
                sector.ambient = source.value("ambient", 1.0);
                sector.skyCeiling = source.value("sky_ceiling", false);
                sector.npcAccess = source.value("npc_access", true);
                sector.floor = ParsePlane(source.value("floor", json::object()), 0.0);
                sector.ceiling = ParsePlane(source.value("ceiling", json::object()), 2.4);
                if (source.contains("historical"))
                    sector.historical = ParseHistorical(source["historical"]);
                if (!sector.id.empty())
                    out.sectors.push_back(std::move(sector));
            }
        }

        if (out.geometryMode == "polygon" && root.contains("rooms") && root["rooms"].is_array())
        {
            const std::string fallbackFloor = root.value("default_materials", json::object()).value("floor", std::string());
            const std::string fallbackWall = root.value("default_materials", json::object()).value("wall", out.defaultSolidMaterialId);
            const std::string fallbackCeiling = root.value("default_materials", json::object()).value("ceiling", std::string());

            for (const auto& source : root["rooms"])
            {
                PolygonRoomDef room;
                room.id = source.value("id", std::string());
                room.label = source.value("label", std::string());
                room.displayName = source.value("name", source.value("display_name", room.id));
                room.roomFunction = source.value("function", source.value("room_function", std::string()));
                room.boundaryMaterialId = source.value("boundary_material", fallbackWall);
                room.wallHeight = source.value("wall_height", -1.0);
                room.ambient = source.value("ambient", room.roomFunction == "courtyard" ? 1.06 : 0.70);
                room.npcAccess = source.value("npc_access", true);
                room.notes = source.value("notes", std::string());

                if (source.contains("polygon") && source["polygon"].is_array())
                {
                    for (const auto& point : source["polygon"])
                        room.polygon.push_back(ParseVec2(point));
                }

                room.floor = ParsePlane(source.value("floor", json::object()), 0.0);
                room.ceiling = ParsePlane(source.value("ceiling", json::object()), 2.6);
                if (room.floor.materialId.empty()) room.floor.materialId = fallbackFloor;
                if (room.ceiling.materialId.empty()) room.ceiling.materialId = fallbackCeiling;
                const std::string ceilingType = source.value("ceiling", json::object()).value("type", std::string());
                room.skyCeiling = ceilingType == "sky" || source.value("sky_ceiling", false);
                if (source.contains("historical"))
                    room.historical = ParseHistorical(source["historical"]);

                if (!room.id.empty())
                {
                    out.polygonRooms.push_back(room);
                    out.sectors.push_back(SectorFromPolygonRoom(room));
                }
            }

            if (out.defaultSectorId.empty() && !out.polygonRooms.empty())
                out.defaultSectorId = out.polygonRooms.front().id;
        }

        if (root.contains("raster") && root["raster"].is_object())
        {
            const auto& raster = root["raster"];
            if (raster.contains("walkable_regions") && raster["walkable_regions"].is_array())
            {
                for (const auto& source : raster["walkable_regions"])
                {
                    RasterRegion region;
                    region.sectorId = source.value("sector", std::string());
                    region.rect = ParseRect(source.value("rect", json::array()));
                    out.walkableRegions.push_back(std::move(region));
                }
            }

            if (raster.contains("solid_regions") && raster["solid_regions"].is_array())
            {
                for (const auto& source : raster["solid_regions"])
                {
                    SolidRegion region;
                    region.materialId = source.value("material", std::string());
                    region.sectorId = source.value("sector", std::string());
                    region.rect = ParseRect(source.value("rect", json::array()));
                    out.solidRegions.push_back(std::move(region));
                }
            }

            if (raster.contains("cell_overrides") && raster["cell_overrides"].is_array())
            {
                for (const auto& source : raster["cell_overrides"])
                {
                    CellOverride cell;
                    cell.x = source.value("x", 0);
                    cell.y = source.value("y", 0);
                    cell.solid = source.value("solid", false);
                    cell.materialId = source.value("material", std::string());
                    cell.sectorId = source.value("sector", std::string());
                    out.cellOverrides.push_back(std::move(cell));
                }
            }
        }

        if (root.contains("features") && root["features"].is_array())
        {
            for (const auto& source : root["features"])
            {
                WallFeatureDef feature;
                feature.id = source.value("id", std::string());
                feature.type = source.value("type", std::string());
                feature.roomId = source.value("room", source.value("from", std::string()));
                feature.otherRoomId = source.value("to", std::string());
                feature.side = source.value("side", std::string());
                feature.x = source.value("x", 0);
                feature.y = source.value("y", 0);
                feature.span = std::max(1, source.value("span", 1));
                const std::string axis = source.value("axis", std::string("x"));
                feature.axis = axis.empty() ? 'x' : axis.front();
                if (feature.axis != 'x' && feature.axis != 'y') feature.axis = 'x';
                feature.width = source.value("width", static_cast<double>(feature.span));
                feature.height = source.value("height", -1.0);
                feature.shape = source.value("shape", std::string());
                feature.materialId = source.value("material", std::string());
                feature.motion = source.value("motion", std::string());
                feature.locked = source.value("locked", false);
                feature.initiallyOpen = source.value("initially_open", false);
                feature.speed = source.value("speed", 1.8);
                feature.interactive = source.value("interactive", false);
                feature.interactionLabel = source.value("interaction_label", std::string());
                feature.targetLocation = source.value("target_location", std::string());
                feature.targetSpawn = source.value("target_spawn", std::string());
                if (source.contains("position"))
                {
                    feature.position = ParseVec2(source["position"]);
                    feature.hasPosition = true;
                }
                if (source.contains("segment") && source["segment"].is_array() &&
                    source["segment"].size() >= 2)
                {
                    feature.segmentStart = ParseVec2(source["segment"][0]);
                    feature.segmentEnd = ParseVec2(source["segment"][1]);
                    feature.hasSegment = true;
                }
                feature.leaves = std::clamp(source.value(
                    "leaves", source.value("leaf_count", 1)), 1, 2);
                feature.hinge = source.value("hinge", std::string("start"));
                feature.opensToward = source.value("opens_toward", std::string());
                feature.swingDirection = source.value("swing_direction", 0.0);
                feature.swingDegrees = std::clamp(
                    source.value("swing_degrees", 90.0), 15.0, 170.0);
                feature.compileConnector = source.value("compile_connector", true);
                if (source.contains("historical"))
                    feature.historical = ParseHistorical(source["historical"]);
                if (!feature.id.empty())
                    out.wallFeatures.push_back(std::move(feature));
            }
        }

        if (root.contains("stairs") && root["stairs"].is_array())
        {
            for (const auto& source : root["stairs"])
            {
                StairDef stair;
                stair.id = source.value("id", std::string());
                stair.roomId = source.value("room", std::string());
                stair.kind = source.value("kind", std::string("straight"));
                stair.start = ParseVec2(source.value("start", json::array()));
                stair.end = ParseVec2(source.value("end", json::array()));
                if (source.contains("start") && source["start"].is_array() && source["start"].size() >= 3)
                    stair.startHeight = source["start"][2].get<double>();
                else
                    stair.startHeight = source.value("start_height", 0.0);
                if (source.contains("end") && source["end"].is_array() && source["end"].size() >= 3)
                    stair.endHeight = source["end"][2].get<double>();
                else
                    stair.endHeight = source.value("end_height", 0.0);
                stair.steps = std::max(1, source.value("steps", 1));
                stair.width = std::max(0.25, source.value("width", 1.0));
                stair.compileGeometry = source.value("compile_geometry", stair.kind == "segmented");
                stair.smoothTraversal =
                    source.value("smooth_traversal", true);
                stair.floorMaterialId = source.value("floor_material", std::string());
                stair.boundaryMaterialId = source.value("boundary_material", std::string());
                stair.targetLocation = source.value("target_location", std::string());
                stair.targetSpawn = source.value("target_spawn", std::string());
                if (source.contains("historical"))
                    stair.historical = ParseHistorical(source["historical"]);
                if (!stair.id.empty())
                    out.stairs.push_back(std::move(stair));
            }
        }

        if (root.contains("wall_segments") && root["wall_segments"].is_array())
        {
            for (const auto& source : root["wall_segments"])
            {
                if (!source.is_object())
                    continue;
                WallSegmentDef wall;
                wall.id = source.value("id", std::string());
                wall.start = {
                    source.value("x0", 0.0),
                    source.value("y0", 0.0)
                };
                wall.end = {
                    source.value("x1", 0.0),
                    source.value("y1", 0.0)
                };
                wall.bottom = source.value("bottom_z", 0.0);
                wall.top = source.value("top_z", 3.0);
                wall.hasBottomEnd = source.contains("bottom_z_end");
                wall.hasTopEnd = source.contains("top_z_end");
                wall.bottomEnd = source.value("bottom_z_end", wall.bottom);
                wall.topEnd = source.value("top_z_end", wall.top);
                if (source.contains("top_profile") && source["top_profile"].is_array())
                {
                    for (const auto& value : source["top_profile"])
                        if (value.is_number())
                            wall.topProfile.push_back(value.get<double>());
                }
                wall.materialId = source.value("material", std::string());
                wall.ambient = source.value("ambient", 1.0);
                wall.textureScale = source.value("texture_scale", 1.0);
                wall.textureUOffset = source.value("texture_u_offset", 0.0);
                wall.worldAlignedTexture = source.value("world_aligned_texture", false);
                wall.solid = source.value("solid", true);
                wall.twoSided = source.value("two_sided", true);
                if (source.contains("historical"))
                    wall.historical = ParseHistorical(source["historical"]);
                if (!wall.id.empty())
                    out.wallSegments.push_back(std::move(wall));
            }
        }

        if (root.contains("room_graph") && root["room_graph"].is_object())
        {
            for (auto it = root["room_graph"].begin(); it != root["room_graph"].end(); ++it)
            {
                if (!it.value().is_array())
                    continue;
                auto& targets = out.roomGraph[it.key()];
                for (const auto& value : it.value())
                {
                    if (value.is_string())
                        targets.push_back(value.get<std::string>());
                }
            }
        }

        if (root.contains("doors") && root["doors"].is_array())
        {
            for (const auto& source : root["doors"])
            {
                DoorDef door;
                door.id = source.value("id", std::string());
                door.x = source.value("x", 0);
                door.y = source.value("y", 0);
                door.locked = source.value("locked", false);
                door.initiallyOpen = source.value("initially_open", false);
                door.speed = source.value("speed", 1.8);
                door.materialId = source.value("material", std::string());
                door.motion = source.value("motion", std::string());
                door.span = std::max(1, source.value("span", 1));
                const std::string axis = source.value("axis", std::string("x"));
                door.axis = axis.empty() ? 'x' : axis.front();
                if (door.axis != 'x' && door.axis != 'y') door.axis = 'x';
                door.height = source.value("height", -1.0);
                if (source.contains("segment") && source["segment"].is_array() &&
                    source["segment"].size() >= 2)
                {
                    door.segmentStart = ParseVec2(source["segment"][0]);
                    door.segmentEnd = ParseVec2(source["segment"][1]);
                    door.hasSegment = true;
                }
                door.interactionGroup = source.value("interaction_group", door.id);
                const std::string hinge = source.value("hinge", std::string("start"));
                door.hingeAtEnd = hinge == "end" || hinge == "right" || hinge == "high";
                door.swingDirection = source.value("swing_direction", 1.0) < 0.0 ? -1.0 : 1.0;
                door.swingDegrees = std::clamp(
                    source.value("swing_degrees", 90.0), 15.0, 170.0);
                door.textureU0 = std::clamp(source.value("texture_u0", 0.0), 0.0, 1.0);
                door.textureU1 = std::clamp(source.value("texture_u1", 1.0), 0.0, 1.0);
                door.targetLocation = source.value("target_location", std::string());
                door.targetSpawn = source.value("target_spawn", std::string());
                if (door.motion.empty())
                    door.motion = door.targetLocation.empty() ? "slide" : "transition";
                if (source.contains("historical"))
                    door.historical = ParseHistorical(source["historical"]);
                out.doors.push_back(std::move(door));
            }
        }

        if (root.contains("spawns") && root["spawns"].is_array())
        {
            for (const auto& source : root["spawns"])
            {
                SpawnDef spawn;
                spawn.id = source.value("id", std::string());
                spawn.roomId = source.value("room", std::string());
                if (source.contains("position") && source["position"].is_array())
                {
                    spawn.x = source["position"].size() > 0 ? source["position"][0].get<double>() : 0.0;
                    spawn.y = source["position"].size() > 1 ? source["position"][1].get<double>() : 0.0;
                }
                else
                {
                    spawn.x = source.value("x", 0.0);
                    spawn.y = source.value("y", 0.0);
                }
                spawn.angleDegrees = source.value("angle_degrees", 0.0);
                spawn.pitch = source.value("pitch", 0.0);
                if (!spawn.id.empty())
                    out.spawns.push_back(std::move(spawn));
            }
        }

        const std::filesystem::path entitiesPath = base / "entities" / (mapId + ".entities.json");
        if (std::filesystem::exists(entitiesPath))
        {
            json entitiesRoot;
            if (!LoadJsonFile(entitiesPath, entitiesRoot, error))
                return false;

            if (entitiesRoot.contains("entities") && entitiesRoot["entities"].is_array())
            {
                for (const auto& source : entitiesRoot["entities"])
                {
                    EntityDef entity;
                    entity.id = source.value("id", std::string());
                    entity.prototypeId = source.value("prototype", std::string());
                    entity.materialId = source.value("material", std::string());
                    entity.renderMode = source.value("render_mode", std::string("billboard"));
                    entity.x = source.value("x", 0.0);
                    entity.y = source.value("y", 0.0);
                    entity.zOffset = source.value("z_offset", 0.0);
                    entity.scale = source.value("scale", 1.0);
                    entity.solid = source.value("solid", false);
                    entity.interactionLabel = source.value("interaction_label", std::string());
                    entity.targetLocation = source.value("target_location", std::string());
                    entity.targetSpawn = source.value("target_spawn", std::string());
                    if (source.contains("historical"))
                        entity.historical = ParseHistorical(source["historical"]);
                    if (!entity.id.empty())
                        out.entities.push_back(std::move(entity));
                }
            }
        }

        const std::filesystem::path lightsPath = base / "navigation" / (mapId + ".navigation.json");
        if (std::filesystem::exists(lightsPath))
        {
            json navigationRoot;
            if (!LoadJsonFile(lightsPath, navigationRoot, error))
                return false;

            if (navigationRoot.contains("lights") && navigationRoot["lights"].is_array())
            {
                for (const auto& source : navigationRoot["lights"])
                {
                    LightDef light;
                    light.id = source.value("id", std::string());
                    light.type = source.value("type", std::string());
                    light.x = source.value("x", 0.0);
                    light.y = source.value("y", 0.0);
                    light.z = source.value("z", 0.0);
                    light.radius = source.value("radius", 0.0);
                    light.intensity = source.value("intensity", 1.0);
                    light.npcAccess = source.value("npc_access", true);
                    if (source.contains("historical"))
                        light.historical = ParseHistorical(source["historical"]);
                    if (!light.id.empty())
                        out.lights.push_back(std::move(light));
                }
            }
        }

        return true;
    }
}
