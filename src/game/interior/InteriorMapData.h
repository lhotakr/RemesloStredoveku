#pragma once

#include "HistoricalMetadata.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace interior
{
    struct RectI
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    struct Vec2d
    {
        double x = 0.0;
        double y = 0.0;
    };

    struct MaterialDef
    {
        std::string id;
        std::string texturePath;
        std::string kind;
        HistoricalMetadata historical;
    };

    struct PlaneDef
    {
        double height = 0.0;
        double slopeX = 0.0;
        double slopeY = 0.0;
        double originX = 0.0;
        double originY = 0.0;
        std::string materialId;
    };

    struct SectorDef
    {
        std::string id;
        std::string displayName;
        std::string roomId;
        std::string roomFunction;
        PlaneDef floor;
        PlaneDef ceiling;
        std::string boundaryMaterialId;
        double wallHeight = -1.0;
        double ambient = 1.0;
        bool skyCeiling = false;
        bool npcAccess = true;
        HistoricalMetadata historical;
    };

    struct PolygonRoomDef
    {
        std::string id;
        std::string label;
        std::string displayName;
        std::string roomFunction;
        std::vector<Vec2d> polygon;
        PlaneDef floor;
        PlaneDef ceiling;
        std::string boundaryMaterialId;
        double wallHeight = -1.0;
        double ambient = 1.0;
        bool skyCeiling = false;
        bool npcAccess = true;
        std::string notes;
        HistoricalMetadata historical;
    };

    struct WallFeatureDef
    {
        std::string id;
        std::string type;
        std::string roomId;
        std::string otherRoomId;
        std::string side;
        int x = 0;
        int y = 0;
        int span = 1;
        char axis = 'x';
        double width = 1.0;
        double height = -1.0;
        std::string shape;
        std::string materialId;
        std::string motion;
        bool locked = false;
        bool initiallyOpen = false;
        double speed = 1.8;
        bool interactive = false;
        std::string interactionLabel;
        std::string targetLocation;
        std::string targetSpawn;
        Vec2d position;
        bool hasPosition = false;
        Vec2d segmentStart;
        Vec2d segmentEnd;
        bool hasSegment = false;
        // Swinging entrances can be authored as a true pair of leaves.  The
        // compiler expands them to two runtime doors that share one
        // interaction group, so pressing use opens/closes both at once.
        int leaves = 1;
        std::string hinge = "start";
        std::string opensToward;
        double swingDirection = 0.0;
        double swingDegrees = 90.0;
        // Polygon maps normally compile a small threshold/corridor between
        // the two room boundaries. Stairs and other custom geometry can own
        // that connection themselves and disable the generated threshold.
        bool compileConnector = true;
        HistoricalMetadata historical;
    };

    struct StairDef
    {
        std::string id;
        std::string roomId;
        std::string kind;
        Vec2d start;
        Vec2d end;
        double startHeight = 0.0;
        double endHeight = 0.0;
        int steps = 1;
        int width = 1;
        bool compileGeometry = false;
        // Keep visible treads discrete while collision follows the continuous
        // walking line. This avoids a camera jolt on every generated sector.
        bool smoothTraversal = true;
        std::string floorMaterialId;
        std::string boundaryMaterialId;
        std::string targetLocation;
        std::string targetSpawn;
        HistoricalMetadata historical;
    };

    struct RasterRegion
    {
        std::string sectorId;
        RectI rect;
    };

    struct SolidRegion
    {
        std::string materialId;
        std::string sectorId;
        RectI rect;
    };

    struct CellOverride
    {
        int x = 0;
        int y = 0;
        bool solid = false;
        std::string materialId;
        std::string sectorId;
    };

    struct SpawnDef
    {
        std::string id;
        std::string roomId;
        double x = 0.0;
        double y = 0.0;
        double angleDegrees = 0.0;
        double pitch = 0.0;
    };

    struct DoorDef
    {
        std::string id;
        int x = 0;
        int y = 0;
        bool locked = false;
        bool initiallyOpen = false;
        double speed = 1.8;
        std::string materialId;
        std::string motion = "slide";
        int span = 1;
        char axis = 'x';
        double height = -1.0;
        Vec2d segmentStart;
        Vec2d segmentEnd;
        bool hasSegment = false;
        std::string interactionGroup;
        bool hingeAtEnd = false;
        double swingDirection = 1.0;
        double swingDegrees = 90.0;
        double textureU0 = 0.0;
        double textureU1 = 1.0;
        std::string targetLocation;
        std::string targetSpawn;
        HistoricalMetadata historical;
    };

    struct EntityDef
    {
        std::string id;
        std::string prototypeId;
        std::string materialId;
        std::string renderMode = "billboard";
        double x = 0.0;
        double y = 0.0;
        double zOffset = 0.0;
        double scale = 1.0;
        bool solid = false;
        std::string interactionLabel;
        std::string targetLocation;
        std::string targetSpawn;
        HistoricalMetadata historical;
    };

    struct LightDef
    {
        std::string id;
        std::string type;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double radius = 0.0;
        double intensity = 1.0;
        bool npcAccess = true;
        HistoricalMetadata historical;
    };

    struct InteriorMapDef
    {
        int schemaVersion = 1;
        std::string geometryMode = "raster";
        std::string castleId;
        std::string id;
        std::string displayName;
        std::string mapKind = "interior_25d";
        int width = 0;
        int height = 0;
        double cellSize = 1.0;
        double fovDegrees = 72.0;
        double eyeHeight = 0.56;
        // Uniform authoring scale for ordinary room doors. Transition gates
        // remain full-size. This keeps one coherent door style/size across a
        // castle map without duplicating values on every feature.
        double doorScale = 1.0;
        std::string defaultDoorMaterialId;
        std::string defaultSolidMaterialId;
        std::string defaultSectorId;
        std::string skyMaterialId;

        std::vector<SectorDef> sectors;
        std::vector<PolygonRoomDef> polygonRooms;
        std::vector<WallFeatureDef> wallFeatures;
        std::vector<StairDef> stairs;
        std::unordered_map<std::string, std::vector<std::string>> roomGraph;

        std::vector<RasterRegion> walkableRegions;
        std::vector<SolidRegion> solidRegions;
        std::vector<CellOverride> cellOverrides;
        std::vector<DoorDef> doors;
        std::vector<EntityDef> entities;
        std::vector<LightDef> lights;
        std::vector<SpawnDef> spawns;

        HistoricalMetadata historical;
    };

    struct CastleProjectDef
    {
        int schemaVersion = 1;
        std::string id;
        std::string displayName;
        std::string defaultMap;
        std::vector<std::string> maps;
        std::unordered_map<std::string, MaterialDef> materials;
    };

    struct CastleLocationRef
    {
        std::string castleId;
        std::string mapId;
    };
}
