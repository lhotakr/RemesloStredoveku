#include "TerrainRenderer.h"
#include "TileMap.h"
#include "TerrainTileset.h"

#include <string>

std::string TerrainRenderer::tileIdToSurface(uint16_t tileId)
{
    if (tileId == 2) return "water";
    if (tileId == 3) return "mud";
    return "grass";
}

uint16_t TerrainRenderer::getTileSafe(const TileMap& map, int x, int y, uint16_t oobValue)
{
    uint16_t t = map.get(x, y);
    if (t == 0) return oobValue;
    return t;
}

void TerrainRenderer::renderTerrain(
    SDL_Renderer* renderer,
    const TerrainTileset& tileset,
    const TileMap& map,
    const View& view) const
{
    drawBase(renderer, tileset, map, view);
    drawOverrides(renderer, tileset, map, view);
    drawTransitions(renderer, tileset, map, view);
}

void TerrainRenderer::drawBase(
    SDL_Renderer* renderer,
    const TerrainTileset& tileset,
    const TileMap& map,
    const View& view) const
{
    const int firstTileX = view.camX / view.tileSize;
    const int firstTileY = view.camY / view.tileSize;
    const int offsetX = -(view.camX % view.tileSize);
    const int offsetY = -(view.camY % view.tileSize);

    for (int y = 0; y <= view.screenH / view.tileSize + 1; ++y)
        for (int x = 0; x <= view.screenW / view.tileSize + 1; ++x)
        {
            const int mapX = firstTileX + x;
            const int mapY = firstTileY + y;

            const uint16_t tileId = getTileSafe(map, mapX, mapY, 1); // OOB = grass
            const uint8_t var = map.getVar(mapX, mapY);

            SDL_Rect dst{
                offsetX + x * view.tileSize,
                offsetY + y * view.tileSize,
                view.tileSize,
                view.tileSize
            };

            const std::string surf = tileIdToSurface(tileId);

            if (const auto* base = tileset.pickFill(surf, (int)var))
                SDL_RenderCopy(renderer, tileset.atlas(), &base->src, &dst);
        }
}

void TerrainRenderer::drawOverrides(
    SDL_Renderer* renderer,
    const TerrainTileset& tileset,
    const TileMap& map,
    const View& view) const
{
    const int firstTileX = view.camX / view.tileSize;
    const int firstTileY = view.camY / view.tileSize;
    const int offsetX = -(view.camX % view.tileSize);
    const int offsetY = -(view.camY % view.tileSize);

    for (int y = 0; y <= view.screenH / view.tileSize + 1; ++y)
        for (int x = 0; x <= view.screenW / view.tileSize + 1; ++x)
        {
            const int mapX = firstTileX + x;
            const int mapY = firstTileY + y;

            const uint16_t ov = map.getOverride(mapX, mapY);
            if (ov == 0) continue;

            const int defIdx = (int)ov - 1;
            const auto* d = tileset.tileByIndex(defIdx);
            if (!d) continue;

            SDL_Rect dst{
                offsetX + x * view.tileSize,
                offsetY + y * view.tileSize,
                view.tileSize,
                view.tileSize
            };

            SDL_RenderCopy(renderer, tileset.atlas(), &d->src, &dst);
        }
}

// --- mud -> (grass|water) : hrany + L rohy, kreslí se NA MUD tile
void TerrainRenderer::drawMudTo(
    SDL_Renderer* renderer,
    const TerrainTileset& tileset,
    const std::function<std::string(int, int)>& surfAt,
    int x, int y,
    const SDL_Rect& dst,
    const std::string& toSurface)
{
    if (surfAt(x, y) != "mud") return;

    const bool N = (surfAt(x, y - 1) == toSurface);
    const bool E = (surfAt(x + 1, y) == toSurface);
    const bool S = (surfAt(x, y + 1) == toSurface);
    const bool W = (surfAt(x - 1, y) == toSurface);

    auto drawTr = [&](int mask)
        {
            if (const auto* tr = tileset.findTransition("mud", toSurface, mask))
                SDL_RenderCopy(renderer, tileset.atlas(), &tr->src, &dst);
        };

    // hrany (pokud v JSON existují, použijí se)
    if (N) drawTr(1);
    if (E) drawTr(2);
    if (S) drawTr(4);
    if (W) drawTr(8);

    // L rohy (ty máš jistì pro mud->water)
    if (N && E) drawTr(16);
    if (S && E) drawTr(32);
    if (S && W) drawTr(64);
    if (N && W) drawTr(128);
}

// --- water inside corners: kreslí se NA WATER tile (water->mud, mask 16/32/64/128)
void TerrainRenderer::drawWaterInsideCorners(
    SDL_Renderer* renderer,
    const TerrainTileset& tileset,
    const std::function<std::string(int, int)>& surfAt,
    int x, int y,
    const SDL_Rect& dst)
{
    if (surfAt(x, y) != "water") return;

    const bool wN = (surfAt(x, y - 1) == "water");
    const bool wE = (surfAt(x + 1, y) == "water");
    const bool wS = (surfAt(x, y + 1) == "water");
    const bool wW = (surfAt(x - 1, y) == "water");

    // diagonála = mud => "vnitøní roh" vody
    const bool mNE = (surfAt(x + 1, y - 1) == "mud");
    const bool mSE = (surfAt(x + 1, y + 1) == "mud");
    const bool mSW = (surfAt(x - 1, y + 1) == "mud");
    const bool mNW = (surfAt(x - 1, y - 1) == "mud");

    auto drawTr = [&](int mask)
        {
            if (const auto* tr = tileset.findTransition("water", "mud", mask))
                SDL_RenderCopy(renderer, tileset.atlas(), &tr->src, &dst);
        };

    if (wN && wE && mNE) drawTr(16);
    if (wS && wE && mSE) drawTr(32);
    if (wS && wW && mSW) drawTr(64);
    if (wN && wW && mNW) drawTr(128);
}

// ========= water dots in mud corners
// kreslí se NA MUD tile, sprite je water->mud (mask 16/32/64/128)
static void drawWaterDotsInMudCorners(
    SDL_Renderer* renderer,
    const TerrainTileset& tileset,
    const std::function<std::string(int, int)>& surfAt,
    int x, int y,
    const SDL_Rect& dst)
{
    if (surfAt(x, y) != "mud") return;

    const bool mN = (surfAt(x, y - 1) == "mud");
    const bool mE = (surfAt(x + 1, y) == "mud");
    const bool mS = (surfAt(x, y + 1) == "mud");
    const bool mW = (surfAt(x - 1, y) == "mud");

    const bool wNE = (surfAt(x + 1, y - 1) == "water");
    const bool wSE = (surfAt(x + 1, y + 1) == "water");
    const bool wSW = (surfAt(x - 1, y + 1) == "water");
    const bool wNW = (surfAt(x - 1, y - 1) == "water");

    auto drawTr = [&](int mask)
        {
            if (const auto* tr = tileset.findTransition("water", "mud", mask))
                SDL_RenderCopy(renderer, tileset.atlas(), &tr->src, &dst);
        };

    // NE=16, SE=32, SW=64, NW=128 (jak píšeš v JSON)
    if (mN && mE && wNE) drawTr(16);
    if (mS && mE && wSE) drawTr(32);
    if (mS && mW && wSW) drawTr(64);
    if (mN && mW && wNW) drawTr(128);
}

void TerrainRenderer::drawTransitions(
    SDL_Renderer* renderer,
    const TerrainTileset& tileset,
    const TileMap& map,
    const View& view) const
{
    const int firstTileX = view.camX / view.tileSize;
    const int firstTileY = view.camY / view.tileSize;
    const int offsetX = -(view.camX % view.tileSize);
    const int offsetY = -(view.camY % view.tileSize);

    auto getTileSafeLocal = [&](int x, int y) -> uint16_t {
        // OOB ber jako grass (pozadí)
        return getTileSafe(map, x, y, 1);
        };

    auto surfAt = [&](int x, int y) -> std::string {
        return tileIdToSurface(getTileSafeLocal(x, y));
        };

    auto drawTr = [&](const std::string& from, const std::string& to, int mask, const SDL_Rect& dst)
        {
            if (const auto* tr = tileset.findTransition(from, to, mask))
                SDL_RenderCopy(renderer, tileset.atlas(), &tr->src, &dst);
        };

    auto drawGrassDotsInMud = [&](int x, int y, const SDL_Rect& dst)
        {
            // kreslíme na MUD tile sprite grass->mud (konkávní rohy)
            if (surfAt(x, y) != "mud") return;

            const bool mN = (surfAt(x, y - 1) == "mud");
            const bool mE = (surfAt(x + 1, y) == "mud");
            const bool mS = (surfAt(x, y + 1) == "mud");
            const bool mW = (surfAt(x - 1, y) == "mud");

            const bool gNE = (surfAt(x + 1, y - 1) == "grass");
            const bool gSE = (surfAt(x + 1, y + 1) == "grass");
            const bool gSW = (surfAt(x - 1, y + 1) == "grass");
            const bool gNW = (surfAt(x - 1, y - 1) == "grass");

            if (mN && mE && gNE) drawTr("grass", "mud", 16, dst);   // NE
            if (mS && mE && gSE) drawTr("grass", "mud", 32, dst);   // SE
            if (mS && mW && gSW) drawTr("grass", "mud", 64, dst);   // SW
            if (mN && mW && gNW) drawTr("grass", "mud", 128, dst);  // NW
        };

    auto drawMudGrassBorder = [&](int x, int y, const SDL_Rect& dst)
        {
            if (surfAt(x, y) != "mud") return;

            const bool gN = (surfAt(x, y - 1) == "grass");
            const bool gE = (surfAt(x + 1, y) == "grass");
            const bool gS = (surfAt(x, y + 1) == "grass");
            const bool gW = (surfAt(x - 1, y) == "grass");

            // hrany (mud -> grass)
            if (gN) drawTr("mud", "grass", 1, dst);
            if (gE) drawTr("mud", "grass", 2, dst);
            if (gS) drawTr("mud", "grass", 4, dst);
            if (gW) drawTr("mud", "grass", 8, dst);

            // L rohy (mud -> grass) NE=16, SE=32, SW=64, NW=128
            if (gN && gE) drawTr("mud", "grass", 16, dst);
            if (gS && gE) drawTr("mud", "grass", 32, dst);
            if (gS && gW) drawTr("mud", "grass", 64, dst);
            if (gN && gW) drawTr("mud", "grass", 128, dst);
        };

    auto drawMudWaterShore = [&](int x, int y, const SDL_Rect& dst)
        {
            if (surfAt(x, y) != "mud") return;

            const bool wN = (surfAt(x, y - 1) == "water");
            const bool wE = (surfAt(x + 1, y) == "water");
            const bool wS = (surfAt(x, y + 1) == "water");
            const bool wW = (surfAt(x - 1, y) == "water");

            // hrany (mud -> water)
            if (wN) drawTr("mud", "water", 1, dst);
            if (wE) drawTr("mud", "water", 2, dst);
            if (wS) drawTr("mud", "water", 4, dst);
            if (wW) drawTr("mud", "water", 8, dst);

            // L rohy (mud -> water) NE=16, SE=32, SW=64, NW=128
            if (wN && wE) drawTr("mud", "water", 16, dst);
            if (wS && wE) drawTr("mud", "water", 32, dst);
            if (wS && wW) drawTr("mud", "water", 64, dst);
            if (wN && wW) drawTr("mud", "water", 128, dst);
        };

    auto drawWaterDotsInMud = [&](int x, int y, const SDL_Rect& dst)
        {
            // "teèky" = konkávní roh vody v mud oblasti
            // kreslíme na MUD tile sprite water->mud
            if (surfAt(x, y) != "mud") return;

            // ortogonály musí být MUD (tj. žádná voda ani tráva)
            const bool mN = (surfAt(x, y - 1) == "mud");
            const bool mE = (surfAt(x + 1, y) == "mud");
            const bool mS = (surfAt(x, y + 1) == "mud");
            const bool mW = (surfAt(x - 1, y) == "mud");

            // diagonály musí být WATER
            const bool wNE = (surfAt(x + 1, y - 1) == "water");
            const bool wSE = (surfAt(x + 1, y + 1) == "water");
            const bool wSW = (surfAt(x - 1, y + 1) == "water");
            const bool wNW = (surfAt(x - 1, y - 1) == "water");

            // dùležité: teèka se kreslí jen když je to opravdu "vnitøní roh":
            // tedy N i E jsou mud a diagonála NE je water atd.
            if (mN && mE && wNE) drawTr("water", "mud", 16, dst);   // NE
            if (mS && mE && wSE) drawTr("water", "mud", 32, dst);   // SE
            if (mS && mW && wSW) drawTr("water", "mud", 64, dst);   // SW
            if (mN && mW && wNW) drawTr("water", "mud", 128, dst);  // NW
        };

    // --- loop visible tiles ---
    for (int y = 0; y <= view.screenH / view.tileSize + 1; ++y)
        for (int x = 0; x <= view.screenW / view.tileSize + 1; ++x)
        {
            const int mapX = firstTileX + x;
            const int mapY = firstTileY + y;

            SDL_Rect dst{
                offsetX + x * view.tileSize,
                offsetY + y * view.tileSize,
                view.tileSize,
                view.tileSize
            };

            // 1) grass <-> mud (kreslí mud)
            drawMudGrassBorder(mapX, mapY, dst);

            // 1,5) grasa <-> mud rohy
            drawGrassDotsInMud(mapX, mapY, dst);

            // 2) mud <-> water (kreslí mud)
            drawMudWaterShore(mapX, mapY, dst);

            // 3) vnitøní rohy vody (water->mud teèky) – až po shore
            drawWaterDotsInMud(mapX, mapY, dst);
        }
}