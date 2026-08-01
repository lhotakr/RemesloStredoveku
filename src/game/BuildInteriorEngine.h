#pragma once

#include <SDL.h>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "interior/TextureSpriteEditor.h"

// Build-inspired 2.5D interior renderer for Remeslo stredoveku.
// It intentionally uses original code/data and does not copy Build Engine code.
//
// Features in this version:
// - polygon-authored sectors with arbitrary XY vertices and independent floor/ceiling height,
// - portal-style rendering of height transitions between sectors,
// - textured floors and ceilings,
// - hinged, sliding and raising doors plus static transition portals,
// - voxelized 3D objects with billboard fallback and map transitions,
// - built-in ImGui editor for polygons, sectors, doors and objects, with legacy grid compatibility.
class BuildInteriorEngine
{
public:
    BuildInteriorEngine() = default;
    ~BuildInteriorEngine();

    bool init(SDL_Window* window, SDL_Renderer* renderer);
    void shutdown();

    void handleEvent(const SDL_Event& e);
    void update(float dt);
    void render();

    bool loadInterior(const std::string& interiorIdOrPath);
    bool saveInterior(const std::string& interiorIdOrPath = "");

    void setEditorMode(bool enabled);
    bool editorMode() const { return m_editorMode; }

    const std::string& lastError() const { return m_lastError; }
    const std::string& currentInteriorId() const { return m_currentInteriorId; }

private:
    using TextureKey = std::uint16_t;
    static constexpr TextureKey kNoTexture = 0;
    static constexpr TextureKey kLegacyTextureBase = 1;

    struct VoxelCell
    {
        float x0 = 0.0f;
        float x1 = 0.0f;
        float z0 = 0.0f;
        float z1 = 0.0f;
        std::uint32_t color = 0xffffffffu;
        std::uint8_t exposed = 0u;
    };

    struct TextureFrame
    {
        int w = 0;
        int h = 0;
        std::vector<std::uint32_t> pixels;
        std::vector<VoxelCell> voxelCells;
        int voxelGridW = 0;
        int voxelGridH = 0;
    };

    struct TextureRef
    {
        SDL_Texture* texture = nullptr;
        int w = 0;
        int h = 0;
        std::vector<std::uint32_t> pixels;
        std::vector<VoxelCell> voxelCells;
        int voxelGridW = 0;
        int voxelGridH = 0;
        std::vector<TextureFrame> animationFrames;
        std::string path;
    };

    struct SectorDef
    {
        char symbol = 'A';
        std::string id = "main";
        std::string name = "Main sector";
        TextureKey floorTexture = static_cast<TextureKey>('F');
        TextureKey ceilingTexture = static_cast<TextureKey>('C');
        TextureKey boundaryTexture = static_cast<TextureKey>('1');
        int floorColorR = 54;
        int floorColorG = 40;
        int floorColorB = 28;
        int ceilingColorR = 24;
        int ceilingColorG = 22;
        int ceilingColorB = 20;
        double floorHeight = 0.0;
        double ceilingHeight = 1.0;
        double ambient = 1.0;
        // Height of solid boundary walls measured from the sector floor.
        // A negative value means "use the sector ceiling". Outdoor sectors
        // need this so the sky can be very high without producing 12 m walls.
        double wallHeight = -1.0;
        bool skyCeiling = false;

        // Build-style sloped planes. Heights are evaluated as:
        // base + slopeX * (worldX - slopeOriginX)
        //      + slopeY * (worldY - slopeOriginY).
        // Values are world-height units per map cell.
        double floorSlopeX = 0.0;
        double floorSlopeY = 0.0;
        double ceilingSlopeX = 0.0;
        double ceilingSlopeY = 0.0;
        double slopeOriginX = 0.0;
        double slopeOriginY = 0.0;
    };

    enum class DoorMotion
    {
        Swing,
        Slide,
        Raise,
        Transition
    };

    struct DoorDef
    {
        std::string id;
        int x = 0;
        int y = 0;
        bool locked = false;
        bool targetOpen = false;
        double openAmount = 0.0;
        double speed = 1.8;
        DoorMotion motion = DoorMotion::Swing;
        TextureKey texture = static_cast<TextureKey>('D');
        int span = 1;
        char axis = 'x';
        double height = -1.0;
        bool hingeAtEnd = false;
        double swingDirection = 1.0;
        double swingDegrees = 90.0;
        double thickness = 0.08;
        std::string interactionGroup;
        double textureU0 = 0.0;
        double textureU1 = 1.0;
        // Polygon-native doors use an exact world-space segment instead of a
        // tile cell. Legacy maps keep the x/y/span/axis representation.
        bool hasSegment = false;
        double segmentX0 = 0.0;
        double segmentY0 = 0.0;
        double segmentX1 = 1.0;
        double segmentY1 = 0.0;
        std::string targetInterior;
        std::string targetSpawn;
    };

    struct WallSegmentDef
    {
        std::string id;
        double x0 = 0.0;
        double y0 = 0.0;
        double x1 = 1.0;
        double y1 = 0.0;
        // Start- and end-point heights are stored independently.  Legacy JSON
        // only supplies bottom_z/top_z and therefore remains rectangular.
        // Sloped masonry (most importantly the continuous stair cheeks) uses
        // bottom_z_end/top_z_end to form a real trapezoid instead of a row of
        // overlapping rectangular strips.
        double bottomZ = 0.0;
        double topZ = 3.0;
        double bottomZEnd = 0.0;
        double topZEnd = 3.0;
        // Optional exact stair-step crown sampled along the segment.  One
        // segment can therefore render a complete solid cheek without seams
        // between dozens of short wall rectangles.
        std::vector<double> topProfile;
        TextureKey texture = static_cast<TextureKey>('1');
        double ambient = 1.0;
        double textureScale = 1.0;
        double textureUOffset = 0.0;
        bool worldAlignedTexture = false;
        bool solid = true;
        bool twoSided = true;
    };

    struct PolygonEdgeOpening
    {
        int edge = 0;
        double start = 0.35;
        double end = 0.65;
        double bottom = 0.0;
        double height = 2.10;
    };

    struct PolygonSectorRegion
    {
        std::string id;
        char sector = 'A';
        std::vector<std::array<double, 2>> vertices;
        bool boundarySolid = true;
        // Generated stairs that descend through an already elevated room
        // replace that room's floor inside their footprint. Ordinary internal
        // stairs keep the room floor below them so their solid base cannot
        // turn into a black void.
        bool cutsUnderlyingFloor = false;
        // A stair cut through an elevated room floor is a closed masonry
        // volume. Its tread replaces the room floor, while this optional
        // underside closes the volume at the lower supporting level.
        bool hasSupportBottom = false;
        double supportBottomZ = 0.0;
        TextureKey wallTexture = kNoTexture;
        double wallAmbient = 1.0;
        double wallTextureScale = 1.0;
        std::vector<PolygonEdgeOpening> openings;
        double minX = 0.0;
        double minY = 0.0;
        double maxX = 0.0;
        double maxY = 0.0;
        bool outlineSimple = false;
        // Cached ear-clipped triangles. Polygon floors and ceilings are drawn
        // as real depth-tested surfaces, so triangulation is performed once
        // when the map is loaded rather than every frame.
        std::vector<std::array<int, 3>> triangles;
    };

    struct VectorPortalDef
    {
        double x0 = 0.0;
        double y0 = 0.0;
        double x1 = 1.0;
        double y1 = 0.0;
        double bottomZ = 0.0;
        double topZ = 2.1;
        char sectorA = '\0';
        char sectorB = '\0';
    };

    struct TraversalRampDef
    {
        std::string id;
        double startX = 0.0;
        double startY = 0.0;
        double endX = 1.0;
        double endY = 0.0;
        double startHeight = 0.0;
        double endHeight = 0.0;
        double width = 1.0;
    };

    struct SpawnDef
    {
        double x = 2.5;
        double y = 2.5;
        double angle = 0.0;
        double pitch = 0.0;
    };

    enum class ObjectRenderMode
    {
        Billboard,
        Voxel
    };

    struct SpriteDef
    {
        std::string id;
        TextureKey texture = static_cast<TextureKey>('T');
        double x = 0.0;
        double y = 0.0;
        double zOffset = 0.0;
        double scale = 0.75;
        bool solid = false;
        ObjectRenderMode renderMode = ObjectRenderMode::Billboard;
        double yaw = 0.0;
        // World-space thickness. A value <= 0 uses an automatic fraction of
        // the object's height, which is useful for imported legacy sprites.
        double voxelDepth = 0.0;

        // Animated fire/torch settings. Animation frames are discovered next to
        // the base texture as <stem>_anim_00.png, _01.png, ... . Each object
        // owns its own pseudo-random timing, so nearby fires do not flicker in sync.
        bool animated = false;
        bool randomAnimation = true;
        double animationMinFps = 7.0;
        double animationMaxFps = 12.0;
        int animationFrame = 0;
        double animationTimer = 0.0;
        std::uint32_t animationSeed = 1u;

        // Warm point light emitted by fire-like objects. The lightweight software
        // lighting is evaluated in world space for floors, ceilings, walls and objects.
        bool emitsLight = false;
        int lightR = 255;
        int lightG = 150;
        int lightB = 72;
        double lightRadius = 3.2;
        double lightIntensity = 0.85;
        double lightHeight = 0.55;
        double lightFlicker = 0.18;
        double lightMultiplier = 1.0;

        std::string interactionLabel;
        std::string targetInterior;
        std::string targetSpawn;
    };

    struct ProjectedVertex
    {
        double x = 0.0;
        double y = 0.0;
        double depth = 1.0;
        double u = 0.0;
        double v = 0.0;
    };

    struct RuntimeLight
    {
        const SpriteDef* source = nullptr;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double radius = 0.0;
        double radiusSquared = 0.0;
        double intensity = 0.0;
        int r = 255;
        int g = 160;
        int b = 80;
        char sector = 'A';
    };

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    TextureSpriteEditor m_textureSpriteEditor;

    std::string m_currentInteriorId = "test_chamber";
    std::string m_displayName = "Interior prototype";
    std::string m_loadedPath;
    std::string m_loadedCastleId;
    std::string m_loadedCastleMapId;
    std::string m_lastError;
    std::string m_status;

    std::vector<std::vector<TextureKey>> m_grid;
    std::vector<std::string> m_sectorGrid;
    std::unordered_map<TextureKey, TextureRef> m_textures;
    std::unordered_map<TextureKey, std::string> m_texturePaths;
    std::unordered_map<char, SectorDef> m_sectors;
    // Hot-path lookup tables. The software renderer samples these structures
    // hundreds of thousands of times per frame, so avoid unordered_map lookups
    // inside pixel and ray loops.
    std::array<const TextureRef*, 65536> m_textureLookup{};
    std::array<const SectorDef*, 256> m_sectorLookup{};
    std::vector<DoorDef> m_doors;
    std::vector<WallSegmentDef> m_wallSegments;
    std::vector<WallSegmentDef> m_polygonBoundaryWalls;
    std::vector<PolygonSectorRegion> m_polygonSectors;
    std::vector<VectorPortalDef> m_polygonPortals;
    std::vector<TraversalRampDef> m_traversalRamps;
    std::array<bool, 256> m_floorCutoutSectors{};
    std::vector<std::size_t> m_floorCutoutPolygonIndices;
    double m_floorCutoutMinX = 1.0e30;
    double m_floorCutoutMinY = 1.0e30;
    double m_floorCutoutMaxX = -1.0e30;
    double m_floorCutoutMaxY = -1.0e30;
    std::vector<SpriteDef> m_sprites;
    std::vector<RuntimeLight> m_renderLights;
    std::unordered_map<std::string, SpawnDef> m_namedSpawns;

    double m_posX = 2.5;
    double m_posY = 2.5;
    double m_angle = 0.0;
    double m_fov = 72.0 * 3.14159265358979323846 / 180.0;
    double m_eyeHeight = 0.56;
    double m_pitch = 0.0;
    double m_bobPhase = 0.0;
    double m_moveBlend = 0.0;
    double m_cameraFloorZ = 0.0;
    bool m_cameraFloorInitialized = false;
    double m_lastSafeX = 2.5;
    double m_lastSafeY = 2.5;

    bool m_mouseLook = false;
    bool m_editorMode = false;
    bool m_wasUseKeyDown = false;
    bool m_wasRecoverKeyDown = false;
    bool m_wasJumpKeyDown = false;
    bool m_editorReloadRequested = false;
    bool m_sceneDirty = true;
    double m_smoothedFps = 0.0;

    // Vertical player state. m_playerZ is the absolute height of the feet.
    // The floor remains sector based, so this is still a 2.5D engine rather
    // than free-form 3D movement.
    double m_playerZ = 0.0;
    double m_verticalVelocity = 0.0;
    bool m_grounded = true;
    bool m_playerZInitialized = false;

    SDL_Texture* m_sceneTexture = nullptr;
    int m_sceneW = 0;
    int m_sceneH = 0;
    std::vector<std::uint32_t> m_framebuffer;
    std::vector<double> m_zBuffer;
    // Per-pixel depth for hinged doors and voxel/billboard objects. Walls keep
    // their fast per-column z-buffer; dynamic geometry then adds finer depth.
    std::vector<double> m_dynamicDepthBuffer;

    // Vector geometry is independent of the legacy tile grid. Polygon sectors
    // are rasterized once into a small sub-cell lookup so floor/ceiling queries
    // remain fast without snapping angled rooms back to whole tiles.
    static constexpr int kVectorSectorSubdiv = 8;
    int m_vectorSectorLookupW = 0;
    int m_vectorSectorLookupH = 0;
    double m_vectorLookupOriginX = 0.0;
    double m_vectorLookupOriginY = 0.0;
    std::vector<char> m_vectorSectorLookup;

    struct EditorMapChoice
    {
        std::string label;
        std::string target;
    };

    // Editor state.
    std::vector<EditorMapChoice> m_editorMapChoices;
    int m_editorSelectedMap = -1;
    bool m_editorLoadRequested = false;
    bool m_editorRefreshMapListRequested = true;
    std::string m_editorPendingLoadTarget;
    TextureKey m_editorTileBrush = static_cast<TextureKey>('1');
    char m_editorSectorBrush = 'A';
    int m_editorSelectedDoor = -1;
    int m_editorSelectedSprite = -1;
    int m_editorSelectedPolygon = -1;
    int m_editorSelectedPolygonVertex = -1;
    int m_editorSelectedOpening = -1;
    bool m_editorPolygonPointMode = false;
    bool m_editorPolygonDragDirty = false;
    float m_editorMapCellSize = 22.0f;

    // Stair wizard. Stairs are authored as a run of real floor-height sectors,
    // not as a flat billboard sprite. This keeps them compatible with the
    // portal renderer and gives every step a proper tread and riser.
    int m_editorStairDirection = 2; // 0 north, 1 east, 2 south, 3 west
    int m_editorStairSteps = 4;
    int m_editorStairWidth = 1;
    float m_editorStairTotalRise = -0.40f;
    bool m_editorStairCeilingFollows = true;
    char m_editorStairFirstSector = 'H';

    // The renderer is software based. Rendering at full desktop resolution in a
    // Debug build is unnecessarily expensive, especially because floor/ceiling
    // casting touches every pixel. The scene is rendered to a smaller internal
    // texture and SDL scales it to the window.
    float m_gameRenderScale = 0.36f;
    float m_editorRenderScale = 0.30f;
    bool m_editorPreview3D = false;
    bool m_fastFloorCasting = true;

    bool loadInteriorAtSpawn(const std::string& interiorIdOrPath, const std::string& spawnId);
    bool loadInteriorAtSpawnUnsafe(const std::string& interiorIdOrPath, const std::string& spawnId);
    void swapLoadedMapState(BuildInteriorEngine& other);
    bool loadCastleEntityOverlay(const std::string& castleId, const std::string& mapId);
    bool loadCastleGeometryOverlay(const std::string& castleId, const std::string& mapId);
    bool saveCastleGeometryOverlay();
    bool saveCastleEntityOverlay();
    bool hasVectorGeometry() const;
    static bool pointInPolygon(const PolygonSectorRegion& region, double x, double y);
    static double polygonSignedArea(const PolygonSectorRegion& region);
    static bool polygonIsSimple(const PolygonSectorRegion& region);
    static std::vector<std::array<int, 3>> triangulatePolygon(
        const PolygonSectorRegion& region);
    static void updatePolygonBounds(PolygonSectorRegion& region);
    const PolygonSectorRegion* polygonRegionAtWorld(double worldX, double worldY) const;
    void rebuildVectorSectorLookup();
    void rebuildPolygonBoundaryWalls();
    void setMouseLookEnabled(bool enabled);
    void clearTextures();
    void rebuildTextureLookup();
    void rebuildSectorLookup();
    void buildVoxelModel(TextureRef& texture);
    void buildVoxelFrame(TextureFrame& frame);
    bool loadTextureFrame(const std::string& absolutePath, TextureFrame& frame);
    void loadTextureAnimationFrames(TextureRef& texture);
    void prepareTextureModels();
    bool loadTextureForCell(TextureKey key, const std::string& relativePath);
    const TextureFrame* activeAnimationFrame(const TextureRef& texture, const SpriteDef& sprite) const;
    void ensureSceneBuffer(int width, int height);

    bool isInside(int x, int y) const;
    TextureKey cellAt(int x, int y) const;
    char sectorSymbolAt(int x, int y) const;
    const SectorDef& sectorAt(int x, int y) const;
    char sectorSymbolAtWorld(double worldX, double worldY) const;
    const SectorDef& sectorAtWorld(double worldX, double worldY) const;
    const SectorDef& sectorAtPlayer() const;
    double floorHeightAt(const SectorDef& sector, double worldX, double worldY) const;
    double ceilingHeightAt(const SectorDef& sector, double worldX, double worldY) const;
    double floorHeightAtWorld(double worldX, double worldY) const;
    double ceilingHeightAtWorld(double worldX, double worldY) const;
    bool traversalHeightAtWorld(double worldX, double worldY,
                                double& height) const;
    bool surfaceVisibleAlongRay(double worldX, double worldY, double worldZ, double eyeZ) const;
    bool wallSegmentBlocksPoint(const WallSegmentDef& wall, double x, double y, double radius) const;
    const WallSegmentDef* nearestWallSegmentHit(double originX, double originY,
                                                double rayX, double rayY,
                                                double& distance, double& textureU) const;
    static double wallBottomAt(const WallSegmentDef& wall, double amount);
    static double wallTopAt(const WallSegmentDef& wall, double amount);

    // Build-style door helpers. Keep these declared in the class because the
    // implementations in BuildInteriorEngine.cpp use the private DoorMotion
    // and DoorDef types.
    static DoorMotion parseDoorMotion(const std::string& value);
    static const char* doorMotionName(DoorMotion motion);
    bool doorCoversCell(const DoorDef& door, int x, int y) const;
    double doorCenterX(const DoorDef& door) const;
    double doorCenterY(const DoorDef& door) const;
    double doorTextureU(const DoorDef& door, int mapX, int mapY, double wallU) const;
    char doorLeafAxis(const DoorDef& door) const;
    void doorLeafSegment(const DoorDef& door, double& x0, double& y0, double& x1, double& y1) const;
    void doorVerticalBounds(const DoorDef& door, double& bottomZ, double& topZ) const;
    void doorRevealDepths(const DoorDef& door,
                          double& negativeDepth,
                          double& positiveDepth,
                          char& negativeSector,
                          char& positiveSector) const;
    bool doorBlocksPoint(const DoorDef& door, double x, double y, double radius) const;

    DoorDef* doorAt(int x, int y);
    const DoorDef* doorAt(int x, int y) const;
    DoorDef* nearestUsableDoor(double maxDistance = 1.45);
    SpriteDef* nearestUsableSprite(double maxDistance = 1.45);

    bool isDoorOpenEnough(int x, int y) const;
    bool isSolidCell(int x, int y) const;
    bool canStandAt(double x, double y) const;
    bool canMoveBetween(double fromX, double fromY, double toX, double toY) const;
    bool spriteBlocks(double x, double y) const;
    void tryMove(double dx, double dy);
    void recoverToLastSafePosition();
    void sanitizeEditorState();
    void useNearestInteraction();
    void toggleNearestDoor();
    void updateDoors(float dt);
    void updateAnimatedEffects(float dt);
    void rebuildRenderLights();
    std::uint32_t applyWorldLighting(std::uint32_t color, double baseShade,
                                     double worldX, double worldY, double worldZ,
                                     const SpriteDef* selfEmitter = nullptr) const;

    void renderScene(int renderW, int renderH);
    void renderFloorAndCeiling(int screenW, int screenH, int horizon, double eyeZ);
    void renderPolygonSurfaces(int screenW, int screenH, int horizon, double eyeZ);
    void renderDoorSoffits(int screenW, int screenH, int horizon, double eyeZ);
    void drawPolygonSurfaceTriangle(const ProjectedVertex& a,
                                    const ProjectedVertex& b,
                                    const ProjectedVertex& c,
                                    const PolygonSectorRegion& region,
                                    const SectorDef& sector,
                                    int surfaceKind);
    std::uint32_t sampleSky(int x, int y, int screenW, int screenH, int horizon) const;
    void renderPortalWalls(int screenW, int screenH, int horizon, double eyeZ);
    void renderVectorWalls(int screenW, int screenH, int horizon, double eyeZ);
    void renderSwingDoors(int screenW, int screenH, int horizon, double eyeZ);
    void renderSprites(int screenW, int screenH, int horizon, double eyeZ);
    void renderBillboardSprite(const SpriteDef& sprite, int screenW, int screenH, int horizon, double eyeZ);
    void renderVoxelSprite(const SpriteDef& sprite, int screenW, int screenH, int horizon, double eyeZ);
    void renderOverlay();
    void renderEditorOverlay();
    void refreshEditorMapList();
    void renderEditorMap();
    void renderEditorPolygons();
    void paintEditorCell(int x, int y, bool sectorOnly);
    void generateEditorStairs();
    void ensureSectorGrid();
    void ensureDefaultSectors();
    void rebuildDoorsFromGridIfMissing();
    std::string makeDoorId(int x, int y) const;
    std::string makeSpriteId() const;

    std::uint32_t sampleTexture(TextureKey key, double u, double v, std::uint32_t fallback) const;
    void putPixel(int x, int y, std::uint32_t color);
    void drawTexturedColumn(int x, int y0, int y1, TextureKey textureKey, double u,
                            double distance, double ambient, int clipTop, int clipBottom,
                            double vStart = 0.0, double vEnd = 1.0, int pixelWidth = 1,
                            double lightWorldX = 1.0e30, double lightWorldY = 1.0e30,
                            double lightWorldZ = 0.0);
    void drawFlatTriangle(const ProjectedVertex& a, const ProjectedVertex& b,
                          const ProjectedVertex& c, std::uint32_t color);
    void drawTexturedTriangle(const ProjectedVertex& a, const ProjectedVertex& b,
                              const ProjectedVertex& c, const TextureRef& texture,
                              double shade);
    void drawFlatQuad(const ProjectedVertex& a, const ProjectedVertex& b,
                      const ProjectedVertex& c, const ProjectedVertex& d,
                      std::uint32_t color);
    void drawTexturedQuad(const ProjectedVertex& a, const ProjectedVertex& b,
                          const ProjectedVertex& c, const ProjectedVertex& d,
                          const TextureRef& texture, double shade);
    int projectZ(double worldZ, double distance, int screenH, int horizon, double eyeZ) const;

    static std::string resolveInteriorPath(const std::string& interiorIdOrPath);
    static std::string resolveProjectPath(const std::string& relativePath);
};
