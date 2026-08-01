#pragma once
#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gameobj
{
    struct RectI { int x = 0, y = 0, w = 0, h = 0; };
    struct PivotI { int x = 0, y = 0; };
    struct ColliderI { int x = 0, y = 0, w = 0, h = 0; bool enabled = true; };

    std::string Trim(const std::string& s);
    std::string ToLower(std::string s);

    struct RectF
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    struct ObjectDef
    {
        std::string id;
        std::string name;
        std::string image;

        bool enabled = true;
        bool solid = false;
        bool has_sprite = true;
        bool unique = false;

        RectI src;
        PivotI pivot;
        ColliderI collider;
        float scale = 1.0f;

        std::vector<std::string> tags_raw;
        std::vector<std::string> tags;

        std::vector<RectF> collisionRects;
        std::vector<RectF> fadeRects;
        std::vector<RectF> walkableRects;

        bool HasTag(const std::string& token) const;

        std::string sourceFile;
    };

    std::vector<SDL_Rect> GetWorldColliderRects(
        const ObjectDef& def,
        int wx,
        int wy,
        float scale = 1.0f);

    std::vector<SDL_Rect> GetWorldFadeRects(
        const ObjectDef& def,
        int wx,
        int wy,
        float scale = 1.0f);

    std::vector<SDL_Rect> GetWorldWalkableRects(
    const ObjectDef& def,
    int wx,
    int wy,
    float scale);

    class ObjectCatalog
    {
    public:
        const std::string& ImageName() const { return m_image; }
        const std::vector<ObjectDef>& Objects() const { return m_objects; }

        bool LoadFromFile(const std::string& path, std::string* outError = nullptr);
        bool LoadFromString(const std::string& text, std::string* outError = nullptr);
        bool AppendFromFile(const std::string& path, std::string* outError = nullptr);

        const ObjectDef* FindById(const std::string& id) const;
        const ObjectDef* FindByName(const std::string& name) const;
        const ObjectDef* findById(const std::string& id) const;

    private:
        std::string m_image;
        std::vector<ObjectDef> m_objects;
        std::unordered_map<std::string, size_t> m_byId;


        void RebuildIndex();
        static void TokenizeTags(const std::vector<std::string>& raw, std::vector<std::string>& outTokens);
    };

    inline void RenderObjectAtPivot(
        SDL_Renderer* renderer,
        SDL_Texture* atlas,
        const ObjectDef& obj,
        int worldX,
        int worldY,
        float scale = 1.0f)
    {
        if (!renderer || !atlas) return;
        if (!obj.enabled || !obj.has_sprite) return;
        if (obj.src.w <= 0 || obj.src.h <= 0) return;

        SDL_Rect src{ obj.src.x, obj.src.y, obj.src.w, obj.src.h };

        const int w = (int)std::lround(obj.src.w * scale);
        const int h = (int)std::lround(obj.src.h * scale);
        const int dx = (int)std::lround(obj.pivot.x * scale);
        const int dy = (int)std::lround(obj.pivot.y * scale);

        SDL_Rect dst{ worldX - dx, worldY - dy, w, h };
        SDL_RenderCopy(renderer, atlas, &src, &dst);
    }

    inline std::optional<SDL_Rect> GetWorldColliderRect(
        const ObjectDef& obj,
        int worldX,
        int worldY,
        float scale = 1.0f)
    {
        if (!obj.enabled || !obj.has_sprite) return std::nullopt;
        if (!obj.collider.enabled) return std::nullopt;

        const int baseX = (int)std::lround(worldX - obj.pivot.x * scale);
        const int baseY = (int)std::lround(worldY - obj.pivot.y * scale);

        SDL_Rect r;
        r.x = baseX + (int)std::lround(obj.collider.x * scale);
        r.y = baseY + (int)std::lround(obj.collider.y * scale);
        r.w = (int)std::lround(obj.collider.w * scale);
        r.h = (int)std::lround(obj.collider.h * scale);
        return r;
    }
}