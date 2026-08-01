#pragma once
#include <SDL.h>
#include <string>
#include <unordered_map>
#include <vector>

class TerrainTileset
{
public:

    struct CollisionRectNorm
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    struct TerrainTileDef
    {
        std::string uid;
        int atlas_x = 0;
        int atlas_y = 0;

        std::string surface;
        std::string feature = "normal";
        int variant = 0;
        std::string layer = "base";
        int mask = 0;
        std::string transition_to;

        bool hasComposite = false;
        std::string composite_id;
        int composite_x = 0;
        int composite_y = 0;

        SDL_Rect src{};

        bool hasCollision = false;
        std::vector<CollisionRectNorm> collisionRects;
    };

    struct CompositeGroup
    {
        std::string id;
        int minx = 0, miny = 0;
        int maxx = 0, maxy = 0;
        std::vector<int> defIndices; // indexy do m_tilesAll
    };

public:
    TerrainTileset() = default;
    ~TerrainTileset() { destroyAtlas(); }

    bool loadFromJson(SDL_Renderer* renderer,
        const std::string& jsonPath,
        const std::string& atlasPath);

    void destroyAtlas();

    SDL_Texture* atlas() const { return m_atlasTex; }
    const TerrainTileDef* tileByIndex(int defIdx) const;

    // base fill
    const TerrainTileDef* pickFill(const std::string& surface, int varIndex) const;
    int fillCount(const std::string& surface) const;

    // transitions
    const TerrainTileDef* findTransition(const std::string& from,
        const std::string& to,
        int mask) const;

    // composite groups
    std::vector<CompositeGroup> buildCompositeGroups() const;

    // pøístup k definicím (když chceš v Game zobrazovat preview)
    const std::vector<TerrainTileDef>& tiles() const { return m_tilesAll; }

private:
    struct TrKey
    {
        std::string from;
        std::string to;
        int mask = 0;

        bool operator==(const TrKey& o) const {
            return mask == o.mask && from == o.from && to == o.to;
        }
    };

    struct TrKeyHash
    {
        size_t operator()(const TrKey& k) const noexcept {
            std::hash<std::string> hs;
            std::hash<int> hi;
            size_t h = hs(k.from);
            h ^= hs(k.to) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= hi(k.mask) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };

private:
    void rebuildLookups();

private:
    SDL_Texture* m_atlasTex = nullptr;
    int m_tileSize = 32;

    std::vector<TerrainTileDef> m_tilesAll;

    // fill listy pro "base"
    std::unordered_map<std::string, std::vector<int>> m_fillIndicesBySurface;

    // transition LUT
    std::unordered_map<TrKey, int, TrKeyHash> m_transitionLut;

};