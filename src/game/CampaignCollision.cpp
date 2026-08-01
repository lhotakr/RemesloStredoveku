#include "Campaign.h"

#include <algorithm>
#include <vector>
#include <cmath>

static bool AABB_Intersect(const SDL_Rect& a, const SDL_Rect& b)
{
    return (a.x < b.x + b.w) &&
        (a.x + a.w > b.x) &&
        (a.y < b.y + b.h) &&
        (a.y + a.h > b.y);
}
static SDL_Rect MakePlayerAABBAt(const Player& player, float testX, float testY)
{
    SDL_Rect r = player.worldAABB();

    const int curX = (int)std::lround(player.x);
    const int curY = (int)std::lround(player.y);

    const int newX = (int)std::lround(testX);
    const int newY = (int)std::lround(testY);

    r.x += (newX - curX);
    r.y += (newY - curY);
    return r;
}

static SDL_Rect ExpandRect(const SDL_Rect& r, int pad)
{
    SDL_Rect out = r;
    out.x -= pad;
    out.y -= pad;
    out.w += pad * 2;
    out.h += pad * 2;
    return out;
}

static SDL_Rect UnionRects(const std::vector<SDL_Rect>& rects)
{
    if (rects.empty())
        return SDL_Rect{ 0, 0, 0, 0 };

    int minX = rects[0].x;
    int minY = rects[0].y;
    int maxX = rects[0].x + rects[0].w;
    int maxY = rects[0].y + rects[0].h;

    for (size_t i = 1; i < rects.size(); ++i)
    {
        minX = std::min(minX, rects[i].x);
        minY = std::min(minY, rects[i].y);
        maxX = std::max(maxX, rects[i].x + rects[i].w);
        maxY = std::max(maxY, rects[i].y + rects[i].h);
    }


    return SDL_Rect{
        minX,
        minY,
        maxX - minX,
        maxY - minY
    };
}

static std::vector<SDL_Rect> GetWorldTerrainColliderRects(
    const TerrainTileset::TerrainTileDef& def,
    int wx,
    int wy,
    int tileSize)
{
    std::vector<SDL_Rect> out;

    if (!def.hasCollision)
        return out;

    for (const auto& rc : def.collisionRects)
    {
        SDL_Rect r{};
        r.x = wx + (int)std::lround(rc.x * tileSize);
        r.y = wy + (int)std::lround((1.0f - rc.y - rc.h) * tileSize);
        r.w = (int)std::lround(rc.w * tileSize);
        r.h = (int)std::lround(rc.h * tileSize);

        if (r.w > 0 && r.h > 0)
            out.push_back(r);
    }

    return out;
}

static const TerrainTileset::TerrainTileDef* GetTerrainDefAt(
    const TileMap& map,
    const TerrainTileset& tileset,
    int x,
    int y)
{
    const int baseId = map.get(x, y);
    if (baseId <= 0)
        return nullptr;

    const uint16_t overrideId = map.getOverride(x, y);
    if (overrideId > 0)
        return tileset.tileByIndex((int)overrideId - 1);

    std::string surface;
    switch (baseId)
    {
    case 1: surface = "grass"; break;
    case 2: surface = "water"; break;
    case 3: surface = "mud";   break;
    case 4: surface = "cliff"; break;
    default:
        return nullptr;
    }

    const int varIndex = (int)map.getVar(x, y);
    return tileset.pickFill(surface, varIndex);
}

static bool RectFullyCoveredByAny(
    const SDL_Rect& target,
    const std::vector<SDL_Rect>& covers)
{
    for (const SDL_Rect& c : covers)
    {
        if (c.x <= target.x &&
            c.y <= target.y &&
            c.x + c.w >= target.x + target.w &&
            c.y + c.h >= target.y + target.h)
        {
            return true;
        }
    }

    return false;
}

static std::vector<SDL_Rect> CollectWorldWalkableRectsFromObjects(
    const TileMap& map,
    const gameobj::ObjectCatalog& catalog,
    int tileSize)
{
    std::vector<SDL_Rect> out;

    for (int y = 0; y < map.height(); ++y)
    {
        for (int x = 0; x < map.width(); ++x)
        {
            const auto* def = map.getObjDefAt(catalog, x, y);
            if (!def)
                continue;

            int wx = 0;
            int wy = 0;
            map.getObjPivotWorld(x, y, wx, wy);

            const float objScale = map.getObjScale(x, y);
            const auto rects = gameobj::GetWorldWalkableRects(*def, wx, wy, objScale);

            out.insert(out.end(), rects.begin(), rects.end());
        }
    }

    return out;
}

bool Campaign::wouldPlayerCollideWithTerrainAt(float testX, float testY) const
{
    const SDL_Rect playerRect = MakePlayerAABBAt(m_player, testX, testY);

    // sesbíráme v?echny pr?chozí zóny objekt? (mosty, lávky, mola...)
    const auto walkableRects =
        CollectWorldWalkableRectsFromObjects(m_map, m_objCatalog, m_tileSize);

    for (int y = 0; y < m_map.height(); ++y)
    {
        for (int x = 0; x < m_map.width(); ++x)
        {
            const auto* def = GetTerrainDefAt(m_map, m_tileset, x, y);
            if (!def || !def->hasCollision)
                continue;

            const int wx = x * m_tileSize;
            const int wy = y * m_tileSize;

            const auto rects = GetWorldTerrainColliderRects(*def, wx, wy, m_tileSize);
            const int baseId = m_map.get(x, y);
            const bool isWater = (baseId == 2);

            for (const SDL_Rect& c : rects)
            {
                if (!AABB_Intersect(playerRect, c))
                    continue;

                // voda m??e být p?ebita pr?chozí oblastí objektu (nap?. most)
                if (isWater)
                {
                    SDL_Rect overlap{};
                    if (SDL_IntersectRect(&playerRect, &c, &overlap))
                    {
                        if (RectFullyCoveredByAny(overlap, walkableRects))
                            continue;
                    }
                }

                return true;
            }
        }
    }

    return false;
}

bool Campaign::wouldPlayerCollideWithNPCsAt(float testX, float testY) const
{
    const SDL_Rect playerRect = MakePlayerAABBAt(m_player, testX, testY);

    const auto& npcs = m_npcManager.npcs();

    for (const auto& npc : npcs)
    {
        SDL_Rect npcRect{};
        npcRect.w = (int)std::lround(m_tileSize * 0.70f);
        npcRect.h = (int)std::lround(m_tileSize * 0.70f);

        npcRect.x = (int)std::lround(npc.x - npcRect.w * 0.5f);
        npcRect.y = (int)std::lround(npc.y - npcRect.h);

        if (AABB_Intersect(playerRect, npcRect))
            return true;
    }

    return false;
}

static SDL_Rect GetObjectWorldRectScaled(const gameobj::ObjectDef& def, int wx, int wy, float scale)
{
    SDL_Rect r{};
    r.w = (int)std::lround(def.src.w * scale);
    r.h = (int)std::lround(def.src.h * scale);
    r.x = (int)std::lround(wx - def.pivot.x * scale);
    r.y = (int)std::lround(wy - def.pivot.y * scale);
    return r;
}

void Campaign::resolvePlayerVsObjects()
{
    auto testAndResolveAxis = [&](bool resolveX)
    {
        SDL_Rect pp = m_player.worldAABB();
        SDL_Rect broadPhase = ExpandRect(pp, 64);

        for (int y = 0; y < m_map.height(); ++y)
        {
            for (int x = 0; x < m_map.width(); ++x)
            {
                const auto* def = m_map.getObjDefAt(m_objCatalog, x, y);
                if (!def)
                    continue;

                int wx = 0;
                int wy = 0;
                m_map.getObjPivotWorld(x, y, wx, wy);

                const float objScale = m_map.getObjScale(x, y) * def->scale;
                const auto rects = gameobj::GetWorldColliderRects(*def, wx, wy, objScale);
                const auto walkableRects = gameobj::GetWorldWalkableRects(*def, wx, wy, objScale);

                // objekt blokuje hrá?e, pokud je solid
                // nebo pokud má definované collision rects / collider
                if (!def->solid && rects.empty())
                    continue;

                const SDL_Rect objectBounds = !rects.empty()
                    ? UnionRects(rects)
                    : GetObjectWorldRectScaled(*def, wx, wy, objScale);

                if (!AABB_Intersect(broadPhase, objectBounds))
                    continue;

                if (!rects.empty())
                {
                    for (const SDL_Rect& c : rects)
                    {
                        if (!AABB_Intersect(pp, c))
                            continue;

                        SDL_Rect overlap{};
                        if (SDL_IntersectRect(&pp, &c, &overlap) && RectFullyCoveredByAny(overlap, walkableRects))
                            continue;

                        if (resolveX)
                        {
                            const int leftPen = (pp.x + pp.w) - c.x;
                            const int rightPen = (c.x + c.w) - pp.x;

                            if (leftPen < rightPen)
                                m_player.x -= (float)leftPen;
                            else
                                m_player.x += (float)rightPen;

                            pp = m_player.worldAABB();
                            broadPhase = ExpandRect(pp, 64);
                        }
                        else
                        {
                            const int upPen = (pp.y + pp.h) - c.y;
                            const int downPen = (c.y + c.h) - pp.y;

                            if (upPen < downPen)
                                m_player.y -= (float)upPen;
                            else
                                m_player.y += (float)downPen;

                            pp = m_player.worldAABB();
                            broadPhase = ExpandRect(pp, 64);
                        }
                    }
                }
                else
                {
                    const SDL_Rect c = GetObjectWorldRectScaled(*def, wx, wy, objScale);

                    if (!AABB_Intersect(pp, c))
                        continue;

                    if (resolveX)
                    {
                        const int leftPen = (pp.x + pp.w) - c.x;
                        const int rightPen = (c.x + c.w) - pp.x;

                        if (leftPen < rightPen)
                            m_player.x -= (float)leftPen;
                        else
                            m_player.x += (float)rightPen;

                        pp = m_player.worldAABB();
                        broadPhase = ExpandRect(pp, 64);
                    }
                    else
                    {
                        const int upPen = (pp.y + pp.h) - c.y;
                        const int downPen = (c.y + c.h) - pp.y;

                        if (upPen < downPen)
                            m_player.y -= (float)upPen;
                        else
                            m_player.y += (float)downPen;

                        pp = m_player.worldAABB();
                        broadPhase = ExpandRect(pp, 64);
                    }
                }
            }
        }
    };

    testAndResolveAxis(true);
    testAndResolveAxis(false);
}

bool Campaign::wouldPlayerCollideWithObjectsAt(float testX, float testY) const
{
    SDL_Rect playerRect = m_player.worldAABB();
    playerRect.x = (int)std::lround(testX - m_player.colW * 0.5f);
    playerRect.y = (int)std::lround(testY - m_player.colH);

    for (int y = 0; y < m_map.height(); ++y)
    {
        for (int x = 0; x < m_map.width(); ++x)
        {
            const auto* def = m_map.getObjDefAt(m_objCatalog, x, y);
            if (!def)
                continue;

            int wx = 0;
            int wy = 0;
            m_map.getObjPivotWorld(x, y, wx, wy);

            const float objScale = m_map.getObjScale(x, y) * def->scale;
            const auto rects = gameobj::GetWorldColliderRects(*def, wx, wy, objScale);
            const auto walkableRects = gameobj::GetWorldWalkableRects(*def, wx, wy, objScale);

            if (!def->solid && rects.empty())
                continue;

            if (!rects.empty())
            {
                for (const SDL_Rect& r : rects)
                {
                    SDL_Rect overlap{};
                    if (!SDL_IntersectRect(&playerRect, &r, &overlap))
                        continue;

                    if (RectFullyCoveredByAny(overlap, walkableRects))
                        continue;

                    return true;
                }
            }
            else
            {
                const SDL_Rect fallback = GetObjectWorldRectScaled(*def, wx, wy, objScale);
                if (SDL_HasIntersection(&playerRect, &fallback))
                    return true;
            }
        }
    }

    return false;
}

void Campaign::clampPlayerToMap()
{
    SDL_Rect aabb = m_player.worldAABB();

    const int mapPixelW = m_map.width() * m_tileSize;
    const int mapPixelH = m_map.height() * m_tileSize;

    if (aabb.x < 0)
        m_player.x += (float)(-aabb.x);

    if (aabb.y < 0)
        m_player.y += (float)(-aabb.y);

    if (aabb.x + aabb.w > mapPixelW)
        m_player.x -= (float)((aabb.x + aabb.w) - mapPixelW);

    if (aabb.y + aabb.h > mapPixelH)
        m_player.y -= (float)((aabb.y + aabb.h) - mapPixelH);
}
