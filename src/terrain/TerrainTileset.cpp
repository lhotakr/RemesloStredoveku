#include "TerrainTileset.h"

#include <SDL_image.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <cmath>

using json = nlohmann::json;

namespace
{
    static bool ParseCollisionRects(
        const json& tileJson,
        std::vector<TerrainTileset::CollisionRectNorm>& outRects)
    {
        outRects.clear();

        if (!tileJson.contains("collision") || !tileJson["collision"].is_object())
            return false;

        const auto& jc = tileJson["collision"];
        const std::string type = jc.value("type", "");

        // nový formát z Python editoru:
        // "collision": { "type":"rects", "rects":[ [x,y,w,h], ... ] }
        if (type == "rects")
        {
            if (!jc.contains("rects") || !jc["rects"].is_array())
                return false;

            for (const auto& jr : jc["rects"])
            {
                if (!jr.is_array() || jr.size() != 4)
                    continue;

                TerrainTileset::CollisionRectNorm r;
                r.x = jr[0].get<float>();
                r.y = jr[1].get<float>();
                r.w = jr[2].get<float>();
                r.h = jr[3].get<float>();

                // odfiltrovat nesmysly
                if (r.w <= 0.0f || r.h <= 0.0f)
                    continue;

                outRects.push_back(r);
            }

            return !outRects.empty();
        }

        // fallback pro starší poly formát:
        // "collision": { "type":"poly", "points":[ [x,y], ... ] }
        // pøevedeme na jeden bbox rect, aby stará data dál fungovala
        if (type == "poly")
        {
            if (!jc.contains("points") || !jc["points"].is_array())
                return false;

            const auto& pts = jc["points"];
            if (pts.size() < 3)
                return false;

            float minX = 1.0f;
            float minY = 1.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;

            bool any = false;

            for (const auto& jp : pts)
            {
                if (!jp.is_array() || jp.size() != 2)
                    continue;

                const float x = jp[0].get<float>();
                const float y = jp[1].get<float>();

                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
                any = true;
            }

            if (!any)
                return false;

            TerrainTileset::CollisionRectNorm r;
            r.x = minX;
            r.y = minY;
            r.w = std::max(0.0f, maxX - minX);
            r.h = std::max(0.0f, maxY - minY);

            if (r.w > 0.0f && r.h > 0.0f)
                outRects.push_back(r);

            return !outRects.empty();
        }

        return false;
    }
}

bool TerrainTileset::loadFromJson(SDL_Renderer* renderer,
    const std::string& jsonPath,
    const std::string& atlasPath)
{
    // 1) JSON
    std::ifstream f(jsonPath);
    if (!f) {
        SDL_Log("TerrainTileset: JSON not found: %s", jsonPath.c_str());
        return false;
    }

    json j;
    try {
        f >> j;
    }
    catch (...) {
        SDL_Log("TerrainTileset: JSON parse failed: %s", jsonPath.c_str());
        return false;
    }

    m_tileSize = j.value("tile_size", 32);

    // 2) Atlas texture
    SDL_Surface* surf = IMG_Load(atlasPath.c_str());
    if (!surf) {
        SDL_Log("TerrainTileset: IMG_Load failed %s: %s", atlasPath.c_str(), IMG_GetError());
        return false;
    }

    destroyAtlas();
    m_atlasTex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);

    if (!m_atlasTex) {
        SDL_Log("TerrainTileset: CreateTextureFromSurface failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(m_atlasTex, SDL_BLENDMODE_BLEND);

    // 3) Tiles parse
    m_tilesAll.clear();
    if (!j.contains("tiles") || !j["tiles"].is_array()) {
        SDL_Log("TerrainTileset: missing tiles[] array");
        return false;
    }

    for (auto& t : j["tiles"])
    {
        TerrainTileDef def;
        def.uid = t.value("uid", "");
        def.atlas_x = t.value("atlas_x", 0);
        def.atlas_y = t.value("atlas_y", 0);

        def.surface = t.value("surface", "");
        def.feature = t.value("feature", "normal");
        def.variant = t.value("variant", 0);
        def.layer = t.value("layer", "base");
        def.mask = t.value("mask", 0);

        if (t.contains("transition_to") && !t["transition_to"].is_null())
            def.transition_to = t["transition_to"].get<std::string>();

        def.hasComposite = (t.contains("composite") && !t["composite"].is_null());
        if (def.hasComposite) {
            def.composite_id = t["composite"].value("id", "");
            def.composite_x = t["composite"].value("x", 0);
            def.composite_y = t["composite"].value("y", 0);
        }

        def.src = SDL_Rect{
            def.atlas_x * m_tileSize,
            def.atlas_y * m_tileSize,
            m_tileSize,
            m_tileSize
        };

        // --- NOVÉ: collision rects ---
        def.collisionRects.clear();
        def.hasCollision = ParseCollisionRects(t, def.collisionRects);

        if (def.surface.empty())
            continue;

        m_tilesAll.push_back(std::move(def));
    }

    rebuildLookups();
    SDL_Log("TerrainTileset loaded: tiles=%d tileSize=%d", (int)m_tilesAll.size(), m_tileSize);
    return !m_tilesAll.empty();
}

void TerrainTileset::destroyAtlas()
{
    if (m_atlasTex) {
        SDL_DestroyTexture(m_atlasTex);
        m_atlasTex = nullptr;
    }
}

const TerrainTileset::TerrainTileDef* TerrainTileset::tileByIndex(int defIdx) const
{
    if (defIdx < 0 || defIdx >= (int)m_tilesAll.size())
        return nullptr;
    return &m_tilesAll[defIdx];
}

void TerrainTileset::rebuildLookups()
{
    m_fillIndicesBySurface.clear();
    m_transitionLut.clear();

    // fill indices: base + normal + no composite + no transition_to
    for (int i = 0; i < (int)m_tilesAll.size(); ++i)
    {
        const auto& d = m_tilesAll[i];

        const bool isFill =
            (d.layer == "base") &&
            (d.feature == "normal") &&
            (!d.hasComposite) &&
            (d.transition_to.empty());

        if (isFill)
            m_fillIndicesBySurface[d.surface].push_back(i);

        if (d.layer == "transition" && !d.transition_to.empty())
        {
            TrKey k{ d.surface, d.transition_to, d.mask };
            if (m_transitionLut.find(k) == m_transitionLut.end())
                m_transitionLut.emplace(std::move(k), i);
        }
    }

    // seøadit fill listy podle variant
    for (auto& kv : m_fillIndicesBySurface)
    {
        auto& vec = kv.second;
        std::sort(vec.begin(), vec.end(), [&](int a, int b) {
            return m_tilesAll[a].variant < m_tilesAll[b].variant;
            });
    }
}

int TerrainTileset::fillCount(const std::string& surface) const
{
    auto it = m_fillIndicesBySurface.find(surface);
    if (it == m_fillIndicesBySurface.end())
        return 0;
    return (int)it->second.size();
}

const TerrainTileset::TerrainTileDef* TerrainTileset::pickFill(const std::string& surface, int varIndex) const
{
    auto it = m_fillIndicesBySurface.find(surface);
    if (it == m_fillIndicesBySurface.end() || it->second.empty())
        return nullptr;

    const auto& vec = it->second;
    int idx = 0;
    if (varIndex >= 0)
        idx = varIndex % (int)vec.size();

    return &m_tilesAll[vec[idx]];
}

const TerrainTileset::TerrainTileDef* TerrainTileset::findTransition(const std::string& from,
    const std::string& to,
    int mask) const
{
    TrKey k{ from, to, mask };
    auto it = m_transitionLut.find(k);
    if (it == m_transitionLut.end())
        return nullptr;

    int defIdx = it->second;
    if (defIdx < 0 || defIdx >= (int)m_tilesAll.size())
        return nullptr;

    return &m_tilesAll[defIdx];
}

std::vector<TerrainTileset::CompositeGroup> TerrainTileset::buildCompositeGroups() const
{
    std::vector<CompositeGroup> out;
    std::unordered_map<std::string, int> idToIndex;

    for (int i = 0; i < (int)m_tilesAll.size(); ++i)
    {
        const auto& d = m_tilesAll[i];
        if (!d.hasComposite)
            continue;
        if (d.composite_id.empty())
            continue;

        int gi = -1;
        auto it = idToIndex.find(d.composite_id);
        if (it == idToIndex.end())
        {
            CompositeGroup g;
            g.id = d.composite_id;
            g.minx = g.maxx = d.composite_x;
            g.miny = g.maxy = d.composite_y;
            out.push_back(std::move(g));
            gi = (int)out.size() - 1;
            idToIndex[d.composite_id] = gi;
        }
        else
        {
            gi = it->second;
            auto& g = out[gi];
            g.minx = std::min(g.minx, d.composite_x);
            g.miny = std::min(g.miny, d.composite_y);
            g.maxx = std::max(g.maxx, d.composite_x);
            g.maxy = std::max(g.maxy, d.composite_y);
        }

        out[gi].defIndices.push_back(i);
    }

    std::sort(out.begin(), out.end(), [](const CompositeGroup& a, const CompositeGroup& b) {
        return a.id < b.id;
        });

    for (auto& g : out)
    {
        std::sort(g.defIndices.begin(), g.defIndices.end(), [&](int a, int b) {
            const auto& A = m_tilesAll[a];
            const auto& B = m_tilesAll[b];
            if (A.composite_y != B.composite_y)
                return A.composite_y < B.composite_y;
            return A.composite_x < B.composite_x;
            });
    }

    return out;
}