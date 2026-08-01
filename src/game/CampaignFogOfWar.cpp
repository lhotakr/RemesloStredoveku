#include "Campaign.h"

#include <algorithm>
#include <cmath>

static int CountVisitedNeighbors(const std::vector<uint8_t>& visited, int mapW, int mapH, int x, int y)
{
    int count = 0;

    for (int oy = -1; oy <= 1; ++oy)
    {
        for (int ox = -1; ox <= 1; ++ox)
        {
            if (ox == 0 && oy == 0)
                continue;

            const int nx = x + ox;
            const int ny = y + oy;

            if (nx < 0 || ny < 0 || nx >= mapW || ny >= mapH)
                continue;

            if (visited[ny * mapW + nx] != 0)
                ++count;
        }
    }

    return count;
}

static int WrapMod(int v, int m)
{
    if (m <= 0) return 0;
    int r = v % m;
    return (r < 0) ? (r + m) : r;
}

static void RenderWrappedFogBlock(
    SDL_Renderer* renderer,
    SDL_Texture* tex,
    int texW,
    int texH,
    int worldX,
    int worldY,
    int screenX,
    int screenY,
    int drawW,
    int drawH,
    Uint8 alpha)
{
    if (!renderer || !tex || texW <= 0 || texH <= 0 || drawW <= 0 || drawH <= 0)
        return;

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex, alpha);

    const int srcX = WrapMod(worldX, texW);
    const int srcY = WrapMod(worldY, texH);

    const int firstW = std::min(drawW, texW - srcX);
    const int firstH = std::min(drawH, texH - srcY);

    {
        SDL_Rect src{ srcX, srcY, firstW, firstH };
        SDL_Rect dst{ screenX, screenY, firstW, firstH };
        SDL_RenderCopy(renderer, tex, &src, &dst);
    }

    if (firstW < drawW)
    {
        SDL_Rect src{ 0, srcY, drawW - firstW, firstH };
        SDL_Rect dst{ screenX + firstW, screenY, drawW - firstW, firstH };
        SDL_RenderCopy(renderer, tex, &src, &dst);
    }

    if (firstH < drawH)
    {
        SDL_Rect src{ srcX, 0, firstW, drawH - firstH };
        SDL_Rect dst{ screenX, screenY + firstH, firstW, drawH - firstH };
        SDL_RenderCopy(renderer, tex, &src, &dst);
    }

    if (firstW < drawW && firstH < drawH)
    {
        SDL_Rect src{ 0, 0, drawW - firstW, drawH - firstH };
        SDL_Rect dst{ screenX + firstW, screenY + firstH, drawW - firstW, drawH - firstH };
        SDL_RenderCopy(renderer, tex, &src, &dst);
    }
}

void Campaign::ensureFowMask(int screenW, int screenH)
{
    (void)screenW;
    (void)screenH;
}

void Campaign::updateFogOfWar()
{
    const int mapW = m_map.width();
    const int mapH = m_map.height();

    if (mapW <= 0 || mapH <= 0)
        return;

    if ((int)m_fowVisited.size() != mapW * mapH)
        m_fowVisited.assign(mapW * mapH, 0);

    const int px = (int)(m_player.x / m_tileSize);
    const int py = (int)(m_player.y / m_tileSize);

    const int revealRadius = 13;

    for (int y = py - revealRadius; y <= py + revealRadius; ++y)
    {
        for (int x = px - revealRadius; x <= px + revealRadius; ++x)
        {
            if (x < 0 || y < 0 || x >= mapW || y >= mapH)
                continue;

            const int dx = x - px;
            const int dy = y - py;

            if (dx * dx + dy * dy <= revealRadius * revealRadius)
                m_fowVisited[y * mapW + x] = 1;
        }
    }
}

void Campaign::renderFogOfWar(int screenW, int screenH)
{
    const float darkness = computeSkyDarkness();

    const bool isNightFogMode = darkness > 0.45f;

    const Uint8 unexploredAlpha = isNightFogMode
        ? (Uint8)std::clamp((int)std::lround(210.0f - darkness * 20.0f), 170, 210)
        : (Uint8)std::clamp((int)std::lround(245.0f - darkness * 35.0f), 170, 245);

    const Uint8 visitedAlpha = isNightFogMode
        ? 0
        : (Uint8)std::clamp((int)std::lround(75.0f - darkness * 35.0f), 10, 75);

    if (!m_fowOverlay)
        return;

    const int mapW = m_map.width();
    const int mapH = m_map.height();

    if (mapW <= 0 || mapH <= 0)
        return;

    if ((int)m_fowVisited.size() != mapW * mapH)
        return;

    int texW = 0;
    int texH = 0;
    SDL_QueryTexture(m_fowOverlay, nullptr, nullptr, &texW, &texH);
    if (texW <= 0 || texH <= 0)
        return;

    const float px = m_player.x / (float)m_tileSize;
    const float py = m_player.y / (float)m_tileSize;

    const float innerRadius = isNightFogMode ? 9.5f : 7.0f;
    const float outerRadius = isNightFogMode ? 9.5f : 10.0f;

    // -------------------------------------------------
    // 1) Vzdálený textured fog po tilech
    // -------------------------------------------------
    const int firstTileX = std::max(0, m_camX / m_tileSize);
    const int firstTileY = std::max(0, m_camY / m_tileSize);
    const int lastTileX = std::min(mapW - 1, (m_camX + screenW) / m_tileSize + 1);
    const int lastTileY = std::min(mapH - 1, (m_camY + screenH) / m_tileSize + 1);

    for (int ty = firstTileY; ty <= lastTileY; ++ty)
    {
        for (int tx = firstTileX; tx <= lastTileX; ++tx)
        {
            const float tileCx = tx + 0.5f;
            const float tileCy = ty + 0.5f;

            const float dx = tileCx - px;
            const float dy = tileCy - py;
            const float dist = std::sqrt(dx * dx + dy * dy);

            if (dist < outerRadius)
                continue;

            bool visitedBefore = (m_fowVisited[ty * mapW + tx] != 0);

            if (visitedBefore)
            {
                const int neighbors = CountVisitedNeighbors(m_fowVisited, mapW, mapH, tx, ty);
                if (neighbors <= 2)
                    visitedBefore = false;
            }

            const Uint8 alpha = visitedBefore ? visitedAlpha : unexploredAlpha;

            const int block = m_tileSize / 4;

            for (int oy = 0; oy < m_tileSize; oy += block)
            {
                for (int ox = 0; ox < m_tileSize; ox += block)
                {
                    const int sx = tx * m_tileSize + ox - m_camX;
                    const int sy = ty * m_tileSize + oy - m_camY;

                    RenderWrappedFogBlock(
                        m_renderer,
                        m_fowOverlay,
                        texW,
                        texH,
                        tx * m_tileSize + ox,
                        ty * m_tileSize + oy,
                        sx,
                        sy,
                        block,
                        block,
                        alpha
                    );
                }
            }
        }
    }
    
    if (!isNightFogMode) {
        // -------------------------------------------------
        // 2) Jemný textured pøechodový prstenec
        // -------------------------------------------------
        const int step = 4; // 2 = jemnìjší, 4 = rychlejší

        const int ringPx = (int)std::ceil((outerRadius + 1.0f) * (float)m_tileSize);
        const int playerScreenX = (int)std::lround(m_player.x) - m_camX;
        const int playerScreenY = (int)std::lround(m_player.y) - m_camY;

        const int minSX = std::max(0, playerScreenX - ringPx);
        const int minSY = std::max(0, playerScreenY - ringPx);
        const int maxSX = std::min(screenW, playerScreenX + ringPx);
        const int maxSY = std::min(screenH, playerScreenY + ringPx);

        for (int sy = minSY; sy < maxSY; sy += step)
        {
            for (int sx = minSX; sx < maxSX; sx += step)
            {
                const int drawW = std::min(step, screenW - sx);
                const int drawH = std::min(step, screenH - sy);

                const float worldX = (float)(m_camX + sx + drawW / 2) / (float)m_tileSize;
                const float worldY = (float)(m_camY + sy + drawH / 2) / (float)m_tileSize;

                const float dx = worldX - px;
                const float dy = worldY - py;
                const float dist = std::sqrt(dx * dx + dy * dy);

                if (dist <= innerRadius || dist >= outerRadius)
                    continue;

                const int tx = (int)std::floor(worldX);
                const int ty = (int)std::floor(worldY);

                bool visitedBefore = false;
                if (tx >= 0 && ty >= 0 && tx < mapW && ty < mapH)
                {
                    visitedBefore = (m_fowVisited[ty * mapW + tx] != 0);

                    if (visitedBefore)
                    {
                        const int neighbors = CountVisitedNeighbors(m_fowVisited, mapW, mapH, tx, ty);
                        if (neighbors <= 2)
                            visitedBefore = false;
                    }
                }

                const float t = std::clamp(
                    (dist - innerRadius) / (outerRadius - innerRadius),
                    0.0f,
                    1.0f
                );

                const Uint8 alpha = visitedBefore
                    ? (Uint8)std::lround(t * (float)visitedAlpha)
                    : (Uint8)std::lround(t * (float)unexploredAlpha);

                if (alpha == 0)
                    continue;

                RenderWrappedFogBlock(
                    m_renderer,
                    m_fowOverlay,
                    texW,
                    texH,
                    m_camX + sx,
                    m_camY + sy,
                    sx,
                    sy,
                    drawW,
                    drawH,
                    alpha
                );
            }
        }

        SDL_SetTextureAlphaMod(m_fowOverlay, 255);

    }
}