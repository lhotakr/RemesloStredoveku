#include "game/BuildInteriorEngine.h"
#include "game/CinematicScene.h"
#include "game/InventorySpriteCatalog.h"
#include "game/sequence/SequenceEditing.h"
#include "game/sequence/SequenceTimeline.h"
#include "CharacterManager.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace fs = std::filesystem;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(\
    std::string(__func__) + ": " + #expression); } while (false)

namespace
{
bool near(double a, double b) { return std::abs(a - b) < 0.00001; }

struct TemporaryDirectory
{
    fs::path path = fs::temp_directory_path() /
        ("remeslo-interior-editor-" + std::to_string(SDL_GetPerformanceCounter()));
    TemporaryDirectory() { fs::create_directories(path); }
    ~TemporaryDirectory() { std::error_code ec; fs::remove_all(path, ec); }
};

struct MotionContext : sequence::ISequenceContext
{
    CinematicScene& scene;
    explicit MotionContext(CinematicScene& value) : scene(value) {}
    void sequenceLog(const std::string&) override {}
    bool hasStoryFlag(const std::string&) const override { return false; }
    void setStoryFlag(const std::string&) override {}
    bool loadScene(const std::string&) override { return true; }
    bool resolveMarker(const std::string& id, sequence::Vec2& out) const override { return scene.resolveMarker(id, out); }
    bool spawnActor(const sequence::ActorSpawnDef& def) override { return scene.upsertActor(def, {}); }
    void removeActor(const std::string& id) override { scene.removeActorOrProp(id); }
    void setActorVisible(const std::string& id, bool value) override { scene.setActorVisible(id, value); }
    bool getActorPosition(const std::string& id, sequence::Vec2& out) const override { return scene.getActorPosition(id, out); }
    bool setActorPosition(const std::string& id, sequence::Vec2 pos) override { return scene.setActorPosition(id, pos); }
    void setActorAnimation(const std::string&, const std::string&) override {}
    void faceActor(const std::string&, const std::string&) override {}
    void setCameraPosition(sequence::Vec2 pos) override { scene.setCameraPosition(pos); }
    bool resolveCameraMarker(const std::string& id, sequence::CameraPose& out) const override { return scene.resolveCameraMarker(id, out); }
    bool getCameraPose(sequence::CameraPose& out) const override { out = scene.cameraPose(); return true; }
    void setCameraPose(const sequence::CameraPose& pose) override { scene.setCameraPose(pose); }
    void setCameraFollow(const std::string& id) override { scene.setCameraFollow(id); }
    void setCameraFollow(const std::string& id, const sequence::CameraFollowSettings& settings) override { scene.setCameraFollow(id, settings); }
    void setCameraShake(float, float, float) override {}
    void setFadeAlpha(float) override {}
    void setFlashAlpha(float) override {}
    void showSequenceText(const std::string&, float) override {}
    void hideSequenceText() override {}
    void playMusic(const std::string&, int, int) override {}
    void stopMusic(int) override {}
    void playSfx(const std::string&) override {}
    void playVoice(const std::string&) override {}
    void setTimeOfDay(const std::string&) override {}
    void setWeather(const nlohmann::json&) override {}
    bool loadMapForSequence(const std::string&, const std::string&) override { return false; }
    void teleportPlayer(sequence::Vec2) override {}
    void enablePlayerControl(bool) override {}
    void startDialog(const std::string&) override {}
    void changeRendererMode(const std::string&) override {}
};

sequence::SequenceDefinition MotionSequence(nlohmann::json actions)
{
    const auto parsed = sequence::SequenceFromJson({{"id", "motion"}, {"start", "start"},
        {"nodes", {{{"id", "start"}, {"actions", std::move(actions)}}}}}, "motion fixture");
    CHECK(parsed.ok);
    return parsed.sequence;
}
}

struct BuildInteriorEditorTestAccess
{
    using Engine = BuildInteriorEngine;

    static void loadFixture(Engine& engine)
    {
        TemporaryDirectory temporary;
        const auto path = temporary.path / "editor_fixture.json";
        const nlohmann::json root = {
            {"id", "editor_fixture"}, {"geometry_mode", "polygon_sectors"},
            {"spawn", {{"x", 4}, {"y", 6.4}}},
            {"sectors", {{"O", {{"floor_height", 0}, {"ceiling_height", 12}, {"sky_ceiling", true}}}}},
            {"polygon_sectors", {{{"id", "test_room"}, {"sector", "O"}, {"boundary_solid", true},
                {"vertices", {{1,1}, {15,1}, {15,11}, {1,11}}}}}},
            {"textures", {{{"key", 76}, {"path", "assets/Castles/Houska1400/mixed_forest_tree.png"}}}},
            {"sprites", {{{"id", "test_tree"}, {"texture_key", 76}, {"x", 2.2}, {"y", 2.8}, {"scale", 2.2}, {"solid", false}}}}
        };
        { std::ofstream file(path); file << root.dump(); }
        CHECK(engine.loadInterior(path.string()));
    }

    static void key(Engine& engine, SDL_Scancode code, bool pressed)
    {
        SDL_Event event{};
        event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.scancode = code;
        engine.handleEvent(event);
    }

    static void movement(Engine& engine)
    {
        loadFixture(engine);
        engine.setEditorMode(true);
        engine.setPlayerPose(4.0, 6.4, 0.0, 0.0);
        ImGui::GetIO().WantTextInput = false;
        key(engine, SDL_SCANCODE_W, true);
        engine.update(0.05f);
        CHECK(engine.m_posX > 4.1);
        CHECK(near(engine.m_posY, 6.4));
        key(engine, SDL_SCANCODE_W, false);
        const double stoppedX = engine.m_posX;
        engine.update(0.05f);
        CHECK(near(engine.m_posX, stoppedX));

        ImGui::GetIO().WantTextInput = true;
        key(engine, SDL_SCANCODE_W, true);
        engine.update(0.05f);
        CHECK(near(engine.m_posX, stoppedX));
        ImGui::GetIO().WantTextInput = false;
        engine.update(0.05f);
        CHECK(near(engine.m_posX, stoppedX));

        key(engine, SDL_SCANCODE_W, true);
        SDL_Event lost{};
        lost.type = SDL_WINDOWEVENT;
        lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        engine.handleEvent(lost);
        engine.update(0.05f);
        CHECK(near(engine.m_posX, stoppedX));

        engine.setPlayerPose(14.75, 5.0, 0.0, 0.0);
        engine.m_editorCameraCollision = true;
        key(engine, SDL_SCANCODE_W, true);
        engine.update(0.05f);
        CHECK(engine.m_posX < 14.83);
        engine.m_editorCameraCollision = false;
        engine.update(0.05f);
        engine.update(0.05f);
        CHECK(engine.m_posX > 15.0);
        key(engine, SDL_SCANCODE_W, false);
    }

    static void picking(Engine& engine)
    {
        loadFixture(engine);
        engine.m_sprites.clear();
        engine.setPlayerPose(4.0, 6.4, 0.0, 0.0);
        Engine::RuntimeSpriteDef object;
        object.id = "back";
        object.x = 9.0; object.y = 6.4; object.scale = 2.0;
        object.pixelW = 4; object.pixelH = 4;
        object.pixels.assign(16, 0xff00ff00u);
        CHECK(engine.upsertRuntimeSprite(object));
        object.id = "front"; object.x = 7.0; object.scale = 1.5;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                object.pixels[y * 4 + x] = x < 2 ? 0u : 0xffffffffu;
        CHECK(engine.upsertRuntimeSprite(object));
        engine.renderScene(320, 180);
        CHECK(std::count(engine.m_editorObjectPixels.begin(), engine.m_editorObjectPixels.end(), 0) > 0);
        CHECK(std::count(engine.m_editorObjectPixels.begin(), engine.m_editorObjectPixels.end(), 1) > 0);
        const auto front = std::find(engine.m_editorObjectPixels.begin(), engine.m_editorObjectPixels.end(), 1);
        const auto pixel = std::distance(engine.m_editorObjectPixels.begin(), front);
        int windowW = 0, windowH = 0;
        SDL_GetWindowSize(engine.m_window, &windowW, &windowH);
        SDL_Event click{};
        click.type = SDL_MOUSEBUTTONDOWN;
        click.button.button = SDL_BUTTON_LEFT;
        click.button.x = (pixel % 320) * windowW / 320 + windowW / 640;
        click.button.y = (pixel / 320) * windowH / 180 + windowH / 360;
        ImGui::GetIO().WantCaptureMouse = false;
        CHECK(engine.pickEditorObject(click.button.x, click.button.y) == 1);
        engine.handleEvent(click);
        CHECK(engine.m_editorSelectedSprite == 1 && engine.m_editorSelectObjectsTab);
        CHECK(engine.pickEditorObject(-1, 0) == -1);

        CHECK(engine.setRuntimeSpriteVisible("front", false));
        engine.renderScene(320, 180);
        CHECK(std::count(engine.m_editorObjectPixels.begin(), engine.m_editorObjectPixels.end(), 1) == 0);
        CHECK(engine.setRuntimeSpritePosition("back", 18.0, 6.4));
        engine.renderScene(320, 180);
        CHECK(std::count(engine.m_editorObjectPixels.begin(), engine.m_editorObjectPixels.end(), 0) == 0);

        object.id = "front"; object.voxel = true; object.pixels.assign(16, 0xffffffffu);
        CHECK(engine.upsertRuntimeSprite(object));
        engine.renderScene(320, 180);
        CHECK(std::count(engine.m_editorObjectPixels.begin(), engine.m_editorObjectPixels.end(), 1) > 0);
    }

    static void floorSprites(Engine& engine, const fs::path& temporary)
    {
        loadFixture(engine);
        engine.m_sprites.clear();
        engine.setEditorMode(true);
        engine.setPlayerPose(4, 6, 0, 0);
        engine.setSceneEyeHeight(1.6);
        engine.renderScene(320, 180);
        const auto groundPixels = engine.m_framebuffer;
        const auto sectorCount = engine.m_polygonSectors.size();
        Engine::RuntimeSpriteDef decal;
        decal.id = "floor_decal"; decal.x = 8; decal.y = 6;
        decal.floor = true; decal.solid = true; // Floor mode never blocks movement.
        decal.floorWidth = 4; decal.floorDepth = 2;
        decal.pixelW = decal.pixelH = 8;
        decal.pixels.assign(64, 0xffff0000u);
        CHECK(engine.upsertRuntimeSprite(decal));
        CHECK(!engine.spriteBlocks(8, 6));
        CHECK(engine.m_polygonSectors.size() == sectorCount);
        auto render = [&] {
            engine.renderScene(320, 180);
            return std::count(engine.m_editorObjectPixels.begin(), engine.m_editorObjectPixels.end(), 0);
        };
        const auto opaqueCount = render();
        CHECK(opaqueCount > 100);
        for (int y = 0; y < 90; ++y)
            CHECK(std::count(engine.m_editorObjectPixels.begin() + y * 320,
                             engine.m_editorObjectPixels.begin() + (y + 1) * 320, 0) == 0);
        const auto opaqueIds = engine.m_editorObjectPixels;
        // Transparent texels leave the floor color, depth and picking intact.
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 4; ++x) decal.pixels[y * 8 + x] = 0;
        CHECK(engine.upsertRuntimeSprite(decal));
        const auto alphaCount = render();
        CHECK(alphaCount > 0 && alphaCount < opaqueCount);
        int holes = 0;
        for (std::size_t i = 0; i < opaqueIds.size(); ++i)
            if (opaqueIds[i] == 0 && engine.m_editorObjectPixels[i] == -1)
            { CHECK(engine.m_framebuffer[i] == groundPixels[i]); ++holes; }
        CHECK(holes > 0);
        decal.pixels.assign(64, 0xffff0000u);
        decal.yawDegrees = 90;
        CHECK(engine.upsertRuntimeSprite(decal));
        CHECK(render() > 0);
        CHECK(engine.m_editorObjectPixels != opaqueIds); // Rotation changes the footprint.
        // A sprite straddling the camera near plane must not disappear.
        decal.x = 4.2; decal.floorDepth = 12;
        CHECK(engine.upsertRuntimeSprite(decal));
        CHECK(render() > 100);
        // A solid boundary occludes even a large floor decoration beyond it.
        decal.x = 18; decal.floorWidth = 2; decal.floorDepth = 2;
        CHECK(engine.upsertRuntimeSprite(decal));
        CHECK(render() == 0);
        decal.x = 8; decal.floorWidth = 4; decal.floorDepth = 4; decal.yawDegrees = 0;
        CHECK(engine.upsertRuntimeSprite(decal));
        CHECK(render() > 0);
        // Upright sprites must cover floor sprites regardless of insertion order.
        auto actor = decal; actor.id = "upright"; actor.x = 7; actor.floor = false;
        actor.solid = false; actor.scale = 2; actor.pixels.assign(64, 0xff00ff00u);
        CHECK(engine.upsertRuntimeSprite(actor));
        render();
        const auto covered = engine.m_editorObjectPixels;
        CHECK(std::count(covered.begin(), covered.end(), 1) > 0);
        CHECK(engine.setRuntimeSpriteVisible(decal.id, false));
        render();
        for (std::size_t i = 0; i < covered.size(); ++i)
            if (engine.m_editorObjectPixels[i] == 1) CHECK(covered[i] == 1);
        CHECK(engine.removeRuntimeSprite(actor.id));
        CHECK(engine.setRuntimeSpriteVisible(decal.id, true));
        engine.m_sectors.at('O').floorSlopeX = 0.15;
        engine.rebuildVectorSectorLookup();
        CHECK(render() > 0); // Decal follows the sloping floor.
        engine.m_sectors.at('O').floorSlopeX = 0;
        engine.rebuildVectorSectorLookup();
        // Coplanar transparent decorations blend in authored order.
        render();
        const auto redPixels = engine.m_framebuffer;
        auto overlay = decal; overlay.id = "overlay"; overlay.pixels.assign(64, 0x800000ffu);
        CHECK(engine.upsertRuntimeSprite(overlay));
        render();
        auto found = std::find(engine.m_editorObjectPixels.begin(), engine.m_editorObjectPixels.end(), 1);
        CHECK(found != engine.m_editorObjectPixels.end());
        const auto pixel = std::distance(engine.m_editorObjectPixels.begin(), found);
        CHECK(engine.m_framebuffer[pixel] != redPixels[pixel]);
        CHECK((engine.m_framebuffer[pixel] & 0xffu) > (redPixels[pixel] & 0xffu));
        engine.removeRuntimeSprite(overlay.id);
        // Save/reload both the interior and castle entity formats.
        decal.pixels.clear(); decal.pixelW = decal.pixelH = 0;
        decal.texturePath = "assets/Interior/sprite_crate.png";
        decal.yawDegrees = 37; decal.floorWidth = 3.25; decal.floorDepth = 1.75;
        CHECK(engine.upsertRuntimeSprite(decal));
        const auto path = temporary / "floor_decorations.json";
        CHECK(engine.saveInterior(path.string()));
        auto copy = std::make_unique<Engine>();
        CHECK(copy->loadInterior(path.string()));
        auto checkSaved = [&] {
            const auto* saved = copy->findSpriteById(decal.id);
            CHECK(saved && saved->renderMode == Engine::ObjectRenderMode::Floor);
            CHECK(near(saved->floorWidth, 3.25) && near(saved->floorDepth, 1.75));
            CHECK(std::abs(saved->yaw * 180 / 3.141592653589793 - 37) < 1e-5);
            CHECK(!copy->spriteBlocks(8, 6));
        };
        checkSaved();
        engine.m_loadedCastleId = (temporary / "floor_castle").string();
        engine.m_loadedCastleMapId = "floor_test";
        CHECK(engine.saveCastleEntityOverlay());
        copy->m_sprites.clear();
        CHECK(copy->loadCastleEntityOverlay(engine.m_loadedCastleId, "floor_test"));
        checkSaved();
        engine.m_loadedCastleId.clear();
        engine.m_loadedCastleMapId.clear();
    }

    static void spatialProjection(Engine& engine)
    {
        loadFixture(engine);
        engine.m_sprites.clear();
        engine.setPlayerPose(4, 6, 0, 0);
        engine.setSceneEyeHeight(1.6);
        for (const auto& size : {std::pair{320, 180}, std::pair{640, 400}})
        {
            engine.renderScene(size.first, size.second);
            double u = 0, v = 0;
            bool occluded = false;
            CHECK(engine.projectScenePoint({8, 6, 0}, u, v, occluded));
            CHECK(!occluded && std::abs(u - 0.5) < 0.001 && v > 0.5);
            Engine::ScenePoint plane;
            CHECK(engine.intersectSceneHeight(u, v, 0, plane));
            CHECK(near(plane.x, 8) && near(plane.y, 6) && near(plane.z, 0));
            Engine::ScenePoint picked;
            CHECK(engine.pickScenePoint(u, v, picked));
            CHECK(std::abs(picked.x - 8) < 0.15 && std::abs(picked.y - 6) < 0.05 && std::abs(picked.z) < 0.05);
            CHECK(engine.projectScenePoint({8, 6, 1.6}, u, v, occluded));
            CHECK(std::abs(v - 0.5) < 0.01);
            CHECK(!engine.intersectSceneHeight(u, v, 0, plane));
            CHECK(engine.projectScenePoint({18, 6, 1.6}, u, v, occluded));
            CHECK(occluded); // Outside the room, behind its solid boundary.
            CHECK(!engine.projectScenePoint({2, 6, 1}, u, v, occluded));
            CHECK(!engine.pickScenePoint(0.5, 0, picked)); // Open sky has no surface.
            CHECK(!engine.pickScenePoint(-0.1, 0.5, picked));
        }
        engine.setSceneEyeHeight(3);
        engine.setPlayerPose(4, 6, 0.3, 0.1);
        engine.renderScene(400, 240);
        double u, v;
        bool occluded;
        const Engine::ScenePoint point{9, 7, 1.5};
        CHECK(engine.projectScenePoint(point, u, v, occluded));
        Engine::ScenePoint intersection;
        CHECK(engine.intersectSceneHeight(u, v, point.z, intersection));
        CHECK(near(intersection.x, point.x) && near(intersection.y, point.y));
        CHECK(engine.projectScenePoint({6, 6.5, 0}, u, v, occluded));
        CHECK(v > 1); // Ground footprints stay usable below the preview.
        CHECK(engine.intersectSceneHeight(u, v, 0, intersection));
        CHECK(near(intersection.x, 6) && near(intersection.y, 6.5));
        CHECK(engine.sceneBoundaries().size() == 1);
    }

    static void dimensions(Engine& engine, const fs::path& temporary)
    {
        loadFixture(engine);
        auto& polygon = engine.m_polygonSectors.front();
        polygon.vertices = {{{-2, -1}}, {{4, -1}}, {{4, 2}}, {{1, 2}}, {{1, 5}}, {{-2, 5}}};
        engine.updatePolygonBounds(polygon);
        engine.rebuildPolygonBoundaryWalls();
        engine.rebuildVectorSectorLookup();
        CHECK(engine.polygonIsSimple(polygon));
        auto& sector = engine.m_sectors.at('O');
        sector.floorSlopeX = 0.10;
        sector.floorSlopeY = 0.05;
        const double floorBefore = engine.floorHeightAt(sector, 0.0, 0.0);
        engine.m_sprites[0].x = 0.0; engine.m_sprites[0].y = 0.0;
        engine.m_namedSpawns["test"].x = 0.0; engine.m_namedSpawns["test"].y = 0.0;
        engine.setPlayerPose(-1.0, 0.0, 0.0, 0.0);
        Engine::DoorDef door;
        door.id = "test_door"; door.hasSegment = true;
        door.segmentX0 = 0.0; door.segmentY0 = 0.0;
        door.segmentX1 = 1.0; door.segmentY1 = 0.0;
        engine.m_doors = {door};
        CHECK(engine.resizeEditorEnvironment(12.0, 3.0));
        CHECK(near(polygon.vertices[2][0], 10.0) && near(polygon.vertices[2][1], 0.5));
        CHECK(near(engine.m_posX, 0.0) && near(engine.m_posY, -0.5));
        CHECK(near(engine.m_sprites[0].x, 2.0) && near(engine.m_sprites[0].y, -0.5));
        CHECK(near(engine.m_namedSpawns.at("test").x, 2.0));
        CHECK(near(engine.floorHeightAt(sector, 2.0, -0.5), floorBefore));
        CHECK(near(engine.m_doors[0].segmentX1, 4.0));
        const auto vertices = polygon.vertices;
        CHECK(!engine.resizeEditorEnvironment(0.0, 2.0));
        CHECK(!engine.resizeEditorEnvironment(std::numeric_limits<double>::quiet_NaN(), 2.0));
        CHECK(!engine.resizeEditorEnvironment(1000000.0, 1000000.0));
        CHECK(polygon.vertices == vertices);
        CHECK(engine.resizeEditorPolygon(0, 6.0, 6.0));
        CHECK(near(polygon.maxX - polygon.minX, 6.0) && near(polygon.maxY - polygon.minY, 6.0));
        CHECK(engine.polygonIsSimple(polygon));

        const auto saved = temporary / "resized.json";
        CHECK(engine.saveInterior(saved.string()));
        auto reloadedInstance = std::make_unique<Engine>();
        Engine& reloaded = *reloadedInstance;
        reloaded.m_window = engine.m_window;
        reloaded.m_renderer = engine.m_renderer;
        CHECK(reloaded.loadInterior(saved.string()));
        CHECK(reloaded.m_polygonSectors[0].vertices == polygon.vertices);
        CHECK(near(reloaded.m_sprites[0].x, engine.m_sprites[0].x));
        CHECK(near(reloaded.m_doors[0].segmentX1, engine.m_doors[0].segmentX1));

        // Editor geometry must replace generated castle geometry on reload,
        // including removed sectors and modifications to existing IDs.
        engine.m_loadedCastleId = temporary.string();
        engine.m_loadedCastleMapId = "test_map";
        const auto snapshot = polygon.vertices;
        CHECK(engine.saveCastleGeometryOverlay());
        engine.m_polygonSectors.push_back(polygon);
        engine.m_polygonSectors.back().id = "deleted_after_generation";
        engine.m_polygonSectors.front().vertices[0][0] -= 3.0;
        CHECK(engine.loadCastleGeometryOverlay(temporary.string(), "test_map"));
        CHECK(engine.m_polygonSectors.size() == 1);
        CHECK(engine.m_polygonSectors[0].vertices == snapshot);
        engine.m_polygonSectors.clear();
        CHECK(engine.saveCastleGeometryOverlay());
        CHECK(engine.loadCastleGeometryOverlay(temporary.string(), "test_map"));
        CHECK(engine.hasVectorGeometry() && engine.m_polygonSectors.empty());
        engine.m_loadedCastleId.clear();
        engine.m_loadedCastleMapId.clear();
    }

    static void mapInteraction(Engine& engine)
    {
        loadFixture(engine);
        engine.m_editorMapTool = Engine::EditorMapTool::Polygons;
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 720);
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImVec2 origin;
        auto frame = [&]
        {
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Map interaction", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            origin = ImGui::GetCursorScreenPos();
            engine.renderEditorMap();
            ImGui::End();
            ImGui::Render();
        };
        frame();
        frame();
        const double oldX = engine.m_sprites[0].x, oldY = engine.m_sprites[0].y;
        const float cell = engine.m_editorMapCellSize;
        const ImVec2 spritePoint(origin.x + oldX * cell, origin.y + oldY * cell);
        io.AddMousePosEvent(spritePoint.x, spritePoint.y);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        CHECK(engine.m_editorSelectedSprite == 0);
        io.AddMousePosEvent(spritePoint.x + cell * 0.5f, spritePoint.y + cell * 0.5f);
        frame();
        CHECK(near(engine.m_sprites[0].x, oldX + 0.5));
        CHECK(near(engine.m_sprites[0].y, oldY + 0.5));
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        CHECK(engine.m_editorSelectObjectsTab);

        const ImVec2 vertexPoint(origin.x + cell, origin.y + cell);
        io.AddMousePosEvent(vertexPoint.x, vertexPoint.y);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        CHECK(engine.m_editorDraggingVertex);
        const float vertexTargetX = std::floor(vertexPoint.x + cell * 0.37f);
        const float vertexTargetY = std::floor(vertexPoint.y + cell * 0.19f);
        io.AddMousePosEvent(vertexTargetX, vertexTargetY);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        CHECK(near(engine.m_polygonSectors[0].vertices[0][0], 1.0 + (vertexTargetX - vertexPoint.x) / cell));
        CHECK(near(engine.m_polygonSectors[0].vertices[0][1], 1.0 + (vertexTargetY - vertexPoint.y) / cell));
        const auto vertices = engine.m_polygonSectors[0].vertices;

        // Clicking empty space after a vertex edit must not move that vertex.
        io.AddMousePosEvent(origin.x + cell * 7, origin.y + cell * 8);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        io.AddMousePosEvent(origin.x + cell * 8, origin.y + cell * 9);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        CHECK(engine.m_polygonSectors[0].vertices == vertices);

        // Build the complete editor too: selection must reveal the object
        // inspector, and all dimension widgets must accept this ImGui version.
        engine.m_editorSelectObjectsTab = true;
        for (int i = 0; i < 3; ++i)
        {
            ImGui::NewFrame();
            engine.renderEditorOverlay();
            ImGui::Render();
        }
        CHECK(engine.m_editorMapTool == Engine::EditorMapTool::Objects);
        CHECK(!engine.m_editorSelectObjectsTab);
    }

    static void projectObjectLibrary(Engine& engine, const fs::path& temporary)
    {
        loadFixture(engine);
        const auto loadedCount = engine.m_textures.size();
        engine.refreshEditorTextureAssets();
        CHECK(engine.m_textures.size() == loadedCount); // Listing never eagerly loads every atlas.
        auto asset = [&](const std::string& id)
        {
            const auto found = std::find_if(engine.m_editorTextureAssets.begin(), engine.m_editorTextureAssets.end(),
                [&](const auto& item) { return item.objectId == id; });
            CHECK(found != engine.m_editorTextureAssets.end());
            return *found;
        };
        const auto hoe = asset("artifact_hoe");
        CHECK(hoe.inventory && hoe.name == "Podivná motyka");
        CHECK(hoe.path == "assets/data/items/artifact_hoe.png" && hoe.sourceRect.w == 0);
        CHECK(hoe.search.find("motyka") != std::string::npos);
        CHECK(asset("hoe_basic").path == "assets/data/items/hoe_basic.png");
        const auto tree = asset("treeandstonesprites_obj_001");
        const auto otherTree = asset("treeandstonesprites_obj_002");
        asset("decoration_obj_001");
        asset("houses_obj_001");
        asset("castle_houska");
        asset("smithy_anvil");
        asset("map_link_smithy_portal");
        CHECK(std::any_of(engine.m_editorTextureAssets.begin(), engine.m_editorTextureAssets.end(),
            [](const auto& item) { return item.path == "assets/Interior/sprite_well.png"; }));
        CHECK(std::none_of(engine.m_editorTextureAssets.begin(), engine.m_editorTextureAssets.end(),
            [](const auto& item) { return item.objectId == "spawn_default"; }));
        const auto key = engine.textureKeyForSource(tree.path, tree.sourceRect, tree.name);
        const auto secondKey = engine.textureKeyForSource(otherTree.path, otherTree.sourceRect, otherTree.name);
        CHECK(key != Engine::kNoTexture && secondKey != Engine::kNoTexture && key != secondKey);
        const auto& texture = engine.m_textures.at(key);
        CHECK(texture.w == tree.sourceRect.w && texture.h == tree.sourceRect.h && texture.name == tree.name);
        const auto pixels = texture.pixels;
        CHECK(engine.textureKeyForSource(tree.path, tree.sourceRect, tree.name) == key);
        CHECK(engine.m_textures.size() == loadedCount + 2);
        CHECK(!engine.loadTextureForCell(key, tree.path, {-1, 0, 10, 10}));
        CHECK(engine.m_textures.at(key).pixels == pixels); // Failed selection preserves the previous object.

        SDL_Surface* source = IMG_Load((fs::path(REMESLO_PROJECT_ROOT) / tree.path).string().c_str());
        CHECK(source);
        SDL_Surface* rgba = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(source);
        CHECK(rgba);
        for (int y = 0; y < tree.sourceRect.h; ++y)
        {
            const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(rgba->pixels) +
                (y + tree.sourceRect.y) * rgba->pitch);
            for (int x = 0; x < tree.sourceRect.w; ++x)
                CHECK(pixels[y * tree.sourceRect.w + x] == row[x + tree.sourceRect.x]);
        }
        SDL_FreeSurface(rgba);

        engine.m_sprites.front().texture = key;
        engine.m_sprites.front().x = 7;
        engine.m_sprites.front().y = 6.4;
        engine.setPlayerPose(4, 6.4, 0, 0);
        engine.setEditorMode(true);
        engine.renderScene(320, 180);
        CHECK(std::count(engine.m_editorObjectPixels.begin(), engine.m_editorObjectPixels.end(), 0) > 0);
        const auto saved = temporary / "atlas_interior.json";
        CHECK(engine.saveInterior(saved.string()));
        auto copy = std::make_unique<Engine>();
        copy->m_window = engine.m_window;
        copy->m_renderer = engine.m_renderer;
        CHECK(copy->loadInterior(saved.string()));
        CHECK(copy->m_textures.at(key).pixels == pixels);
        CHECK(copy->m_textures.at(key).name == tree.name);
        CHECK(copy->m_textures.at(secondKey).pixels == engine.m_textures.at(secondKey).pixels);
        CHECK(copy->m_sprites.front().texture == key);

        const auto castle = temporary / "atlas_castle";
        engine.m_loadedCastleId = castle.string();
        engine.m_loadedCastleMapId = "test";
        CHECK(engine.saveCastleEntityOverlay());
        loadFixture(*copy);
        CHECK(copy->loadCastleEntityOverlay(castle.string(), "test"));
        const auto restoredKey = copy->m_sprites.front().texture;
        CHECK(copy->m_textures.at(restoredKey).pixels == pixels);
        CHECK(copy->m_textures.at(restoredKey).name == tree.name);
        engine.m_loadedCastleId.clear();
        engine.m_loadedCastleMapId.clear();
    }

    static void inventorySprites(Engine& engine, const fs::path& temporary)
    {
        const auto choices = inventorysprites::Load(REMESLO_PROJECT_ROOT);
        auto hoe = std::find_if(choices.begin(), choices.end(), [](const auto& choice) { return choice.id == "artifact_hoe"; });
        CHECK(hoe != choices.end() && hoe->path == "assets/data/items/artifact_hoe.png");
        // Same icon as the inventory, including its original alpha channel.
        loadFixture(engine);
        Engine::RuntimeSpriteDef sprite;
        sprite.id = "hoe_decoration"; sprite.texturePath = hoe->path;
        sprite.floor = true; sprite.x = 8; sprite.y = 6; sprite.scale = 2;
        CHECK(engine.upsertRuntimeSprite(sprite));
        const auto* object = engine.findSpriteById(sprite.id);
        CHECK(object && object->renderMode == Engine::ObjectRenderMode::Floor);
        const auto& texture = engine.m_textures.at(object->texture);
        SDL_Surface* source = IMG_Load((fs::path(REMESLO_PROJECT_ROOT) / hoe->path).string().c_str());
        CHECK(source);
        SDL_Surface* rgba = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(source);
        CHECK(rgba && texture.w == rgba->w && texture.h == rgba->h);
        for (int y = 0; y < rgba->h; ++y)
        {
            const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(rgba->pixels) + y * rgba->pitch);
            for (int x = 0; x < rgba->w; ++x) CHECK(texture.pixels[y * rgba->w + x] == row[x]);
        }
        SDL_FreeSurface(rgba);
        CHECK(std::any_of(texture.pixels.begin(), texture.pixels.end(), [](auto pixel) { return (pixel >> 24) == 0; }));
        CHECK(std::any_of(texture.pixels.begin(), texture.pixels.end(), [](auto pixel) { return (pixel >> 24) > 0; }));
        CHECK(!engine.spriteBlocks(8, 6));
        CHECK(engine.textureKeyForSource(hoe->path, {}, hoe->name) == object->texture);
        CHECK(texture.name == hoe->name); // A cached raw icon gains the selected inventory label.

        // Catalogs may reference either bare sprite IDs or explicit image paths.
        const auto root = temporary / "inventory_catalog";
        fs::create_directories(root / "assets/data/items");
        fs::create_directories(root / "assets/custom");
        fs::create_directories(root / "data/items");
        fs::copy_file(fs::path(REMESLO_PROJECT_ROOT) / hoe->path, root / "assets/data/items/tool.png");
        fs::copy_file(fs::path(REMESLO_PROJECT_ROOT) / hoe->path, root / "assets/custom/icon.png");
        nlohmann::json base = {{"items", {{{"id", "tool"}, {"name", "Old name"}, {"spriteId", "tool"}}}}};
        { std::ofstream file(root / "data/items/Items.json"); file << base.dump(); }
        nlohmann::json catalog = {{"items", {
            {{"id", "tool"}, {"name", "New name"}, {"spriteId", "tool"}},
            {{"id", "custom"}, {"spriteId", "assets/custom/icon"}},
            {{"id", "backslash"}, {"spriteId", "assets\\custom\\icon.png"}},
            {{"id", "missing"}, {"spriteId", "no_such_sprite"}}
        }}};
        { std::ofstream file(root / "assets/data/Items.json"); file << catalog.dump(); }
        const auto custom = inventorysprites::Load(root);
        CHECK(custom.size() == 3);
        CHECK(std::count_if(custom.begin(), custom.end(), [](const auto& item) { return item.id == "tool" && item.name == "New name"; }) == 1);
        CHECK(std::count_if(custom.begin(), custom.end(), [](const auto& item) { return item.path == "assets/custom/icon.png"; }) == 2);
    }

    static void projectObjectPickerInteraction(Engine& engine)
    {
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 720);
        Engine::TextureKey selected = static_cast<Engine::TextureKey>('B');
        bool changed = false;
        ImVec2 origin;
        auto frame = [&]
        {
            io.DeltaTime = 1.0f / 60;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Project object picker", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            origin = ImGui::GetCursorScreenPos();
            changed |= engine.renderEditorTexturePicker("Object texture", selected, true);
            ImGui::End();
            ImGui::Render();
        };
        frame(); frame();
        io.AddMousePosEvent(origin.x + 70, origin.y + 8); frame();
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame();
        CHECK(!GImGui->OpenPopupStack.empty());
        frame(); // Let the popup settle after ImGui measures its first contents.
        const auto search = GImGui->OpenPopupStack.back().Window->DC.CursorStartPos;
        io.AddMousePosEvent(search.x + 60, search.y + 8); frame();
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame();
        io.AddInputCharactersUTF8("smithy_anvil"); frame();
        ImGuiWindow* list = nullptr;
        for (auto* window : GImGui->Windows)
            if (window->Active && std::string(window->Name).find("TextureChoices") != std::string::npos) list = window;
        CHECK(list);
        const auto row = list->DC.CursorStartPos;
        const float rowY = row.y + 2 * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 1 + ImGui::GetFontSize() * 0.5f;
        io.AddMousePosEvent(row.x + 70, rowY); frame();
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame();
        CHECK(changed && selected != static_cast<Engine::TextureKey>('B'));
        CHECK(engine.m_textures.at(selected).path == "assets/Objects/Smithy/SmithyEnvironmentAtlas.png");
        CHECK(engine.m_textures.at(selected).sourceRect.w > 0);
        CHECK(GImGui->OpenPopupStack.empty());
    }

    static void sectorInsertion(Engine& engine, const fs::path& temporary)
    {
        loadFixture(engine);
        engine.m_editorMapTool = Engine::EditorMapTool::Polygons;
        auto road = engine.m_sectors.at('O');
        road.symbol = 'E'; road.id = "asphalt"; road.name = "Asphalt road";
        road.floorTexture = 266;
        engine.m_sectors['E'] = road;
        engine.m_editorSectorBrush = 'E';
        engine.m_editorNewSectorWalls = false;
        engine.rebuildSectorLookup();
        CHECK(engine.loadTexturePixelsForCell(266, std::vector<std::uint32_t>(16, 0xffff0000u), 4, 4, "test_asphalt"));
        const auto clearing = engine.m_polygonSectors[0].vertices;
        const double treeX = engine.m_sprites[0].x, treeY = engine.m_sprites[0].y;
        CHECK(!engine.createEditorSectorRectangle(2, 2, 2, 4));
        CHECK(!engine.createEditorSectorRectangle(2, 2, std::numeric_limits<double>::quiet_NaN(), 4));
        CHECK(!engine.createEditorSectorRectangle(2, 2, 1000000, 1000000));
        CHECK(engine.m_polygonSectors.size() == 1);

        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 720);
        ImVec2 origin;
        auto frame = [&]
        {
            io.DeltaTime = 1.0f / 60;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Sector insertion", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            origin = ImGui::GetCursorScreenPos();
            engine.renderEditorMap();
            ImGui::End();
            ImGui::Render();
        };
        frame(); frame();
        const float cell = engine.m_editorMapCellSize;
        engine.m_editorSectorPlacement = true;
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        // Starting near an existing object must draw the prepared sector,
        // rather than move the tree or sample the clearing's sector O.
        io.AddMousePosEvent(origin.x + cell * 3, origin.y + cell * 3); frame();
        io.AddMouseButtonEvent(0, true); frame();
        CHECK(engine.m_editorDrawingSector);
        CHECK(engine.m_editorSectorBrush == 'E');
        io.AddMousePosEvent(origin.x + cell * 12.06f, origin.y + cell * 9.06f); frame();
        CHECK(engine.m_polygonSectors.size() == 1);
        io.AddMouseButtonEvent(0, false); frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, false); frame();
        CHECK(engine.m_polygonSectors.size() == 2);
        CHECK(!engine.m_editorSectorPlacement && !engine.m_editorDrawingSector);
        CHECK(engine.m_editorSelectedPolygon == 1 && engine.m_editorSectorBrush == 'E');
        CHECK(engine.m_polygonSectors[0].vertices == clearing);
        CHECK(near(engine.m_sprites[0].x, treeX) && near(engine.m_sprites[0].y, treeY));
        const auto inserted = engine.m_polygonSectors[1];
        CHECK(near(inserted.minX, 3) && near(inserted.minY, 3));
        CHECK(near(inserted.maxX, 12) && near(inserted.maxY, 9));
        CHECK(!inserted.boundarySolid && inserted.cutsUnderlyingFloor);
        CHECK(engine.sectorAtWorld(8, 6).symbol == 'E');
        CHECK(engine.sectorAtWorld(2, 6).symbol == 'O');
        CHECK(engine.canMoveBetween(2.8, 6, 3.2, 6));

        // Equal-height asphalt must be visible immediately over the old floor.
        engine.m_sprites.clear();
        engine.setPlayerPose(4, 6, 0, 0);
        engine.setSceneEyeHeight(1.6);
        engine.renderScene(320, 180);
        const auto livePixels = engine.m_framebuffer;
        const auto redPixels = std::count_if(livePixels.begin(), livePixels.end(), [](auto pixel) {
            return ((pixel >> 16u) & 255u) > 80 && ((pixel >> 8u) & 255u) < 10 && (pixel & 255u) < 10;
        });
        CHECK(redPixels > 1000);

        // Cancelling a drag, and clicking without a rectangle, leave geometry intact.
        engine.m_editorSectorPlacement = true;
        io.AddMousePosEvent(origin.x + cell * 5, origin.y + cell * 5); frame();
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMousePosEvent(origin.x + cell * 9, origin.y + cell * 8); frame();
        io.AddKeyEvent(ImGuiKey_Escape, true); frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        io.AddMouseButtonEvent(0, false); frame();
        CHECK(!engine.m_editorDrawingSector && !engine.m_editorSectorPlacement);
        CHECK(engine.m_polygonSectors.size() == 2);
        engine.m_editorSectorPlacement = true;
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame();
        CHECK(engine.m_polygonSectors.size() == 2);
        io.AddMouseButtonEvent(1, true); frame();
        io.AddMouseButtonEvent(1, false); frame();
        CHECK(!engine.m_editorSectorPlacement);

        engine.m_editorSectorPlacement = true;
        io.AddMousePosEvent(origin.x + cell * 5, origin.y + cell * 5); frame();
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMousePosEvent(1200, 650); frame();
        io.AddMouseButtonEvent(0, false); frame();
        CHECK(engine.m_polygonSectors.size() == 2);
        CHECK(!engine.m_editorDrawingSector && engine.m_editorSectorPlacement);
        CHECK(engine.cancelEditorSectorPlacement());
        CHECK(!engine.cancelEditorSectorPlacement());

        const auto saved = temporary / "asphalt.json";
        CHECK(engine.saveInterior(saved.string()));
        CHECK(engine.loadInterior(saved.string()));
        CHECK(engine.m_polygonSectors.size() == 2);
        CHECK(engine.m_polygonSectors[1].vertices == inserted.vertices);
        CHECK(engine.m_polygonSectors[1].cutsUnderlyingFloor);
        CHECK(!engine.m_polygonSectors[1].boundarySolid);
        CHECK(engine.m_sectors.at('E').floorTexture == 266);
        CHECK(engine.sectorAtWorld(8, 6).symbol == 'E');

        // Reversed drags and deletion/reinsertion keep valid bounds and unique IDs.
        engine.m_editorSectorBrush = 'E';
        CHECK(engine.createEditorSectorRectangle(13, 10, 12, 9));
        const auto removedId = engine.m_polygonSectors[1].id;
        const auto survivingId = engine.m_polygonSectors[2].id;
        CHECK(removedId != survivingId);
        engine.m_polygonSectors.erase(engine.m_polygonSectors.begin() + 1);
        CHECK(engine.createEditorSectorRectangle(3, 3, 5, 5));
        CHECK(engine.m_polygonSectors.back().id != survivingId);
        engine.m_polygonSectors.erase(engine.m_polygonSectors.begin() + 1, engine.m_polygonSectors.end());
        engine.rebuildVectorSectorLookup();
        CHECK(!engine.m_floorCutoutSectors[static_cast<unsigned char>('E')]);
        CHECK(engine.m_floorCutoutPolygonIndices.empty());

        // Switching to Objects must release the insertion tool for object dragging.
        engine.m_editorSectorPlacement = true;
        engine.m_editorSelectObjectsTab = true;
        for (int i = 0; i < 3; ++i)
        {
            ImGui::NewFrame();
            engine.renderEditorOverlay();
            ImGui::Render();
        }
        CHECK(engine.m_editorMapTool == Engine::EditorMapTool::Objects);
        CHECK(!engine.m_editorSectorPlacement && !engine.m_editorDrawingSector);
    }

    static void sceneEntities(Engine& engine, const fs::path& temporary)
    {
        loadFixture(engine);
        const auto interior = temporary / "scene_environment.json";
        CHECK(engine.saveInterior(interior.string()));
        CharacterManager characters;
        CHECK(characters.init(engine.m_renderer, std::string(REMESLO_PROJECT_ROOT) + "/assets/characters"));
        const auto ids = characters.characterIds();
        CHECK(ids.size() >= 2);
        CinematicScene scene;
        CHECK(scene.init(engine.m_window, engine.m_renderer, &characters));
        nlohmann::json document = {
            {"id", "scene_entities"}, {"interior", interior.string()},
            {"markers", {{{"id", "car_position"}, {"x", 7}, {"y", 6}, {"z", 0.25}}}},
            {"actors", {{{"id", "player"}, {"character", ids[0]}, {"marker", "car_position"}, {"scale", 0.9}}}},
            {"props", {{{"id", "car"}, {"marker", "car_position"}, {"texture", "assets/Interior/sprite_crate.png"}, {"scale", 1.5}}}}
        };
        CHECK(scene.loadScene("scene_entities", {}, &document));
        CHECK(scene.hasActor("player") && scene.hasProp("car"));
        auto& rendered = const_cast<Engine&>(scene.environment());
        CHECK(rendered.findSpriteById("player") && rendered.findSpriteById("car"));
        CHECK(near(rendered.findSpriteById("player")->zOffset, 0.25));
        const auto originalTexture = rendered.findSpriteById("player")->texture;
        auto& actor = document["actors"][0];
        actor["character"] = ids[1]; actor["animation"] = "walk_left";
        actor["scale"] = 1.2; actor["z_offset"] = 0.4;
        CHECK(scene.setEditorEntity(actor, false));
        CHECK(rendered.findSpriteById("player")->texture != originalTexture);
        CHECK(near(rendered.findSpriteById("player")->scale, 1.2));
        CHECK(near(rendered.findSpriteById("player")->zOffset, 0.65));
        auto& prop = document["props"][0];
        prop["texture"] = "assets/data/items/artifact_hoe.png";
        prop["scale"] = 2.0; prop["visible"] = false;
        prop["render_mode"] = "floor"; prop["floor_width"] = 2.5; prop["floor_depth"] = 1.25;
        prop["yaw_degrees"] = 45;
        CHECK(scene.setEditorEntity(prop, true));
        CHECK(!rendered.findSpriteById("car")->visible);
        CHECK(near(rendered.findSpriteById("car")->scale, 2));
        CHECK(rendered.findSpriteById("car")->renderMode == Engine::ObjectRenderMode::Floor);
        CHECK(near(rendered.findSpriteById("car")->floorWidth, 2.5));
        auto marker = sequence::MarkerFromJson(document["markers"][0]);
        marker.x = 9; marker.z = 1.0;
        scene.setEditorMarkers({marker});
        CHECK(near(rendered.findSpriteById("player")->x, 9));
        CHECK(near(rendered.findSpriteById("player")->zOffset, 1.4));
        // Appearance edits keep marker height relative to the floor exactly once.
        CHECK(scene.setEditorEntity(actor, false));
        CHECK(near(rendered.findSpriteById("player")->zOffset, 1.4));
        document["markers"][0] = sequence::MarkerToJson(marker);
        const auto saved = temporary / "scene_entities.json";
        { std::ofstream file(saved); file << document.dump(2); }
        CHECK(scene.loadScene(saved.string(), {}));
        CHECK(near(rendered.findSpriteById("player")->zOffset, 1.4));
        CHECK(near(rendered.findSpriteById("player")->x, 9));
        CHECK(!rendered.findSpriteById("car")->visible);
        CHECK(rendered.findSpriteById("car")->renderMode == Engine::ObjectRenderMode::Floor);
        CHECK(near(rendered.findSpriteById("car")->floorDepth, 1.25));
        CHECK(near(rendered.findSpriteById("car")->yaw, 3.141592653589793 / 4));
        CHECK(rendered.m_textures.at(rendered.findSpriteById("car")->texture).path == "assets/data/items/artifact_hoe.png");
        // The intro hoe belongs to the interior rather than the scene's props array.
        CHECK(scene.loadScene("intro_houska_camp", {}));
        auto* hoe = rendered.findSpriteById("interior_object_3");
        CHECK(hoe && hoe->visible && hoe->renderMode == Engine::ObjectRenderMode::Floor);
        CHECK(rendered.m_textures.at(hoe->texture).path == "assets/data/items/artifact_hoe.png");
        CHECK(!scene.hasProp("interior_object_3"));
        sequence::Vec2 hoePosition;
        CHECK(scene.getActorPosition("interior_object_3", hoePosition));
        CHECK(std::abs(hoePosition.x - hoe->x) < 0.0001 && std::abs(hoePosition.y - hoe->y) < 0.0001);
        CHECK(scene.setActorVisible("interior_object_3", false));
        CHECK(!hoe->visible);
        scene.shutdown();
        characters.shutdown();
    }

    static void imageScenes(Engine& engine, const fs::path& temporary)
    {
        const auto redPath = temporary / "red.bmp", bluePath = temporary / "blue.bmp";
        auto* surface = SDL_CreateRGBSurfaceWithFormat(0, 4, 4, 32, SDL_PIXELFORMAT_ABGR8888);
        CHECK(surface);
        SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 255, 0, 0, 255));
        CHECK(SDL_SaveBMP(surface, redPath.c_str()) == 0);
        SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 0, 0, 255, 255));
        CHECK(SDL_SaveBMP(surface, bluePath.c_str()) == 0);
        SDL_FreeSurface(surface);
        CinematicScene scene;
        CHECK(scene.init(engine.m_window, engine.m_renderer, nullptr));
        nlohmann::json document = {{"id", "image_test"}, {"type", "image_2d"},
            {"canvas", {{"width", 200}, {"height", 100}}},
            {"markers", {{{"id", "destination"}, {"x", 100}, {"y", 30}}}},
            {"props", {
                {{"id", "red"}, {"texture", redPath.string()}, {"x", 10}, {"y", 10}, {"width", 20}, {"height", 20}, {"layer", 1}},
                {{"id", "blue"}, {"texture", bluePath.string()}, {"x", 10}, {"y", 10}, {"width", 20}, {"height", 20}, {"layer", 2}},
                {{"id", "title"}, {"type", "text"}, {"text", "Houska\nIntro"}, {"x", 80}, {"y", 60}, {"font_size", 12}, {"width", 100}, {"layer", 3}}
            }}};
        CHECK(scene.loadScene("image_test", {}, &document));
        CHECK(scene.isImageScene()); CHECK(scene.hasProp("title"));
        CHECK(scene.imageLayerOrder() == std::vector<std::string>({"red", "blue", "title"}));
        const auto fit = scene.imageViewport(400, 400);
        CHECK(near(fit.x, 0) && near(fit.y, 100) && near(fit.w, 400) && near(fit.h, 200));
        SDL_FRect bounds;
        CHECK(scene.imageEntityBounds("red", bounds)); CHECK(near(bounds.w, 20));
        auto* target = SDL_CreateTexture(engine.m_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, 400, 400);
        CHECK(target); CHECK(SDL_SetRenderTarget(engine.m_renderer, target) == 0);
        SDL_RenderSetViewport(engine.m_renderer, nullptr);
        auto pixel = [&](int x, int y) {
            Uint32 value = 0; SDL_Rect rect{x,y,1,1};
            CHECK(SDL_RenderReadPixels(engine.m_renderer, &rect, SDL_PIXELFORMAT_ABGR8888, &value, 4) == 0);
            return value;
        };
        scene.render();
        CHECK((pixel(25,125) & 0xffffff) == 0xff0000); // Blue covers red.
        CHECK((pixel(25,25) & 0xffffff) == 0); // Aspect ratio is preserved.
        bool glyphFound = false;
        for (int y = 220; y < 270 && !glyphFound; ++y)
            for (int x = 160; x < 280 && !glyphFound; ++x) glyphFound = (pixel(x,y) & 0xffffff) != 0;
        CHECK(glyphFound);
        CHECK(scene.setActorVisible("blue", false)); scene.render();
        CHECK((pixel(25,125) & 0xffffff) == 0x0000ff);
        auto& blue = document["props"][1]; blue["opacity"] = .5;
        CHECK(scene.setEditorEntity(blue, true)); scene.render();
        const auto mixed = pixel(25,125);
        CHECK((mixed & 255) > 110 && (mixed & 255) < 145);
        CHECK(((mixed >> 16) & 255) > 110 && ((mixed >> 16) & 255) < 145);
        CHECK(scene.setActorPosition("blue", {140, 30})); scene.render();
        CHECK((pixel(25,125) & 0xffffff) == 0x0000ff);
        blue["marker"] = "destination";
        CHECK(scene.setEditorEntity(blue, true));
        auto marker = sequence::MarkerFromJson(document["markers"][0]); marker.x = 150;
        scene.setEditorMarkers({marker});
        sequence::Vec2 position; CHECK(scene.getActorPosition("blue", position)); CHECK(near(position.x, 150));
        scene.removeActorOrProp("blue"); CHECK(!scene.hasProp("blue"));
        document["markers"][0] = sequence::MarkerToJson(marker);
        const auto path = temporary / "image_scene.json";
        { std::ofstream file(path); file << document.dump(2); }
        CHECK(scene.loadScene(path.string(), {}));
        CHECK(scene.imageEntityBounds("blue", bounds)); CHECK(near(bounds.x, 150));
        CHECK(scene.loadScene("houska_intro_2d", {}));
        CHECK(near(scene.imageCanvasSize().x, 1672)); CHECK(near(scene.imageCanvasSize().y, 941));
        scene.render();
        CHECK((pixel(200,200) & 0xffffff) != 0);
        loadFixture(engine);
        const auto interior = temporary / "image_transition.json"; CHECK(engine.saveInterior(interior.string()));
        nlohmann::json spatial = {{"interior", interior.string()}};
        CHECK(scene.loadScene("spatial", {}, &spatial)); CHECK(!scene.isImageScene());
        CHECK(scene.loadScene(path.string(), {})); CHECK(scene.isImageScene());
        auto missing = document; missing["background_image"] = "assets/does-not-exist.png";
        CHECK(!scene.loadScene("missing", {}, &missing));
        CHECK(scene.loadScene(path.string(), {}));
        SDL_SetRenderTarget(engine.m_renderer, nullptr); SDL_DestroyTexture(target);
        scene.shutdown();
    }

    static void sequenceNodeDeletion(const fs::path& temporary)
    {
        const nlohmann::json fixture = {
            {"id", "node_deletion"}, {"start", "entry"},
            {"nodes", {
                {{"id", "entry"}, {"next", "middle"}, {"actions", nlohmann::json::array()}},
                {{"id", "middle"}, {"next", "finish"}, {"actions", {{{"type", "wait"}, {"duration", 1}}}}},
                {{"id", "finish"}, {"actions", nlohmann::json::array()}},
                {{"id", "branch"}, {"actions", {
                    {{"type", "goto"}, {"target", "middle"}},
                    {{"type", "parallel"}, {"actions", {
                        {{"type", "goto"}, {"node", "middle"}},
                        {{"type", "require_flag"}, {"flag", "ready"}, {"on_fail", "middle"}, {"goto", "middle"}},
                        {{"type", "show_text"}, {"text", "middle"}}
                    }}}
                }}}
            }},
            {"on_skip", {{{"type", "goto"}, {"target", "middle"}}}}
        };
        auto parsed = sequence::SequenceFromJson(fixture, "node deletion fixture");
        CHECK(parsed.ok);
        auto& definition = parsed.sequence;
        const auto original = sequence::SequenceToJson(definition);
        CHECK(!sequence::DeleteNode(definition, "missing"));
        CHECK(sequence::SequenceToJson(definition) == original);
        CHECK(sequence::DeleteNode(definition, "middle"));
        CHECK(definition.nodes.size() == 3 && !definition.findNode("middle"));
        CHECK(definition.start == "entry");
        CHECK(definition.findNode("entry")->next == "finish");
        const auto& branch = definition.findNode("branch")->actions;
        CHECK(branch[0].data.at("target") == "finish");
        CHECK(branch[1].children[0].data.at("target") == "finish");
        CHECK(branch[1].children[1].data.at("on_fail") == "finish");
        CHECK(branch[1].children[1].data.at("goto") == "finish");
        CHECK(branch[1].children[2].data.at("text") == "middle");
        CHECK(definition.onSkipActions[0].data.at("target") == "finish");
        CHECK(sequence::SequenceFromJson(sequence::SequenceToJson(definition), "after delete").ok);
        CHECK(sequence::DeleteNode(definition, "entry"));
        CHECK(definition.start == "finish");
        // Removing the start/terminal node must not leave broken jump targets.
        CHECK(sequence::DeleteNode(definition, "finish"));
        CHECK(definition.start == "branch");
        const auto& remaining = definition.nodes[0].actions;
        CHECK(remaining[0].type == "end_sequence");
        CHECK(remaining[1].children[0].type == "end_sequence");
        CHECK(!remaining[1].children[1].data.contains("on_fail"));
        CHECK(!remaining[1].children[1].data.contains("goto"));
        CHECK(definition.onSkipActions[0].type == "end_sequence");
        const auto saved = temporary / "deleted_nodes.json";
        CHECK(sequence::SaveSequenceToFile(definition, saved.string()));
        auto reloaded = sequence::LoadSequenceFromFile(saved.string());
        CHECK(reloaded.ok);
        CHECK(sequence::SequenceToJson(reloaded.sequence) == sequence::SequenceToJson(definition));
        CHECK(sequence::DeleteNode(definition, "branch"));
        CHECK(definition.nodes.empty() && definition.start.empty());
        CHECK(!sequence::DeleteNode(definition, "branch"));

        auto cycle = sequence::SequenceFromJson({{"id", "cycle"}, {"start", "a"}, {"nodes", {
            {{"id", "a"}, {"next", "b"}}, {{"id", "b"}, {"next", "a"}}
        }}}, "cycle");
        CHECK(cycle.ok && sequence::DeleteNode(cycle.sequence, "b"));
        CHECK(cycle.sequence.nodes[0].next.empty());
        CHECK(sequence::SequenceFromJson(sequence::SequenceToJson(cycle.sequence), "after cycle delete").ok);
        auto self = sequence::SequenceFromJson({{"id", "self"}, {"start", "a"}, {"nodes", {
            {{"id", "a"}, {"next", "a"}}, {{"id", "b"}, {"next", "a"}}
        }}}, "self");
        CHECK(self.ok && sequence::DeleteNode(self.sequence, "a"));
        CHECK(self.sequence.start == "b" && self.sequence.nodes[0].next.empty());
    }

    static void cameraAndActorMotion(Engine& engine, const fs::path& temporary)
    {
        loadFixture(engine);
        const auto interior = temporary / "motion_environment.json";
        CHECK(engine.saveInterior(interior.string()));
        CinematicScene scene;
        CHECK(scene.init(engine.m_window, engine.m_renderer, nullptr));
        const nlohmann::json document = {
            {"id", "motion"}, {"interior", interior.string()},
            {"camera", {{"x", 2}, {"y", 6}, {"height", 1.6}, {"angle_degrees", 350}, {"pitch", .1}}},
            {"actors", {{{"id", "hero"}, {"texture", "assets/Interior/sprite_crate.png"}, {"x", 8}, {"y", 6}}}},
            {"markers", {{{"id", "shot"}, {"type", "camera"}, {"x", 4}, {"y", 6}, {"z", 2}, {"angle_degrees", 10}, {"pitch", .3}}}}
        };
        CHECK(scene.loadScene("motion", {}, &document));
        CHECK(scene.hasActor("hero"));
        const auto initial = scene.cameraPose();
        MotionContext context(scene);
        sequence::SequenceRuntime runtime;
        auto definition = MotionSequence({{{"type", "camera_move"}, {"target_actor", "hero"},
            {"distance", 2}, {"height", 2.4}, {"pitch", .2}, {"duration", 2}, {"follow_after", true},
            {"look_at", ""}, {"follow_actor", ""}}});
        const auto saved = sequence::SequenceToJson(definition);
        auto reloaded = sequence::SequenceFromJson(saved, "motion round trip");
        CHECK(reloaded.ok && saved == sequence::SequenceToJson(reloaded.sequence));
        CHECK(runtime.start(&definition, &context));
        auto tick = [&](float dt) { runtime.update(dt); scene.update(dt); };
        tick(0);
        tick(.5f);
        sequence::Vec2 hero;
        CHECK(scene.getActorPosition("hero", hero) && near(hero.x, 8) && near(hero.y, 6));
        CHECK(near(scene.cameraPose().position.x, 2.625)); // smoothstep at 1/4
        CHECK(near(scene.cameraPose().angleDegrees, 351.5625)); // no immediate turn on approach
        tick(.5f); tick(.5f); tick(.5f);
        auto pose = scene.cameraPose();
        CHECK(near(pose.position.x, 6) && near(pose.position.y, 6));
        CHECK(near(pose.height, 2.4) && near(pose.pitch, .2));
        CHECK(scene.status().cameraFollowActor == "hero");
        scene.update(0);
        CHECK(near(scene.cameraPose().position.x, pose.position.x)); // no jump when follow takes over

        definition = MotionSequence({{{"type", "move_actor"}, {"actor", "hero"}, {"x", 12}, {"y", 8}, {"duration", 2}}});
        CHECK(runtime.start(&definition, &context));
        tick(0); tick(.5f);
        CHECK(scene.getActorPosition("hero", hero));
        pose = scene.cameraPose();
        CHECK(near(pose.position.x, hero.x - 2) && near(pose.position.y, hero.y));
        CHECK(near(pose.height, 2.4) && near(pose.pitch, .2));

        // Free camera movement detaches follow and leaves the actor untouched.
        const auto stationaryHero = hero;
        definition = MotionSequence({{{"type", "camera_move"}, {"target_marker", "shot"}, {"duration", 2}, {"easing", "linear"}}});
        CHECK(runtime.start(&definition, &context));
        tick(0);
        CHECK(scene.status().cameraFollowActor.empty());
        tick(1); tick(1);
        CHECK(scene.getActorPosition("hero", hero) && near(hero.x, stationaryHero.x) && near(hero.y, stationaryHero.y));
        CHECK(near(scene.cameraPose().position.x, 4) && near(scene.cameraPose().height, 2));
        CHECK(near(scene.cameraPose().angleDegrees, 10) && near(scene.cameraPose().pitch, .3));
        const auto freeCamera = scene.cameraPose();
        definition = MotionSequence({{{"type", "move_actor"}, {"actor", "hero"}, {"x", 8}, {"y", 6}, {"duration", 1}}});
        CHECK(runtime.start(&definition, &context));
        tick(0); tick(1);
        CHECK(near(scene.cameraPose().position.x, freeCamera.position.x) && near(scene.cameraPose().position.y, freeCamera.position.y));

        // A camera authored before the actor in a parallel group still sees this frame's actor position.
        scene.setCameraPose(initial);
        CHECK(scene.setActorPosition("hero", {8, 6}));
        definition = MotionSequence({{{"type", "parallel"}, {"actions", {
            {{"type", "camera_move"}, {"target_actor", "hero"}, {"distance", 2}, {"duration", 2}, {"easing", "linear"}, {"follow_after", true}},
            {{"type", "move_actor"}, {"actor", "hero"}, {"x", 12}, {"y", 6}, {"duration", 2}, {"easing", "linear"}}
        }}}});
        CHECK(runtime.start(&definition, &context));
        tick(0); tick(.5f);
        CHECK(scene.getActorPosition("hero", hero) && near(hero.x, 9));
        CHECK(near(scene.cameraPose().position.x, 3.25));
        tick(.5f); tick(.5f); tick(.5f);
        CHECK(near(scene.cameraPose().position.x, 10));
        CHECK(scene.setActorPosition("hero", {12, 7}));
        scene.update(.01f);
        CHECK(near(scene.cameraPose().position.x, 10) && near(scene.cameraPose().position.y, 7));

        // Turning follow off, or following with smoothing, is also independent of actor placement.
        definition = MotionSequence({{{"type", "camera_follow"}, {"enabled", false}}});
        CHECK(runtime.start(&definition, &context)); tick(0);
        CHECK(scene.status().cameraFollowActor.empty());
        sequence::CameraFollowSettings follow;
        follow.smoothing = 4;
        scene.setCameraFollow("hero", follow);
        CHECK(scene.setActorPosition("hero", {12, 8}));
        scene.update(.25f);
        CHECK(near(scene.cameraPose().position.y, 7 + (1 - std::exp(-1.f))));

        // Insertion keeps both motions alive when the camera lasts longer than the existing walk.
        scene.setCameraPose(initial);
        CHECK(scene.setActorPosition("hero", {8, 6}));
        definition = MotionSequence({
            {{"type", "load_scene"}, {"scene", "fixture"}},
            {{"type", "move_actor"}, {"actor", "hero"}, {"x", 12}, {"y", 6}, {"duration", 2}},
            {{"type", "end_sequence"}}
        });
        auto approach = MotionSequence({{{"type", "camera_move"}, {"target_actor", "hero"},
            {"distance", 2}, {"duration", 4}, {"follow_after", true}}}).nodes[0].actions[0];
        sequence::InsertTimedAction(definition.nodes[0], approach, 0);
        CHECK(runtime.start(&definition, &context));
        tick(0); tick(1); tick(1);
        CHECK(runtime.isRunning() && scene.getActorPosition("hero", hero) && near(hero.x, 12));
        CHECK(scene.cameraPose().position.x < 10);
        tick(1); tick(1);
        CHECK(runtime.isCompleted() && near(scene.cameraPose().position.x, 10));
        CHECK(scene.status().cameraFollowActor == "hero");
        scene.shutdown();
    }

    static void motionTimeline()
    {
        auto definition = MotionSequence({
            {{"type", "load_scene"}, {"scene", "fixture"}},
            {{"type", "move_actor"}, {"actor", "hero"}, {"x", 12}, {"duration", 2}},
            {{"type", "end_sequence"}}
        });
        auto camera = MotionSequence({{{"type", "camera_move"}, {"target_actor", "hero"}, {"duration", 4}}}).nodes[0].actions[0];
        auto& node = definition.nodes[0];
        auto path = sequence::InsertTimedAction(node, camera, 0);
        CHECK(node.actions[0].type == "load_scene" && path == std::vector<int>({1, 1}));
        CHECK(node.actions[1].type == "parallel" && node.actions[1].children[0].type == "move_actor");
        CHECK(sequence::FindTimelineAction(node, path)->blocking);
        auto clips = sequence::BuildTimeline(node);
        CHECK(near(clips.back().start, 4)); // longer camera finishes before end_sequence
        CHECK(near(clips[2].start, 0) && near(clips[3].start, 0));

        // Starting within a movement or its delay never postpones the existing actor's start.
        node.actions = {node.actions[1].children[0]};
        sequence::SetActionDelay(node.actions[0], 2);
        sequence::SetActionDuration(camera, 1);
        path = sequence::InsertTimedAction(node, camera, .5f);
        CHECK(path == std::vector<int>({0}) && near(sequence::BuildTimeline(node).back().start, 2));
        sequence::SetActionDuration(camera, 5);
        path = sequence::InsertTimedAction(node, camera, 3);
        CHECK(path.size() == 2 && sequence::FindTimelineAction(node, path)->blocking);
        clips = sequence::BuildTimeline(node);
        CHECK(near(clips[2].start, 2) && near(clips[3].start, 3));
        CHECK(near(sequence::ActionTimelineDuration(node.actions[1]), 6));

        auto overlap = MotionSequence({{{"type", "move_actor"}, {"startDelay", 2}, {"duration", 2}}}).nodes[0];
        path = sequence::InsertTimedAction(overlap, camera, 1);
        clips = sequence::BuildTimeline(overlap);
        CHECK(path.size() == 2 && near(clips[1].start, 2) && near(clips[2].start, 1));

        auto ending = MotionSequence({{{"type", "end_sequence"}}}).nodes[0];
        path = sequence::InsertTimedAction(ending, camera, 0);
        CHECK(path == std::vector<int>({0}) && ending.actions[1].type == "end_sequence");
    }

    static void legacyConversion(Engine& engine)
    {
        engine.m_polygonGeometryPrimary = false;
        engine.m_polygonSectors.clear(); engine.m_wallSegments.clear();
        engine.m_doors.clear(); engine.m_sprites.clear();
        engine.m_sectors.at('O').floorSlopeX = 0.0;
        engine.m_sectors.at('O').floorSlopeY = 0.0;
        engine.m_grid = {{'1','1','1','1'}, {'1',0,0,'1'}, {'1',0,'1','1'}, {'1','1','1','1'}};
        engine.m_sectorGrid.assign(4, "OOOO");
        CHECK(engine.convertEditorGridToPolygons());
        CHECK(engine.canStandAt(1.5, 1.5));
        CHECK(engine.canStandAt(2.5, 1.5));
        CHECK(engine.canStandAt(1.5, 2.5));
        CHECK(!engine.canStandAt(2.5, 2.5));
        CHECK(engine.canMoveBetween(1.5, 1.5, 1.5, 2.5));
    }

    static void run(SDL_Window* window, SDL_Renderer* renderer)
    {
        TemporaryDirectory temporary;
        auto instance = std::make_unique<Engine>();
        Engine& engine = *instance;
        engine.m_window = window;
        engine.m_renderer = renderer;
        movement(engine);
        picking(engine);
        floorSprites(engine, temporary.path);
        spatialProjection(engine);
        mapInteraction(engine);
        dimensions(engine, temporary.path);
        projectObjectLibrary(engine, temporary.path);
        inventorySprites(engine, temporary.path);
        projectObjectPickerInteraction(engine);
        sectorInsertion(engine, temporary.path);
        sceneEntities(engine, temporary.path);
        imageScenes(engine, temporary.path);
        sequenceNodeDeletion(temporary.path);
        cameraAndActorMotion(engine, temporary.path);
        motionTimeline();
        legacyConversion(engine);
    }
};

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_Window* window = SDL_CreateWindow("Interior editor tests", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    if (!renderer) { SDL_Quit(); return 1; }
    IMG_Init(IMG_INIT_PNG);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    int result = 0;
    try
    {
        BuildInteriorEditorTestAccess::run(window, renderer);
        std::cout << "Interior editor geometry, scenes, node deletion, camera/actor motion, timeline and persistence tests passed.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    ImGui::DestroyContext();
    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
