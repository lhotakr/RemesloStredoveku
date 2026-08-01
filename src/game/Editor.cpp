#define NOMINMAX
#include "Editor.h"
#include "EditorHelpers.h"

#include <algorithm>
#include <random>
#include <string>
#include <cmath>
#include <filesystem>
#include <cstring>
#include <nlohmann/json.hpp>

#include <SDL.h>
#include "imgui.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma comment(lib, "Comdlg32.lib")
#endif

namespace fs = std::filesystem;

static uint32_t EditorMapHash2d(int x, int y, uint32_t seed = 1337u)
{
    uint32_t h = seed;
    h ^= (uint32_t)x + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= (uint32_t)y + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}

static std::string NormalizeRvmFileName(std::string name)
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

static std::vector<fs::path> ListRvmMaps(const std::string& mapsDir)
{
    std::vector<fs::path> out;
    std::error_code ec;

    if (!fs::exists(mapsDir, ec))
        return out;

    for (const auto& entry : fs::directory_iterator(mapsDir, ec))
    {
        if (ec)
            break;

        if (!entry.is_regular_file())
            continue;

        if (entry.path().extension() == ".rvm")
            out.push_back(entry.path());
    }

    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b)
    {
        return a.filename().string() < b.filename().string();
    });

    return out;
}

void Editor::newMap(int width, int height)
{
    width = std::clamp(width, 8, 512);
    height = std::clamp(height, 8, 512);

    m_map = TileMap(width, height, m_tileSize);
    m_npcSpawns.clear();
    m_npcZones.clear();
    m_forageSpawns.clear();
    m_selectedNpcSpawnIndex = -1;
    m_selectedNpcZoneIndex = -1;
    m_selectedForageSpawnIndex = -1;

    const int grassCount = std::max(1, m_tileset.fillCount("grass"));

    for (int y = 0; y < m_map.height(); ++y)
    {
        for (int x = 0; x < m_map.width(); ++x)
        {
            const uint32_t h = EditorMapHash2d(x, y);
            m_map.set(x, y, 1);
            m_map.setVar(x, y, (uint8_t)(h % (uint32_t)grassCount));
            m_map.clearOverride(x, y);
            m_map.clearObj(x, y);
        }
    }

    m_camX = 0;
    m_camY = 0;
    m_mapDirty = true;
    m_lastIoStatus = "New map created.";
}

Editor::ZoneDragHandle Editor::hitTestZoneHandle(int tx, int ty) const
{
    if (m_selectedNpcZoneIndex < 0 || m_selectedNpcZoneIndex >= (int)m_npcZones.size())
        return ZoneDragHandle::None;

    const auto& z = m_npcZones[m_selectedNpcZoneIndex];

    const int minX = std::min(z.minX, z.maxX);
    const int minY = std::min(z.minY, z.maxY);
    const int maxX = std::max(z.minX, z.maxX);
    const int maxY = std::max(z.minY, z.maxY);

    const bool onMinX = (tx == minX);
    const bool onMaxX = (tx == maxX);
    const bool onMinY = (ty == minY);
    const bool onMaxY = (ty == maxY);

    if (onMinX && onMinY) return ZoneDragHandle::MinXMinY;
    if (onMaxX && onMinY) return ZoneDragHandle::MaxXMinY;
    if (onMinX && onMaxY) return ZoneDragHandle::MinXMaxY;
    if (onMaxX && onMaxY) return ZoneDragHandle::MaxXMaxY;

    if (onMinX && ty >= minY && ty <= maxY) return ZoneDragHandle::MinX;
    if (onMaxX && ty >= minY && ty <= maxY) return ZoneDragHandle::MaxX;
    if (onMinY && tx >= minX && tx <= maxX) return ZoneDragHandle::MinY;
    if (onMaxY && tx >= minX && tx <= maxX) return ZoneDragHandle::MaxY;

    if (tx >= minX && tx <= maxX && ty >= minY && ty <= maxY)
        return ZoneDragHandle::Move;

    return ZoneDragHandle::None;
}

void Editor::beginZoneDrag(int tx, int ty, ZoneDragHandle handle)
{
    if (m_selectedNpcZoneIndex < 0 || m_selectedNpcZoneIndex >= (int)m_npcZones.size())
        return;

    pushUndoState("resize zone");

    m_zoneResizeActive = true;
    m_zoneDragHandle = handle;
    m_zoneDragAnchorMouseTileX = tx;
    m_zoneDragAnchorMouseTileY = ty;
    m_zoneDragOriginal = m_npcZones[m_selectedNpcZoneIndex];
}

void Editor::updateZoneDrag(int tx, int ty)
{
    if (!m_zoneResizeActive)
        return;

    if (m_selectedNpcZoneIndex < 0 || m_selectedNpcZoneIndex >= (int)m_npcZones.size())
        return;

    auto& z = m_npcZones[m_selectedNpcZoneIndex];
    z = m_zoneDragOriginal;

    const int dx = tx - m_zoneDragAnchorMouseTileX;
    const int dy = ty - m_zoneDragAnchorMouseTileY;

    switch (m_zoneDragHandle)
    {
    case ZoneDragHandle::Move:
        z.minX += dx; z.maxX += dx;
        z.minY += dy; z.maxY += dy;
        break;

    case ZoneDragHandle::MinXMinY:
        z.minX += dx; z.minY += dy;
        break;
    case ZoneDragHandle::MaxXMinY:
        z.maxX += dx; z.minY += dy;
        break;
    case ZoneDragHandle::MinXMaxY:
        z.minX += dx; z.maxY += dy;
        break;
    case ZoneDragHandle::MaxXMaxY:
        z.maxX += dx; z.maxY += dy;
        break;

    case ZoneDragHandle::MinX:
        z.minX += dx;
        break;
    case ZoneDragHandle::MaxX:
        z.maxX += dx;
        break;
    case ZoneDragHandle::MinY:
        z.minY += dy;
        break;
    case ZoneDragHandle::MaxY:
        z.maxY += dy;
        break;

    default:
        break;
    }

    if (z.minX > z.maxX) std::swap(z.minX, z.maxX);
    if (z.minY > z.maxY) std::swap(z.minY, z.maxY);

    m_mapDirty = true;
}

void Editor::endZoneDrag()
{
    m_zoneResizeActive = false;
    m_zoneDragHandle = ZoneDragHandle::None;
}

void Editor::renderBrushPanel()
{
    if (!ImGui::Begin("Brush / Palette"))
    {
        ImGui::End();
        return;
    }

    renderBrushPanelContents();
    ImGui::End();
}

void Editor::renderBrushPanelContents()
{
    ImGui::Text("Brush mode");

    int brushMode = (int)m_brushMode;
    ImGui::RadioButton("Terrain", &brushMode, (int)BrushMode::Terrain); ImGui::SameLine();
    ImGui::RadioButton("Nature", &brushMode, (int)BrushMode::NatureObjects); ImGui::SameLine();
    ImGui::RadioButton("Tech", &brushMode, (int)BrushMode::TechObjects); ImGui::SameLine();
    ImGui::RadioButton("Castles", &brushMode, (int)BrushMode::Castles); ImGui::SameLine();
    ImGui::RadioButton("Houses", &brushMode, (int)BrushMode::Houses); ImGui::SameLine();
    ImGui::RadioButton("Decoration", &brushMode, (int)BrushMode::Decoration); ImGui::SameLine();


    m_brushMode = (BrushMode)brushMode;

    ImGui::Checkbox("Draw obj colliders", &m_drawObjColliders);
    ImGui::Separator();

    if (m_brushMode == BrushMode::Terrain)
    {
        ImGui::Text("Terrain");

        int terrain = (int)m_brushTile;
        ImGui::RadioButton("Grass", &terrain, 1); ImGui::SameLine();
        ImGui::RadioButton("Water", &terrain, 2); ImGui::SameLine();
        ImGui::RadioButton("Mud", &terrain, 3);
        m_brushTile = (uint16_t)terrain;

        int terrainMode = (int)m_terrainBrushMode;
        ImGui::Text("Terrain brush mode");
        ImGui::RadioButton("Simple", &terrainMode, (int)TerrainBrushMode::Simple); ImGui::SameLine();
        ImGui::RadioButton("Composite", &terrainMode, (int)TerrainBrushMode::Composite);
        m_terrainBrushMode = (TerrainBrushMode)terrainMode;

        if (m_terrainBrushMode == TerrainBrushMode::Simple)
        {
            ImGui::Text("Brush size");
            ImGui::RadioButton("1", &m_brushSize, 1); ImGui::SameLine();
            ImGui::RadioButton("3", &m_brushSize, 3); ImGui::SameLine();
            ImGui::RadioButton("5", &m_brushSize, 5);

            ImGui::Checkbox("Randomize variants", &m_randomizeVariants);
        }
        else
        {
            ImGui::Checkbox("Ghost preview", &m_compositeGhost);

            ImGui::Text("Composite palette");
            if (m_compositeGroups.empty()) {
                ImGui::Text("No composite groups in JSON.");
            }
            else if (ImGui::BeginListBox("##composite_list", ImVec2(-1, 220)))
            {
                for (int i = 0; i < (int)m_compositeGroups.size(); ++i)
                {
                    bool selected = (m_selectedCompositeGroup == i);
                    if (ImGui::Selectable(m_compositeGroups[i].id.c_str(), selected))
                        m_selectedCompositeGroup = i;

                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }

            renderSelectedCompositePreview();
        }
    }
    else if (m_brushMode == BrushMode::NatureObjects)
    {
        ImGui::Text("Nature Objects");

        ImGui::Checkbox("Ghost preview", &m_objGhost);
        ImGui::SameLine();
        ImGui::Checkbox("Randomize objVar", &m_objRandomizeVar);

        ImGui::InputScalar("objVar", ImGuiDataType_U8, &m_objBrushVar);
        ImGui::InputScalar("objHP", ImGuiDataType_U16, &m_objBrushHP);
        ImGui::InputFloat("Scale", &m_objBrushScale, 0.05f, 0.25f, "%.2f");
        m_objBrushScale = std::clamp(m_objBrushScale, 0.05f, 8.0f);
        ImGui::InputText("Filter", m_objFilter, IM_ARRAYSIZE(m_objFilter));

        const auto& defs = m_objCatalog.Objects();
        const auto indices = BuildNatureObjectIndices(m_objCatalog, m_objFilter);

        if (m_selectedNatureObj >= (int)indices.size())
            m_selectedNatureObj = std::max(0, (int)indices.size() - 1);

        if (ImGui::BeginListBox("##nature_objects", ImVec2(-1, 280)))
        {
            for (int visibleIndex = 0; visibleIndex < (int)indices.size(); ++visibleIndex)
            {
                const int i = indices[visibleIndex];
                const auto& o = defs[i];

                std::string label = o.name.empty() ? o.id : (o.name + "##" + o.id);
                bool sel = (m_selectedNatureObj == visibleIndex);
                if (ImGui::Selectable(label.c_str(), sel))
                    m_selectedNatureObj = visibleIndex;

                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }

        const int selectedNatureIndex =
            (m_selectedNatureObj >= 0 && m_selectedNatureObj < (int)indices.size())
            ? indices[m_selectedNatureObj]
            : -1;

        if (selectedNatureIndex >= 0 && selectedNatureIndex < (int)defs.size())
        {
            const auto& o = defs[selectedNatureIndex];
            ImGui::Separator();
            ImGui::Text("Selected:");
            ImGui::Text("ID: %s", o.id.c_str());
            ImGui::Text("Name: %s", o.name.c_str());
            renderSelectedObjectPreview();
        }
    }

    else if (m_brushMode == BrushMode::TechObjects)
{
    ImGui::Text("Tech Objects");

    ImGui::Checkbox("Randomize objVar", &m_objRandomizeVar);
    ImGui::InputScalar("objVar", ImGuiDataType_U8, &m_objBrushVar);
    ImGui::InputScalar("objHP", ImGuiDataType_U16, &m_objBrushHP);
    ImGui::InputText("Filter", m_objFilter, IM_ARRAYSIZE(m_objFilter));

    const int visibleCount = CountVisibleTechObjects(m_objCatalog, m_objFilter);
    if (m_selectedTechObj >= visibleCount)
        m_selectedTechObj = std::max(0, visibleCount - 1);

    const auto& defs = m_objCatalog.Objects();

    if (ImGui::BeginListBox("##tech_objects", ImVec2(-1, 320)))
    {
        int visibleIndex = 0;
        for (int i = 0; i < (int)defs.size(); ++i)
        {
            const auto& o = defs[i];
            if (!IsTechObject(o)) continue;
            if (!ContainsNoCase(o.id + " " + o.name, m_objFilter)) continue;

            std::string label = o.name.empty() ? o.id : (o.name + "##" + o.id);
            bool sel = (m_selectedTechObj == visibleIndex);
            if (ImGui::Selectable(label.c_str(), sel))
                m_selectedTechObj = visibleIndex;

            if (sel)
                ImGui::SetItemDefaultFocus();

            ++visibleIndex;
        }
        ImGui::EndListBox();
    }

    const int selectedIndex = FindNthTechObject(m_objCatalog, m_selectedTechObj, m_objFilter);
    if (selectedIndex >= 0 && selectedIndex < (int)defs.size())
    {
        const auto& o = defs[selectedIndex];
        ImGui::Separator();
        ImGui::Text("Selected:");
        ImGui::Text("ID: %s", o.id.c_str());
        ImGui::Text("Name: %s", o.name.c_str());
        ImGui::Text("objVar on place: %u", (unsigned)m_objBrushVar);
        ImGui::Text("objHP on place: %u", (unsigned)m_objBrushHP);
        renderSelectedObjectPreview();

        if (o.HasTag("map_link"))
        {
            ImGui::Spacing();
            ImGui::TextWrapped("Map link uses objVar as link ID. Set objVar to the value from *.links.json, e.g. 1.");
        }
    }
}
        else if (m_brushMode == BrushMode::Decoration)
    {
        ImGui::Text("Decoration Objects");

        ImGui::Checkbox("Ghost preview", &m_objGhost);
        ImGui::SameLine();
        ImGui::Checkbox("Randomize objVar", &m_objRandomizeVar);

        ImGui::InputScalar("objVar", ImGuiDataType_U8, &m_objBrushVar);
        ImGui::InputScalar("objHP", ImGuiDataType_U16, &m_objBrushHP);
        ImGui::InputFloat("Scale", &m_objBrushScale, 0.05f, 0.25f, "%.2f");
        m_objBrushScale = std::clamp(m_objBrushScale, 0.05f, 8.0f);
        ImGui::InputText("Filter", m_objFilter, IM_ARRAYSIZE(m_objFilter));

        const auto& defs = m_objCatalog.Objects();
        const auto indices = BuildDecorationObjectIndices(m_objCatalog, m_objFilter);

        if (m_selectedDecorationObj >= (int)indices.size())
            m_selectedDecorationObj = std::max(0, (int)indices.size() - 1);

        if (ImGui::BeginListBox("##decoration_objects", ImVec2(-1, 280)))
        {
            for (int visibleIndex = 0; visibleIndex < (int)indices.size(); ++visibleIndex)
            {
                const int i = indices[visibleIndex];
                const auto& o = defs[i];

                std::string label = o.name.empty() ? o.id : (o.name + "##" + o.id);
                bool sel = (m_selectedDecorationObj == visibleIndex);
                if (ImGui::Selectable(label.c_str(), sel))
                    m_selectedDecorationObj = visibleIndex;

                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }

        const int selectedDecorationIndex =
            (m_selectedDecorationObj >= 0 && m_selectedDecorationObj < (int)indices.size())
            ? indices[m_selectedDecorationObj]
            : -1;

        if (selectedDecorationIndex >= 0 && selectedDecorationIndex < (int)defs.size())
        {
            const auto& o = defs[selectedDecorationIndex];
            ImGui::Separator();
            ImGui::Text("Selected:");
            ImGui::Text("ID: %s", o.id.c_str());
            ImGui::Text("Name: %s", o.name.c_str());
            renderSelectedObjectPreview();
        }
    }

    else if (m_brushMode == BrushMode::Castles)
    {
        ImGui::Text("Castle Objects");

        ImGui::Checkbox("Ghost preview", &m_objGhost);
        ImGui::SameLine();
        ImGui::Checkbox("Randomize objVar", &m_objRandomizeVar);

        ImGui::InputScalar("objVar", ImGuiDataType_U8, &m_objBrushVar);
        ImGui::InputScalar("objHP", ImGuiDataType_U16, &m_objBrushHP);
        ImGui::InputFloat("Scale", &m_objBrushScale, 0.05f, 0.25f, "%.2f");
        m_objBrushScale = std::clamp(m_objBrushScale, 0.05f, 8.0f);
        ImGui::InputText("Filter", m_objFilter, IM_ARRAYSIZE(m_objFilter));

        const auto& defs = m_objCatalog.Objects();
        const auto indices = BuildCastleObjectIndices(m_objCatalog, m_objFilter);

        if (m_selectedCastleObj >= (int)indices.size())
            m_selectedCastleObj = std::max(0, (int)indices.size() - 1);

        if (ImGui::BeginListBox("##castle_objects", ImVec2(-1, 280)))
        {
            for (int visibleIndex = 0; visibleIndex < (int)indices.size(); ++visibleIndex)
            {
                const int i = indices[visibleIndex];
                const auto& o = defs[i];

                std::string label = o.name.empty() ? o.id : (o.name + "##" + o.id);
                bool sel = (m_selectedCastleObj == visibleIndex);
                if (ImGui::Selectable(label.c_str(), sel))
                    m_selectedCastleObj = visibleIndex;

                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }

        const int selectedCastleIndex =
            (m_selectedCastleObj >= 0 && m_selectedCastleObj < (int)indices.size())
            ? indices[m_selectedCastleObj]
            : -1;

        if (selectedCastleIndex >= 0 && selectedCastleIndex < (int)defs.size())
        {
            const auto& o = defs[selectedCastleIndex];
            ImGui::Separator();
            ImGui::Text("Selected:");
            ImGui::Text("ID: %s", o.id.c_str());
            ImGui::Text("Name: %s", o.name.c_str());
            renderSelectedObjectPreview();
        }
    }
    else if (m_brushMode == BrushMode::Houses)
    {
        ImGui::Text("House Objects");

        ImGui::Checkbox("Ghost preview", &m_objGhost);
        ImGui::SameLine();
        ImGui::Checkbox("Randomize objVar", &m_objRandomizeVar);

        ImGui::InputScalar("objVar", ImGuiDataType_U8, &m_objBrushVar);
        ImGui::InputScalar("objHP", ImGuiDataType_U16, &m_objBrushHP);
        ImGui::InputFloat("Scale", &m_objBrushScale, 0.05f, 0.25f, "%.2f");
        m_objBrushScale = std::clamp(m_objBrushScale, 0.05f, 8.0f);
        ImGui::InputText("Filter", m_objFilter, IM_ARRAYSIZE(m_objFilter));

        const auto& defs = m_objCatalog.Objects();
        const auto indices = BuildHouseObjectIndices(m_objCatalog, m_objFilter);

        if (m_selectedHouseObj >= (int)indices.size())
            m_selectedHouseObj = std::max(0, (int)indices.size() - 1);

        if (ImGui::BeginListBox("##house_objects", ImVec2(-1, 320)))
        {
            for (int visibleIndex = 0; visibleIndex < (int)indices.size(); ++visibleIndex)
            {
                const int i = indices[visibleIndex];
                const auto& o = defs[i];

                std::string label = o.name.empty() ? o.id : (o.name + "##" + o.id);
                bool sel = (m_selectedHouseObj == visibleIndex);
                if (ImGui::Selectable(label.c_str(), sel))
                    m_selectedHouseObj = visibleIndex;

                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }

        const int selectedHouseIndex =
            (m_selectedHouseObj >= 0 && m_selectedHouseObj < (int)indices.size())
            ? indices[m_selectedHouseObj]
            : -1;

        if (selectedHouseIndex >= 0 && selectedHouseIndex < (int)defs.size())
        {
            const auto& o = defs[selectedHouseIndex];
            ImGui::Separator();
            ImGui::Text("Selected:");
            ImGui::Text("ID: %s", o.id.c_str());
            ImGui::Text("Name: %s", o.name.c_str());
            renderSelectedObjectPreview();
        }
    }
}

int Editor::selectedBrushObjectIndex() const
{
    if (m_brushMode == BrushMode::NatureObjects)
    {
        const auto indices = BuildNatureObjectIndices(m_objCatalog, m_objFilter);
        if (m_selectedNatureObj >= 0 && m_selectedNatureObj < (int)indices.size())
            return indices[m_selectedNatureObj];
    }
    else if (m_brushMode == BrushMode::TechObjects)
    {
        return FindNthTechObject(m_objCatalog, m_selectedTechObj, m_objFilter);
    }
    else if (m_brushMode == BrushMode::Castles)
    {
        const auto indices = BuildCastleObjectIndices(m_objCatalog, m_objFilter);
        if (m_selectedCastleObj >= 0 && m_selectedCastleObj < (int)indices.size())
            return indices[m_selectedCastleObj];
    }
    else if (m_brushMode == BrushMode::Houses)
    {
        const auto indices = BuildHouseObjectIndices(m_objCatalog, m_objFilter);
        if (m_selectedHouseObj >= 0 && m_selectedHouseObj < (int)indices.size())
            return indices[m_selectedHouseObj];
    }
    else if (m_brushMode == BrushMode::Decoration)
    {
        const auto indices = BuildDecorationObjectIndices(m_objCatalog, m_objFilter);
        if (m_selectedDecorationObj >= 0 && m_selectedDecorationObj < (int)indices.size())
            return indices[m_selectedDecorationObj];
    }

    return -1;
}

void Editor::renderBrushGhost(const TerrainRenderer::View& view)
{
    if (!m_showBrushGhost)
        return;

    if (m_activeTab != EditorTab::Map)
        return;

    if (m_npcPlacementMode || m_foragePlacementMode)
        return;

    if (!(m_brushMode == BrushMode::NatureObjects ||
          m_brushMode == BrushMode::TechObjects ||
          m_brushMode == BrushMode::Castles ||
          m_brushMode == BrushMode::Houses ||
          m_brushMode == BrushMode::Decoration))
        return;

    const int objIndex = selectedBrushObjectIndex();
    const auto& defs = m_objCatalog.Objects();

    if (objIndex < 0 || objIndex >= (int)defs.size())
        return;

    const auto& def = defs[objIndex];
    SDL_Texture* tex = textureForObject(def);
    if (!tex || !def.has_sprite)
        return;

    int mouseX = 0, mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);

    const int worldX = m_camX + (int)(mouseX / m_zoom);
    const int worldY = m_camY + (int)(mouseY / m_zoom);

    const int tx = worldX / m_tileSize;
    const int ty = worldY / m_tileSize;

    if (tx < 0 || ty < 0 || tx >= m_map.width() || ty >= m_map.height())
        return;

    int wx = 0, wy = 0;
    m_map.getObjPivotWorld(tx, ty, wx, wy);

    const int sx = wx - view.camX;
    const int sy = wy - view.camY;

    const float previewScale = def.scale * m_objBrushScale;

    SDL_SetTextureAlphaMod(tex, 160);
    gameobj::RenderObjectAtPivot(m_renderer, tex, def, sx, sy, previewScale);
    SDL_SetTextureAlphaMod(tex, 255);

    SDL_Rect anchorTileRect{
        tx * m_tileSize - view.camX,
        ty * m_tileSize - view.camY,
        m_tileSize,
        m_tileSize
    };

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 255, 255, 70);
    SDL_RenderFillRect(m_renderer, &anchorTileRect);

    SDL_SetRenderDrawColor(m_renderer, 0, 255, 255, 255);
    SDL_RenderDrawRect(m_renderer, &anchorTileRect);
}

void Editor::renderForageGhost(const TerrainRenderer::View& view)
{
    if (!m_showBrushGhost || !m_foragePlacementMode)
        return;

    if (m_activeTab != EditorTab::Foraging)
        return;

    int mouseX = 0, mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);

    const int worldX = m_camX + (int)(mouseX / m_zoom);
    const int worldY = m_camY + (int)(mouseY / m_zoom);

    const int tx = worldX / m_tileSize;
    const int ty = worldY / m_tileSize;

    if (tx < 0 || ty < 0 || tx >= m_map.width() || ty >= m_map.height())
        return;

    const int drawX = tx * m_tileSize - m_camX;
    const int drawY = ty * m_tileSize - m_camY;

    SDL_FRect r{
        (float)drawX,
        (float)drawY,
        (float)m_tileSize,
        (float)m_tileSize
    };

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 80, 255, 120, 110);
    SDL_RenderFillRectF(m_renderer, &r);

    SDL_SetRenderDrawColor(m_renderer, 80, 255, 120, 220);
    SDL_RenderDrawRectF(m_renderer, &r);
}

void Editor::renderSelectedObjectPreview()
{
    const int objIndex = selectedBrushObjectIndex();
    const auto& defs = m_objCatalog.Objects();

    if (objIndex < 0 || objIndex >= (int)defs.size())
        return;

    const auto& def = defs[objIndex];
    if (!def.has_sprite)
        return;

    SDL_Texture* tex = textureForObject(def);
    if (!tex)
        return;

    ImGui::Separator();
    ImGui::Text("Preview:");

    const float maxW = ImGui::GetContentRegionAvail().x;
    const float maxH = 220.0f;

    float w = (float)def.src.w;
    float h = (float)def.src.h;

    const float scale = std::min(maxW / w, maxH / h);
    w *= scale;
    h *= scale;

    const float texU0 = (float)def.src.x / 4096.0f; // dočasně nahradíme níž správně
    const float texV0 = (float)def.src.y / 4096.0f;
    const float texU1 = (float)(def.src.x + def.src.w) / 4096.0f;
    const float texV1 = (float)(def.src.y + def.src.h) / 4096.0f;

    int texW = 1;
    int texH = 1;
    SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);

    const ImVec2 uv0((float)def.src.x / texW, (float)def.src.y / texH);
    const ImVec2 uv1((float)(def.src.x + def.src.w) / texW, (float)(def.src.y + def.src.h) / texH);

    ImGui::Image((ImTextureID)tex, ImVec2(w, h), uv0, uv1);
}

void Editor::renderSelectedCompositePreview()
{
    if (m_terrainBrushMode != TerrainBrushMode::Composite)
        return;

    if (m_selectedCompositeGroup < 0 ||
        m_selectedCompositeGroup >= (int)m_compositeGroups.size())
        return;

    const auto& g = m_compositeGroups[m_selectedCompositeGroup];

    const int wTiles = g.maxx - g.minx + 1;
    const int hTiles = g.maxy - g.miny + 1;

    if (wTiles <= 0 || hTiles <= 0)
        return;

    ImGui::Separator();
    ImGui::Text("Composite preview:");
    ImGui::Text("ID: %s", g.id.c_str());
    ImGui::Text("Size: %d x %d", wTiles, hTiles);

    const float cell = 36.0f;
    const float pad = 2.0f;

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(
        wTiles * cell + (wTiles - 1) * pad,
        hTiles * cell + (hTiles - 1) * pad
    );

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(20, 20, 20, 255));
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(90, 90, 90, 255));

    for (int yy = 0; yy < hTiles; ++yy)
    {
        for (int xx = 0; xx < wTiles; ++xx)
        {
            const float x = p.x + xx * (cell + pad);
            const float y = p.y + yy * (cell + pad);

            dl->AddRectFilled(
                ImVec2(x, y),
                ImVec2(x + cell, y + cell),
                IM_COL32(40, 40, 40, 255));

            dl->AddRect(
                ImVec2(x, y),
                ImVec2(x + cell, y + cell),
                IM_COL32(110, 110, 110, 255));
        }
    }

    for (int defIdx : g.defIndices)
    {
        const auto* d = m_tileset.tileByIndex(defIdx);
        if (!d)
            continue;

        const int gx = d->composite_x - g.minx;
        const int gy = d->composite_y - g.miny;

        if (gx < 0 || gy < 0 || gx >= wTiles || gy >= hTiles)
            continue;

        const float x = p.x + gx * (cell + pad);
        const float y = p.y + gy * (cell + pad);

        dl->AddRectFilled(
            ImVec2(x + 1, y + 1),
            ImVec2(x + cell - 1, y + cell - 1),
            IM_COL32(70, 120, 80, 255));

        dl->AddText(
            ImVec2(x + 6, y + 8),
            IM_COL32(255, 255, 255, 255),
            d->surface.empty() ? "tile" : d->surface.c_str());
    }

    ImGui::Dummy(size);
}