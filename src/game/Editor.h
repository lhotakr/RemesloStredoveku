#pragma once
#include <SDL.h>
#include <unordered_map>

#include <random>
#include <string>
#include <vector>

#include "terrain/TerrainTileset.h"
#include "terrain/TerrainRenderer.h"
#include "terrain/TileMap.h"
#include "terrain/ObjectCatalog.h"
#include "CharacterManager.h"

struct NpcSpawn
{
    std::string id;
    std::string typeId;

    std::string name;
    std::string surname;
    std::string greeting;

    std::string scriptId;
    std::string characterId;

    int tileX = 0;
    int tileY = 0;

    int hp = 100;
    int mood = 50;
};

struct EditorNpcType
{
    std::string typeId;
    std::string name;
    std::string characterId;
    std::string defaultScriptId;
    int defaultHP = 100;
    int defaultMood = 50;
};

struct EditorDialogChoice
{
    std::string text;
    std::string next;
    std::string style;
    int npcMoodDelta = 0;
    std::string setFlag;

    std::string requireFlag;
    std::string forbidFlag;
    int requireMoodMin = 0;
    bool closeDialog = false;
    std::string setNpcScript;
    std::string setNpcGreeting;
};

struct EditorDialogNode
{
    std::string id;
    std::string speaker;
    std::string text;
    std::string requireFlag;
    std::string forbidFlag;
    std::vector<EditorDialogChoice> choices;
};

struct EditorDialogDef
{
    std::string dialogId;
    std::string startNode;
    std::vector<EditorDialogNode> nodes;
};

struct EditorNpcZone
{
    std::string id;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
};

struct EditorNpcPhaseEntry
{
    std::string phase;
    std::string zoneId;
};

struct EditorNpcSchedule
{
    std::string npcId;

    std::vector<EditorNpcPhaseEntry> spring;
    std::vector<EditorNpcPhaseEntry> summer;
    std::vector<EditorNpcPhaseEntry> autumn;
    std::vector<EditorNpcPhaseEntry> winter;

    std::vector<EditorNpcPhaseEntry> sunday;
    std::vector<EditorNpcPhaseEntry> feastDefault;
};


struct EditorForageArchetype
{
    std::string id;
    std::string category; // mushroom / herb

    std::string displayUnknown;
    std::string displayPartial;

    std::string genericMapSprite;
    std::string detailPlaceholderSprite;
    std::string herbariumPlaceholderSprite;

    float mapScale = 0.35f;
    float weight = 0.05f;
    float volume = 0.10f;
    int maxStack = 64;

    std::vector<std::string> examinationSlots;
};

struct EditorForageSpecies
{
    std::string id;
    std::string archetypeId;

    std::string trueName;
    std::vector<std::string> folkNames;

    std::string detailSprite;
    std::string inventorySprite;
    std::string herbariumSprite;

    int difficulty = 25;
    std::string description;

    std::unordered_map<std::string, std::vector<std::string>> traits;

    std::string edibility;       // edible / unknown_safe / poisonous
    std::string medicinalValue;  // none / wound_basic / tea / calming
    int toxicityLevel = 0;
    float weight = 0.0f;
    float volume = 0.0f;
    int maxStack = 64;

    std::vector<std::string> effectsOnEat;
    std::vector<std::string> effectsOnUse;
    std::vector<std::string> season;
};

struct EditorForageSpawn
{
    std::string id;
    int tileX = 0;
    int tileY = 0;

    std::string archetypeId;
    std::vector<std::string> speciesPool;

    std::string genericMapSpriteOverride;
    float mapScaleOverride = 0.0f; // 0 = inherit archetype mapScale

    std::vector<std::string> seasonMask;

    int respawnDays = 0;
    bool gatherOnce = false;
    int quantityMin = 1;
    int quantityMax = 1;
    int rarity = 50;

    bool requiresExamination = true;
};

struct EditorUndoState
{
    TileMap map{ 64, 64, 32 };
    std::vector<NpcSpawn> npcSpawns;
    std::vector<EditorNpcZone> npcZones;
    std::vector<EditorNpcSchedule> npcSchedules;
    std::vector<EditorForageSpawn> forageSpawns;
    int selectedNpcSpawnIndex = -1;
    int selectedNpcZoneIndex = -1;
    int selectedNpcScheduleIndex = -1;
    int selectedForageSpawnIndex = -1;
    std::string label;
};

struct EditorQuestDef
{
    std::string id;
    std::string title;
    std::string description;
    std::string startedFlag;
    std::string readyFlag;
    std::string doneFlag;
};

class Editor
{
public:
    bool init(SDL_Window* window, SDL_Renderer* renderer);
    void shutdown();

    void handleEvent(const SDL_Event& e);
    void update(float dt);
    void render();

private:
    void renderMainToolbar();
    void renderBrushPanel();
    void renderBrushPanelContents();
    void renderSelectedObjectPreview();
    void renderSelectedCompositePreview();
    int selectedBrushObjectIndex() const;
    void renderBrushGhost(const TerrainRenderer::View& view);
    void renderForageGhost(const TerrainRenderer::View& view);
    void renderNpcInspector();
    void renderNpcInspectorContents();
    void renderDialogEditor();
    void renderDialogEditorContents();
    void renderDialogPreviewWindow();
    void renderEditorWorkspace();
    void renderMapIoPopup();
    void renderQuestEditorContents();
    void renderNpcScheduleEditorContents();
    void renderNpcZoneEditorContents();
    void renderForageDefinitionEditorContents();
    void renderForageSpawnEditorContents();

    void toggleFullscreen();
    void newMap();
    void newMap(int width, int height);
    void rebuildCompositeGroups();
    void applySelectedCharacterToBrush();

    gameobj::ObjectCatalog m_objCatalog;
    std::unordered_map<std::string, SDL_Texture*> m_objAtlases;

    bool m_paintObjects = false;
    int  m_selectedObjIndex = 0;
	int  m_selectedCastleObj = 0;
    bool m_drawObjColliders = false;

    uint8_t  m_objBrushVar = 0;
    uint16_t m_objBrushHP = 100;
    bool     m_objRandomizeVar = false;
    bool     m_objGhost = true;
    char     m_objFilter[128] = "";
    bool     m_showOnlyTechObjects = false;

    char m_mapPathBuf[260] = "data/maps/test.rvm";
    char m_mapFileNameBuf[128] = "test.rvm";
    int  m_newMapWidth = 128;
    int  m_newMapHeight = 128;

    enum class MapIoPopupMode
    {
        None,
        NewMap,
        LoadMap,
        SaveAs
    };

    MapIoPopupMode m_mapIoPopupMode = MapIoPopupMode::None;
    std::string m_projectRoot;
    std::string m_mapsDir;
    std::string m_mapPath;
    std::string m_lastIoStatus;

    bool saveMap(const char* path);
    bool loadMap(const char* path);

    std::vector<NpcSpawn> m_npcSpawns;

    char m_npcId[64] = "farmer_01";
    char m_npcTypeId[64] = "farmer";
    char m_npcScriptId[64] = "farmer_intro";
    char m_npcCharacterId[64] = "Character_2_char_01";
    char m_npcName[64] = "";
    char m_npcSurname[64] = "";
    char m_npcGreeting[256] = "";

    int m_npcHP = 100;
    int m_npcMood = 50;

    bool saveNpcsForMap(const std::string& mapPath);
    bool loadNpcsForMap(const std::string& mapPath);

    NpcSpawn* findNpcAt(int tileX, int tileY);
    const NpcSpawn* findNpcAt(int tileX, int tileY) const;

    std::vector<EditorNpcType> m_npcTypes;
    int m_selectedNpcTypeIndex = 0;

    bool loadNpcTypes();
    void applySelectedNpcTypeToBrush();

    CharacterManager m_characterManager;
    int m_selectedCharacterIndex = 0;

    int m_selectedNpcSpawnIndex = -1;

    void loadNpcSpawnToBrush(const NpcSpawn& npc);
    void saveBrushToSelectedNpc();
    void deleteSelectedNpc();
    int findNpcSpawnIndexAt(int tileX, int tileY) const;

    std::vector<EditorDialogDef> m_dialogDefs;
    int m_selectedDialogIndex = -1;
    int m_selectedDialogNodeIndex = -1;

    bool loadDialogs();
    bool saveDialogs();
    int findDialogIndexById(const std::string& dialogId) const;
    bool dialogIdExists(const std::string& dialogId) const;

    char m_dialogId[64] = "";
    char m_dialogStartNode[64] = "";

    char m_nodeId[64] = "";
    char m_nodeSpeaker[64] = "";
    char m_nodeText[512] = "";
    char m_nodeRequireFlag[64] = "";
    char m_nodeForbidFlag[64] = "";

    char m_choiceRequireFlag[64] = "";
    char m_choiceForbidFlag[64] = "";
    int  m_choiceRequireMoodMin = 0;
    bool m_choiceCloseDialog = false;
    char m_choiceSetNpcScript[64] = "";
    char m_choiceSetNpcGreeting[256] = "";

    bool dialogNodeExists(const EditorDialogDef& dlg, const std::string& nodeId) const;
    int findDialogNodeIndexById(const EditorDialogDef& dlg, const std::string& nodeId) const;

    bool dialogIdDuplicate(const std::string& dialogId, int ignoreIndex = -1) const;
    bool nodeIdDuplicate(const EditorDialogDef& dlg, const std::string& nodeId, int ignoreIndex = -1) const;

    int m_previewDialogIndex = -1;
    std::string m_previewNodeId;

    std::vector<EditorNpcZone> m_npcZones;
    std::vector<EditorNpcSchedule> m_npcSchedules;

    int m_selectedNpcZoneIndex = -1;
    int m_selectedNpcScheduleIndex = -1;

    bool loadZonesForMap(const std::string& mapPath);
    bool saveZonesForMap(const std::string& mapPath);

    bool loadNpcSchedules();
    bool saveNpcSchedules();

    EditorNpcSchedule* findScheduleForNpc(const std::string& npcId);
    const EditorNpcSchedule* findScheduleForNpc(const std::string& npcId) const;

    void renderNpcZoneEditor();
    void renderNpcScheduleEditor();

    bool m_showNpcZonesOverlay = true;
    bool m_showOnlySelectedZoneOverlay = false;

    bool m_zonePickMode = false;
    bool m_zonePickHasStart = false;
    int  m_zonePickStartX = 0;
    int  m_zonePickStartY = 0;

    // Undo
    std::vector<EditorUndoState> m_undoStack;
    std::vector<EditorUndoState> m_redoStack;
    static constexpr int kMaxUndoStates = 64;

    bool m_undoGestureActive = false;

    EditorUndoState captureUndoState(const char* label) const;
    void restoreUndoState(EditorUndoState&& state);
    void pushUndoState(const char* label);
    bool canUndo() const;
    bool canRedo() const;
    void undoLastStep();
    void redoLastStep();

    enum class ZoneDragHandle
    {
        None,
        Move,
        MinXMinY,
        MaxXMinY,
        MinXMaxY,
        MaxXMaxY,
        MinX,
        MaxX,
        MinY,
        MaxY
    };

    bool m_zoneResizeActive = false;
    ZoneDragHandle m_zoneDragHandle = ZoneDragHandle::None;

    int m_zoneDragAnchorMouseTileX = 0;
    int m_zoneDragAnchorMouseTileY = 0;

    EditorNpcZone m_zoneDragOriginal;

    ZoneDragHandle hitTestZoneHandle(int tx, int ty) const;
    void beginZoneDrag(int tx, int ty, ZoneDragHandle handle);
    void updateZoneDrag(int tx, int ty);
    void endZoneDrag();

    enum class EditorTab
    {
        Map,
        NPC,
        Schedules,
        Dialogs,
        Quests,
        Foraging
    };

    EditorTab m_activeTab = EditorTab::Map;
    bool m_npcPlacementMode = false;
    bool m_foragePlacementMode = false;
    bool m_showBrushGhost = true;
    bool m_renderMapOutsideMapTab = true;
    float m_uiFontScale = 0.70f;

private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;

    bool m_running = true;
    bool m_fullscreen = false;

	float m_zoom = 1.0f;
    int m_tileSize = 32;
    float m_objBrushScale = 1.0f;
    int m_camX = 0, m_camY = 0;
    int m_mouseX = 0, m_mouseY = 0;
    bool m_lmbDown = false, m_rmbDown = false;

    uint16_t m_brushTile = 1;
    int m_brushSize = 1;
    bool m_randomizeVariants = true;

    bool m_compositeBrush = false;
    bool m_compositeGhost = true;
    int  m_selectedCompositeGroup = 0;

    bool m_mapDirty = false;

    std::mt19937 m_rng{ std::random_device{}() };

    TileMap m_map{ 64, 64, 32 };

    TerrainTileset m_tileset;
    TerrainRenderer m_terrainRenderer;
    std::vector<TerrainTileset::CompositeGroup> m_compositeGroups;

    bool loadObjectAtlases(std::string* outError = nullptr);
    void destroyObjectAtlases();
    SDL_Texture* textureForObject(const gameobj::ObjectDef& def) const;

    enum class BrushMode
    {
        Terrain,
        NatureObjects,
        TechObjects,
        Castles,
        Houses,
        Decoration,
        NPC,
        Forage
    };

    BrushMode m_brushMode = BrushMode::Terrain;

    enum class TerrainBrushMode
    {
        Simple,
        Composite
    };

    TerrainBrushMode m_terrainBrushMode = TerrainBrushMode::Simple;

    int m_selectedNatureObj = 0;
    int m_selectedTechObj = 0;
    int m_selectedHouseObj = 0;
    int m_selectedDecorationObj = 0;

    std::vector<EditorQuestDef> m_questDefs;
    int m_selectedQuestIndex = -1;

    // quests
    bool loadQuests();
    bool saveQuests();
    void renderQuestEditor();
    int findQuestIndexById(const std::string& questId) const;
    bool questIdDuplicate(const std::string& questId, int ignoreIndex = -1) const;

    char m_questId[64] = "";
    char m_questTitle[128] = "";
    char m_questDescription[512] = "";
    char m_questStartedFlag[64] = "";
    char m_questReadyFlag[64] = "";
    char m_questDoneFlag[64] = "";


    // foraging - herbs & fungi

    std::vector<EditorForageArchetype> m_forageArchetypes;
    std::vector<EditorForageSpecies> m_forageSpecies;
    std::vector<EditorForageSpawn> m_forageSpawns;

    int m_selectedForageArchetypeIndex = -1;
    int m_selectedForageSpeciesIndex = -1;
    int m_selectedForageSpawnIndex = -1;

    // Forage placement brush: when enabled, clicking the map places the selected Species
    // as a concrete spawn; otherwise it places the selected generic Archetype.
    bool m_placeSelectedForageSpecies = true;

    bool loadForageSprites();
    bool loadForageArchetypes();
    bool saveForageArchetypes();
    bool loadForageSpecies();
    bool saveForageSpecies();
    bool loadForageSpawnsForMap(const std::string& mapPath);
    bool saveForageSpawnsForMap(const std::string& mapPath);

    void renderForageDefinitionEditor();
    void renderForageSpawnEditor();

    void applySelectedForageBrushToSpawn(EditorForageSpawn& spawn) const;
    std::string selectedForageBrushLabel() const;
    std::string makeUniqueForageSpawnId() const;

    EditorForageSpawn* findForageSpawnAt(int tileX, int tileY);
    const EditorForageSpawn* findForageSpawnAt(int tileX, int tileY) const;
    int findForageSpawnIndexAt(int tileX, int tileY) const;
};