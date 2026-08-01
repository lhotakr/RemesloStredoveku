#pragma once

#include "InteriorMapData.h"
#include "InteriorMapValidator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace interior
{
    using RuntimeTextureKey = std::uint16_t;
    static constexpr RuntimeTextureKey kNoRuntimeTexture = 0;
    struct CompileResult
    {
        bool success = false;
        std::filesystem::path outputPath;
        ValidationReport validation;
        std::string error;
    };

    namespace detail
    {
        inline const SectorDef* FindSector(const std::vector<SectorDef>& sectors, const std::string& id)
        {
            for (const SectorDef& sector : sectors)
            {
                if (sector.id == id)
                    return &sector;
            }
            return nullptr;
        }

        inline char NextFreeChar(const std::string& pool, const std::unordered_map<char, bool>& used)
        {
            for (char c : pool)
            {
                if (used.find(c) == used.end())
                    return c;
            }
            return '\0';
        }

        inline void FillRect(std::vector<std::string>& grid, const RectI& rect, char value)
        {
            for (int y = rect.y; y < rect.y + rect.h; ++y)
            {
                for (int x = rect.x; x < rect.x + rect.w; ++x)
                    grid[y][x] = value;
            }
        }


        inline void FillRect(std::vector<std::vector<RuntimeTextureKey>>& grid, const RectI& rect, RuntimeTextureKey value)
        {
            for (int y = rect.y; y < rect.y + rect.h; ++y)
            {
                for (int x = rect.x; x < rect.x + rect.w; ++x)
                    grid[y][x] = value;
            }
        }

        inline bool InBounds(int x, int y, int width, int height)
        {
            return x >= 0 && y >= 0 && x < width && y < height;
        }

        inline std::vector<std::pair<int, int>> FeatureCells(const WallFeatureDef& feature)
        {
            std::vector<std::pair<int, int>> cells;
            for (int i = 0; i < std::max(1, feature.span); ++i)
            {
                cells.emplace_back(
                    feature.x + (feature.axis == 'x' ? i : 0),
                    feature.y + (feature.axis == 'y' ? i : 0));
            }
            return cells;
        }

        inline DoorDef DoorFromFeature(const WallFeatureDef& feature)
        {
            DoorDef door;
            door.id = feature.id;
            door.x = feature.x;
            door.y = feature.y;
            door.locked = feature.locked;
            door.initiallyOpen = feature.initiallyOpen;
            door.speed = feature.speed;
            door.materialId = feature.materialId;
            door.span = std::max(1, feature.span);
            door.axis = feature.axis;
            door.height = feature.height;
            door.segmentStart = feature.segmentStart;
            door.segmentEnd = feature.segmentEnd;
            door.hasSegment = feature.hasSegment;
            door.interactionGroup = feature.id;
            door.hingeAtEnd = feature.hinge == "end" ||
                              feature.hinge == "right" ||
                              feature.hinge == "high";
            door.swingDirection = feature.swingDirection < 0.0 ? -1.0 : 1.0;
            door.swingDegrees = std::clamp(feature.swingDegrees, 15.0, 170.0);
            door.targetLocation = feature.targetLocation;
            door.targetSpawn = feature.targetSpawn;
            door.historical = feature.historical;

            if (feature.type == "map_portal" || feature.type == "vertical_portal")
                door.motion = "transition";
            else if (feature.motion == "swing" || feature.motion == "hinged" ||
                     feature.motion == "slide" || feature.motion == "raise" ||
                     feature.motion == "transition")
                door.motion = feature.motion;
            else
                door.motion = "swing";
            return door;
        }

        struct CompiledOpening
        {
            int edge = 0;
            double start = 0.0;
            double end = 1.0;
            double bottom = 0.0;
            double height = 2.1;
        };

        struct CompiledPolygon
        {
            std::string id;
            std::string sectorId;
            std::string wallMaterialId;
            double wallAmbient = 1.0;
            double wallTextureScale = 1.0;
            bool boundarySolid = true;
            bool cutsUnderlyingFloor = false;
            bool hasSupportBottom = false;
            double supportBottom = 0.0;
            std::vector<Vec2d> vertices;
            std::vector<CompiledOpening> openings;
        };

        struct CompiledWallSegment
        {
            std::string id;
            Vec2d start;
            Vec2d end;
            double bottom = 0.0;
            double top = 0.0;
            std::string materialId;
            double ambient = 1.0;
            double textureScale = 1.0;
            double textureUOffset = 0.0;
            bool worldAlignedTexture = false;
            bool solid = true;
            bool twoSided = true;
            double bottomEnd = std::numeric_limits<double>::quiet_NaN();
            double topEnd = std::numeric_limits<double>::quiet_NaN();
            std::vector<double> topProfile;
        };

        struct BoundaryHit
        {
            bool valid = false;
            int edge = -1;
            double t = 0.0;
            double distance = std::numeric_limits<double>::infinity();
            Vec2d point;
            Vec2d tangent;
        };

        inline double Dot(const Vec2d& a, const Vec2d& b)
        {
            return a.x * b.x + a.y * b.y;
        }

        inline Vec2d Subtract(const Vec2d& a, const Vec2d& b)
        {
            return {a.x - b.x, a.y - b.y};
        }

        inline Vec2d Add(const Vec2d& a, const Vec2d& b)
        {
            return {a.x + b.x, a.y + b.y};
        }

        inline Vec2d Scale(const Vec2d& value, double amount)
        {
            return {value.x * amount, value.y * amount};
        }

        inline double Length(const Vec2d& value)
        {
            return std::hypot(value.x, value.y);
        }

        inline Vec2d Normalize(const Vec2d& value, const Vec2d& fallback = {1.0, 0.0})
        {
            const double length = Length(value);
            if (length <= 1.0e-8)
                return fallback;
            return {value.x / length, value.y / length};
        }

        inline BoundaryHit ClosestBoundaryHit(const CompiledPolygon& polygon,
                                              const Vec2d& query)
        {
            BoundaryHit result;
            if (polygon.vertices.size() < 2)
                return result;

            for (int edge = 0; edge < static_cast<int>(polygon.vertices.size()); ++edge)
            {
                const Vec2d& a = polygon.vertices[static_cast<std::size_t>(edge)];
                const Vec2d& b = polygon.vertices[
                    (static_cast<std::size_t>(edge) + 1u) % polygon.vertices.size()];
                const Vec2d delta = Subtract(b, a);
                const double lengthSquared = Dot(delta, delta);
                if (lengthSquared <= 1.0e-10)
                    continue;
                const double t = std::clamp(
                    Dot(Subtract(query, a), delta) / lengthSquared, 0.0, 1.0);
                const Vec2d point = Add(a, Scale(delta, t));
                const double distance = Length(Subtract(query, point));
                if (distance >= result.distance)
                    continue;
                result.valid = true;
                result.edge = edge;
                result.t = t;
                result.distance = distance;
                result.point = point;
                result.tangent = Normalize(delta);
            }
            return result;
        }

        inline bool AddOpeningAtBoundary(CompiledPolygon& polygon,
                                         const BoundaryHit& hit,
                                         double width,
                                         double absoluteBottom,
                                         double absoluteTop,
                                         double sectorFloor)
        {
            if (!hit.valid || hit.edge < 0 || polygon.vertices.size() < 2)
                return false;
            const Vec2d& a = polygon.vertices[static_cast<std::size_t>(hit.edge)];
            const Vec2d& b = polygon.vertices[
                (static_cast<std::size_t>(hit.edge) + 1u) % polygon.vertices.size()];
            const double edgeLength = Length(Subtract(b, a));
            if (edgeLength <= 1.0e-8)
                return false;

            const double half = std::max(0.10, width * 0.5) / edgeLength;
            CompiledOpening opening;
            opening.edge = hit.edge;
            opening.start = std::clamp(hit.t - half, 0.0, 1.0);
            opening.end = std::clamp(hit.t + half, 0.0, 1.0);
            opening.bottom = std::max(0.0, absoluteBottom - sectorFloor);
            opening.height = std::max(0.10, absoluteTop - absoluteBottom);
            if (opening.end <= opening.start + 1.0e-5)
                return false;
            polygon.openings.push_back(opening);
            return true;
        }

        inline std::pair<Vec2d, Vec2d> AxisFeatureSegment(const WallFeatureDef& feature)
        {
            if (feature.hasSegment)
                return {feature.segmentStart, feature.segmentEnd};
            if (feature.axis == 'y')
            {
                return {
                    {feature.x + 0.5, static_cast<double>(feature.y)},
                    {feature.x + 0.5, feature.y + static_cast<double>(std::max(1, feature.span))}
                };
            }
            return {
                {static_cast<double>(feature.x), feature.y + 0.5},
                {feature.x + static_cast<double>(std::max(1, feature.span)), feature.y + 0.5}
            };
        }

        inline double PlaneHeight(const PlaneDef& plane, const Vec2d& point)
        {
            return plane.height +
                   plane.slopeX * (point.x - plane.originX) +
                   plane.slopeY * (point.y - plane.originY);
        }

        inline Vec2d PolygonAverage(const CompiledPolygon& polygon)
        {
            Vec2d result;
            if (polygon.vertices.empty())
                return result;
            for (const Vec2d& vertex : polygon.vertices)
            {
                result.x += vertex.x;
                result.y += vertex.y;
            }
            const double inverse = 1.0 / polygon.vertices.size();
            result.x *= inverse;
            result.y *= inverse;
            return result;
        }

        inline double SideOfDirectedSegment(const Vec2d& start,
                                            const Vec2d& end,
                                            const Vec2d& point)
        {
            return (end.x - start.x) * (point.y - start.y) -
                   (end.y - start.y) * (point.x - start.x);
        }
    }

    inline CompileResult CompileCastleMapToLegacy(
        const std::filesystem::path& projectRoot,
        const CastleProjectDef& project,
        const InteriorMapDef& map)
    {
        using json = nlohmann::json;
        CompileResult result;
        result.validation = ValidateCastleMap(project, map);
        if (!result.validation.ok())
        {
            result.error = result.validation.summary();
            return result;
        }

        std::vector<SectorDef> runtimeSectors = map.sectors;
        std::unordered_map<std::string, std::vector<std::string>> stairSectorIds;
        std::vector<detail::CompiledPolygon> compiledPolygons;
        std::vector<detail::CompiledWallSegment> compiledStairWalls;
        std::unordered_map<std::string, std::size_t> basePolygonByRoom;

        if (map.geometryMode == "polygon")
        {
            compiledPolygons.reserve(map.polygonRooms.size() + map.stairs.size() * 8u);
            for (const PolygonRoomDef& room : map.polygonRooms)
            {
                detail::CompiledPolygon polygon;
                polygon.id = room.id;
                polygon.sectorId = room.id;
                polygon.wallMaterialId = room.boundaryMaterialId;
                polygon.wallAmbient = room.ambient;
                polygon.vertices = room.polygon;
                basePolygonByRoom[room.id] = compiledPolygons.size();
                compiledPolygons.push_back(std::move(polygon));
            }
        }

        // Polygon maps compile both local and floor-transition stairs as real
        // oriented treads. The transition itself is attached to a marker at
        // the top; the player no longer walks through a door into a dead room.
        for (const StairDef& stair : map.stairs)
        {
            const bool compileStair = stair.compileGeometry ||
                (map.geometryMode == "polygon" && stair.kind == "map_transition");
            if (!compileStair)
                continue;

            const SectorDef* baseSector = detail::FindSector(runtimeSectors, stair.roomId);
            if (!baseSector)
                continue;

            // Keep a stable copy. Pushing generated steps into runtimeSectors can
            // reallocate the vector and invalidate a pointer to one of its items.
            const SectorDef base = *baseSector;
            const int stepCount = std::clamp(
                stair.steps, 1, map.geometryMode == "polygon" ? 32 : 7);
            auto& ids = stairSectorIds[stair.id];
            std::vector<double> stepFloors;
            std::vector<double> stepCeilings;
            stepFloors.reserve(static_cast<std::size_t>(stepCount));
            stepCeilings.reserve(static_cast<std::size_t>(stepCount));
            for (int i = 0; i < stepCount; ++i)
            {
                SectorDef step = base;
                step.id = stair.id + "_step_" + std::to_string(i + 1);
                step.displayName = base.displayName + " – stupeň " + std::to_string(i + 1);
                // V23: the first generated polygon is a real first tread, not a zero-height
                // duplicate of the lower landing. This also gives the first side support
                // and the first riser a non-zero visible height.
                const double t = stepCount <= 1 ? 1.0 :
                    static_cast<double>(i + 1) / stepCount;
                step.floor.height = stair.startHeight + (stair.endHeight - stair.startHeight) * t;
                if (!stair.floorMaterialId.empty())
                    step.floor.materialId = stair.floorMaterialId;
                if (!stair.boundaryMaterialId.empty())
                    step.boundaryMaterialId = stair.boundaryMaterialId;
                step.ceiling.height = std::max(base.ceiling.height, step.floor.height + 2.25);
                step.floor.slopeX = 0.0;
                step.floor.slopeY = 0.0;
                // Stair polygons supply low side faces and risers. Keeping the
                // generated wall height close to one rise avoids the old
                // floor-to-ceiling slabs between individual treads.
                const double rise = stepCount <= 1 ? 0.0 :
                    std::abs(stair.endHeight - stair.startHeight) / (stepCount - 1);
                const Vec2d stepMid = detail::Add(
                    stair.start,
                    detail::Scale(
                        detail::Subtract(stair.end, stair.start),
                        (static_cast<double>(i) + 0.5) / stepCount));
                const double surroundingFloor =
                    detail::PlaneHeight(base.floor, stepMid);
                // A stair cut into a raised rock floor needs a retaining face
                // beside its lower treads.  Using only one riser as the wall
                // height left the sides as disconnected 23 cm strips with
                // open gaps above them.
                step.wallHeight = std::max(
                    0.12, std::max(rise, surroundingFloor - step.floor.height));
                stepFloors.push_back(step.floor.height);
                stepCeilings.push_back(step.ceiling.height);
                ids.push_back(step.id);
                runtimeSectors.push_back(std::move(step));
            }

            if (map.geometryMode == "polygon")
            {
                const Vec2d run = detail::Subtract(stair.end, stair.start);
                const double runLength = detail::Length(run);
                if (runLength > 0.05)
                {
                    const Vec2d direction = detail::Normalize(run);
                    const Vec2d side{-direction.y, direction.x};
                    const double halfWidth = std::max(0.50, stair.width * 0.5);

                    for (int i = 0; i < stepCount; ++i)
                    {
                        const double t0 = static_cast<double>(i) / stepCount;
                        const double t1 = static_cast<double>(i + 1) / stepCount;
                        const Vec2d start = detail::Add(
                            stair.start, detail::Scale(run, t0));
                        const Vec2d end = detail::Add(
                            stair.start, detail::Scale(run, t1));

                        detail::CompiledPolygon polygon;
                        polygon.id = stair.id + "_polygon_" + std::to_string(i + 1);
                        polygon.sectorId = ids[static_cast<std::size_t>(i)];
                        polygon.wallMaterialId = !stair.boundaryMaterialId.empty()
                            ? stair.boundaryMaterialId : base.boundaryMaterialId;
                        polygon.wallAmbient = base.ambient;
                        // Stair blocks provide their own exact support faces.
                        // Generic polygon boundary walls start at the tread
                        // height, which made the sides look like disconnected
                        // strips instead of a solid medieval stair.
                        polygon.boundarySolid = false;
                        // Only a staircase cut into an already raised floor
                        // needs to punch a hole through its owning room. The
                        // two internal stairs start at the room floor and must
                        // leave that floor below their solid masonry body.
                        polygon.cutsUnderlyingFloor =
                            base.floor.height >
                            std::min(stair.startHeight, stair.endHeight) + 0.05;
                        polygon.vertices = {
                            detail::Add(start, detail::Scale(side, -halfWidth)),
                            detail::Add(start, detail::Scale(side, halfWidth)),
                            detail::Add(end, detail::Scale(side, halfWidth)),
                            detail::Add(end, detail::Scale(side, -halfWidth))
                        };

                        const double currentFloor =
                            stepFloors[static_cast<std::size_t>(i)];
                        const double currentCeiling =
                            stepCeilings[static_cast<std::size_t>(i)];

                        const double supportBottom =
                            std::min(stair.startHeight, base.floor.height);
                        polygon.hasSupportBottom =
                            polygon.cutsUnderlyingFloor;
                        polygon.supportBottom = supportBottom;
                        const std::string supportMaterial =
                            !stair.boundaryMaterialId.empty()
                                ? stair.boundaryMaterialId
                                : base.boundaryMaterialId;
                        const Vec2d leftStart =
                            detail::Add(start, detail::Scale(side, -halfWidth));
                        const Vec2d leftEnd =
                            detail::Add(end, detail::Scale(side, -halfWidth));
                        const Vec2d rightStart =
                            detail::Add(start, detail::Scale(side, halfWidth));
                        const Vec2d rightEnd =
                            detail::Add(end, detail::Scale(side, halfWidth));
                        // V23: close every tread as a real masonry block. The old
                        // implementation skipped the first riser completely, leaving a black
                        // opening below the staircase when viewed through the doorway.
                        const double previousFloor = i > 0
                            ? stepFloors[static_cast<std::size_t>(i - 1)]
                            : supportBottom;
                        if (currentFloor > previousFloor + 0.015)
                        {
                            detail::CompiledWallSegment riser;
                            riser.id = stair.id + "_riser_" +
                                std::to_string(i + 1);
                            riser.start = leftStart;
                            riser.end = rightStart;
                            riser.bottom = previousFloor;
                            riser.top = currentFloor;
                            riser.materialId = supportMaterial;
                            riser.ambient = base.ambient;
                            riser.textureScale = 1.0;
                            riser.textureUOffset = 0.0;
                            riser.worldAlignedTexture = true;
                            riser.solid = false;
                            riser.twoSided = true;
                            compiledStairWalls.push_back(std::move(riser));
                        }

                        // V23: explicit support faces for each tread. A single wall with a
                        // sampled top_profile proved fragile in the ray renderer and could
                        // disappear at doorway angles. These short, exact segments form two
                        // guaranteed closed stair cheeks from the base to the tread height.
                        if (currentFloor > supportBottom + 0.015)
                        {
                            const double runOffset =
                                runLength * static_cast<double>(i) / stepCount;

                            detail::CompiledWallSegment leftSupport;
                            leftSupport.id = stair.id + "_left_support_" +
                                std::to_string(i + 1);
                            leftSupport.start = leftStart;
                            leftSupport.end = leftEnd;
                            leftSupport.bottom = supportBottom;
                            leftSupport.top = currentFloor;
                            leftSupport.materialId = supportMaterial;
                            leftSupport.ambient = base.ambient;
                            leftSupport.textureScale = 1.0;
                            leftSupport.textureUOffset = runOffset;
                            leftSupport.worldAlignedTexture = true;
                            leftSupport.solid = true;
                            leftSupport.twoSided = true;
                            compiledStairWalls.push_back(std::move(leftSupport));

                            detail::CompiledWallSegment rightSupport;
                            rightSupport.id = stair.id + "_right_support_" +
                                std::to_string(i + 1);
                            rightSupport.start = rightEnd;
                            rightSupport.end = rightStart;
                            rightSupport.bottom = supportBottom;
                            rightSupport.top = currentFloor;
                            rightSupport.materialId = supportMaterial;
                            rightSupport.ambient = base.ambient;
                            rightSupport.textureScale = 1.0;
                            rightSupport.textureUOffset = runOffset;
                            rightSupport.worldAlignedTexture = true;
                            rightSupport.solid = true;
                            rightSupport.twoSided = true;
                            compiledStairWalls.push_back(std::move(rightSupport));
                        }
                        if (i + 1 == stepCount &&
                            currentFloor > supportBottom + 0.015)
                        {
                            // Close the volume beneath the final tread. The
                            // old top edge was a full-height portal, so looking
                            // back from the lower landing exposed a black hole
                            // beneath an otherwise solid staircase.
                            detail::CompiledWallSegment endSupport;
                            endSupport.id =
                                stair.id + "_end_support";
                            endSupport.start = leftEnd;
                            endSupport.end = rightEnd;
                            endSupport.bottom = supportBottom;
                            endSupport.top = currentFloor;
                            endSupport.materialId = supportMaterial;
                            endSupport.ambient = base.ambient;
                            endSupport.textureScale = 1.0;
                            endSupport.textureUOffset = 0.0;
                            endSupport.worldAlignedTexture = true;
                            endSupport.solid = false;
                            endSupport.twoSided = true;
                            compiledStairWalls.push_back(
                                std::move(endSupport));
                        }

                        // The portal between two treads spans from the higher
                        // floor to the lower of their real ceilings.  Using a
                        // fixed 2.25 m above every individual tread made the
                        // portal top descend together with the stairs.  The
                        // renderer then treated those artificial tops as a
                        // staircase of hanging lintels/ceilings.
                        double previousPortalFloor = currentFloor;
                        double previousPortalCeiling = currentCeiling;
                        if (i > 0)
                        {
                            previousPortalFloor =
                                stepFloors[static_cast<std::size_t>(i - 1)];
                            previousPortalCeiling =
                                stepCeilings[static_cast<std::size_t>(i - 1)];
                        }
                        const double previousBottom =
                            std::max(currentFloor, previousPortalFloor);
                        const double previousTop =
                            std::min(currentCeiling, previousPortalCeiling);
                        polygon.openings.push_back({
                            0, 0.0, 1.0,
                            std::max(0.0, previousBottom - currentFloor),
                            std::max(0.10, previousTop - previousBottom)
                        });
                        if (i + 1 < stepCount)
                        {
                            const double nextFloor =
                                stepFloors[static_cast<std::size_t>(i + 1)];
                            const double nextCeiling =
                                stepCeilings[static_cast<std::size_t>(i + 1)];
                            const double nextBottom =
                                std::max(currentFloor, nextFloor);
                            const double nextTop =
                                std::min(currentCeiling, nextCeiling);
                            polygon.openings.push_back({
                                2, 0.0, 1.0,
                                std::max(0.0, nextBottom - currentFloor),
                                std::max(0.10, nextTop - nextBottom)
                            });
                        }
                        else
                        {
                            polygon.openings.push_back({
                                2, 0.0, 1.0, 0.0,
                                std::max(0.10, currentCeiling - currentFloor)
                            });
                        }

                        // The long sides of a generated tread are not room
                        // boundaries.  They open into the stair's owning room.
                        // Below the surrounding room floor the remaining sill
                        // becomes a continuous retaining wall; above it the
                        // portal prevents the old floating, serrated side
                        // slabs and keeps sector/ceiling traversal connected.
                        const Vec2d treadMid = detail::Add(
                            start, detail::Scale(detail::Subtract(end, start), 0.5));
                        const double surroundingFloor =
                            detail::PlaneHeight(base.floor, treadMid);
                        const double surroundingCeiling =
                            detail::PlaneHeight(base.ceiling, treadMid);
                        const double sideBottom =
                            std::max(currentFloor, surroundingFloor);
                        const double sideTop =
                            std::min(currentCeiling, surroundingCeiling);
                        if (sideTop > sideBottom + 0.10)
                        {
                            const detail::CompiledOpening sideOpening{
                                1, 0.0, 1.0,
                                std::max(0.0, sideBottom - currentFloor),
                                sideTop - sideBottom
                            };
                            polygon.openings.push_back(sideOpening);
                            detail::CompiledOpening oppositeSide = sideOpening;
                            oppositeSide.edge = 3;
                            polygon.openings.push_back(oppositeSide);
                        }
                        compiledPolygons.push_back(std::move(polygon));
                    }

                    // V23: continuous top-profile cheeks removed. The renderer could
                    // drop or mis-sample them at shallow doorway angles. Exact per-tread
                    // support segments are emitted in the loop above instead.
                }
            }
        }

        const std::string sectorPool =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        if (runtimeSectors.size() > sectorPool.size())
        {
            result.error = "Runtime renderer supports at most " +
                std::to_string(sectorPool.size()) +
                " sectors after stair compilation.";
            return result;
        }

        std::unordered_map<std::string, RuntimeTextureKey> materialKeys;

        // Keys 1..255 stay available for old maps; generated materials start at
        // 256 so the compiled file is clearly using the V26 uint16 format.
        RuntimeTextureKey nextMaterialKey = 256;
        if (!map.skyMaterialId.empty())
            materialKeys[map.skyMaterialId] = static_cast<RuntimeTextureKey>('K');

        std::vector<std::string> materialIds;
        materialIds.reserve(project.materials.size());
        for (const auto& pair : project.materials)
            materialIds.push_back(pair.first);
        std::sort(materialIds.begin(), materialIds.end());

        for (const std::string& materialId : materialIds)
        {
            if (materialKeys.find(materialId) != materialKeys.end())
                continue;
            while (nextMaterialKey != 0)
            {
                bool used = false;
                for (const auto& pair : materialKeys)
                    if (pair.second == nextMaterialKey) { used = true; break; }
                if (!used) break;
                ++nextMaterialKey;
            }
            if (nextMaterialKey == 0)
            {
                result.error = "Too many materials for uint16 runtime texture keys (maximum 65535).";
                return result;
            }
            materialKeys[materialId] = nextMaterialKey++;
        }

        std::unordered_map<std::string, char> sectorChars;
        for (std::size_t i = 0; i < runtimeSectors.size(); ++i)
            sectorChars[runtimeSectors[i].id] = sectorPool[i];

        const RuntimeTextureKey defaultSolid = materialKeys.at(map.defaultSolidMaterialId);
        const char defaultSector = sectorChars.at(map.defaultSectorId);
        std::vector<std::vector<RuntimeTextureKey>> grid(
            static_cast<std::size_t>(map.height),
            std::vector<RuntimeTextureKey>(static_cast<std::size_t>(map.width), defaultSolid));
        std::vector<std::string> sectorGrid(
            static_cast<std::size_t>(map.height),
            std::string(static_cast<std::size_t>(map.width), defaultSector));

        if (map.geometryMode == "polygon")
        {
            // Cell-center rasterization preserves the polygon source data while
            // letting the current stable Build-style renderer run it today.
            for (const PolygonRoomDef& room : map.polygonRooms)
            {
                const char sectorChar = sectorChars.at(room.id);
                for (int y = 0; y < map.height; ++y)
                {
                    for (int x = 0; x < map.width; ++x)
                    {
                        if (PointInPolygon(room.polygon, x + 0.5, y + 0.5))
                        {
                            grid[y][x] = kNoRuntimeTexture;
                            sectorGrid[y][x] = sectorChar;
                        }
                    }
                }
            }
        }
        else
        {
            for (const RasterRegion& region : map.walkableRegions)
            {
                detail::FillRect(grid, region.rect, kNoRuntimeTexture);
                detail::FillRect(sectorGrid, region.rect, sectorChars.at(region.sectorId));
            }
        }

        for (const SolidRegion& region : map.solidRegions)
        {
            detail::FillRect(grid, region.rect, materialKeys.at(region.materialId));
            if (!region.sectorId.empty())
                detail::FillRect(sectorGrid, region.rect, sectorChars.at(region.sectorId));
        }

        for (const CellOverride& cell : map.cellOverrides)
        {
            if (!detail::InBounds(cell.x, cell.y, map.width, map.height))
                continue;
            grid[cell.y][cell.x] = cell.solid
                ? materialKeys.at(cell.materialId.empty() ? map.defaultSolidMaterialId : cell.materialId)
                : kNoRuntimeTexture;
            if (!cell.sectorId.empty())
                sectorGrid[cell.y][cell.x] = sectorChars.at(cell.sectorId);
        }

        // Rasterize short segmented stair runs after rooms so their height
        // sectors take priority over the destination room floor.
        for (const StairDef& stair : map.stairs)
        {
            const auto idsIt = stairSectorIds.find(stair.id);
            if (idsIt == stairSectorIds.end() || idsIt->second.empty())
                continue;

            const auto& ids = idsIt->second;
            const double dx = stair.end.x - stair.start.x;
            const double dy = stair.end.y - stair.start.y;
            const double length = std::max(0.001, std::sqrt(dx * dx + dy * dy));
            const double px = -dy / length;
            const double py = dx / length;
            const int samples = std::max(8, static_cast<int>(std::ceil(length * 8.0)));

            for (int sample = 0; sample <= samples; ++sample)
            {
                const double t = static_cast<double>(sample) / samples;
                const int stepIndex = std::clamp(
                    static_cast<int>(std::floor(t * ids.size())),
                    0, static_cast<int>(ids.size()) - 1);
                const char sectorChar = sectorChars.at(ids[stepIndex]);
                const double cx = stair.start.x + dx * t;
                const double cy = stair.start.y + dy * t;
                for (int w = -(stair.width - 1) / 2; w <= stair.width / 2; ++w)
                {
                    const int x = static_cast<int>(std::floor(cx + px * w));
                    const int y = static_cast<int>(std::floor(cy + py * w));
                    if (!detail::InBounds(x, y, map.width, map.height))
                        continue;
                    grid[y][x] = kNoRuntimeTexture;
                    sectorGrid[y][x] = sectorChar;
                }
            }
        }

        std::vector<DoorDef> runtimeDoors = map.doors;
        json compiledWallSegments = json::array();
        for (const detail::CompiledWallSegment& wall : compiledStairWalls)
        {
            const auto materialIt = materialKeys.find(wall.materialId);
            const RuntimeTextureKey texture = materialIt != materialKeys.end()
                ? materialIt->second : defaultSolid;
            compiledWallSegments.push_back({
                {"id", wall.id},
                {"x0", wall.start.x},
                {"y0", wall.start.y},
                {"x1", wall.end.x},
                {"y1", wall.end.y},
                {"bottom_z", wall.bottom},
                {"top_z", wall.top},
                {"bottom_z_end", std::isfinite(wall.bottomEnd)
                    ? wall.bottomEnd : wall.bottom},
                {"top_z_end", std::isfinite(wall.topEnd)
                    ? wall.topEnd : wall.top},
                {"texture_key", texture},
                {"ambient", wall.ambient},
                {"texture_scale", wall.textureScale},
                {"texture_u_offset", wall.textureUOffset},
                {"world_aligned_texture", wall.worldAlignedTexture},
                {"solid", wall.solid},
                {"two_sided", wall.twoSided}
            });
            if (!wall.topProfile.empty())
                compiledWallSegments.back()["top_profile"] =
                    wall.topProfile;
        }
        auto compiledPolygonForRoom = [&](const std::string& roomId)
            -> detail::CompiledPolygon*
        {
            const auto found = basePolygonByRoom.find(roomId);
            if (found == basePolygonByRoom.end() || found->second >= compiledPolygons.size())
                return nullptr;
            return &compiledPolygons[found->second];
        };

        for (const WallFeatureDef& feature : map.wallFeatures)
        {
            const auto authoredSegment = detail::AxisFeatureSegment(feature);
            const Vec2d authoredMid{
                (authoredSegment.first.x + authoredSegment.second.x) * 0.5,
                (authoredSegment.first.y + authoredSegment.second.y) * 0.5
            };
            const bool ordinaryDoor = feature.type == "door";
            const double featureScale = ordinaryDoor ? map.doorScale : 1.0;
            const double featureWidth = std::max(
                0.35, (feature.width > 0.0 ? feature.width
                    : detail::Length(detail::Subtract(
                        authoredSegment.second, authoredSegment.first))) * featureScale);
            const double featureHeight = feature.height > 0.0
                ? feature.height * featureScale
                : feature.height;
            auto compiledDoorFromFeature = [&]() {
                DoorDef door = detail::DoorFromFeature(feature);
                door.height = featureHeight;
                if (ordinaryDoor && !map.defaultDoorMaterialId.empty())
                    door.materialId = map.defaultDoorMaterialId;
                return door;
            };

            detail::CompiledPolygon* sourcePolygon = compiledPolygonForRoom(feature.roomId);
            detail::CompiledPolygon* targetPolygon = compiledPolygonForRoom(feature.otherRoomId);
            const detail::BoundaryHit sourceHit = sourcePolygon
                ? detail::ClosestBoundaryHit(*sourcePolygon, authoredMid)
                : detail::BoundaryHit{};
            const detail::BoundaryHit targetHit = targetPolygon
                ? detail::ClosestBoundaryHit(*targetPolygon, authoredMid)
                : detail::BoundaryHit{};
            const SectorDef* sourceSector = detail::FindSector(runtimeSectors, feature.roomId);
            const SectorDef* targetSector = detail::FindSector(runtimeSectors, feature.otherRoomId);

            const bool passable = feature.type == "door" ||
                                  feature.type == "map_portal" ||
                                  feature.type == "open_portal";
            if (map.geometryMode == "polygon" && passable)
            {
                const double sourceFloor = sourceSector && sourceHit.valid
                    ? detail::PlaneHeight(sourceSector->floor, sourceHit.point) : 0.0;
                const double targetFloor = targetSector && targetHit.valid
                    ? detail::PlaneHeight(targetSector->floor, targetHit.point) : sourceFloor;
                const double sourceCeiling = sourceSector && sourceHit.valid
                    ? detail::PlaneHeight(sourceSector->ceiling, sourceHit.point)
                    : sourceFloor + 2.4;
                const double targetCeiling = targetSector && targetHit.valid
                    ? detail::PlaneHeight(targetSector->ceiling, targetHit.point)
                    : sourceCeiling;
                const double openingBottom = std::max(sourceFloor, targetFloor);
                const double naturalTop = std::min(sourceCeiling, targetCeiling);
                const double openingTop = std::max(
                    openingBottom + 0.35,
                    featureHeight > 0.0
                        ? std::min(naturalTop, openingBottom + featureHeight)
                        : naturalTop);
                const double sourceOpeningBottom =
                    feature.compileConnector ? openingBottom : sourceFloor;
                const double targetOpeningBottom =
                    feature.compileConnector ? openingBottom : targetFloor;
                const double sourceOpeningTop = feature.compileConnector
                    ? openingTop
                    : std::min(sourceCeiling, sourceOpeningBottom +
                        (featureHeight > 0.0 ? featureHeight
                                             : sourceCeiling - sourceOpeningBottom));
                const double targetOpeningTop = feature.compileConnector
                    ? openingTop
                    : std::min(targetCeiling, targetOpeningBottom +
                        (featureHeight > 0.0 ? featureHeight
                                             : targetCeiling - targetOpeningBottom));

                if (sourcePolygon && sourceHit.valid && sourceHit.distance <= 2.25)
                    detail::AddOpeningAtBoundary(
                        *sourcePolygon, sourceHit, featureWidth,
                        sourceOpeningBottom, sourceOpeningTop, sourceFloor);
                if (targetPolygon && targetHit.valid && targetHit.distance <= 2.25)
                    detail::AddOpeningAtBoundary(
                        *targetPolygon, targetHit, featureWidth,
                        targetOpeningBottom, targetOpeningTop, targetFloor);

                Vec2d leafStart = authoredSegment.first;
                Vec2d leafEnd = authoredSegment.second;
                if (feature.hasSegment)
                {
                    const Vec2d authoredDelta =
                        detail::Subtract(authoredSegment.second, authoredSegment.first);
                    const double authoredLength = detail::Length(authoredDelta);
                    if (authoredLength > 0.05)
                    {
                        const Vec2d tangent =
                            detail::Scale(authoredDelta, 1.0 / authoredLength);
                        leafStart = detail::Add(
                            authoredMid, detail::Scale(tangent, -featureWidth * 0.5));
                        leafEnd = detail::Add(
                            authoredMid, detail::Scale(tangent, featureWidth * 0.5));
                    }
                }
                if (!feature.hasSegment && sourceHit.valid)
                {
                    Vec2d tangent = sourceHit.tangent;
                    if (targetHit.valid && detail::Dot(tangent, targetHit.tangent) < 0.0)
                        tangent = detail::Scale(tangent, -1.0);
                    leafStart = detail::Add(sourceHit.point, detail::Scale(tangent, -featureWidth * 0.5));
                    leafEnd = detail::Add(sourceHit.point, detail::Scale(tangent, featureWidth * 0.5));
                }

                if (feature.compileConnector && sourceHit.valid && targetHit.valid)
                {
                    Vec2d sourceTangent = sourceHit.tangent;
                    Vec2d targetTangent = targetHit.tangent;
                    if (detail::Dot(sourceTangent, targetTangent) < 0.0)
                        targetTangent = detail::Scale(targetTangent, -1.0);
                    const double connectionLength = detail::Length(
                        detail::Subtract(targetHit.point, sourceHit.point));

                    if (connectionLength > 0.05)
                    {
                        const Vec2d sourceA = detail::Add(
                            sourceHit.point, detail::Scale(sourceTangent, -featureWidth * 0.5));
                        const Vec2d sourceB = detail::Add(
                            sourceHit.point, detail::Scale(sourceTangent, featureWidth * 0.5));
                        const Vec2d targetA = detail::Add(
                            targetHit.point, detail::Scale(targetTangent, -featureWidth * 0.5));
                        const Vec2d targetB = detail::Add(
                            targetHit.point, detail::Scale(targetTangent, featureWidth * 0.5));

                        detail::CompiledPolygon connector;
                        connector.id = feature.id + "_connector";
                        connector.sectorId = targetSector ? feature.otherRoomId : feature.roomId;
                        const SectorDef* connectorSector = targetSector ? targetSector : sourceSector;
                        connector.wallMaterialId = connectorSector
                            ? connectorSector->boundaryMaterialId : map.defaultSolidMaterialId;
                        connector.wallAmbient = connectorSector ? connectorSector->ambient : 0.75;
                        connector.vertices = {sourceA, sourceB, targetB, targetA};
                        const Vec2d connectorMid{
                            (sourceHit.point.x + targetHit.point.x) * 0.5,
                            (sourceHit.point.y + targetHit.point.y) * 0.5
                        };
                        const double connectorFloor = connectorSector
                            ? detail::PlaneHeight(connectorSector->floor, connectorMid)
                            : openingBottom;
                        connector.openings.push_back({
                            0, 0.0, 1.0,
                            std::max(0.0, openingBottom - connectorFloor),
                            openingTop - openingBottom
                        });
                        connector.openings.push_back({
                            2, 0.0, 1.0,
                            std::max(0.0, openingBottom - connectorFloor),
                            openingTop - openingBottom
                        });
                        compiledPolygons.push_back(std::move(connector));

                        // A closed door belongs in the authored/source wall
                        // plane.  Placing it halfway through a thick connector
                        // made stair-room doors float half a cell away from the
                        // facade and appear rotated against the masonry.
                        if (!feature.hasSegment)
                        {
                            leafStart = sourceA;
                            leafEnd = sourceB;
                        }
                    }
                }

                if (feature.type == "door" || feature.type == "map_portal")
                {
                    double swingDirection = feature.swingDirection;
                    if (std::abs(swingDirection) < 0.5)
                    {
                        swingDirection = 1.0;
                        if (sourcePolygon && !feature.opensToward.empty())
                        {
                            const double sourceSide = detail::SideOfDirectedSegment(
                                leafStart, leafEnd, detail::PolygonAverage(*sourcePolygon));
                            const bool towardsSource =
                                feature.opensToward == "room" ||
                                feature.opensToward == "source" ||
                                feature.opensToward == "from";
                            swingDirection = sourceSide < 0.0 ? -1.0 : 1.0;
                            if (!towardsSource)
                                swingDirection = -swingDirection;
                        }
                    }
                    swingDirection = swingDirection < 0.0 ? -1.0 : 1.0;

                    const bool doubleSwingDoor =
                        feature.type == "door" &&
                        feature.leaves == 2 &&
                        (feature.motion.empty() || feature.motion == "swing" ||
                         feature.motion == "hinged" || feature.motion == "normal");
                    if (doubleSwingDoor)
                    {
                        const Vec2d middle{
                            (leafStart.x + leafEnd.x) * 0.5,
                            (leafStart.y + leafEnd.y) * 0.5
                        };

                        DoorDef left = compiledDoorFromFeature();
                        left.id = feature.id + "_left";
                        left.interactionGroup = feature.id;
                        left.segmentStart = leafStart;
                        left.segmentEnd = middle;
                        left.hasSegment = detail::Length(
                            detail::Subtract(middle, leafStart)) > 0.05;
                        left.hingeAtEnd = false;
                        left.swingDirection = swingDirection;
                        left.textureU0 = 0.0;
                        left.textureU1 = 0.5;

                        DoorDef right = compiledDoorFromFeature();
                        right.id = feature.id + "_right";
                        right.interactionGroup = feature.id;
                        right.segmentStart = middle;
                        right.segmentEnd = leafEnd;
                        right.hasSegment = detail::Length(
                            detail::Subtract(leafEnd, middle)) > 0.05;
                        right.hingeAtEnd = true;
                        right.swingDirection = -swingDirection;
                        right.textureU0 = 0.5;
                        right.textureU1 = 1.0;

                        runtimeDoors.push_back(std::move(left));
                        runtimeDoors.push_back(std::move(right));
                    }
                    else
                    {
                        DoorDef door = compiledDoorFromFeature();
                        door.segmentStart = leafStart;
                        door.segmentEnd = leafEnd;
                        door.hasSegment = detail::Length(
                            detail::Subtract(leafEnd, leafStart)) > 0.05;
                        door.swingDirection = swingDirection;
                        runtimeDoors.push_back(std::move(door));
                    }
                }
            }

            if (feature.type == "open_portal")
            {
                const std::string targetSector = !feature.otherRoomId.empty() ? feature.otherRoomId : feature.roomId;
                for (const auto& cell : detail::FeatureCells(feature))
                {
                    if (!detail::InBounds(cell.first, cell.second, map.width, map.height))
                        continue;
                    grid[cell.second][cell.first] = '.';
                    const auto sectorIt = sectorChars.find(targetSector);
                    if (sectorIt != sectorChars.end())
                        sectorGrid[cell.second][cell.first] = sectorIt->second;
                }
                continue;
            }

            if (feature.type == "door" || feature.type == "map_portal")
            {
                if (map.geometryMode != "polygon")
                    runtimeDoors.push_back(compiledDoorFromFeature());
                continue;
            }

            if (feature.type == "window" || feature.type == "fireplace" ||
                feature.type == "garderobe" || feature.type == "wall_detail")
            {
                if (feature.materialId.empty())
                    continue;
                const auto materialIt = materialKeys.find(feature.materialId);
                if (materialIt == materialKeys.end())
                    continue;
                for (const auto& cell : detail::FeatureCells(feature))
                {
                    if (!detail::InBounds(cell.first, cell.second, map.width, map.height))
                        continue;
                    grid[cell.second][cell.first] = materialIt->second;
                    const auto sectorIt = sectorChars.find(feature.roomId);
                    if (sectorIt != sectorChars.end())
                        sectorGrid[cell.second][cell.first] = sectorIt->second;
                }

                if (map.geometryMode == "polygon" && sourceHit.valid &&
                    sourceHit.distance <= 2.25 && sourceSector)
                {
                    Vec2d detailStart = detail::Add(
                        sourceHit.point, detail::Scale(sourceHit.tangent, -featureWidth * 0.5));
                    Vec2d detailEnd = detail::Add(
                        sourceHit.point, detail::Scale(sourceHit.tangent, featureWidth * 0.5));
                    // Wall details used to occupy exactly the same plane as
                    // the polygon boundary. Tiny floating-point differences
                    // then let the masonry overwrite most of a fireplace or
                    // window, leaving only a few pixels. Move the decorative
                    // face a couple of centimetres into its owning room.
                    Vec2d inward{
                        -sourceHit.tangent.y,
                        sourceHit.tangent.x
                    };
                    const Vec2d roomCenter =
                        detail::PolygonAverage(*sourcePolygon);
                    if (detail::Dot(
                            inward,
                            detail::Subtract(roomCenter, sourceHit.point)) < 0.0)
                        inward = detail::Scale(inward, -1.0);
                    detailStart = detail::Add(
                        detailStart, detail::Scale(inward, 0.025));
                    detailEnd = detail::Add(
                        detailEnd, detail::Scale(inward, 0.025));
                    const double floor = detail::PlaneHeight(sourceSector->floor, sourceHit.point);
                    const double ceiling = detail::PlaneHeight(sourceSector->ceiling, sourceHit.point);
                    const double bottom = feature.type == "window"
                        ? floor + 0.72 : floor;
                    const double defaultHeight = feature.type == "window" ? 1.35 : 2.05;
                    const double top = std::min(
                        ceiling, bottom + (feature.height > 0.0 ? feature.height : defaultHeight));

                    compiledWallSegments.push_back({
                        {"id", feature.id},
                        {"x0", detailStart.x},
                        {"y0", detailStart.y},
                        {"x1", detailEnd.x},
                        {"y1", detailEnd.y},
                        {"bottom_z", bottom},
                        {"top_z", std::max(bottom + 0.10, top)},
                        {"texture_key", materialIt->second},
                        {"ambient", sourceSector->ambient},
                        {"texture_scale", 1.0},
                        {"solid", true},
                        {"two_sided", true}
                    });
                }
            }
        }

        json root;
        root["id"] = map.id;
        root["name"] = map.displayName;
        root["fov_degrees"] = map.fovDegrees;
        root["eye_height"] = map.eyeHeight;
        root["grid_codes"] = grid;
        root["sector_grid"] = sectorGrid;

        json textures = json::array();
        for (const std::string& materialId : materialIds)
        {
            const auto materialIt = project.materials.find(materialId);
            const auto keyIt = materialKeys.find(materialId);
            if (materialIt == project.materials.end() || keyIt == materialKeys.end())
                continue;
            textures.push_back({
                {"key", keyIt->second},
                {"id", materialId},
                {"path", materialIt->second.texturePath}
            });
        }
        root["textures"] = std::move(textures);

        json sectors = json::object();
        for (const SectorDef& sector : runtimeSectors)
        {
            const char symbol = sectorChars.at(sector.id);
            sectors[std::string(1, symbol)] = {
                {"id", sector.id},
                {"name", sector.displayName},
                {"floor_texture_key", materialKeys.at(sector.floor.materialId)},
                {"ceiling_texture_key", sector.skyCeiling ? static_cast<RuntimeTextureKey>('K') : materialKeys.at(sector.ceiling.materialId)},
                {"boundary_texture_key", materialKeys.at(sector.boundaryMaterialId)},
                {"floor_height", sector.floor.height},
                {"ceiling_height", sector.ceiling.height},
                {"ambient", sector.ambient},
                {"wall_height", sector.wallHeight},
                {"sky_ceiling", sector.skyCeiling},
                {"floor_slope_x", sector.floor.slopeX},
                {"floor_slope_y", sector.floor.slopeY},
                {"ceiling_slope_x", sector.ceiling.slopeX},
                {"ceiling_slope_y", sector.ceiling.slopeY},
                {"slope_origin_x", sector.floor.originX},
                {"slope_origin_y", sector.floor.originY}
            };
        }
        root["sectors"] = sectors;

        if (map.geometryMode == "polygon")
        {
            json polygons = json::array();
            for (const detail::CompiledPolygon& polygon : compiledPolygons)
            {
                const auto sectorIt = sectorChars.find(polygon.sectorId);
                if (sectorIt == sectorChars.end() || polygon.vertices.size() < 3)
                    continue;

                json vertices = json::array();
                for (const Vec2d& vertex : polygon.vertices)
                    vertices.push_back({vertex.x, vertex.y});

                json openings = json::array();
                for (const detail::CompiledOpening& opening : polygon.openings)
                {
                    openings.push_back({
                        {"edge", opening.edge},
                        {"start", opening.start},
                        {"end", opening.end},
                        {"bottom", opening.bottom},
                        {"height", opening.height}
                    });
                }

                RuntimeTextureKey wallTexture = defaultSolid;
                const auto materialIt = materialKeys.find(polygon.wallMaterialId);
                if (materialIt != materialKeys.end())
                    wallTexture = materialIt->second;

                polygons.push_back({
                    {"id", polygon.id},
                    {"sector", std::string(1, sectorIt->second)},
                    {"boundary_solid", polygon.boundarySolid},
                    {"cuts_underlying_floor", polygon.cutsUnderlyingFloor},
                    {"has_support_bottom", polygon.hasSupportBottom},
                    {"support_bottom_z", polygon.supportBottom},
                    {"wall_texture_key", wallTexture},
                    {"wall_ambient", polygon.wallAmbient},
                    {"wall_texture_scale", polygon.wallTextureScale},
                    {"vertices", std::move(vertices)},
                    {"edge_openings", std::move(openings)}
                });
            }
            root["polygon_sectors"] = std::move(polygons);
            root["wall_segments"] = std::move(compiledWallSegments);
        }

        json traversalRamps = json::array();
        for (const StairDef& stair : map.stairs)
        {
            const bool compiledStair = stair.compileGeometry ||
                (map.geometryMode == "polygon" &&
                 stair.kind == "map_transition");
            if (!compiledStair || !stair.smoothTraversal)
                continue;
            traversalRamps.push_back({
                {"id", stair.id},
                {"start", {stair.start.x, stair.start.y}},
                {"end", {stair.end.x, stair.end.y}},
                {"start_height", stair.startHeight},
                {"end_height", stair.endHeight},
                {"width", std::max(1.0, static_cast<double>(stair.width))}
            });
        }
        if (!traversalRamps.empty())
            root["traversal_ramps"] = std::move(traversalRamps);

        json spawns = json::object();
        bool wroteDefaultSpawn = false;
        for (const SpawnDef& spawn : map.spawns)
        {
            json value = {
                {"x", spawn.x},
                {"y", spawn.y},
                {"angle_degrees", spawn.angleDegrees},
                {"pitch", spawn.pitch}
            };
            spawns[spawn.id] = value;
            if (!wroteDefaultSpawn)
            {
                root["spawn"] = value;
                wroteDefaultSpawn = true;
            }
        }
        if (!wroteDefaultSpawn)
            root["spawn"] = {{"x", 1.5}, {"y", 1.5}, {"angle_degrees", 0.0}, {"pitch", 0.0}};
        root["spawns"] = spawns;

        json doors = json::array();
        for (const DoorDef& door : runtimeDoors)
        {
            if (!door.hasSegment)
            {
                for (int i = 0; i < std::max(1, door.span); ++i)
                {
                    const int cellX = door.x + (door.axis == 'x' ? i : 0);
                    const int cellY = door.y + (door.axis == 'y' ? i : 0);
                    if (detail::InBounds(cellX, cellY, map.width, map.height))
                        grid[cellY][cellX] = kNoRuntimeTexture;
                }
            }

            RuntimeTextureKey textureKey = static_cast<RuntimeTextureKey>('D');
            if (!door.materialId.empty())
            {
                const auto materialIt = materialKeys.find(door.materialId);
                if (materialIt != materialKeys.end())
                    textureKey = materialIt->second;
            }

            json compiledDoor = {
                {"id", door.id},
                {"x", door.x},
                {"y", door.y},
                {"locked", door.locked},
                {"open", door.initiallyOpen},
                {"speed", door.speed},
                {"motion", door.motion},
                {"texture_key", textureKey},
                {"span", std::max(1, door.span)},
                {"axis", std::string(1, door.axis)},
                {"height", door.height},
                {"interaction_group", door.interactionGroup.empty() ? door.id : door.interactionGroup},
                {"hinge", door.hingeAtEnd ? "end" : "start"},
                {"swing_direction", door.swingDirection},
                {"swing_degrees", door.swingDegrees},
                // V23: thinner physical leaf. The previous 8 cm edge became a
                // huge black post when the open door was viewed nearly edge-on.
                {"thickness", 0.032},
                {"texture_u0", door.textureU0},
                {"texture_u1", door.textureU1},
                {"target_interior", door.targetLocation},
                {"target_spawn", door.targetSpawn}
            };
            if (door.hasSegment)
            {
                compiledDoor["segment"] = {
                    {door.segmentStart.x, door.segmentStart.y},
                    {door.segmentEnd.x, door.segmentEnd.y}
                };
            }
            doors.push_back(std::move(compiledDoor));
        }
        root["grid_codes"] = grid;
        root["doors"] = doors;

        json sprites = json::array();
        for (const EntityDef& entity : map.entities)
        {
            if (entity.renderMode != "billboard" || entity.materialId.empty())
                continue;
            const auto materialIt = materialKeys.find(entity.materialId);
            if (materialIt == materialKeys.end())
                continue;
            sprites.push_back({
                {"id", entity.id},
                {"texture_key", materialIt->second},
                {"x", entity.x},
                {"y", entity.y},
                {"z_offset", entity.zOffset},
                {"scale", entity.scale},
                {"solid", entity.solid},
                {"interaction_label", entity.interactionLabel},
                {"target_interior", entity.targetLocation},
                {"target_spawn", entity.targetSpawn}
            });
        }
        for (const StairDef& stair : map.stairs)
        {
            if (stair.kind != "map_transition" || stair.targetLocation.empty())
                continue;
            const auto markerMaterial = materialKeys.find("spiral_stair_marker");
            if (markerMaterial == materialKeys.end())
                continue;

            const Vec2d direction = detail::Normalize(
                detail::Subtract(stair.end, stair.start));
            const Vec2d markerPosition = detail::Add(
                stair.end, detail::Scale(direction, -0.35));
            sprites.push_back({
                {"id", stair.id + "_transition"},
                {"texture_key", markerMaterial->second},
                {"x", markerPosition.x},
                {"y", markerPosition.y},
                {"z_offset", 0.0},
                {"scale", 0.75},
                {"solid", false},
                {"render_mode", "billboard"},
                {"interaction_label", "Vystoupat do vyššího patra"},
                {"target_interior", stair.targetLocation},
                {"target_spawn", stair.targetSpawn}
            });
        }
        root["sprites"] = sprites;

        root["source"] = {
            {"castle_id", map.castleId},
            {"map_id", map.id},
            {"schema_version", map.schemaVersion},
            {"geometry_mode", map.geometryMode},
            {"compiled_from_data_driven_map", true},
            {"runtime_texture_key_bits", 16}
        };

        const std::filesystem::path compiledDir =
            projectRoot / "data" / "castles" / map.castleId / ".compiled";
        std::error_code ec;
        std::filesystem::create_directories(compiledDir, ec);
        if (ec)
        {
            result.error = "Cannot create compiled map directory: " + ec.message();
            return result;
        }

        result.outputPath = compiledDir / (map.id + ".json");
        std::ofstream output(result.outputPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            result.error = "Cannot write compiled map: " + result.outputPath.string();
            return result;
        }
        output << root.dump(2);
        result.success = true;
        return result;
    }
}
