#include "EditorHelpers.h"

#include <SDL_image.h>

static std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static std::string FileNameOnlyLower(const std::string& path)
{
    std::string s = path;
    const size_t p = s.find_last_of("/\\");
    if (p != std::string::npos)
        s = s.substr(p + 1);

    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });

    return s;
}

static bool ObjectFromSourceFile(const gameobj::ObjectDef& o, const char* fileName)
{
    return FileNameOnlyLower(o.sourceFile) == FileNameOnlyLower(fileName);
}

static std::vector<int> BuildVisibleSortedObjectIndices(
    const gameobj::ObjectCatalog& cat,
    const std::string& filter,
    const char* sourceFile)
{
    std::vector<int> out;
    const auto& defs = cat.Objects();

    for (int i = 0; i < (int)defs.size(); ++i)
    {
        const auto& o = defs[i];
        if (!o.has_sprite) continue;
        if (!ObjectFromSourceFile(o, sourceFile)) continue;
        if (!ContainsNoCase(o.id + " " + o.name, filter)) continue;

        out.push_back(i);
    }

    std::sort(out.begin(), out.end(),
        [&](int a, int b)
        {
            const auto& A = defs[a];
            const auto& B = defs[b];

            std::string an = A.name.empty() ? A.id : A.name;
            std::string bn = B.name.empty() ? B.id : B.name;

            an = ToLower(an);
            bn = ToLower(bn);

            if (an != bn) return an < bn;
            return ToLower(A.id) < ToLower(B.id);
        });

    return out;
}

std::vector<int> BuildNatureObjectIndices(
    const gameobj::ObjectCatalog& cat,
    const char* filter)
{
    return BuildVisibleSortedObjectIndices(cat, filter ? filter : "", "TreeAndStoneSprites.json");
}

int FindNthTechObject(
    const gameobj::ObjectCatalog& cat,
    int nth,
    const char* filter)
{
    const std::string f = filter ? filter : "";
    const auto& defs = cat.Objects();
    int visibleIndex = 0;

    for (int i = 0; i < (int)defs.size(); ++i)
    {
        const auto& o = defs[i];
        if (!IsTechObject(o)) continue;
        if (!ContainsNoCase(o.id + " " + o.name, f)) continue;

        if (visibleIndex == nth)
            return i;

        ++visibleIndex;
    }

    return -1;
}

std::vector<int> BuildCastleObjectIndices(
    const gameobj::ObjectCatalog& cat,
    const char* filter)
{
    return BuildVisibleSortedObjectIndices(cat, filter ? filter : "", "CastleObjects.json");
}

std::vector<int> BuildDecorationObjectIndices(
    const gameobj::ObjectCatalog& cat,
    const char* filter)
{
    return BuildVisibleSortedObjectIndices(cat, filter ? filter : "", "Decoration.json");
}

std::vector<int> BuildHouseObjectIndices(
    const gameobj::ObjectCatalog& cat,
    const char* filter)
{
    return BuildVisibleSortedObjectIndices(cat, filter ? filter : "", "Houses.json");
}

ImVec2 WorldToScreen(float worldX, float worldY, int camX, int camY, float zoom)
{
    return ImVec2(
        (worldX - (float)camX) * zoom,
        (worldY - (float)camY) * zoom
    );
}

const char* ObjectCategoryLabel(const gameobj::ObjectDef& o)
{
    if (!o.has_sprite)
        return "tech";

    if (ObjectFromSourceFile(o, "TreeAndStoneSprites.json"))
        return "nature";
    if (ObjectFromSourceFile(o, "Houses.json"))
        return "house";
    if (ObjectFromSourceFile(o, "CastleObjects.json"))
        return "castle";
    if (ObjectFromSourceFile(o, "Decoration.json"))
        return "decoration";

    return "object";
}

SDL_Texture* loadTextureFile(SDL_Renderer* renderer, const std::string& path)
{
    SDL_Surface* surf = IMG_Load(path.c_str());
    if (!surf)
        return nullptr;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

uint32_t hash2d(int x, int y, uint32_t seed)
{
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= static_cast<uint32_t>(y) + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}

bool ContainsNoCase(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) return true;
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

bool IsTechObject(const gameobj::ObjectDef& o)
{
    return !o.has_sprite;
}

int CountVisibleTechObjects(const gameobj::ObjectCatalog& cat, const std::string& filter)
{
    const auto& defs = cat.Objects();
    int count = 0;
    for (const auto& o : defs) {
        if (!IsTechObject(o)) continue;
        if (!ContainsNoCase(o.id + " " + o.name, filter)) continue;
        ++count;
    }
    return count;
}