#pragma once

#include "InteriorMapData.h"
#include "InteriorMapLoader.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace interior
{
    struct ValidationReport
    {
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        bool ok() const { return errors.empty(); }

        std::string summary() const
        {
            std::ostringstream out;
            out << (ok() ? "Validation OK" : "Validation failed")
                << " (" << errors.size() << " errors, "
                << warnings.size() << " warnings)";
            for (const std::string& error : errors)
                out << "\nERROR: " << error;
            for (const std::string& warning : warnings)
                out << "\nWARN: " << warning;
            return out.str();
        }
    };

    inline bool RectInsideMap(const RectI& rect, int width, int height)
    {
        return rect.w > 0 && rect.h > 0 &&
               rect.x >= 0 && rect.y >= 0 &&
               rect.x + rect.w <= width &&
               rect.y + rect.h <= height;
    }

    inline double Orientation(const Vec2d& a, const Vec2d& b, const Vec2d& c)
    {
        return (b.x - a.x) * (c.y - a.y) -
               (b.y - a.y) * (c.x - a.x);
    }

    inline bool PointOnSegment(const Vec2d& a, const Vec2d& b, const Vec2d& p)
    {
        constexpr double eps = 1.0e-8;
        if (std::abs(Orientation(a, b, p)) > eps)
            return false;
        return p.x >= std::min(a.x, b.x) - eps && p.x <= std::max(a.x, b.x) + eps &&
               p.y >= std::min(a.y, b.y) - eps && p.y <= std::max(a.y, b.y) + eps;
    }

    inline bool SegmentsIntersect(const Vec2d& a, const Vec2d& b,
                                  const Vec2d& c, const Vec2d& d)
    {
        const double o1 = Orientation(a, b, c);
        const double o2 = Orientation(a, b, d);
        const double o3 = Orientation(c, d, a);
        const double o4 = Orientation(c, d, b);
        constexpr double eps = 1.0e-8;

        if (((o1 > eps && o2 < -eps) || (o1 < -eps && o2 > eps)) &&
            ((o3 > eps && o4 < -eps) || (o3 < -eps && o4 > eps)))
            return true;

        if (std::abs(o1) <= eps && PointOnSegment(a, b, c)) return true;
        if (std::abs(o2) <= eps && PointOnSegment(a, b, d)) return true;
        if (std::abs(o3) <= eps && PointOnSegment(c, d, a)) return true;
        if (std::abs(o4) <= eps && PointOnSegment(c, d, b)) return true;
        return false;
    }

    inline bool PolygonSelfIntersects(const std::vector<Vec2d>& polygon)
    {
        if (polygon.size() < 4)
            return false;
        for (std::size_t i = 0; i < polygon.size(); ++i)
        {
            const std::size_t i2 = (i + 1) % polygon.size();
            for (std::size_t j = i + 1; j < polygon.size(); ++j)
            {
                const std::size_t j2 = (j + 1) % polygon.size();
                if (i == j || i2 == j || j2 == i)
                    continue;
                if (i == 0 && j2 == 0)
                    continue;
                if (SegmentsIntersect(polygon[i], polygon[i2], polygon[j], polygon[j2]))
                    return true;
            }
        }
        return false;
    }

    inline bool PointInPolygon(const std::vector<Vec2d>& polygon, double x, double y)
    {
        if (polygon.size() < 3)
            return false;
        bool inside = false;
        for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
        {
            const Vec2d& a = polygon[i];
            const Vec2d& b = polygon[j];
            const bool crosses = ((a.y > y) != (b.y > y)) &&
                (x < (b.x - a.x) * (y - a.y) / ((b.y - a.y) + 1.0e-12) + a.x);
            if (crosses)
                inside = !inside;
        }
        return inside;
    }

    inline ValidationReport ValidateCastleMap(
        const CastleProjectDef& project,
        const InteriorMapDef& map)
    {
        ValidationReport report;

        if (map.schemaVersion != 1 && map.schemaVersion != 2)
            report.warnings.push_back("Unsupported or future schema_version: " + std::to_string(map.schemaVersion));
        if (map.geometryMode != "raster" && map.geometryMode != "polygon")
            report.errors.push_back("Unknown geometry_mode: " + map.geometryMode);
        if (map.id.empty())
            report.errors.push_back("Map id is empty.");
        if (map.width <= 0 || map.height <= 0)
            report.errors.push_back("Map size must be positive.");
        if (map.width > 256 || map.height > 256)
            report.warnings.push_back("Large map; software renderer may be slow in Debug builds.");

        if (project.materials.find(map.defaultSolidMaterialId) == project.materials.end())
            report.errors.push_back("Unknown default_solid_material: " + map.defaultSolidMaterialId);
        if (!map.defaultDoorMaterialId.empty() &&
            project.materials.find(map.defaultDoorMaterialId) == project.materials.end())
            report.errors.push_back("Unknown default door material: " + map.defaultDoorMaterialId);
        if (!map.skyMaterialId.empty() && project.materials.find(map.skyMaterialId) == project.materials.end())
            report.errors.push_back("Unknown sky_material: " + map.skyMaterialId);

        std::unordered_set<std::string> sectorIds;
        for (const SectorDef& sector : map.sectors)
        {
            if (sector.id.empty())
            {
                report.errors.push_back("Sector with empty id.");
                continue;
            }
            if (!sectorIds.insert(sector.id).second)
                report.errors.push_back("Duplicate sector id: " + sector.id);

            if (project.materials.find(sector.floor.materialId) == project.materials.end())
                report.errors.push_back("Sector " + sector.id + " uses unknown floor material: " + sector.floor.materialId);
            if (!sector.skyCeiling && project.materials.find(sector.ceiling.materialId) == project.materials.end())
                report.errors.push_back("Sector " + sector.id + " uses unknown ceiling material: " + sector.ceiling.materialId);
            if (project.materials.find(sector.boundaryMaterialId) == project.materials.end())
                report.errors.push_back("Sector " + sector.id + " uses unknown boundary material: " + sector.boundaryMaterialId);
            if (sector.ceiling.height <= sector.floor.height + 0.35)
                report.errors.push_back("Sector " + sector.id + " has insufficient base headroom.");
        }

        if (!map.defaultSectorId.empty() && sectorIds.find(map.defaultSectorId) == sectorIds.end())
            report.errors.push_back("Unknown default_sector: " + map.defaultSectorId);

        std::unordered_set<std::string> roomIds;
        if (map.geometryMode == "polygon")
        {
            if (map.polygonRooms.empty())
                report.errors.push_back("Polygon map has no rooms.");

            for (const PolygonRoomDef& room : map.polygonRooms)
            {
                if (room.id.empty())
                {
                    report.errors.push_back("Polygon room with empty id.");
                    continue;
                }
                if (!roomIds.insert(room.id).second)
                    report.errors.push_back("Duplicate polygon room id: " + room.id);
                if (room.polygon.size() < 3)
                    report.errors.push_back("Room " + room.id + " has fewer than 3 polygon vertices.");
                if (PolygonSelfIntersects(room.polygon))
                    report.errors.push_back("Room " + room.id + " polygon self-intersects.");
                for (const Vec2d& point : room.polygon)
                {
                    if (point.x < 0.0 || point.y < 0.0 || point.x > map.width || point.y > map.height)
                        report.errors.push_back("Room " + room.id + " has a vertex outside map bounds.");
                }
            }

            for (const auto& edge : map.roomGraph)
            {
                if (roomIds.find(edge.first) == roomIds.end())
                    report.errors.push_back("room_graph references unknown source room: " + edge.first);
                for (const std::string& target : edge.second)
                {
                    if (roomIds.find(target) == roomIds.end())
                        report.errors.push_back("room_graph references unknown target room: " + target);
                }
            }
        }

        for (const RasterRegion& region : map.walkableRegions)
        {
            if (sectorIds.find(region.sectorId) == sectorIds.end())
                report.errors.push_back("Walkable region references unknown sector: " + region.sectorId);
            if (!RectInsideMap(region.rect, map.width, map.height))
                report.errors.push_back("Walkable region is outside map bounds for sector: " + region.sectorId);
        }

        for (const SolidRegion& region : map.solidRegions)
        {
            if (project.materials.find(region.materialId) == project.materials.end())
                report.errors.push_back("Solid region uses unknown material: " + region.materialId);
            if (!region.sectorId.empty() && sectorIds.find(region.sectorId) == sectorIds.end())
                report.errors.push_back("Solid region references unknown sector: " + region.sectorId);
            if (!RectInsideMap(region.rect, map.width, map.height))
                report.errors.push_back("Solid region is outside map bounds.");
        }

        std::unordered_set<std::string> spawnIds;
        for (const SpawnDef& spawn : map.spawns)
        {
            if (!spawnIds.insert(spawn.id).second)
                report.errors.push_back("Duplicate spawn id: " + spawn.id);
            if (spawn.x < 0.0 || spawn.y < 0.0 || spawn.x >= map.width || spawn.y >= map.height)
                report.errors.push_back("Spawn outside map bounds: " + spawn.id);
            if (map.geometryMode == "polygon" && !spawn.roomId.empty())
            {
                const auto roomIt = std::find_if(map.polygonRooms.begin(), map.polygonRooms.end(),
                    [&](const PolygonRoomDef& room) { return room.id == spawn.roomId; });
                if (roomIt == map.polygonRooms.end())
                    report.errors.push_back("Spawn " + spawn.id + " references unknown room: " + spawn.roomId);
                else if (!PointInPolygon(roomIt->polygon, spawn.x, spawn.y))
                    report.warnings.push_back("Spawn " + spawn.id + " is not inside its declared room polygon.");
            }
        }

        std::unordered_set<std::string> featureIds;
        for (const WallFeatureDef& feature : map.wallFeatures)
        {
            if (!featureIds.insert(feature.id).second)
                report.errors.push_back("Duplicate feature id: " + feature.id);
            if (feature.x < 0 || feature.y < 0 || feature.x >= map.width || feature.y >= map.height)
                report.errors.push_back("Feature outside map bounds: " + feature.id);
            const int endX = feature.x + (feature.axis == 'x' ? feature.span - 1 : 0);
            const int endY = feature.y + (feature.axis == 'y' ? feature.span - 1 : 0);
            if (endX >= map.width || endY >= map.height)
                report.errors.push_back("Feature span outside map bounds: " + feature.id);
            if (!feature.roomId.empty() && roomIds.find(feature.roomId) == roomIds.end())
                report.errors.push_back("Feature " + feature.id + " references unknown room: " + feature.roomId);
            if (!feature.otherRoomId.empty() && roomIds.find(feature.otherRoomId) == roomIds.end())
                report.errors.push_back("Feature " + feature.id + " references unknown other room: " + feature.otherRoomId);
            if (!feature.materialId.empty() && project.materials.find(feature.materialId) == project.materials.end())
                report.errors.push_back("Feature " + feature.id + " uses unknown material: " + feature.materialId);
            if ((feature.type == "map_portal" || feature.type == "vertical_portal") && feature.targetLocation.empty())
                report.warnings.push_back("Portal feature " + feature.id + " has no target location.");
        }

        std::unordered_set<std::string> stairIds;
        for (const StairDef& stair : map.stairs)
        {
            if (!stairIds.insert(stair.id).second)
                report.errors.push_back("Duplicate stair id: " + stair.id);
            if (roomIds.find(stair.roomId) == roomIds.end())
                report.errors.push_back("Stair " + stair.id + " references unknown room: " + stair.roomId);
            if (stair.start.x < 0.0 || stair.start.y < 0.0 || stair.start.x >= map.width || stair.start.y >= map.height ||
                stair.end.x < 0.0 || stair.end.y < 0.0 || stair.end.x >= map.width || stair.end.y >= map.height)
                report.errors.push_back("Stair " + stair.id + " lies outside map bounds.");
            if (stair.compileGeometry && stair.steps > 8)
                report.warnings.push_back("Stair " + stair.id + " requests many generated sectors; it may exceed the 26-sector legacy limit.");
        }

        std::unordered_set<std::string> doorIds;
        for (const DoorDef& door : map.doors)
        {
            if (!doorIds.insert(door.id).second)
                report.errors.push_back("Duplicate door id: " + door.id);
            const int endX = door.x + (door.axis == 'x' ? door.span - 1 : 0);
            const int endY = door.y + (door.axis == 'y' ? door.span - 1 : 0);
            if (door.x < 0 || door.y < 0 || endX >= map.width || endY >= map.height)
                report.errors.push_back("Door outside map bounds: " + door.id);
            if (!door.materialId.empty() && project.materials.find(door.materialId) == project.materials.end())
                report.errors.push_back("Door " + door.id + " uses unknown material: " + door.materialId);
            if (door.motion != "slide" && door.motion != "raise" && door.motion != "transition")
                report.errors.push_back("Door " + door.id + " uses unknown motion: " + door.motion);
        }

        std::unordered_set<std::string> entityIds;
        for (const EntityDef& entity : map.entities)
        {
            if (!entityIds.insert(entity.id).second)
                report.errors.push_back("Duplicate entity id: " + entity.id);
            if (entity.x < 0.0 || entity.y < 0.0 || entity.x >= map.width || entity.y >= map.height)
                report.errors.push_back("Entity outside map bounds: " + entity.id);
            if (!entity.materialId.empty() && project.materials.find(entity.materialId) == project.materials.end())
                report.errors.push_back("Entity " + entity.id + " uses unknown material: " + entity.materialId);
        }

        if (map.sectors.size() + 8 > 26 && map.geometryMode == "polygon")
            report.warnings.push_back("Polygon map is close to the legacy 26-sector limit; keep compiled stairs compact.");
        if (map.historical.certainty == HistoricalCertainty::Legendary)
            report.warnings.push_back("Physical map itself is marked legendary; architecture and narrative layers should remain separate.");

        return report;
    }
}
