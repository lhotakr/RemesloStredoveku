#pragma once
#include <SDL.h>
#include <string>
#include <unordered_map>
#include <array>

#include "../terrain/TileMap.h"
#include "../terrain/TerrainRenderer.h"
#include "../terrain/TerrainTileset.h"
#include "../terrain/ObjectCatalog.h"
#include "../ImGuiUtils.h"
#include "DialogManager.h"
#include "Player.h"
#include "game/PlayerStats.h"
#include "CharacterManager.h"
#include "NpcManager.h"
#include "GameTime.h"
#include "LiturgicalCalendar.h"
#include "SunCycle.h"
#include "MoonCycle.h"
#include <unordered_set>
#include "../audio/AudioManager.h"
#include "../PlayerInventory.h"
#include "../ItemDef.h"
#include "../ItemTypes.h"
#include "WeatherSystem.h"
#include "../NpcDefinition.h"
#include <terrain/ObjectCatalog.h>

#include "ForageDatabase.h"
#include "ForageSystem.h"

#include <nlohmann/json.hpp>

enum class DayPhase
{
    Dawn,
    Morning,
    Forenoon,
    Noon,
    Afternoon,
    LateDay,
    Evening,
    Night
};

enum class Season
{
    Spring,
    Summer,
    Autumn,
    Winter
};

class Campaign
{
public:
    bool init(SDL_Window* window, SDL_Renderer* renderer);
    void shutdown();
    bool loadMapLinksForCurrentMap();

    void handleEvent(const SDL_Event& e);
	void update(float dt);
    bool isPlayerNearQuestObject(const std::string& objectId, int radiusPx) const;
    void render();
    bool handleSharedUiEvent(const SDL_Event& e);
    void updateSharedRuntime(float dt, bool playerMoving = false, bool playerRunning = false);
    void renderSharedHudOverlay();

    bool loadMap(const std::string& path, const std::string& spawnId);
    bool saveMap(const std::string& path);
    bool consumePendingInteriorTransition(std::string& outInteriorId, std::string& outSpawnId);
    bool sharedUiBlocksMouseLook() const;
    const std::string& currentMapPath() const { return m_currentMapPath; }
    float playerX() const;
    float playerY() const;
    void setPlayerPosition(float x, float y);
    const PlayerStats& playerStats() const { return m_player.stats; }
    PlayerStats& playerStats() { return m_player.stats; }
    void setGameDateTime(int day, int month, int year, int hour, int minute);
    nlohmann::json saveRuntimeState() const;
    void loadRuntimeState(const nlohmann::json& state);

	NpcDefinitionRegistry m_npcDefinitions;

	struct MapLinkDef
	{
		int id = 0;
		std::string targetMap;
		std::string targetLocation;
		std::string targetSpawnId;
	};

	std::vector<MapLinkDef> m_currentMapLinks;
	std::string m_currentMapPath;

private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;

    // world/camera
    int m_camX = 0, m_camY = 0;
    int m_tileSize = 32;

    // map + render
    TileMap m_map = TileMap(64, 64, 32);
    TerrainTileset m_tileset;
    TerrainRenderer m_terrainRenderer;

    // objects
    gameobj::ObjectCatalog m_objCatalog;
	std::unordered_map<std::string, SDL_Texture*> m_objAtlases; // atlas pro ka�d� surface (pro optimalizaci renderu, aby se nemuselo bindovat X r�zn�ch atlas� pro r�zn� objekty na stejn� tile)
    bool m_drawObjColliders = false;

    bool loadObjectAtlases(std::string* outError = nullptr);
    void destroyObjectAtlases();
    SDL_Texture* textureForObject(const gameobj::ObjectDef& def) const;

    // player
    Player m_player;
    CharacterManager m_characterManager;

    // hud + Player needs
    void updatePlayerNeeds(int elapsedGameMinutes);
    void updatePlayerNeeds(int elapsedGameMinutes, bool moving, bool running);
    void renderDebugHud();
	void renderHud();
	bool m_showDebugHud = false;

    // NPCs
    NpcManager m_npcManager;

    // input
    bool m_running = true;

    // console
    bool m_consoleOpen = false;
    char m_consoleInput[256] = "";
    std::vector<std::string> m_consoleLog;
    bool m_godMode = false;

    void renderConsole();
    void executeConsoleCommand(const std::string& cmd);
    void consoleLog(const std::string& text);

    // Debug time skip: unlike "set time", this advances needs/poisoning systems too.
    void debugAdvanceTimeMinutes(int totalMinutes);
    void debugAdvanceClockOnly(int minutes);
    int m_debugSkipHours = 8;

    std::vector<std::string> m_consoleHistory;
    int m_consoleHistoryIndex = -1; // -1 = not browsing, 0.N-1 = browsing history
    bool m_consoleScrollToBottom = false;
    bool m_consoleFocusInput = false;

    static int ConsoleInputCallback(ImGuiInputTextCallbackData* data);

    bool shouldBlockLetterHotkeys() const;

	std::string m_consoleAutocompleteBase;
	std::vector<std::string> m_consoleAutocompleteMatches;
	int m_consoleAutocompleteIndex = -1;

	std::vector<std::string> buildConsoleCommandList() const;
	std::vector<std::string> findConsoleMatches(const std::string& prefix) const;
	void resetConsoleAutocomplete();
	int handleConsoleAutocomplete(ImGuiInputTextCallbackData* data);
	int hudStatusColumnIndex(float value01) const;
	int hudStatusFrameAt(int row, int col) const;
    float hudHeat01() const;
    float hudCold01() const;
    bool getHudDayBlendFrames(int& outA, int& outB, float& outT) const;


    // time&date management
    GameTime m_gameTime;
    LiturgicalCalendar m_liturgicalCalendar;
    SunCycle m_sunCycle;

    // moon cycle (for moon phase and brightness, which can affect sky darkness at night)
    MoonCycle m_moonCycle;
    SDL_Texture* m_skyOverlay = nullptr; // rendered on top of everything, tinted based on time of day (e.g. dark at night, reddish during sunrise/sunset)
    float m_skyOverlayScrollX = 0.0f; // for parallax effect
    float m_skyOverlayScrollY = 0.0f;
    float m_cloudTime = 0.0f; // for animating clouds in the sky overlay
    float m_cloudBaseSpeedX = 5.0f; // pixels per second
    float m_cloudBaseSpeedY = 1.5f; // pixels per second
    const char* seasonToString(Season season) const;

    DayPhase getDynamicDayPhase() const;
    Season getSeason() const;
    const char* dayPhaseToString(DayPhase phase) const;

	float computeSkyDarkness() const; // 0 = fully bright, 1 = fully dark
	void updateSkyOverlay(float dt);
	void renderSkyOverlay();
	void renderSharedTimeOverlay();

	// lighting
	SDL_Texture* m_lightMask = nullptr; // for dynamic lighting (e.g. torch light around player)
	SDL_Texture* m_lightSoftTex = nullptr; // for soft circular light (used as a mask when rendering the light mask)
    
	void ensureLightMask(int scrennW, int screenH);
	void renderLightMask(int screenW, int screenH, float darkness);
	void drawLightOnMask(int screenX, int screenY, float radiusPx, Uint8 intensity);

	// fog of war (black mask that hides unexplored areas of the map; revealed areas are "cut out" from the mask, and explored but currently out-of-sight areas are darkened but still visible)
	// WarCraft II style :)
	SDL_Texture* m_fowOverlay = nullptr; // rendered on top of everything, with "holes" cut out for currently visible area, and darkened for explored but currently not visible area
    SDL_Texture* m_fowMask = nullptr;

    std::vector<uint8_t> m_fowVisited;

    void ensureFowMask(int screenW, int screenH);
    void updateFogOfWar();
    void renderFogOfWar(int screenW, int screenH);

	// NPC interaction
    int m_nearNpcIndex = -1;
    bool m_interactPressedLastFrame = false;
	bool m_npcDialogOpen = false;
	int m_dialogNpcIndex = -1;

    void resolvePlayerVsNpcs();
    void updateNearbyNpc();
    void tryInteractWithNpc();
    void tryUseMapLinkUnderPlayer();
    void renderNpcDialog();
    std::string m_pendingInteriorTransitionId;
    std::string m_pendingInteriorTransitionSpawnId;

	// dialogs
    DialogManager m_dialogManager;

    const DialogDefinition* m_activeDialog = nullptr;
    std::string m_activeDialogNodeId;

    std::unordered_set<std::string> m_storyFlags;

    void applyDialogChoiceEffects(const DialogChoice& choice);
    std::string formatChoiceLabel(const DialogChoice& choice) const;
	bool isDialogNodeAvailable(const DialogNode& node) const;
	bool isDialogChoiceAvailable(const DialogChoice& choice) const;

private:
    void ensureCameraOnPlayer(int screenW, int screenH);

    void clampPlayerToMap();
    bool spawnPlayerFromMap();
    bool spawnPlayerFromMapAt(const std::string& spawnId);

    // collisions
	bool wouldPlayerCollideWithObjectsAt(float testX, float testY) const;
	bool wouldPlayerCollideWithTerrainAt(float testX, float testY) const;
	bool wouldPlayerCollideWithNPCsAt(float testX, float testY) const;
    void resolvePlayerVsObjects();

    AudioManager m_audioManager;
	
    // items
    std::unordered_map<std::string, ItemDef> m_itemDefs;
    bool loadItemDefs(const std::string& path, std::string* outError = nullptr);

    // inventory ui
    bool m_inventoryOpen = false;
	bool m_inventoryFocus = false;
	SDL_Texture* m_defaultItemIcon = nullptr;

	// pro jednoduchost jen jedna "drzena" item stack promenna pro celej inventar, ktery se zobrazuje jako "drzeny" pri drag&dropu mezi sloty (kdyz se klikne na slot s itemem, tak se ten item "vezme do ruky" a zobrazi se jako drzeny, a source ukazuje na slot, ze ktereho se item vzal, ktery se pri dropu aktualizuje)
    struct InventoryDragState
    {
        bool active = false;
        ItemStack stack;
        ItemStack* source = nullptr;
    };

	// proste struct pro uchovani stavu pri drag&dropu itemu mezi sloty (kdyz je aktivni, tak se stack zobrazuje jako "drzeny" a source ukazuje na slot, ze ktereho se item vzal, ktery se pri dropu aktualizuje)
    InventoryDragState m_dragItem;
    void drawLabeledSlot(const char* label, const char* id, ItemStack& slotRef, float slotSize);
    bool drawItemSlot(
        const char* id,
        ItemStack& slot,
        const std::unordered_map<std::string, ItemDef>& defs,
        float size = 40.0f);

    void renderInventoryUI();
    void renderQuickAccessBar();
    void renderPlayerOverviewUI();
    bool m_playerOverviewOpen = false;
    bool m_playerOverviewFocus = false;

    bool canPlaceItemIntoSlot(
        const ItemStack& movingStack,
        const ItemStack& targetSlot,
        const std::string& slotId,
        const std::unordered_map<std::string, ItemDef>& defs) const;

    bool isEquipmentSlotId(const std::string& slotId) const;

    bool drawContainerSlot(
        const char* id,
        ContainerInventory& container,
        int index,
        const std::unordered_map<std::string, ItemDef>& defs,
        float size);

    std::unordered_map<std::string, SDL_Texture*> m_itemIconCache;

    SDL_Texture* getItemIconTexture(const ItemDef& def);
    void unloadItemIcons();

    void renderDraggedItemIcon();

    bool isInspectHeld() const;
    void tryInspectWorld();

    void applyNpcDefinitionsToInstances();

    void updateNearNpc();

    SDL_Cursor* m_defaultCursor = nullptr;
    SDL_Cursor* m_inspectCursor = nullptr;
    void loadInspectCursor();
    void updateInspectCursor();

	// weather
    WeatherSystem m_weatherSystem;
    WeatherDayProfile m_todayWeather;
    WeatherRuntimeState m_runtimeWeather;
    
    int m_cachedWeatherDay = -1;
    int m_cachedWeatherMonth = -1;
    int m_cachedWeatherYear = -1;

	void updateWeather();

    void applyWeatherDebugOverride();

    struct WeatherDebugOverride
    {
        bool enabled = false;

        bool overrideRain = false;
        bool forcedRain = false;
        float forcedRainIntensity = 0.0f; // 0..1

        bool overrideFog = false;
        bool forcedFog = false;

        bool overrideTemp = false;
        float forcedTemp = 0.0f;

        bool overrideWind = false;
        float forcedWind = 0.0f;

        bool overrideCloudiness = false;
        float forcedCloudiness = 0.0f; // 0..100

        bool overrideGroundWetness = false;
        float forcedGroundWetness = 0.0f; // 0..100
    };

    WeatherDebugOverride m_weatherDebug;

	struct QuestDef
	{
		std::string id;
		std::string title;
		std::string description;

		std::string startedFlag;
		std::string readyFlag;
		std::string doneFlag;
	};

	std::vector<QuestDef> m_questDefs;
	bool m_questJournalOpen = false;

	void renderQuestJournal();
	bool hasStoryFlag(const std::string& flag) const;
	void setStoryFlag(const std::string& flag);
	bool isQuestActive(const QuestDef& q) const;
	bool isQuestReadyToTurnIn(const QuestDef& q) const;
	bool isQuestDone(const QuestDef& q) const;
	bool loadQuestDefs(const std::string& path, std::string* outError = nullptr);
	bool saveQuestDefs(const std::string& path) const;
	bool m_questJournalFocus = false;

	bool isPlayerNearObjectTag(const std::string& tag, int radiusPx = 28) const;
	void tryUseQuestTrigger();
	void applyQuestRewardEffects(const std::string& flag);

	struct HudAssets
	{
		SDL_Texture* moonSheet = nullptr;
		SDL_Texture* dayAnimSheet = nullptr;
		SDL_Texture* statusSheet = nullptr;

		std::vector<SDL_Rect> moonFrames;
		std::vector<SDL_Rect> dayFrames;
		std::vector<std::vector<SDL_Rect>> statusFrames; // [status][level]
	};

	struct HudAtlas
	{
		SDL_Texture* texture = nullptr;
		std::vector<SDL_Rect> frames;
		int imageW = 0;
		int imageH = 0;

		int cols = 1;
		int rows = 1;
	};

	// HUD atlas loader

	HudAtlas m_hudMoon;
	HudAtlas m_hudDay;
	HudAtlas m_hudStatus;

	float m_dayHudAnimTime = 0.0f;

	bool loadHudAtlas(const std::string& imagePath, const std::string& jsonPath, HudAtlas& outAtlas);
	void destroyHudAtlas(HudAtlas& atlas);
	bool loadHudAssets();
	void destroyHudAssets();

	int hudMoonFrameIndex() const;
	int hudDayFrameIndex() const;
	int hudStatusFrameIndex(float value01, bool invert = false) const;
	int hudDayRowIndex() const;
	int hudDayAnimatedFrameIndex() const;
	void drawHudFrame(const HudAtlas& atlas, int frameIndex, int x, int y, float scale = 1.0f) const;

	class HudRenderer
	{
	public:
		bool load(SDL_Renderer* renderer);
		void render(SDL_Renderer* renderer, const Campaign& campaign, const Player& player);

	private:
		HudAssets m_assets;
	};
	void drawHudFrameAlpha(const HudAtlas& atlas, int frameIndex, int x, int y, float scale, Uint8 alpha) const;

	enum class MoonPhase
	{
		NewMoon = 0,
		WaxingCrescent = 1,
		FirstQuarter = 2,
		WaxingGibbous = 3,
		FullMoon = 4,
		WaningGibbous = 5,
		LastQuarter = 6,
		WaningCrescent = 7
	};

	// foraging :)
	ForageDatabase m_forageDb;
	ForageSystem m_forageSystem;
	PlayerForageKnowledgeState m_forageKnowledge;

	struct ForageSpriteRuntimeDef
	{
		std::string id;
		std::string image;
		SDL_Rect src{ 0, 0, 0, 0 };
		int pivotX = 0;
		int pivotY = 0;
	};

	std::unordered_map<std::string, ForageSpriteRuntimeDef> m_forageSprites;
	std::unordered_map<std::string, SDL_Texture*> m_forageAtlases;
	std::unordered_map<std::string, float> m_forageArchetypeMapScale;
	std::unordered_map<std::string, float> m_forageSpawnScaleOverride;
	std::unordered_set<std::string> m_depletedForageSpawnIds;
	std::unordered_map<std::string, int> m_forageRespawnAvailableDay;

	struct ForageInventoryStack
	{
		std::string speciesId;
		std::string displayName;
		std::string spriteId;
		int count = 0;
	};

	std::vector<ForageInventoryStack> m_collectedForageItems;
	static constexpr int kForageStackMax = 64;

	bool m_forageWindowOpen = false;
	bool m_forageWindowFocus = false;

	std::string m_activeForageSpawnId;
	int m_activeForageTileX = -1;
	int m_activeForageTileY = -1;
	std::string m_activeForageSpeciesId;
	std::vector<ForageExaminationAnswer> m_activeForageAnswers;
	std::unordered_map<std::string, std::array<char, 160>> m_activeForageTraitText;
	char m_activeForageDescriptionText[768] = "";
	char m_activeForageNameGuess[160] = "";
	ForageExaminationResult m_lastForageExaminationResult{};
	bool m_hasLastForageExaminationResult = false;

	bool loadForagingData();
	bool loadForageSpriteData(std::string* outError = nullptr);
	void destroyForageAtlases();
	SDL_Texture* textureForForageSprite(const ForageSpriteRuntimeDef& sprite) const;
	std::string resolveForageSpriteId(const ForageSpawnDef& spawn) const;
	std::string resolveForageDisplaySpriteId(const ForageSpawnDef& spawn) const;
	float resolveForageMapScale(const ForageSpawnDef& spawn) const;
	bool isForageSpeciesKnown(const std::string& speciesId) const;
	const ForageSpeciesDef* resolveForageSpeciesForSpawn(const ForageSpawnDef& spawn) const;
	bool isForageSpawnAvailable(const ForageSpawnDef& spawn) const;
	std::string forageSpawnRuntimeKey(const ForageSpawnDef& spawn) const;
	int currentForageDaySerial() const;
	void markForageSpawnGathered(const ForageSpawnDef& spawn);
	bool addForageToInventory(const ForageSpeciesDef* species, const ForageArchetypeDef& archetype, int count);
	const ForageSpawnDef* findNearbyForageSpawn(int radiusPx = 42) const;
	void tryInteractWithForage();
	void renderForageSpawns(const TerrainRenderer::View& view);
	void renderForagePrompt();
	void renderForageWindow();

	// Inventory item usage / basic consumption effects.
	bool isInventoryItemUsable(const ItemDef& def, const ItemStack& stack) const;
	bool useInventoryItem(ItemStack& stack);
	bool applyInventoryItemUseEffects(const std::string& itemId, const ItemDef& def, bool& outConsumeOne);
	void refreshInventoryDerivedStats();

	struct ActiveFoodPoisoning
	{
		bool active = false;
		bool fatal = false;
		std::string sourceName;
		float elapsedHours = 0.0f;
		float onsetHours = 0.0f;
		float durationHours = 0.0f;
		float healthDrainPerHour = 0.0f;
		float hydrationDrainPerHour = 0.0f;
		bool onsetLogged = false;
		bool endLogged = false;
		int lastLoggedHour = -1;
		int lastLoggedMilestone = -1;
	};

	ActiveFoodPoisoning m_activeFoodPoisoning;
	void startFoodPoisoning(const std::string& sourceName, bool fatal);
	void updateFoodPoisoning(int elapsedGameMinutes);
	void clearFoodPoisoning(const std::string& reason = std::string());
	std::string activeFoodPoisoningStatusText() const;
};
