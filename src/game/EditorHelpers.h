#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <SDL.h>
#include "imgui.h"
#include "terrain/ObjectCatalog.h"

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