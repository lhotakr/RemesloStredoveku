#include "BuildInteriorEngine.h"
#include "Utf8.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
bool validDimensions(double width, double height)
{
    // Keep the renderer's sub-cell lookup bounded before changing any geometry.
    return std::isfinite(width) && std::isfinite(height) &&
        width >= 0.25 && height >= 0.25 && width <= 8192.0 && height <= 8192.0 &&
        (std::ceil(width) + 4.0) * (std::ceil(height) + 4.0) * 64.0 <= 16777216.0;
}
}

void BuildInteriorEngine::setSceneEyeHeight(double height)
{
    if (std::isfinite(height)) m_eyeHeight = std::clamp(height, 0.05, 128.0);
}

bool BuildInteriorEngine::projectScenePoint(ScenePoint point, double& u, double& v, bool& occluded) const
{
    if (m_sceneW <= 0 || m_sceneH <= 0) return false;
    const double dx = point.x - m_renderViewOrigin.x, dy = point.y - m_renderViewOrigin.y;
    const double dirX = std::cos(m_renderViewAngle), dirY = std::sin(m_renderViewAngle);
    const double depth = dx * dirX + dy * dirY;
    if (!std::isfinite(depth) || depth <= 0.035) return false;
    u = 0.5 * (1.0 + (-dx * dirY + dy * dirX) / (depth * std::tan(m_renderViewFov * 0.5)));
    v = static_cast<double>(m_renderViewHorizon) / m_sceneH - (point.z - m_renderViewOrigin.z) / depth;
    if (!std::isfinite(u) || !std::isfinite(v)) return false;
    occluded = false;
    // Offscreen projections are useful for dragging an elevated point whose
    // ground footprint is below the image. Only visible pixels have depth.
    if (u < 0 || u >= 1 || v < 0 || v >= 1) return true;
    // Sample a one-pixel neighborhood: a floor point can fall between depth
    // samples, especially in the reduced-resolution preview near the horizon.
    const int px = static_cast<int>(u * m_sceneW), py = static_cast<int>(v * m_sceneH);
    double visibleDepth = 0;
    for (int y = std::max(0, py - 1); y <= std::min(m_sceneH - 1, py + 1); ++y)
        for (int x = std::max(0, px - 1); x <= std::min(m_sceneW - 1, px + 1); ++x)
        {
            const auto pixel = static_cast<size_t>(y) * m_sceneW + x;
            if (pixel < m_dynamicDepthBuffer.size()) visibleDepth = std::max(visibleDepth, m_dynamicDepthBuffer[pixel]);
        }
    occluded = visibleDepth > 0 && visibleDepth + std::max(0.03, depth * 0.008) < depth;
    return true;
}

bool BuildInteriorEngine::pickScenePoint(double u, double v, ScenePoint& point) const
{
    if (!std::isfinite(u) || !std::isfinite(v) || u < 0 || u >= 1 || v < 0 || v >= 1 || m_sceneW <= 0 || m_sceneH <= 0) return false;
    const int x = static_cast<int>(u * m_sceneW), y = static_cast<int>(v * m_sceneH);
    const auto pixel = static_cast<size_t>(y) * m_sceneW + x;
    if (pixel >= m_dynamicDepthBuffer.size()) return false;
    const double depth = m_dynamicDepthBuffer[pixel];
    if (!std::isfinite(depth) || depth <= 0.0 || depth > 10000.0) return false;
    const double screenX = 2.0 * (x + 0.5) / m_sceneW - 1.0;
    const double dirX = std::cos(m_renderViewAngle), dirY = std::sin(m_renderViewAngle);
    const double plane = std::tan(m_renderViewFov * 0.5) * screenX;
    point = {m_renderViewOrigin.x + depth * (dirX - dirY * plane),
        m_renderViewOrigin.y + depth * (dirY + dirX * plane),
        m_renderViewOrigin.z + depth * (m_renderViewHorizon - (y + 0.5)) / m_sceneH};
    return true;
}

bool BuildInteriorEngine::intersectSceneHeight(double u, double v, double worldZ, ScenePoint& point) const
{
    if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(worldZ) || m_sceneH <= 0) return false;
    const double slope = static_cast<double>(m_renderViewHorizon) / m_sceneH - v;
    if (std::abs(slope) < 0.0001) return false;
    const double depth = (worldZ - m_renderViewOrigin.z) / slope;
    if (depth <= 0.035 || depth > 1000.0) return false;
    const double dirX = std::cos(m_renderViewAngle), dirY = std::sin(m_renderViewAngle);
    const double plane = (2.0 * u - 1.0) * std::tan(m_renderViewFov * 0.5);
    point = {m_renderViewOrigin.x + depth * (dirX - dirY * plane),
        m_renderViewOrigin.y + depth * (dirY + dirX * plane), worldZ};
    return true;
}

std::vector<std::vector<std::array<double, 2>>> BuildInteriorEngine::sceneBoundaries() const
{
    std::vector<std::vector<std::array<double, 2>>> boundaries;
    for (const auto& polygon : m_polygonSectors) boundaries.push_back(polygon.vertices);
    if (boundaries.empty() && !m_grid.empty())
        boundaries.push_back({{0, 0}, {static_cast<double>(m_grid.front().size()), 0},
            {static_cast<double>(m_grid.front().size()), static_cast<double>(m_grid.size())}, {0, static_cast<double>(m_grid.size())}});
    return boundaries;
}

int BuildInteriorEngine::pickEditorObject(int screenX, int screenY) const
{
    if (!m_editorPreview3D || m_editorObjectPixels.empty() || !m_window ||
        m_sceneW <= 0 || m_sceneH <= 0)
        return -1;
    int width = 0, height = 0;
    SDL_GetWindowSize(m_window, &width, &height);
    if (screenX < 0 || screenY < 0 || screenX >= width || screenY >= height)
        return -1;
    const int x = std::clamp(screenX * m_sceneW / width, 0, m_sceneW - 1);
    const int y = std::clamp(screenY * m_sceneH / height, 0, m_sceneH - 1);
    const auto pixel = static_cast<std::size_t>(y) * m_sceneW + x;
    if (pixel >= m_editorObjectPixels.size())
        return -1;
    const int index = m_editorObjectPixels[pixel];
    return index >= 0 && index < static_cast<int>(m_sprites.size()) ? index : -1;
}

bool BuildInteriorEngine::resizeEditorEnvironment(double width, double height)
{
    if (m_polygonSectors.empty() || !validDimensions(width, height))
    {
        m_status = U8("Neplatné rozměry nebo příliš velká plocha prostředí.");
        return false;
    }
    double minX = std::numeric_limits<double>::max(), minY = minX;
    double maxX = std::numeric_limits<double>::lowest(), maxY = maxX;
    for (const auto& region : m_polygonSectors)
    {
        minX = std::min(minX, region.minX); minY = std::min(minY, region.minY);
        maxX = std::max(maxX, region.maxX); maxY = std::max(maxY, region.maxY);
    }
    if (maxX - minX < 0.0001 || maxY - minY < 0.0001)
        return false;
    const double sx = width / (maxX - minX), sy = height / (maxY - minY);
    auto transform = [&](double& x, double& y)
    {
        x = minX + (x - minX) * sx;
        y = minY + (y - minY) * sy;
    };
    for (auto& region : m_polygonSectors)
    {
        for (auto& vertex : region.vertices)
            transform(vertex[0], vertex[1]);
        updatePolygonBounds(region);
    }
    for (auto& wall : m_wallSegments)
    {
        transform(wall.x0, wall.y0);
        transform(wall.x1, wall.y1);
    }
    for (auto& door : m_doors)
    {
        if (!door.hasSegment)
        {
            const bool vertical = doorLeafAxis(door) == 'y';
            door.segmentX0 = door.x + (vertical ? 0.5 : 0.0);
            door.segmentY0 = door.y + (vertical ? 0.0 : 0.5);
            door.segmentX1 = door.segmentX0 + (vertical ? 0.0 : door.span);
            door.segmentY1 = door.segmentY0 + (vertical ? door.span : 0.0);
            door.hasSegment = true;
        }
        transform(door.segmentX0, door.segmentY0);
        transform(door.segmentX1, door.segmentY1);
        door.x = static_cast<int>(std::floor(door.segmentX0));
        door.y = static_cast<int>(std::floor(door.segmentY0));
    }
    for (auto& ramp : m_traversalRamps)
    {
        const double dx = ramp.endX - ramp.startX, dy = ramp.endY - ramp.startY;
        const double length = std::hypot(dx, dy);
        if (length > 0.0001)
            ramp.width *= std::hypot(dy * sx, dx * sy) / length;
        transform(ramp.startX, ramp.startY);
        transform(ramp.endX, ramp.endY);
    }
    for (auto& [symbol, sector] : m_sectors)
    {
        transform(sector.slopeOriginX, sector.slopeOriginY);
        sector.floorSlopeX /= sx; sector.floorSlopeY /= sy;
        sector.ceilingSlopeX /= sx; sector.ceilingSlopeY /= sy;
    }
    for (auto& sprite : m_sprites)
        transform(sprite.x, sprite.y);
    for (auto& [id, spawn] : m_namedSpawns)
        transform(spawn.x, spawn.y);
    transform(m_posX, m_posY);
    transform(m_lastSafeX, m_lastSafeY);
    rebuildPolygonBoundaryWalls();
    rebuildVectorSectorLookup();
    m_sceneDirty = true;
    m_status = U8("Rozměry prostředí změněny; objekty a spawny se posunuly spolu s geometrií.");
    return true;
}

bool BuildInteriorEngine::resizeEditorPolygon(int index, double width, double height)
{
    if (index < 0 || index >= static_cast<int>(m_polygonSectors.size()) ||
        !validDimensions(width, height))
        return false;
    auto& region = m_polygonSectors[index];
    const double oldWidth = region.maxX - region.minX, oldHeight = region.maxY - region.minY;
    if (oldWidth < 0.0001 || oldHeight < 0.0001)
        return false;
    const double newMaxX = region.minX + width, newMaxY = region.minY + height;
    double minX = region.minX, minY = region.minY, maxX = newMaxX, maxY = newMaxY;
    for (int i = 0; i < static_cast<int>(m_polygonSectors.size()); ++i)
    {
        if (i == index) continue;
        const auto& other = m_polygonSectors[i];
        minX = std::min(minX, other.minX); minY = std::min(minY, other.minY);
        maxX = std::max(maxX, other.maxX); maxY = std::max(maxY, other.maxY);
    }
    if (!validDimensions(maxX - minX, maxY - minY))
    {
        m_status = U8("Celkové rozměry polygonálního prostředí jsou příliš velké.");
        return false;
    }
    for (auto& vertex : region.vertices)
    {
        vertex[0] = region.minX + (vertex[0] - region.minX) * width / oldWidth;
        vertex[1] = region.minY + (vertex[1] - region.minY) * height / oldHeight;
    }
    updatePolygonBounds(region);
    rebuildPolygonBoundaryWalls();
    rebuildVectorSectorLookup();
    m_sceneDirty = true;
    return true;
}

bool BuildInteriorEngine::createEditorSectorRectangle(double x0, double y0, double x1, double y1)
{
    if (!hasVectorGeometry() || m_sectors.find(m_editorSectorBrush) == m_sectors.end() ||
        !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) ||
        std::abs(x0) > 1000000.0 || std::abs(y0) > 1000000.0 ||
        std::abs(x1) > 1000000.0 || std::abs(y1) > 1000000.0 ||
        !validDimensions(std::abs(x1 - x0), std::abs(y1 - y0)))
    {
        m_status = U8("Plochu nelze vložit: vyberte sektor a obdélník alespoň 0.25 × 0.25 v polygonální mapě.");
        return false;
    }
    const double left = std::min(x0, x1), top = std::min(y0, y1);
    const double right = std::max(x0, x1), bottom = std::max(y0, y1);
    double minX = left, minY = top, maxX = right, maxY = bottom;
    for (const auto& region : m_polygonSectors)
    {
        minX = std::min(minX, region.minX); minY = std::min(minY, region.minY);
        maxX = std::max(maxX, region.maxX); maxY = std::max(maxY, region.maxY);
    }
    if (!validDimensions(maxX - minX, maxY - minY))
    {
        m_status = U8("Celkové rozměry polygonálního prostředí jsou příliš velké.");
        return false;
    }

    const auto& sector = m_sectors.at(m_editorSectorBrush);
    PolygonSectorRegion region;
    const std::string prefix = sector.id.empty() ? "polygon" : sector.id;
    int suffix = 1;
    do { region.id = prefix + "_" + std::to_string(suffix++); }
    while (std::any_of(m_polygonSectors.begin(), m_polygonSectors.end(),
        [&](const auto& existing) { return existing.id == region.id; }));
    region.sector = m_editorSectorBrush;
    region.boundarySolid = m_editorNewSectorWalls;
    // A road at the clearing's floor height must replace that floor, including
    // in the live preview, rather than lose the depth test to the older surface.
    region.cutsUnderlyingFloor = true;
    region.vertices = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
    updatePolygonBounds(region);
    m_polygonSectors.push_back(std::move(region));
    m_editorSelectedPolygon = static_cast<int>(m_polygonSectors.size()) - 1;
    m_editorSelectedPolygonVertex = -1;
    m_editorSelectedOpening = -1;
    m_editorPolygonPointMode = false;
    m_editorSectorPlacement = false;
    m_editorDrawingSector = false;
    rebuildPolygonBoundaryWalls();
    rebuildVectorSectorLookup();
    m_sceneDirty = true;
    m_status = U8("Vložena plocha sektoru: ") + sector.name + U8(". Změny uloží Save JSON.");
    return true;
}

void BuildInteriorEngine::renderEditorSectorPlacement(bool showSectorPicker)
{
    ImGui::PushID("SectorPlacement");
    if (showSectorPicker)
    {
        const auto selected = m_sectors.find(m_editorSectorBrush);
        const std::string preview = selected != m_sectors.end()
            ? selected->second.name : U8("Vyberte sektor");
        if (ImGui::BeginCombo(U8("Sektor pro vložení"), preview.c_str()))
        {
            std::vector<char> symbols;
            for (const auto& pair : m_sectors) symbols.push_back(pair.first);
            std::sort(symbols.begin(), symbols.end(), [](char a, char b) {
                return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
            });
            for (char symbol : symbols)
            {
                const std::string label = m_sectors.at(symbol).name + "##" +
                    std::to_string(static_cast<unsigned char>(symbol));
                if (ImGui::Selectable(label.c_str(), symbol == m_editorSectorBrush))
                    m_editorSectorBrush = symbol;
            }
            ImGui::EndCombo();
        }
    }
    ImGui::TextWrapped(U8("Vybraný sektor vložíte jako novou obdélníkovou plochu. Podlahu pod ní nahradí jeho materiál; okolí zůstane zachované."));
    ImGui::BeginDisabled(!hasVectorGeometry());
    ImGui::Checkbox(U8("Stěny kolem nové plochy"), &m_editorNewSectorWalls);
    if (ImGui::Button(m_editorSectorPlacement ? U8("Zrušit vkládání") : U8("Vložit tažením v mapě")))
    {
        m_editorSectorPlacement = !m_editorSectorPlacement;
        m_editorDrawingSector = false;
        m_editorPolygonPointMode = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(U8("Vložit plochu u kamery")))
    {
        if (validDimensions(m_editorNewSectorWidth, m_editorNewSectorDepth))
            createEditorSectorRectangle(m_posX - m_editorNewSectorWidth * 0.5,
                m_posY - m_editorNewSectorDepth * 0.5, m_posX + m_editorNewSectorWidth * 0.5,
                m_posY + m_editorNewSectorDepth * 0.5);
        else
            m_status = U8("Neplatné rozměry nové plochy; minimum je 0.25 × 0.25.");
    }
    if (ImGui::TreeNode(U8("Rozměry vložení u kamery")))
    {
        ImGui::InputDouble(U8("Šířka nové plochy"), &m_editorNewSectorWidth, 0.25, 1.0, "%.2f");
        ImGui::InputDouble(U8("Hloubka nové plochy"), &m_editorNewSectorDepth, 0.25, 1.0, "%.2f");
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
    if (m_editorSectorPlacement)
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "%s",
            U8("Táhněte levým tlačítkem v mapě. Ctrl přichytává po 0.25; Esc nebo pravé tlačítko zruší vkládání."));
    ImGui::PopID();
}

bool BuildInteriorEngine::convertEditorGridToPolygons()
{
    if (hasVectorGeometry() || m_grid.empty())
        return false;
    std::vector<PolygonSectorRegion> regions;
    std::vector<std::vector<bool>> used;
    for (const auto& row : m_grid)
        used.emplace_back(row.size(), false);
    auto available = [&](int x, int y, char sector)
    {
        return isInside(x, y) && !used[y][x] && sectorSymbolAt(x, y) == sector &&
            (cellAt(x, y) == kNoTexture || cellAt(x, y) == static_cast<TextureKey>('.') ||
             cellAt(x, y) == static_cast<TextureKey>('D'));
    };
    // Combine adjacent cells into rooms. Shared edges become real portals;
    // subsequent edits operate only on polygon vertices, without tile snapping.
    for (int y = 0; y < static_cast<int>(m_grid.size()); ++y)
        for (int x = 0; x < static_cast<int>(m_grid[y].size()); ++x)
        {
            const char sector = sectorSymbolAt(x, y);
            if (!available(x, y, sector)) continue;
            int right = x + 1, bottom = y + 1;
            while (available(right, y, sector)) ++right;
            for (; bottom < static_cast<int>(m_grid.size()); ++bottom)
            {
                bool wholeRow = true;
                for (int col = x; col < right; ++col)
                    if (!available(col, bottom, sector)) { wholeRow = false; break; }
                if (!wholeRow) break;
            }
            for (int row = y; row < bottom; ++row)
                for (int col = x; col < right; ++col)
                    used[row][col] = true;
            PolygonSectorRegion region;
            region.id = "room_" + std::to_string(regions.size() + 1);
            region.sector = sector;
            region.vertices = {{{double(x), double(y)}}, {{double(right), double(y)}},
                               {{double(right), double(bottom)}}, {{double(x), double(bottom)}}};
            updatePolygonBounds(region);
            regions.push_back(std::move(region));
        }
    if (regions.empty())
    {
        m_status = U8("Mapa neobsahuje prostor pro vytvoření polygonálních sektorů.");
        return false;
    }
    m_polygonSectors = std::move(regions);
    m_polygonGeometryPrimary = true;
    for (auto& door : m_doors)
    {
        if (door.hasSegment) continue;
        const bool vertical = doorLeafAxis(door) == 'y';
        door.segmentX0 = door.x + (vertical ? 0.5 : 0.0);
        door.segmentY0 = door.y + (vertical ? 0.0 : 0.5);
        door.segmentX1 = door.segmentX0 + (vertical ? 0.0 : door.span);
        door.segmentY1 = door.segmentY0 + (vertical ? door.span : 0.0);
        door.hasSegment = true;
    }
    m_editorSelectedPolygon = 0;
    m_editorSectorBrush = m_polygonSectors.front().sector;
    m_editorSelectedPolygonVertex = -1;
    m_editorSelectedOpening = -1;
    rebuildPolygonBoundaryWalls();
    rebuildVectorSectorLookup();
    m_sceneDirty = true;
    m_status = U8("Mapa převedena na polygonální sektory. Změny uloží Save JSON.");
    return true;
}

void BuildInteriorEngine::renderEditorEnvironmentSize()
{
    if (m_polygonSectors.empty())
        return;
    double minX = m_polygonSectors.front().minX, minY = m_polygonSectors.front().minY;
    double maxX = m_polygonSectors.front().maxX, maxY = m_polygonSectors.front().maxY;
    for (const auto& region : m_polygonSectors)
    {
        minX = std::min(minX, region.minX); minY = std::min(minY, region.minY);
        maxX = std::max(maxX, region.maxX); maxY = std::max(maxY, region.maxY);
    }
    if (ImGui::CollapsingHeader(U8("Rozměry prostředí"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        double width = maxX - minX, height = maxY - minY;
        bool changed = ImGui::InputDouble(U8("Šířka prostředí"), &width, 0.5, 5.0, "%.2f");
        changed |= ImGui::InputDouble(U8("Hloubka prostředí"), &height, 0.5, 5.0, "%.2f");
        if (changed)
            resizeEditorEnvironment(width, height);
        ImGui::TextWrapped(U8("Rozměry se mění ihned. Geometrie, objekty i spawny se přepočítají od levého horního rohu."));
    }
}
