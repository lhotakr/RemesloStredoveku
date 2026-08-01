#pragma once

#include <SDL.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "imgui.h"

#include "terrain/ObjectCatalog.h"

struct ObjectEditorEntry
{
    gameobj::ObjectDef def;
    std::string sourceFile;
};

class ObjectEditor
{
public:
    bool init(SDL_Window* window, SDL_Renderer* renderer);
    void shutdown();

    void handleEvent(const SDL_Event& e);
    void update(float dt);
    void render();

private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;

    std::vector<ObjectEditorEntry> m_entries;
    int m_selectedIndex = -1;

    std::unordered_map<std::string, SDL_Texture*> m_textures;
    std::unordered_map<std::string, std::string> m_defaultImageBySource;

    std::string m_lastStatus;
    char m_filter[128] = "";

    char m_idBuf[128] = "";
    char m_nameBuf[128] = "";
    char m_imageBuf[260] = "";
    char m_tagsBuf[512] = "";

    bool m_enabledBuf = true;
    bool m_solidBuf = false;
    bool m_hasSpriteBuf = true;
    bool m_uniqueBuf = false;

    int m_srcX = 0;
    int m_srcY = 0;
    int m_srcW = 0;
    int m_srcH = 0;

    int m_pivotX = 0;
    int m_pivotY = 0;

    bool m_colliderEnabled = true;
    int m_colX = 0;
    int m_colY = 0;
    int m_colW = 0;
    int m_colH = 0;

    float m_scale = 1.0f;

    float m_atlasZoom = 1.0f;
    float m_spritePreviewZoom = 1.0f;
    int m_spriteEditMode = 0; // 0=legacy collider drag rectangle, 1=pivot click/drag
    int m_zoneEditLayer = 0;   // 0=collision_rects, 1=walkable_rects, 2=fade_rects
    int m_selectedZoneRectIndex = -1;
    bool m_draggingSrcRect = false;
    bool m_draggingColliderRect = false;
    bool m_draggingZoneRect = false;
    bool m_draggingPivot = false;
    ImVec2 m_dragStartLocal = ImVec2(0.0f, 0.0f);
    ImVec2 m_dragEndLocal = ImVec2(0.0f, 0.0f);

private:
    bool loadCatalog();
    bool saveAll();
    bool saveSourceFile(const std::string& sourceFile);

    void loadSelectedToBuffers();
    void applyBuffersToSelected();

    void renderObjectList();
    void renderObjectDetails();
    void renderObjectPreview(const gameobj::ObjectDef& def);
    void renderAtlasSpriteSelector(const gameobj::ObjectDef& def);
    void renderSpriteColliderEditor(const gameobj::ObjectDef& def);
    void renderSpriteZonesEditor(gameobj::ObjectDef& def);

    bool isDuplicateId(const std::string& id, int ignoreIndex = -1) const;
    int duplicateCountForId(const std::string& id) const;
    std::string makeUniqueIdForEntry(int index, const std::string& baseId) const;
    void fixDuplicateIdsGlobally();

    SDL_Texture* textureForImage(const std::string& image);
    void destroyTextures();

    std::vector<std::string> objectJsonFiles() const;
    std::string forageSpritesJsonFile() const;
    bool isForageSpriteSource(const std::string& sourceFile) const;
    void loadForageSpritesIntoEntries();
    bool saveForageSpritesFile(const std::string& sourceFile);
    std::string resolveImagePath(const std::string& image) const;

    static std::vector<std::string> SplitTags(const std::string& text);
    static std::string JoinTags(const std::vector<std::string>& tags);
};
