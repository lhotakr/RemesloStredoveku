#pragma once

#include <SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace gamecontrols
{
struct Settings
{
    SDL_Scancode moveForward = SDL_SCANCODE_W;
    SDL_Scancode moveBackward = SDL_SCANCODE_S;
    SDL_Scancode moveLeft = SDL_SCANCODE_A;
    SDL_Scancode moveRight = SDL_SCANCODE_D;
    SDL_Scancode run = SDL_SCANCODE_LSHIFT;
    SDL_Scancode jump = SDL_SCANCODE_SPACE;
    SDL_Scancode use = SDL_SCANCODE_E;
    SDL_Scancode inventory = SDL_SCANCODE_I;
    SDL_Scancode journal = SDL_SCANCODE_Q;

    float mouseSensitivity = 1.0f;
    bool invertMouseY = false;
    bool alwaysMouseLook = true;
};

inline Settings& Mutable()
{
    static Settings settings;
    return settings;
}

inline const Settings& Get()
{
    return Mutable();
}

inline void Set(const Settings& settings)
{
    Mutable() = settings;
}

inline SDL_Scancode ReadScancode(const nlohmann::json& source,
                                 const char* key,
                                 SDL_Scancode fallback)
{
    const int raw = source.value(key, static_cast<int>(fallback));
    if (raw < 0 || raw >= SDL_NUM_SCANCODES)
        return fallback;
    return static_cast<SDL_Scancode>(raw);
}

inline void LoadFromJson(const nlohmann::json& root, Settings& out)
{
    if (!root.contains("controls") || !root["controls"].is_object())
        return;

    const auto& controls = root["controls"];
    out.moveForward = ReadScancode(controls, "move_forward", out.moveForward);
    out.moveBackward = ReadScancode(controls, "move_backward", out.moveBackward);
    out.moveLeft = ReadScancode(controls, "move_left", out.moveLeft);
    out.moveRight = ReadScancode(controls, "move_right", out.moveRight);
    out.run = ReadScancode(controls, "run", out.run);
    out.jump = ReadScancode(controls, "jump", out.jump);
    out.use = ReadScancode(controls, "use", out.use);
    out.inventory = ReadScancode(controls, "inventory", out.inventory);
    out.journal = ReadScancode(controls, "journal", out.journal);
    out.mouseSensitivity = std::clamp(controls.value("mouse_sensitivity", out.mouseSensitivity), 0.15f, 4.0f);
    out.invertMouseY = controls.value("invert_mouse_y", out.invertMouseY);
    out.alwaysMouseLook = controls.value("always_mouse_look", out.alwaysMouseLook);
}

inline void SaveToJson(nlohmann::json& root, const Settings& settings)
{
    root["controls"] = {
        {"move_forward", static_cast<int>(settings.moveForward)},
        {"move_backward", static_cast<int>(settings.moveBackward)},
        {"move_left", static_cast<int>(settings.moveLeft)},
        {"move_right", static_cast<int>(settings.moveRight)},
        {"run", static_cast<int>(settings.run)},
        {"jump", static_cast<int>(settings.jump)},
        {"use", static_cast<int>(settings.use)},
        {"inventory", static_cast<int>(settings.inventory)},
        {"journal", static_cast<int>(settings.journal)},
        {"mouse_sensitivity", std::clamp(settings.mouseSensitivity, 0.15f, 4.0f)},
        {"invert_mouse_y", settings.invertMouseY},
        {"always_mouse_look", settings.alwaysMouseLook}
    };
}

inline std::filesystem::path SettingsPath()
{
#ifdef REMESLO_PROJECT_ROOT
    return std::filesystem::path(REMESLO_PROJECT_ROOT) / "data" / "settings" / "game_settings.json";
#else
    return std::filesystem::current_path() / "data" / "settings" / "game_settings.json";
#endif
}

inline bool LoadFromDisk()
{
    std::ifstream input(SettingsPath(), std::ios::binary);
    if (!input)
        return false;

    try
    {
        nlohmann::json root;
        input >> root;
        Settings settings = Mutable();
        LoadFromJson(root, settings);
        Set(settings);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

inline const char* KeyName(SDL_Scancode scancode)
{
    const char* name = SDL_GetScancodeName(scancode);
    return (name && name[0] != '\0') ? name : "?";
}

inline SDL_Scancode* BindingByAction(Settings& settings, const std::string& action)
{
    if (action == "move_forward") return &settings.moveForward;
    if (action == "move_backward") return &settings.moveBackward;
    if (action == "move_left") return &settings.moveLeft;
    if (action == "move_right") return &settings.moveRight;
    if (action == "run") return &settings.run;
    if (action == "jump") return &settings.jump;
    if (action == "use") return &settings.use;
    if (action == "inventory") return &settings.inventory;
    if (action == "journal") return &settings.journal;
    return nullptr;
}
}
