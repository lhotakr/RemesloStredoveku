#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <SDL.h>
#include "imgui.h"
#include "terrain/ObjectCatalog.h"

// Cross-platform strncpy_s wrapper
#ifdef _MSC_VER
    // MSVC has strncpy_s and _TRUNCATE
#else
    // Linux/GCC: define compatible versions
    // MSVC uses 3-parameter version: strncpy_s(dest, src, _TRUNCATE)
    // where dest is a fixed-size array and compiler deduces size
    #define _TRUNCATE ((size_t)-1)

    // Helper to get array size at compile time
    template<size_t N>
    inline int strncpy_s(char (&dest)[N], const char* src, size_t count) {
        if (count == _TRUNCATE) {
            count = N - 1;
        }
        size_t len = std::min(count, N - 1);
        std::strncpy(dest, src, len);
        dest[len] = '\0';
        return 0;
    }
#endif

bool ContainsNoCase(const std::string& haystack, const std::string& needle);
bool IsTechObject(const gameobj::ObjectDef& o);

std::vector<int> BuildNatureObjectIndices(const gameobj::ObjectCatalog& cat, const char* filter);
int FindNthTechObject(const gameobj::ObjectCatalog& cat, int nth, const char* filter);
std::vector<int> BuildCastleObjectIndices(const gameobj::ObjectCatalog& cat, const char* filter);
std::vector<int> BuildDecorationObjectIndices(const gameobj::ObjectCatalog& cat, const char* filter);
std::vector<int> BuildHouseObjectIndices(const gameobj::ObjectCatalog& cat, const char* filter);
int CountVisibleTechObjects(const gameobj::ObjectCatalog& cat, const std::string& filter);

ImVec2 WorldToScreen(float worldX, float worldY, int camX, int camY, float zoom);
const char* ObjectCategoryLabel(const gameobj::ObjectDef& o);

SDL_Texture* loadTextureFile(SDL_Renderer* renderer, const std::string& path);
uint32_t hash2d(int x, int y, uint32_t seed = 1337u);