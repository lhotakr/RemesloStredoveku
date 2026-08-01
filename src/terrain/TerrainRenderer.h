#pragma once
#include <SDL.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

class TileMap;
class TerrainTileset;

class TerrainRenderer
{
public:
    struct View
    {
        int camX = 0;
        int camY = 0;
        int tileSize = 32;
        int screenW = 0;
        int screenH = 0;
    };

    void renderTerrain(SDL_Renderer* renderer,
        const TerrainTileset& tileset,
        const TileMap& map,
        const View& view) const;

private:
    static std::string tileIdToSurface(uint16_t tileId);
    static uint16_t getTileSafe(const TileMap& map, int x, int y, uint16_t oobValue);

    void drawBase(SDL_Renderer* renderer,
        const TerrainTileset& tileset,
        const TileMap& map,
        const View& view) const;

    void drawOverrides(SDL_Renderer* renderer,
        const TerrainTileset& tileset,
        const TileMap& map,
        const View& view) const;

    void drawTransitions(SDL_Renderer* renderer,
        const TerrainTileset& tileset,
        const TileMap& map,
        const View& view) const;

    // pøechody kreslené NA MUD tile
    static void drawMudTo(
        SDL_Renderer* renderer,
        const TerrainTileset& tileset,
        const std::function<std::string(int, int)>& surfAt,
        int x, int y,
        const SDL_Rect& dst,
        const std::string& toSurface);

    // "teèky" kreslené NA WATER tile (water -> mud, mask 16/32/64/128)
    static void drawWaterInsideCorners(
        SDL_Renderer* renderer,
        const TerrainTileset& tileset,
        const std::function<std::string(int, int)>& surfAt,
        int x, int y,
        const SDL_Rect& dst);
};