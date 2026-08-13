#include "Editor.h"
#include "EditorHelpers.h"
#include "PathUtils.h"

#include <algorithm>
#include <random>
#include <string>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <nlohmann/json.hpp>

#include <SDL.h>
#include <SDL_image.h>
#include "imgui.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "Comdlg32.lib")
#endif
namespace fs = std::filesystem;

namespace
{
    constexpr float kEditorTopMenuHeight = 76.0f;
    constexpr float kEditorWindowMargin = 8.0f;

    struct EditorCoreForageSpriteDef
    {
        std::string id;
        std::string image;
        int x = 0;
        int y = 0;
        int w = 32;
        int h = 32;
        int pivotX = 16;
        int pivotY = 32;
    };

    std::vector<EditorCoreForageSpriteDef> g_editorCoreForageSprites;
    std::unordered_map<std::string, SDL_Texture*> g_editorCoreForageTextures;
    bool g_editorCoreForageSpritesLoaded = false;

    static bool LoadEditorCoreForageSprites()
    {
        if (g_editorCoreForageSpritesLoaded)
            return true;

        g_editorCoreForageSpritesLoaded = true;
        g_editorCoreForageSprites.clear();

        const fs::path p = pathutils::DataDir() / "foraging" / "forage_sprites.json";
        std::ifstream f(p, std::ios::binary);
        if (!f)
            return false;

        nlohmann::json root;
        try { f >> root; }
        catch (...) { return false; }

        if (!root.contains("sprites") || !root["sprites"].is_array())
            return false;

        for (const auto& js : root["sprites"])
        {
            EditorCoreForageSpriteDef sp;
            sp.id = js.value("id", "");
            sp.image = js.value("image", "");

            if (js.contains("src") && js["src"].is_object())
            {
                const auto& r = js["src"];
                sp.x = r.value("x", 0);
                sp.y = r.value("y", 0);
                sp.w = r.value("w", 32);
                sp.h = r.value("h", 32);
            }

            if (js.contains("pivot") && js["pivot"].is_object())
            {
                const auto& pv = js["pivot"];
                sp.pivotX = pv.value("x", sp.w / 2);
                sp.pivotY = pv.value("y", sp.h);
            }

            if (!sp.id.empty() && !sp.image.empty() && sp.w > 0 && sp.h > 0)
                g_editorCoreForageSprites.push_back(std::move(sp));
        }

        return !g_editorCoreForageSprites.empty();
    }

    static const EditorCoreForageSpriteDef* FindEditorCoreForageSprite(const std::string& id)
    {
        if (id.empty())
            return nullptr;

        LoadEditorCoreForageSprites();

        for (const auto& sp : g_editorCoreForageSprites)
        {
            if (sp.id == id)
                return &sp;
        }
        return nullptr;
    }

    static SDL_Texture* LoadEditorCoreForageTexture(SDL_Renderer* renderer, const std::string& image)
    {
        if (!renderer || image.empty())
            return nullptr;

        auto it = g_editorCoreForageTextures.find(image);
        if (it != g_editorCoreForageTextures.end())
            return it->second;

        const fs::path p = pathutils::ProjectRoot() / "assets" / "Foraging" / image;
        SDL_Texture* tex = IMG_LoadTexture(renderer, p.string().c_str());
        if (!tex)
            return nullptr;

        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        g_editorCoreForageTextures[image] = tex;
        return tex;
    }

    static const EditorForageArchetype* FindEditorCoreForageArchetype(
        const std::vector<EditorForageArchetype>& archetypes,
        const std::string& id)
    {
        for (const auto& a : archetypes)
        {
            if (a.id == id)
                return &a;
        }
        return nullptr;
    }

    static std::string ResolveEditorCoreForageSpriteId(
        const EditorForageSpawn& spawn,
        const std::vector<EditorForageArchetype>& archetypes)
    {
        if (!spawn.genericMapSpriteOverride.empty())
            return spawn.genericMapSpriteOverride;

        const auto* archetype = FindEditorCoreForageArchetype(archetypes, spawn.archetypeId);
        if (archetype && !archetype->genericMapSprite.empty())
            return archetype->genericMapSprite;

        return {};
    }

    static float ResolveEditorCoreForageScale(
        const EditorForageSpawn& spawn,
        const std::vector<EditorForageArchetype>& archetypes)
    {
        if (spawn.mapScaleOverride > 0.0f)
            return std::clamp(spawn.mapScaleOverride, 0.05f, 4.0f);

        const auto* archetype = FindEditorCoreForageArchetype(archetypes, spawn.archetypeId);
        if (archetype && archetype->mapScale > 0.0f)
            return std::clamp(archetype->mapScale, 0.05f, 4.0f);

        return 0.35f;
    }


    static void RenderEditorCoreForageFallbackMarker(
        SDL_Renderer* renderer,
        ImDrawList* drawList,
        const EditorForageSpawn& spawn,
        bool selected,
        int tileSize,
        int camX,
        int camY,
        float zoom)
    {
        SDL_Rect tileRect{
            spawn.tileX * tileSize - camX,
            spawn.tileY * tileSize - camY,
            tileSize,
            tileSize
        };

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, selected ? 60 : 30, selected ? 255 : 190, selected ? 120 : 80, selected ? 145 : 90);
        SDL_RenderFillRect(renderer, &tileRect);

        SDL_SetRenderDrawColor(renderer, selected ? 140 : 80, 255, selected ? 170 : 120, 255);
        SDL_RenderDrawRect(renderer, &tileRect);

        if (drawList)
        {
            const ImVec2 p(
                ((float)(spawn.tileX * tileSize) - (float)camX) * zoom,
                ((float)(spawn.tileY * tileSize) - (float)camY) * zoom);

            const std::string label = spawn.archetypeId.empty() ? spawn.id : spawn.archetypeId;
            drawList->AddText(
                ImVec2(p.x + 3.0f, p.y + 3.0f),
                IM_COL32(120, 255, 160, 255),
                label.c_str());
        }
    }

    static void RenderEditorCoreForageSpawns(
        SDL_Renderer* renderer,
        ImDrawList* drawList,
        const std::vector<EditorForageSpawn>& spawns,
        const std::vector<EditorForageArchetype>& archetypes,
        int selectedIndex,
        int tileSize,
        int camX,
        int camY,
        float zoom)
    {
        LoadEditorCoreForageSprites();

        for (int i = 0; i < (int)spawns.size(); ++i)
        {
            const auto& spawn = spawns[i];
            const bool selected = (i == selectedIndex);

            const std::string spriteId = ResolveEditorCoreForageSpriteId(spawn, archetypes);
            const auto* sprite = FindEditorCoreForageSprite(spriteId);
            SDL_Texture* tex = sprite ? LoadEditorCoreForageTexture(renderer, sprite->image) : nullptr;

            if (!sprite || !tex)
            {
                RenderEditorCoreForageFallbackMarker(renderer, drawList, spawn, selected, tileSize, camX, camY, zoom);
                continue;
            }

            const int worldPivotX = spawn.tileX * tileSize + tileSize / 2;
            const int worldPivotY = spawn.tileY * tileSize + tileSize;

            const float mapScale = ResolveEditorCoreForageScale(spawn, archetypes);
            const int dstW = std::max(1, (int)std::lround((float)sprite->w * mapScale));
            const int dstH = std::max(1, (int)std::lround((float)sprite->h * mapScale));
            const int dstPivotX = (int)std::lround((float)sprite->pivotX * mapScale);
            const int dstPivotY = (int)std::lround((float)sprite->pivotY * mapScale);

            SDL_Rect src{ sprite->x, sprite->y, sprite->w, sprite->h };
            SDL_Rect dst{
                worldPivotX - dstPivotX - camX,
                worldPivotY - dstPivotY - camY,
                dstW,
                dstH
            };

            SDL_RenderCopy(renderer, tex, &src, &dst);

            if (selected)
            {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 120, 255, 160, 255);
                SDL_RenderDrawRect(renderer, &dst);

                SDL_Rect tileRect{
                    spawn.tileX * tileSize - camX,
                    spawn.tileY * tileSize - camY,
                    tileSize,
                    tileSize
                };
                SDL_SetRenderDrawColor(renderer, 120, 255, 160, 120);
                SDL_RenderDrawRect(renderer, &tileRect);
            }
        }
    }
}


bool Editor::init(SDL_Window* window, SDL_Renderer* renderer)
{
    m_window = window;
    m_renderer = renderer;
    if (!m_window || !m_renderer) return false;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    m_tileSize = 32;

    if (!m_tileset.loadFromJson(
        m_renderer,
        "assets/Tileset/terrain_tiles.json",
        "assets/Tileset/terrain_atlas.png"))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tileset error",
            "terrain_tiles.json / terrain_atlas.png load failed", m_window);
        return false;
    }

    {
        std::string err;
        if (!m_objCatalog.LoadFromFile("assets/Objects/TreeAndStoneSprites.json", &err)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Objects JSON error (TreeAndStoneSprites)", err.c_str(), m_window);
            return false;
        }

        if (!m_objCatalog.AppendFromFile("assets/Objects/TechObjects.json", &err)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Objects JSON error (TechObjects)", err.c_str(), m_window);
            return false;
        }

        if (!m_objCatalog.AppendFromFile("assets/Objects/CastleObjects.json", &err)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Objects JSON error (CastleObjects)", err.c_str(), m_window);
            return false;
        }

        if (!m_objCatalog.AppendFromFile("assets/Objects/Houses.json", &err)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Objects JSON error (Houses)", err.c_str(), m_window);
            return false;
        }

        if (!m_objCatalog.AppendFromFile("assets/Objects/Decoration.json", &err)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Objects JSON error (Decoration)", err.c_str(), m_window);
            return false;
        }

        if (!loadObjectAtlases(&err)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Objects atlas error", err.c_str(), m_window);
            return false;
        }

        if (!loadQuests()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Quest load error",m_lastIoStatus.c_str(),m_window);
            return false;
        }
    }

    if (!m_characterManager.init(m_renderer, "assets/characters")) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "CharacterManager error",
            m_characterManager.lastError().c_str(),
            m_window
        );
        return false;
    }

    m_projectRoot = pathutils::ProjectRoot().string();
    m_mapsDir = pathutils::MapsDir().string();

    std::error_code ec;
    fs::create_directories(pathutils::MapsDir(), ec);
    fs::create_directories(pathutils::NpcsDir(), ec);

    rebuildCompositeGroups();
    newMap();

    m_mapPath = (pathutils::MapsDir() / "test.rvm").string();
    m_lastIoStatus = std::string("Maps dir: ") + m_mapsDir;

    if (!loadNpcTypes()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NPC Types load error", m_lastIoStatus.c_str(), m_window);
        return false;
    }

    if (!loadDialogs()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dialogs load error", m_lastIoStatus.c_str(), m_window);
        return false;
    }

    if (!loadNpcSchedules()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"NPC Schedules load error",m_lastIoStatus.c_str(),
            m_window
        );
        return false;
    }

    if (!loadForageSprites())
    {
        m_lastIoStatus = "Forage sprites load failed.";
    }

    if (!loadForageArchetypes())
    {
        m_lastIoStatus = "Forage archetypes load failed.";
    }

    if (!loadForageSpecies())
    {
        m_lastIoStatus = "Forage species load failed.";
    }

    return true;
}

void Editor::shutdown()
{
    m_characterManager.shutdown();
    m_tileset.destroyAtlas();

    destroyObjectAtlases();

    m_renderer = nullptr;
    m_window = nullptr;
}

void Editor::handleEvent(const SDL_Event& e)
{
    if (e.type == SDL_QUIT)
    {
        m_running = false;
        return;
    }

    // --- mouse move ---
    if (e.type == SDL_MOUSEMOTION)
    {
        m_mouseX = e.motion.x;
        m_mouseY = e.motion.y;

        if (m_zoneResizeActive)
        {
            const int worldX = (int)(m_mouseX / m_zoom) + m_camX;
            const int worldY = (int)(m_mouseY / m_zoom) + m_camY;
            const int tx = worldX / m_tileSize;
            const int ty = worldY / m_tileSize;

            updateZoneDrag(tx, ty);
            return;
        }
    }

    // --- key down ---
    if (e.type == SDL_KEYDOWN && e.key.repeat == 0)
    {
        const bool ctrlHeld = (e.key.keysym.mod & KMOD_CTRL) != 0;

        if (ctrlHeld && e.key.keysym.sym == SDLK_z)
        {
            const bool shiftHeld = (e.key.keysym.mod & KMOD_SHIFT) != 0;
            if (shiftHeld)
                redoLastStep();
            else
                undoLastStep();
            return;
        }

        if (ctrlHeld && e.key.keysym.sym == SDLK_y)
        {
            redoLastStep();
            return;
        }

        if (e.key.keysym.sym == SDLK_F11)
        {
            toggleFullscreen();
            return;
        }

        if (m_brushMode == BrushMode::Terrain)
        {
            if (e.key.keysym.sym == SDLK_1) { m_brushTile = 1; return; }
            if (e.key.keysym.sym == SDLK_2) { m_brushTile = 2; return; }
            if (e.key.keysym.sym == SDLK_3) { m_brushTile = 3; return; }
        }
    }

    // --- mouse down ---
    if (e.type == SDL_MOUSEBUTTONDOWN)
    {
        ImGuiIO& io = ImGui::GetIO();

        m_mouseX = e.button.x;
        m_mouseY = e.button.y;

        const int worldX = (int)(m_mouseX / m_zoom) + m_camX;
        const int worldY = (int)(m_mouseY / m_zoom) + m_camY;
        const int tx = worldX / m_tileSize;
        const int ty = worldY / m_tileSize;

        if (e.button.button == SDL_BUTTON_LEFT)
        {
            // 1) zone pick mode
            if (m_zonePickMode && !io.WantCaptureMouse)
            {
                if (m_selectedNpcZoneIndex >= 0 &&
                    m_selectedNpcZoneIndex < (int)m_npcZones.size())
                {
                    auto& z = m_npcZones[m_selectedNpcZoneIndex];

                    if (!m_zonePickHasStart)
                    {
                        m_zonePickStartX = tx;
                        m_zonePickStartY = ty;
                        m_zonePickHasStart = true;
                        m_lastIoStatus = "Prvni roh zony vybran. Klikni na druhy roh.";
                    }
                    else
                    {
                        pushUndoState("set zone rect");

                        z.minX = std::min(m_zonePickStartX, tx);
                        z.minY = std::min(m_zonePickStartY, ty);
                        z.maxX = std::max(m_zonePickStartX, tx);
                        z.maxY = std::max(m_zonePickStartY, ty);

                        m_zonePickMode = false;
                        m_zonePickHasStart = false;
                        m_mapDirty = true;
                        m_lastIoStatus = "Zona nastavena z mapy.";
                    }
                }

                return;
            }

            // 2) zone resize / move
            if (!io.WantCaptureMouse && !m_zonePickMode)
            {
                ZoneDragHandle h = hitTestZoneHandle(tx, ty);
                if (h != ZoneDragHandle::None)
                {
                    beginZoneDrag(tx, ty, h);
                    return;
                }
            }

            // 3) normal LMB painting
            m_lmbDown = true;

            if (!io.WantCaptureMouse &&
                !m_zoneResizeActive &&
                !m_zonePickMode &&
                !m_undoGestureActive)
            {
                pushUndoState("paint");
                m_undoGestureActive = true;
            }

            return;
        }

        if (e.button.button == SDL_BUTTON_RIGHT)
        {
            m_rmbDown = true;

            if (!io.WantCaptureMouse &&
                !m_zoneResizeActive &&
                !m_zonePickMode &&
                !m_undoGestureActive)
            {
                pushUndoState("erase");
                m_undoGestureActive = true;
            }

            return;
        }
    }

    // --- mouse up ---
    if (e.type == SDL_MOUSEBUTTONUP)
    {
        m_mouseX = e.button.x;
        m_mouseY = e.button.y;

        if (e.button.button == SDL_BUTTON_LEFT)
        {
            if (m_zoneResizeActive)
            {
                endZoneDrag();
                return;
            }

            m_lmbDown = false;
            m_undoGestureActive = false;
            return;
        }

        if (e.button.button == SDL_BUTTON_RIGHT)
        {
            m_rmbDown = false;
            m_undoGestureActive = false;
            return;
        }
    }

    // --- wheel ---
    if (e.type == SDL_MOUSEWHEEL)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse)
            return;

        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        const bool ctrlHeld =
            ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL];

        if (ctrlHeld || m_brushMode == BrushMode::Castles)
        {
            if (e.wheel.y > 0)
                m_zoom *= 1.15f;
            else if (e.wheel.y < 0)
                m_zoom /= 1.15f;

            m_zoom = std::clamp(m_zoom, 0.20f, 4.0f);
            return;
        }

        if (m_brushMode == BrushMode::Terrain)
        {
            int b = (int)m_brushTile + (e.wheel.y > 0 ? 1 : -1);
            if (b < 1) b = 3;
            if (b > 3) b = 1;
            m_brushTile = (uint16_t)b;
            return;
        }
    }
}

void Editor::update(float dt)
{
    ImGuiIO& io = ImGui::GetIO();

    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    const int speed = 500;
    int dx = 0, dy = 0;
    if (ks[SDL_SCANCODE_LEFT])  dx -= 1;
    if (ks[SDL_SCANCODE_RIGHT]) dx += 1;
    if (ks[SDL_SCANCODE_UP])    dy -= 1;
    if (ks[SDL_SCANCODE_DOWN])  dy += 1;
    m_camX += (int)(dx * speed * dt);
    m_camY += (int)(dy * speed * dt);

    auto surfaceFromBrush = [&](uint16_t t) -> std::string {
        if (t == 2) return "water";
        if (t == 3) return "mud";
        return "grass";
        };

    auto randomVarForFill = [&](const std::string& surface) -> uint8_t {
        int cnt = m_tileset.fillCount(surface);
        if (cnt <= 0) return 0;
        std::uniform_int_distribution<int> d(0, cnt - 1);
        return (uint8_t)d(m_rng);
        };

    auto enforceWaterMudBufferInRect = [&](int minx, int miny, int maxx, int maxy)
        {
            minx -= 2; miny -= 2; maxx += 2; maxy += 2;

            for (int y = miny; y <= maxy; ++y)
                for (int x = minx; x <= maxx; ++x)
                {
                    uint16_t t = m_map.get(x, y);
                    if (t == 0) continue;

                    bool nearWater = false;
                    for (int ddy = -2; ddy <= 2 && !nearWater; ++ddy)
                        for (int ddx = -2; ddx <= 2 && !nearWater; ++ddx)
                        {
                            if (std::max(std::abs(ddx), std::abs(ddy)) > 2) continue;
                            if (m_map.get(x + ddx, y + ddy) == 2) nearWater = true;
                        }

                    if (nearWater && t != 2)
                    {
                        if (t != 3) {
                            m_map.set(x, y, 3);
                            m_map.clearOverride(x, y);
                            if (m_randomizeVariants) m_map.setVar(x, y, randomVarForFill("mud"));
                        }
                    }
                }
        };

    auto setBase = [&](int x, int y, uint16_t id)
        {
            if (m_map.get(x, y) == 0) return;
            m_map.set(x, y, id);
            m_map.clearOverride(x, y);

            if (m_randomizeVariants) {
                m_map.setVar(x, y, randomVarForFill(surfaceFromBrush(id)));
            }
        };

    auto paintWaterWithMudBuffer = [&](int cx, int cy)
        {
            setBase(cx, cy, 2);

            for (int ddy = -2; ddy <= 2; ++ddy)
                for (int ddx = -2; ddx <= 2; ++ddx)
                {
                    if (ddx == 0 && ddy == 0) continue;
                    if (std::abs(ddx) + std::abs(ddy) > 2) continue;

                    int x = cx + ddx;
                    int y = cy + ddy;

                    if (m_map.get(x, y) == 2) continue;
                    setBase(x, y, 3);
                }
        };

    if (m_lmbDown)
    {
        if (io.WantCaptureMouse) return;

        const int worldX = (int)(m_mouseX / m_zoom) + m_camX;
        const int worldY = (int)(m_mouseY / m_zoom) + m_camY;
        const int tx = worldX / m_tileSize;
        const int ty = worldY / m_tileSize;
        const int activeBrushSize = (m_brushMode == BrushMode::Terrain) ? m_brushSize : 1;
        const int r = activeBrushSize / 2;

        if (m_foragePlacementMode)
        {
            if (selectedForageBrushLabel() == "<none>")
                return;

            auto* existing = findForageSpawnAt(tx, ty);
            if (existing)
            {
                applySelectedForageBrushToSpawn(*existing);
                m_selectedForageSpawnIndex = findForageSpawnIndexAt(tx, ty);
                m_lastIoStatus = "Forage spawn updated.";
            }
            else
            {
                EditorForageSpawn s;
                s.id = makeUniqueForageSpawnId();
                s.tileX = tx;
                s.tileY = ty;
                applySelectedForageBrushToSpawn(s);
                s.quantityMin = 1;
                s.quantityMax = 1;
                s.rarity = 50;
                s.requiresExamination = true;

                m_forageSpawns.push_back(std::move(s));
                m_selectedForageSpawnIndex = (int)m_forageSpawns.size() - 1;
                m_lastIoStatus = "Forage spawn placed.";
            }

            m_mapDirty = true;
            return;
        }

        if (m_npcPlacementMode)
        {
            const int clickedNpcIndex = findNpcSpawnIndexAt(tx, ty);

            if (clickedNpcIndex >= 0)
            {
                m_selectedNpcSpawnIndex = clickedNpcIndex;
                loadNpcSpawnToBrush(m_npcSpawns[clickedNpcIndex]);
                m_lastIoStatus = "NPC loaded to form.";
                return;
            }

            if (std::string(m_npcId).empty())
            {
                m_lastIoStatus = "NPC ID nesmi byt prazdne.";
                return;
            }

            auto it = std::find_if(
                m_npcSpawns.begin(),
                m_npcSpawns.end(),
                [&](const NpcSpawn& n) { return n.id == m_npcId; });

            if (it != m_npcSpawns.end())
            {
                it->tileX = tx;
                it->tileY = ty;
                it->typeId = m_npcTypeId;
                it->name = m_npcName;
                it->surname = m_npcSurname;
                it->greeting = m_npcGreeting;
                it->scriptId = m_npcScriptId;
                it->characterId = m_npcCharacterId;
                it->hp = m_npcHP;
                it->mood = m_npcMood;

                m_selectedNpcSpawnIndex = (int)std::distance(m_npcSpawns.begin(), it);
                m_lastIoStatus = "NPC moved/updated.";
            }
            else
            {
                NpcSpawn n;
                n.id = m_npcId;
                n.typeId = m_npcTypeId;
                n.name = m_npcName;
                n.surname = m_npcSurname;
                n.greeting = m_npcGreeting;
                n.scriptId = m_npcScriptId;
                n.characterId = m_npcCharacterId;
                n.tileX = tx;
                n.tileY = ty;
                n.hp = m_npcHP;
                n.mood = m_npcMood;

                m_npcSpawns.push_back(std::move(n));
                m_selectedNpcSpawnIndex = (int)m_npcSpawns.size() - 1;
                m_lastIoStatus = "NPC placed.";
            }

            m_mapDirty = true;
            return;
        }

        if (m_brushMode == BrushMode::NatureObjects ||
            m_brushMode == BrushMode::TechObjects ||
            m_brushMode == BrushMode::Houses ||
            m_brushMode == BrushMode::Decoration ||
            m_brushMode == BrushMode::Castles)
        {
            int objIndex = -1;

            if (m_brushMode == BrushMode::NatureObjects)
            {
                const auto indices = BuildNatureObjectIndices(m_objCatalog, m_objFilter);
                if (m_selectedNatureObj >= 0 && m_selectedNatureObj < (int)indices.size())
                    objIndex = indices[m_selectedNatureObj];
            }
            else if (m_brushMode == BrushMode::TechObjects)
            {
                objIndex = FindNthTechObject(m_objCatalog, m_selectedTechObj, m_objFilter);
            }
            else if (m_brushMode == BrushMode::Castles)
            {
                const auto indices = BuildCastleObjectIndices(m_objCatalog, m_objFilter);
                if (m_selectedCastleObj >= 0 && m_selectedCastleObj < (int)indices.size())
                    objIndex = indices[m_selectedCastleObj];
            }
            else if (m_brushMode == BrushMode::Decoration)
            {
                const auto indices = BuildDecorationObjectIndices(m_objCatalog, m_objFilter);
                if (m_selectedDecorationObj >= 0 && m_selectedDecorationObj < (int)indices.size())
                    objIndex = indices[m_selectedDecorationObj];
            }
            else if (m_brushMode == BrushMode::Houses)
            {
                const auto indices = BuildHouseObjectIndices(m_objCatalog, m_objFilter);
                if (m_selectedHouseObj >= 0 && m_selectedHouseObj < (int)indices.size())
                    objIndex = indices[m_selectedHouseObj];
            }

            const auto& defs = m_objCatalog.Objects();
            if (objIndex >= 0 && objIndex < (int)defs.size())
            {
                const auto& def = defs[objIndex];
                const std::string objectId = def.id;
                std::uniform_int_distribution<int> dVar(0, 255);

                for (int yy = ty - r; yy <= ty + r; ++yy)
                    for (int xx = tx - r; xx <= tx + r; ++xx)
                    {
                        if (m_map.get(xx, yy) == 0) continue;

                        const auto& def = defs[objIndex];

                        if (def.HasTag("spawn"))
                        {
                            for (int y = 0; y < m_map.height(); ++y)
                                for (int x = 0; x < m_map.width(); ++x)
                                {
                                    const auto* d = m_map.getObjDefAt(m_objCatalog, x, y);
                                    if (!d) continue;

                                    if (d->HasTag("spawn"))
                                        m_map.clearObj(x, y);
                                }
                        }

                        m_map.setObjId(xx, yy, objectId);

                        uint8_t v = m_objBrushVar;
                        if (m_objRandomizeVar) v = (uint8_t)dVar(m_rng);
                        m_map.setObjVar(xx, yy, v);
                        m_map.setObjHP(xx, yy, m_objBrushHP);
                        m_map.setObjScale(xx, yy, m_objBrushScale);
                    }

                m_mapDirty = true;
            }

            return;
        }

        for (int yy = ty - r; yy <= ty + r; ++yy)
            for (int xx = tx - r; xx <= tx + r; ++xx)
            {
                if (m_terrainBrushMode == TerrainBrushMode::Composite &&
                    m_selectedCompositeGroup >= 0 &&
                    m_selectedCompositeGroup < (int)m_compositeGroups.size())
                {
                    const auto& g = m_compositeGroups[m_selectedCompositeGroup];

                    auto surfaceToTileIdLocal = [&](const std::string& s) -> uint16_t {
                        return (s == "mud") ? 3 : 1;
                        };

                    auto setSurfaceAtLocal = [&](int x, int y, const std::string& s)
                        {
                            m_map.set(x, y, surfaceToTileIdLocal(s));
                            m_map.clearOverride(x, y);
                            if (m_randomizeVariants) {
                                m_map.setVar(x, y, randomVarForFill(s));
                            }
                        };

                    std::string featureSurface = "grass";
                    for (int defIdx : g.defIndices)
                    {
                        const auto* d = m_tileset.tileByIndex(defIdx);
                        if (!d) continue;

                        if (!d->surface.empty()) featureSurface = d->surface;

                        const int px = xx + (d->composite_x - g.minx);
                        const int py = yy + (d->composite_y - g.miny);

                        setSurfaceAtLocal(px, py, featureSurface);
                        m_map.setOverride(px, py, (uint16_t)(defIdx + 1));
                    }

                    const int wTiles = (g.maxx - g.minx + 1);
                    const int hTiles = (g.maxy - g.miny + 1);

                    const int left = xx;
                    const int top = yy;
                    const int right = xx + wTiles - 1;
                    const int bottom = yy + hTiles - 1;

                    for (int x2 = left; x2 <= right; ++x2) {
                        setSurfaceAtLocal(x2, top - 1, featureSurface);
                        setSurfaceAtLocal(x2, bottom + 1, featureSurface);
                    }
                    for (int y2 = top; y2 <= bottom; ++y2) {
                        setSurfaceAtLocal(left - 1, y2, featureSurface);
                        setSurfaceAtLocal(right + 1, y2, featureSurface);
                    }

                    setSurfaceAtLocal(left - 1, top - 1, featureSurface);
                    setSurfaceAtLocal(right + 1, top - 1, featureSurface);
                    setSurfaceAtLocal(left - 1, bottom + 1, featureSurface);
                    setSurfaceAtLocal(right + 1, bottom + 1, featureSurface);

                    continue;
                }

                if (m_brushTile == 2) {
                    paintWaterWithMudBuffer(xx, yy);
                }
                else {
                    setBase(xx, yy, m_brushTile);
                }
            }

        enforceWaterMudBufferInRect(tx - r, ty - r, tx + r, ty + r);
        m_mapDirty = true;
    }

    if (m_rmbDown)
    {
        if (io.WantCaptureMouse) return;

        const int worldX = (int)(m_mouseX / m_zoom) + m_camX;
        const int worldY = (int)(m_mouseY / m_zoom) + m_camY;
        const int tx = worldX / m_tileSize;
        const int ty = worldY / m_tileSize;

        if (m_brushMode == BrushMode::NatureObjects ||
    m_brushMode == BrushMode::TechObjects ||
    m_brushMode == BrushMode::Castles ||
    m_brushMode == BrushMode::Decoration ||
    m_brushMode == BrushMode::Houses)
        {
            m_map.clearObj(tx, ty);
            m_mapDirty = true;
            return;
        }

        if (m_npcPlacementMode)
        {
            const int removedIndex = findNpcSpawnIndexAt(tx, ty);

            auto it = std::remove_if(m_npcSpawns.begin(), m_npcSpawns.end(),
                [&](const NpcSpawn& n) { return n.tileX == tx && n.tileY == ty; });

            if (it != m_npcSpawns.end()) {
                m_npcSpawns.erase(it, m_npcSpawns.end());

                if (m_selectedNpcSpawnIndex == removedIndex)
                    m_selectedNpcSpawnIndex = -1;

                m_mapDirty = true;
                m_lastIoStatus = "NPC removed.";
            }
            return;
        }
        else if (m_foragePlacementMode)
        {
            auto it = std::remove_if(
                m_forageSpawns.begin(),
                m_forageSpawns.end(),
                [&](const EditorForageSpawn& s)
                {
                    return s.tileX == tx && s.tileY == ty;
                });

            if (it != m_forageSpawns.end())
            {
                m_forageSpawns.erase(it, m_forageSpawns.end());
                m_lastIoStatus = "Forage spawn removed.";
                m_mapDirty = true;
            }
            return;
        }

        enforceWaterMudBufferInRect(tx - 2, ty - 2, tx + 2, ty + 2);
        m_map.clearOverride(tx, ty);
        m_mapDirty = true;
    }
}

void Editor::render()
{
    ImGui::GetIO().FontGlobalScale = m_uiFontScale;

    m_renderMapOutsideMapTab = true;

    renderMainToolbar();
    renderMapIoPopup();
    renderEditorWorkspace();
    renderDialogPreviewWindow();


    const bool shouldRenderMap = true;

    if (shouldRenderMap)
    {
        int screenW = 0, screenH = 0;
        SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);

        SDL_RenderSetScale(m_renderer, m_zoom, m_zoom);

        TerrainRenderer::View view;
        view.camX = m_camX;
        view.camY = m_camY;
        view.tileSize = m_tileSize;
        view.screenW = (int)(screenW / m_zoom);
        view.screenH = (int)(screenH / m_zoom);

        m_terrainRenderer.renderTerrain(m_renderer, m_tileset, m_map, view);

        const int firstTileX = m_camX / m_tileSize;
        const int firstTileY = m_camY / m_tileSize;
        const int offsetX = -(m_camX % m_tileSize);
        const int offsetY = -(m_camY % m_tileSize);

        {
            const int tilesWide = view.screenW / view.tileSize + 1;
            const int tilesHigh = view.screenH / view.tileSize + 1;

            ImDrawList* fg = ImGui::GetForegroundDrawList();

            for (int y = 0; y <= tilesHigh; ++y)
            {
                for (int x = 0; x <= tilesWide; ++x)
                {
                    const int mapX = firstTileX + x;
                    const int mapY = firstTileY + y;

                    const auto* def = m_map.getObjDefAt(m_objCatalog, mapX, mapY);
                    if (!def)
                        continue;

                    int wx = 0, wy = 0;
                    m_map.getObjPivotWorld(mapX, mapY, wx, wy);

                    const int sx = wx - view.camX;
                    const int sy = wy - view.camY;

                    if (!def->has_sprite)
                    {
                        SDL_Rect tileRect{
                            mapX * m_tileSize - view.camX,
                            mapY * m_tileSize - view.camY,
                            m_tileSize,
                            m_tileSize
                        };

                        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(m_renderer, 0, 180, 0, 80);
                        SDL_RenderFillRect(m_renderer, &tileRect);

                        SDL_SetRenderDrawColor(m_renderer, 0, 255, 0, 255);
                        SDL_RenderDrawRect(m_renderer, &tileRect);

                        const ImVec2 p = WorldToScreen((float)wx, (float)wy, m_camX, m_camY, m_zoom);
                        fg->AddText(
                            ImVec2(p.x + 6.0f, p.y - 18.0f),
                            IM_COL32(0, 255, 0, 255),
                            def->name.c_str()
                        );
                        continue;
                    }

                    SDL_Texture* tex = textureForObject(*def);
                    if (tex)
                    {
                        const float instanceScale = def->scale * m_map.getObjScale(mapX, mapY);
                        gameobj::RenderObjectAtPivot(m_renderer, tex, *def, sx, sy, instanceScale);

                        if (m_drawObjColliders && def->solid)
                        {
                            const auto rects = gameobj::GetWorldColliderRects(*def, wx, wy, instanceScale);

                            SDL_SetRenderDrawColor(m_renderer, 0, 255, 0, 255);
                            for (const SDL_Rect& c : rects)
                            {
                                SDL_Rect r = c;
                                r.x -= view.camX;
                                r.y -= view.camY;
                                SDL_RenderDrawRect(m_renderer, &r);
                            }
                        }
                    }
                }
            }
            RenderEditorCoreForageSpawns(
                m_renderer,
                ImGui::GetForegroundDrawList(),
                m_forageSpawns,
                m_forageArchetypes,
                m_selectedForageSpawnIndex,
                m_tileSize,
                view.camX,
                view.camY,
                m_zoom);

            renderBrushGhost(view);
            renderForageGhost(view);
        }

        for (int i = 0; i < (int)m_npcSpawns.size(); ++i)
        {
            const auto& npc = m_npcSpawns[i];
            SDL_Rect tileRect{
                npc.tileX * m_tileSize - view.camX,
                npc.tileY * m_tileSize - view.camY,
                m_tileSize,
                m_tileSize
            };

            const bool selected = (i == m_selectedNpcSpawnIndex);

            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

            if (selected)
            {
                SDL_SetRenderDrawColor(m_renderer, 0, 200, 255, 110);
                SDL_RenderFillRect(m_renderer, &tileRect);

                SDL_SetRenderDrawColor(m_renderer, 0, 255, 255, 255);
                SDL_RenderDrawRect(m_renderer, &tileRect);
            }
            else
            {
                SDL_SetRenderDrawColor(m_renderer, 255, 200, 0, 90);
                SDL_RenderFillRect(m_renderer, &tileRect);

                SDL_SetRenderDrawColor(m_renderer, 255, 220, 0, 255);
                SDL_RenderDrawRect(m_renderer, &tileRect);
            }

            std::string npcLabel = npc.id;
            if (!npc.name.empty())
            {
                npcLabel = npc.name;
                if (!npc.surname.empty())
                    npcLabel += " " + npc.surname;
            }

            ImDrawList* bg = ImGui::GetBackgroundDrawList();

            const float wx = (float)(npc.tileX * m_tileSize);
            const float wy = (float)(npc.tileY * m_tileSize);

            const ImVec2 p = WorldToScreen(wx, wy, m_camX, m_camY, m_zoom);

            bg->AddText(
                ImVec2(p.x + 3.0f, p.y + 3.0f),
                IM_COL32(255, 220, 0, 255),
                npcLabel.c_str()
            );
        }

        if (m_foragePlacementMode)
        {
            const int worldX = (int)(m_mouseX / m_zoom) + m_camX;
            const int worldY = (int)(m_mouseY / m_zoom) + m_camY;
            const int tx = worldX / m_tileSize;
            const int ty = worldY / m_tileSize;

            SDL_Rect tileRect{
                tx * m_tileSize - view.camX,
                ty * m_tileSize - view.camY,
                m_tileSize,
                m_tileSize
            };

            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer, 80, 220, 120, 80);
            SDL_RenderFillRect(m_renderer, &tileRect);

            SDL_SetRenderDrawColor(m_renderer, 120, 255, 160, 255);
            SDL_RenderDrawRect(m_renderer, &tileRect);

            const std::string label = selectedForageBrushLabel();

            ImGui::GetForegroundDrawList()->AddText(
                ImVec2((float)tileRect.x + 3.0f, (float)tileRect.y + 3.0f),
                IM_COL32(120, 255, 160, 255),
                label.c_str()
            );
        }

        if (m_npcPlacementMode)
        {
            const int worldX = (int)(m_mouseX / m_zoom) + m_camX;
            const int worldY = (int)(m_mouseY / m_zoom) + m_camY;
            const int tx = worldX / m_tileSize;
            const int ty = worldY / m_tileSize;

            SDL_Rect tileRect{
                tx * m_tileSize - view.camX,
                ty * m_tileSize - view.camY,
                m_tileSize,
                m_tileSize
            };

            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer, 255, 200, 0, 80);
            SDL_RenderFillRect(m_renderer, &tileRect);

            SDL_SetRenderDrawColor(m_renderer, 255, 220, 0, 255);
            SDL_RenderDrawRect(m_renderer, &tileRect);

            std::string npcBrushLabel = m_npcId;
            if (m_npcName[0] != '\0')
            {
                npcBrushLabel = m_npcName;
                if (m_npcSurname[0] != '\0')
                {
                    npcBrushLabel += " ";
                    npcBrushLabel += m_npcSurname;
                }
            }

            ImGui::GetForegroundDrawList()->AddText(
                ImVec2((float)tileRect.x + 3.0f, (float)tileRect.y + 3.0f),
                IM_COL32(255, 220, 0, 255),
                npcBrushLabel.c_str()
            );
        }

        if (m_brushMode == BrushMode::Terrain &&
            m_terrainBrushMode == TerrainBrushMode::Composite &&
            m_compositeGhost &&
            m_selectedCompositeGroup >= 0 &&
            m_selectedCompositeGroup < (int)m_compositeGroups.size())
        {
            const auto& g = m_compositeGroups[m_selectedCompositeGroup];

            const int worldX = (int)(m_mouseX / m_zoom) + m_camX;
            const int worldY = (int)(m_mouseY / m_zoom) + m_camY;
            const int tx = worldX / m_tileSize;
            const int ty = worldY / m_tileSize;

            SDL_SetTextureAlphaMod(m_tileset.atlas(), 128);

            for (int defIdx : g.defIndices)
            {
                const auto* d = m_tileset.tileByIndex(defIdx);
                if (!d) continue;

                const int px = tx + (d->composite_x - g.minx);
                const int py = ty + (d->composite_y - g.miny);

                SDL_Rect dst{
                    (px - firstTileX) * m_tileSize + offsetX,
                    (py - firstTileY) * m_tileSize + offsetY,
                    m_tileSize,
                    m_tileSize
                };

                // pokud sem časem doplníš ghost render kompozitů přes atlas, patří to sem
                (void)dst;
            }

            SDL_SetTextureAlphaMod(m_tileset.atlas(), 255);
        }

        if (m_showNpcZonesOverlay)
        {
            ImDrawList* fg = ImGui::GetBackgroundDrawList();

            for (int i = 0; i < (int)m_npcZones.size(); ++i)
            {
                if (m_showOnlySelectedZoneOverlay && i != m_selectedNpcZoneIndex)
                    continue;

                const auto& z = m_npcZones[i];

                const int minX = std::min(z.minX, z.maxX);
                const int minY = std::min(z.minY, z.maxY);
                const int maxX = std::max(z.minX, z.maxX);
                const int maxY = std::max(z.minY, z.maxY);

                const float wx0 = (float)(minX * m_tileSize);
                const float wy0 = (float)(minY * m_tileSize);
                const float wx1 = (float)((maxX + 1) * m_tileSize);
                const float wy1 = (float)((maxY + 1) * m_tileSize);

                const ImVec2 p0 = WorldToScreen(wx0, wy0, m_camX, m_camY, m_zoom);
                const ImVec2 p1 = WorldToScreen(wx1, wy1, m_camX, m_camY, m_zoom);

                const bool selected = (i == m_selectedNpcZoneIndex);

                const ImU32 fillCol = selected
                    ? IM_COL32(255, 190, 60, 48)
                    : IM_COL32(80, 180, 255, 36);

                const ImU32 lineCol = selected
                    ? IM_COL32(255, 220, 80, 255)
                    : IM_COL32(80, 210, 255, 220);

                fg->AddRectFilled(p0, p1, fillCol);
                fg->AddRect(p0, p1, lineCol, 0.0f, 0, selected ? 3.0f : 2.0f);

                if (!z.id.empty())
                {
                    fg->AddText(
                        ImVec2(p0.x + 4.0f, p0.y + 4.0f),
                        lineCol,
                        z.id.c_str()
                    );
                }
            }
        }

        if (m_showNpcZonesOverlay && m_selectedNpcZoneIndex >= 0 &&
        m_selectedNpcZoneIndex < (int)m_npcZones.size())
        {
            const auto& z = m_npcZones[m_selectedNpcZoneIndex];
            const int minX = std::min(z.minX, z.maxX);
            const int minY = std::min(z.minY, z.maxY);
            const int maxX = std::max(z.minX, z.maxX);
            const int maxY = std::max(z.minY, z.maxY);

            ImDrawList* fg = ImGui::GetBackgroundDrawList();

            auto drawHandle = [&](int tx, int ty)
            {
                const float wx0 = (float)(tx * m_tileSize);
                const float wy0 = (float)(ty * m_tileSize);
                const float wx1 = (float)((tx + 1) * m_tileSize);
                const float wy1 = (float)((ty + 1) * m_tileSize);

                const ImVec2 p0 = WorldToScreen(wx0, wy0, m_camX, m_camY, m_zoom);
                const ImVec2 p1 = WorldToScreen(wx1, wy1, m_camX, m_camY, m_zoom);

                fg->AddRectFilled(p0, p1, IM_COL32(255, 220, 80, 90));
                fg->AddRect(p0, p1, IM_COL32(255, 240, 120, 255), 0.0f, 0, 2.0f);
            };

            drawHandle(minX, minY);
            drawHandle(maxX, minY);
            drawHandle(minX, maxY);
            drawHandle(maxX, maxY);
        }

        if (m_zonePickMode && m_zonePickHasStart)
        {
            const int worldX = (int)(m_mouseX / m_zoom) + m_camX;
            const int worldY = (int)(m_mouseY / m_zoom) + m_camY;

            const int tx = worldX / m_tileSize;
            const int ty = worldY / m_tileSize;

            const int minX = std::min(m_zonePickStartX, tx);
            const int minY = std::min(m_zonePickStartY, ty);
            const int maxX = std::max(m_zonePickStartX, tx);
            const int maxY = std::max(m_zonePickStartY, ty);

            const float wx0 = (float)(minX * m_tileSize);
            const float wy0 = (float)(minY * m_tileSize);
            const float wx1 = (float)((maxX + 1) * m_tileSize);
            const float wy1 = (float)((maxY + 1) * m_tileSize);

            const ImVec2 p0 = WorldToScreen(wx0, wy0, m_camX, m_camY, m_zoom);
            const ImVec2 p1 = WorldToScreen(wx1, wy1, m_camX, m_camY, m_zoom);

            ImDrawList* fg = ImGui::GetBackgroundDrawList();
            fg->AddRectFilled(p0, p1, IM_COL32(255, 120, 40, 35));
            fg->AddRect(p0, p1, IM_COL32(255, 160, 60, 255), 0.0f, 0, 2.0f);

            if (m_selectedNpcZoneIndex >= 0 &&
                m_selectedNpcZoneIndex < (int)m_npcZones.size())
            {
                const auto& z = m_npcZones[m_selectedNpcZoneIndex];
                if (!z.id.empty())
                {
                    fg->AddText(
                        ImVec2(p0.x + 4.0f, p0.y + 4.0f),
                        IM_COL32(255, 200, 80, 255),
                        z.id.c_str()
                    );
                }
            }
        }

        {
            ImGuiIO& io = ImGui::GetIO();
            if (!io.WantCaptureMouse)
            {
                const int worldX = (int)(m_mouseX / m_zoom) + m_camX;
                const int worldY = (int)(m_mouseY / m_zoom) + m_camY;
                const int tx = worldX / m_tileSize;
                const int ty = worldY / m_tileSize;

                const auto* def = m_map.getObjDefAt(m_objCatalog, tx, ty);
                if (def)
                {
                    const float placedScale = m_map.getObjScale(tx, ty);

                    const char* name =
                        !def->name.empty() ? def->name.c_str() :
                        !def->id.empty()   ? def->id.c_str() :
                                             "(unnamed object)";

                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(name);
                    ImGui::Separator();
                    ImGui::Text("ID: %s", def->id.c_str());
                    ImGui::Text("Type: %s", ObjectCategoryLabel(*def));
                    ImGui::Text("Scale: %.2f", placedScale);
                    ImGui::Text("Tile: %d, %d", tx, ty);
                    ImGui::EndTooltip();
                }
            }
        }

        SDL_RenderSetScale(m_renderer, 1.0f, 1.0f);
    }
}
void Editor::renderEditorWorkspace()
{
    ImGuiWindowFlags windowFlags = 0;
    const ImGuiIO& io = ImGui::GetIO();

    if (m_editorWorkspaceMaximized)
    {
        const ImVec2 pos(kEditorWindowMargin, kEditorTopMenuHeight + kEditorWindowMargin);
        const ImVec2 size(
            std::max(320.0f, io.DisplaySize.x - kEditorWindowMargin * 2.0f),
            std::max(240.0f, io.DisplaySize.y - kEditorTopMenuHeight - kEditorWindowMargin * 2.0f));

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        windowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    }
    else
    {
        ImGui::SetNextWindowPos(ImVec2(10, kEditorTopMenuHeight + 12.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(560, 760), ImGuiCond_FirstUseEver);
    }

    if (!ImGui::Begin("Editor Workspace", nullptr, windowFlags))
    {
        ImGui::End();
        return;
    }

    const char* maximizeLabel = m_editorWorkspaceMaximized
        ? "Restore##editor_workspace_size"
        : "Maximize##editor_workspace_size";
    if (ImGui::Button(maximizeLabel))
        m_editorWorkspaceMaximized = !m_editorWorkspaceMaximized;
    ImGui::SameLine();
    ImGui::TextDisabled(m_editorWorkspaceMaximized ? "maximized" : "floating");
    ImGui::Separator();

    if (ImGui::BeginTabBar("EditorTabs"))
    {
        if (ImGui::BeginTabItem("Map"))
        {
            m_activeTab = EditorTab::Map;
            renderBrushPanelContents();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("NPC"))
        {
            m_activeTab = EditorTab::NPC;

            ImGui::Checkbox("NPC placement mode", &m_npcPlacementMode);
            if (m_npcPlacementMode)
                m_foragePlacementMode = false;

            ImGui::TextDisabled("Left click = place/update NPC, right click = remove NPC.");
            ImGui::Separator();

            renderNpcInspectorContents();
            ImGui::Separator();
            renderNpcZoneEditorContents();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Schedules"))
        {
            m_activeTab = EditorTab::Schedules;
            renderNpcScheduleEditorContents();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Dialogs"))
        {
            m_activeTab = EditorTab::Dialogs;
            renderDialogEditorContents();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Quests"))
        {
            m_activeTab = EditorTab::Quests;
            renderQuestEditorContents();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Foraging"))
        {
            m_activeTab = EditorTab::Foraging;

            ImGui::Checkbox("Forage placement mode", &m_foragePlacementMode);
            if (m_foragePlacementMode)
                m_npcPlacementMode = false;

            if (ImGui::RadioButton("Place selected species", m_placeSelectedForageSpecies))
                m_placeSelectedForageSpecies = true;
            ImGui::SameLine();
            if (ImGui::RadioButton("Place selected archetype", !m_placeSelectedForageSpecies))
                m_placeSelectedForageSpecies = false;

            ImGui::TextDisabled("Brush: %s", selectedForageBrushLabel().c_str());
            ImGui::TextDisabled("Left click = place/update spawn, right click = remove spawn.");
            ImGui::Separator();

            if (ImGui::BeginChild("ForageDefs", ImVec2(0, 430), true))
            {
                renderForageDefinitionEditorContents();
            }
            ImGui::EndChild();

            ImGui::Spacing();

            if (ImGui::BeginChild("ForageSpawns", ImVec2(0, 0), true))
            {
                renderForageSpawnEditorContents();
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

#ifdef _WIN32
static bool WinOpenFileDialogRvm(std::string& outPathUtf8, const fs::path& initialDir)
{
    wchar_t fileBuf[MAX_PATH] = L"";
    wchar_t initDirW[MAX_PATH] = L"";

    const std::wstring init = initialDir.wstring();
    wcsncpy_s(initDirW, init.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"RVM maps (*.rvm)\0*.rvm\0All files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"rvm";
    ofn.lpstrTitle = L"Load map";
    ofn.lpstrInitialDir = initDirW;

    if (!GetOpenFileNameW(&ofn))
        return false;

    int needed = WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return false;

    std::string utf8(needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, utf8.data(), needed, nullptr, nullptr);

    outPathUtf8 = utf8;
    return true;
}

static bool WinSaveFileDialogRvm(std::string& outPathUtf8, const fs::path& initialDir, const char* suggestedNameUtf8)
{
    wchar_t fileBuf[MAX_PATH] = L"";
    wchar_t initDirW[MAX_PATH] = L"";

    const std::wstring init = initialDir.wstring();
    wcsncpy_s(initDirW, init.c_str(), _TRUNCATE);

    if (suggestedNameUtf8 && suggestedNameUtf8[0]) {
        MultiByteToWideChar(CP_UTF8, 0, suggestedNameUtf8, -1, fileBuf, MAX_PATH);
    }
    else {
        wcscpy_s(fileBuf, L"test.rvm");
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"RVM maps (*.rvm)\0*.rvm\0All files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"rvm";
    ofn.lpstrTitle = L"Save map";
    ofn.lpstrInitialDir = initDirW;

    if (!GetSaveFileNameW(&ofn))
        return false;

    int needed = WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return false;

    std::string utf8(needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, utf8.data(), needed, nullptr, nullptr);

    outPathUtf8 = utf8;
    return true;
}
#endif

static std::string MapFileNameOnly(const std::string& path)
{
    if (path.empty())
        return "(none)";
    return fs::path(path).filename().string();
}

static std::string NormalizeMapFileName(std::string name)
{
    for (char& c : name)
    {
        if (c == '/' || c == '\\')
            c = '_';
    }

    if (name.empty())
        name = "untitled.rvm";

    fs::path p(name);
    if (p.extension().empty())
        p.replace_extension(".rvm");

    return p.filename().string();
}

static std::vector<fs::path> CollectEditorMaps(const std::string& mapsDir)
{
    std::vector<fs::path> maps;
    std::error_code ec;

    if (!fs::exists(mapsDir, ec))
        return maps;

    for (const auto& entry : fs::directory_iterator(mapsDir, ec))
    {
        if (ec)
            break;

        if (!entry.is_regular_file())
            continue;

        if (entry.path().extension() == ".rvm")
            maps.push_back(entry.path());
    }

    std::sort(maps.begin(), maps.end(), [](const fs::path& a, const fs::path& b)
    {
        return a.filename().string() < b.filename().string();
    });

    return maps;
}

void Editor::renderMapIoPopup()
{
    const char* popupTitle = nullptr;

    switch (m_mapIoPopupMode)
    {
    case MapIoPopupMode::NewMap: popupTitle = "New map"; break;
    case MapIoPopupMode::LoadMap: popupTitle = "Load map"; break;
    case MapIoPopupMode::SaveAs: popupTitle = "Save map as"; break;
    default: break;
    }

    if (popupTitle)
        ImGui::OpenPopup(popupTitle);

    if (ImGui::BeginPopupModal("New map", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Creates a new empty grass map. No Windows file dialog, only the project maps folder.");
        ImGui::Separator();

        ImGui::InputText("File", m_mapFileNameBuf, sizeof(m_mapFileNameBuf));
        ImGui::InputInt("Width tiles", &m_newMapWidth);
        ImGui::InputInt("Height tiles", &m_newMapHeight);

        m_newMapWidth = std::clamp(m_newMapWidth, 8, 512);
        m_newMapHeight = std::clamp(m_newMapHeight, 8, 512);

        ImGui::TextDisabled("Allowed size: 8..512 tiles. Suggested: 128 x 128.");

        if (ImGui::Button("Create", ImVec2(110, 0)))
        {
            pushUndoState("new map");
            newMap(m_newMapWidth, m_newMapHeight);

            const std::string fileName = NormalizeMapFileName(m_mapFileNameBuf);
            const fs::path p = fs::path(m_mapsDir) / fileName;
            m_mapPath = p.string();
            std::strncpy(m_mapPathBuf, m_mapPath.c_str(), sizeof(m_mapPathBuf) - 1);
            m_mapPathBuf[sizeof(m_mapPathBuf) - 1] = '\0';
            std::strncpy(m_mapFileNameBuf, fileName.c_str(), sizeof(m_mapFileNameBuf) - 1);
            m_mapFileNameBuf[sizeof(m_mapFileNameBuf) - 1] = '\0';

            m_lastIoStatus = "New map ready: " + fileName;
            m_mapIoPopupMode = MapIoPopupMode::None;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0)))
        {
            m_mapIoPopupMode = MapIoPopupMode::None;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Load map", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Maps folder: %s", m_mapsDir.c_str());
        ImGui::Separator();

        const auto maps = CollectEditorMaps(m_mapsDir);
        if (maps.empty())
        {
            ImGui::TextDisabled("No .rvm files found.");
        }
        else
        {
            ImGui::BeginChild("MapList", ImVec2(420, 260), true);
            for (const auto& p : maps)
            {
                const std::string fileName = p.filename().string();
                const bool selected = (fs::path(m_mapPath).filename() == p.filename());

                if (ImGui::Selectable(fileName.c_str(), selected))
                {
                    loadMap(p.string().c_str());
                    m_mapIoPopupMode = MapIoPopupMode::None;
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
            ImGui::EndChild();
        }

        if (ImGui::Button("Close", ImVec2(110, 0)))
        {
            m_mapIoPopupMode = MapIoPopupMode::None;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Save map as", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Save only inside: %s", m_mapsDir.c_str());
        ImGui::Separator();

        ImGui::InputText("File", m_mapFileNameBuf, sizeof(m_mapFileNameBuf));

        const std::string fileName = NormalizeMapFileName(m_mapFileNameBuf);
        const fs::path savePath = fs::path(m_mapsDir) / fileName;
        const bool exists = fs::exists(savePath);

        if (exists)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "This file already exists and will be overwritten.");

        const auto maps = CollectEditorMaps(m_mapsDir);
        if (!maps.empty())
        {
            ImGui::TextDisabled("Existing maps:");
            ImGui::BeginChild("ExistingMapList", ImVec2(420, 160), true);
            for (const auto& p : maps)
            {
                const std::string existingName = p.filename().string();
                if (ImGui::Selectable(existingName.c_str(), false))
                {
                    std::strncpy(m_mapFileNameBuf, existingName.c_str(), sizeof(m_mapFileNameBuf) - 1);
                    m_mapFileNameBuf[sizeof(m_mapFileNameBuf) - 1] = '\0';
                }
            }
            ImGui::EndChild();
        }

        if (ImGui::Button("Save", ImVec2(110, 0)))
        {
            saveMap(savePath.string().c_str());
            m_mapIoPopupMode = MapIoPopupMode::None;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0)))
        {
            m_mapIoPopupMode = MapIoPopupMode::None;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (popupTitle)
        m_mapIoPopupMode = MapIoPopupMode::None;
}

void Editor::renderMainToolbar()
{
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::max(320.0f, io.DisplaySize.x), kEditorTopMenuHeight), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Map Tools", nullptr, flags);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Map");
    ImGui::SameLine();

    if (ImGui::Button("New", ImVec2(80, 0)))
    {
        if (!m_mapPath.empty())
        {
            const std::string currentName = fs::path(m_mapPath).filename().string();
            std::strncpy(m_mapFileNameBuf, currentName.c_str(), sizeof(m_mapFileNameBuf) - 1);
            m_mapFileNameBuf[sizeof(m_mapFileNameBuf) - 1] = '\0';
        }
        m_mapIoPopupMode = MapIoPopupMode::NewMap;
    }

    ImGui::SameLine();
    if (ImGui::Button("Load", ImVec2(80, 0)))
        m_mapIoPopupMode = MapIoPopupMode::LoadMap;

    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(80, 0)))
    {
        if (m_mapPath.empty())
        {
            const fs::path p = fs::path(m_mapsDir) / NormalizeMapFileName(m_mapFileNameBuf);
            saveMap(p.string().c_str());
        }
        else
        {
            saveMap(m_mapPath.c_str());
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Save as", ImVec2(80, 0)))
    {
        const std::string currentName = m_mapPath.empty()
            ? NormalizeMapFileName(m_mapFileNameBuf)
            : fs::path(m_mapPath).filename().string();

        std::strncpy(m_mapFileNameBuf, currentName.c_str(), sizeof(m_mapFileNameBuf) - 1);
        m_mapFileNameBuf[sizeof(m_mapFileNameBuf) - 1] = '\0';
        m_mapIoPopupMode = MapIoPopupMode::SaveAs;
    }

    ImGui::SameLine();
    const bool undoDisabled = !canUndo();
    bool doUndo = false;
    ImGui::BeginDisabled(undoDisabled);
    doUndo = ImGui::Button("Undo", ImVec2(80, 0));
    ImGui::EndDisabled();

    ImGui::SameLine();

    const bool redoDisabled = !canRedo();
    bool doRedo = false;
    ImGui::BeginDisabled(redoDisabled);
    doRedo = ImGui::Button("Redo", ImVec2(80, 0));
    ImGui::EndDisabled();

    if (doUndo)
        undoLastStep();

    if (doRedo)
        redoLastStep();

    ImGui::SameLine();
    if (ImGui::Button("Reset zoom", ImVec2(100, 0)))
        m_zoom = 1.0f;

    ImGui::SameLine();
    ImGui::Checkbox("Brush ghost preview", &m_showBrushGhost);

    int fontPercent = (int)std::round(m_uiFontScale * 100.0f);
    fontPercent = std::clamp(fontPercent, 1, 100);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderInt("UI font scale (%)", &fontPercent, 1, 100))
        m_uiFontScale = fontPercent / 100.0f;

    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Current: %s", m_mapPath.empty() ? "(none)" : MapFileNameOnly(m_mapPath).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", m_mapPath.empty() ? m_mapsDir.c_str() : m_mapPath.c_str());

    ImGui::SameLine();
    ImGui::Text("Size: %d x %d", m_map.width(), m_map.height());

    ImGui::SameLine();
    ImGui::Text("Zoom: %.0f %%", m_zoom * 100.0f);

    ImGui::SameLine();
    ImGui::TextColored(
        m_mapDirty ? ImVec4(1.0f, 0.72f, 0.28f, 1.0f) : ImVec4(0.55f, 1.0f, 0.55f, 1.0f),
        "%s",
        m_mapDirty ? "DIRTY" : "OK");

    ImGui::SameLine();
    if (!m_lastIoStatus.empty())
        ImGui::TextDisabled("%s", m_lastIoStatus.c_str());

    ImGui::End();
    ImGui::PopStyleVar(2);
}
