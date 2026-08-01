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

    void enterCampaign();
    void leaveCampaign();

    void enterEditor();
    void leaveEditor();

    void enterObjectEditor();
    void leaveObjectEditor();

    void enterTextureSpriteEditor();
    void leaveTextureSpriteEditor();

    void enterInterior2D(const std::string& interiorId = "castle:houska_1400/houska_exterior");
    void enterInteriorEditor(const std::string& interiorId = "castle:houska_1400/houska_exterior");
    void leaveInterior2D();

    void toggleFullscreen();

    void loadSettings();
    void saveSettings();
    void applySettingsToRuntime();

    void loadProfileDraft(PlayerStats::Background background);
    void saveProfileDraft();
    void resetProfileDraft();

private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;

    bool m_running = true;
    Mode m_mode = Mode::Menu;
    PlayerStats::Background m_pendingBackground = PlayerStats::Background::ScholarAthlete;
    PlayerStats::Background m_profileEditBackground = PlayerStats::Background::ScholarAthlete;
    PlayerStats m_profileDraft;
    bool m_profileDraftLoaded = false;
    bool m_profileDirty = false;
    std::string m_profileStatus;
    std::string m_pendingControlBinding;
    GameSettings m_settings;
    float m_smoothedFps = 0.0f;
    float m_smoothedFrameMs = 0.0f;

    // menu bg
    SDL_Texture* m_menuBg = nullptr;
    int m_menuBgW = 0;
    int m_menuBgH = 0;

    Editor* m_editor = nullptr;
    ObjectEditor* m_objectEditor = nullptr;
    TextureSpriteEditor* m_textureSpriteEditor = nullptr;
    BuildInteriorEngine* m_interior2D = nullptr;
};
