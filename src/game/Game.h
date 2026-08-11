#pragma once
#include <SDL.h>
#include <string>
#include "audio/AudioManager.h"
#include "game/playerstats.h"
#include "ControlSettings.h"

class Editor;
class ObjectEditor;
class Campaign;
class BuildInteriorEngine;
class TextureSpriteEditor;

class Game
{
public:
    Game();
    ~Game();

    bool init(SDL_Window* window);
    void shutdown();

    void handleEvent(const SDL_Event& e);
    void update(float dt);
    void render();

    bool isRunning() const { return m_running; }

private:
    enum class Mode
    {
        Menu,
        StyleSelect,
        Settings,
        ProfileEditor,
        Interior2D,
        InteriorEditor,
        Editor,
        ObjectEditor,
        TextureSpriteEditor,
        Campaign
    };

    enum class GameMenuAction
    {
        None,
        SaveProgress,
        LoadProgress,
        MainMenu,
        QuitGame
    };

    enum class SaveSlotMode
    {
        None,
        Save,
        Load
    };

    struct SaveSlotSummary
    {
        bool exists = false;
        std::string name;
        std::string location;
        std::string savedAt;
    };

    struct GameSettings
    {
        std::string language = "cs";
        int musicVolume = 10;
        int sfxVolume = 80;
        int voiceVolume = 80;
        bool showFps = true;
        gamecontrols::Settings controls;
    };

    Campaign* m_campaign = nullptr;
    AudioManager m_audio;

    void enterCampaign(
        const std::string& mapPath = std::string(),
        const std::string& spawnId = std::string(),
        bool useCustomStartDateTime = false);
    void leaveCampaign();
    bool loadCampaignMap(const std::string& mapPath, const std::string& spawnId);

    void enterEditor();
    void leaveEditor();

    void enterObjectEditor();
    void leaveObjectEditor();

    void enterTextureSpriteEditor();
    void leaveTextureSpriteEditor();

    void enterInterior2D(
        const std::string& interiorId = "castle:houska_1400/houska_exterior",
        const std::string& spawnId = std::string());
    void enterInteriorEditor(const std::string& interiorId = "castle:houska_1400/houska_exterior");
    void leaveInterior2D();

    void toggleFullscreen();

    void loadSettings();
    void saveSettings();
    void applySettingsToRuntime();

    void loadProfileDraft(PlayerStats::Background background);
    void saveProfileDraft();
    void resetProfileDraft();
    bool isGameplayMode() const;
    void openGameMenu();
    void closeGameMenu();
    void releaseMouseForGameMenu();
    void resetGameMenuButtonRects();
    void storeGameMenuButtonRect(int index);
    int hitGameMenuButton(float x, float y) const;
    bool handleGameMenuMouseEvent(const SDL_Event& e);
    void triggerGameMenuButton(int index);
    void requestGameMenuAction(GameMenuAction action);
    void processPendingGameMenuAction();
    void renderGameMenuOverlay(int screenW, int screenH);
    void openSaveSlotMenu(SaveSlotMode mode);
    void closeSaveSlotMenu();
    void loadSaveSlotNameBuffers();
    void renderSaveSlotMenu(int screenW, int screenH);
    SaveSlotSummary readSaveSlotSummary(int slotIndex) const;
    std::string saveSlotName(int slotIndex) const;
    bool saveProgressToSlot(int slotIndex);
    bool loadProgressFromSlot(int slotIndex);
    bool renameSaveSlot(int slotIndex);
    void requestSaveSlotAction(int slotIndex);
    void requestLoadSlotAction(int slotIndex);
    void syncInteriorMouseLookSuppression();
    void returnToMainMenuFromGame();

private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;

    bool m_running = true;
    Mode m_mode = Mode::Menu;
    PlayerStats::Background m_pendingBackground = PlayerStats::Background::ScholarAthlete;
    int m_newCampaignStartDay = 29;
    int m_newCampaignStartMonth = 3;
    int m_newCampaignStartYear = 1400;
    int m_newCampaignStartHour = 8;
    int m_newCampaignStartMinute = 0;
    PlayerStats::Background m_profileEditBackground = PlayerStats::Background::ScholarAthlete;
    PlayerStats m_profileDraft;
    bool m_profileDraftLoaded = false;
    bool m_profileDirty = false;
    std::string m_profileStatus;
    std::string m_pendingControlBinding;
    GameSettings m_settings;
    float m_smoothedFps = 0.0f;
    float m_smoothedFrameMs = 0.0f;
    bool m_gameMenuOpen = false;
    std::string m_gameMenuStatus;
    struct UiRect
    {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        bool valid = false;
    };
    UiRect m_gameMenuButtonRects[4];
    int m_gameMenuPressedButton = -1;
    bool m_gameMenuClickHandled = false;
    GameMenuAction m_pendingGameMenuAction = GameMenuAction::None;
    SaveSlotMode m_saveSlotMode = SaveSlotMode::None;
    std::string m_saveSlotStatus;
    char m_saveSlotNameBuffers[3][64] = {};
    int m_pendingSaveSlotIndex = -1;
    int m_pendingLoadSlotIndex = -1;

    // menu bg
    SDL_Texture* m_menuBg = nullptr;
    int m_menuBgW = 0;
    int m_menuBgH = 0;

    Editor* m_editor = nullptr;
    ObjectEditor* m_objectEditor = nullptr;
    TextureSpriteEditor* m_textureSpriteEditor = nullptr;
    BuildInteriorEngine* m_interior2D = nullptr;
};
