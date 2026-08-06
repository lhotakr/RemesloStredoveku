#define NOMINMAX
#include "Game.h"
#include "Editor.h"
#include "ObjectEditor.h"
#include "BuildInteriorEngine.h"
#include "interior/TextureSpriteEditor.h"
#include "ControlSettings.h"
#include "Campaign.h"
#include "PlayerProfileStore.h"
#include "Player.h"
#include "Utf8.h"
#include "ImGuiUtils.h"

#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

static constexpr const char* kDefaultHouskaLocation = "castle:houska_1400/houska_exterior";

static SDL_Texture* loadTexture(SDL_Renderer* r, const char* path, int& outW, int& outH)
{
    outW = outH = 0;
    SDL_Surface* surf = IMG_Load(path);
    if (!surf) return nullptr;

    outW = surf->w;
    outH = surf->h;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    return tex;
}

static fs::path ProjectRootPath()
{
#ifdef REMESLO_PROJECT_ROOT
    return fs::path(REMESLO_PROJECT_ROOT);
#else
    return fs::current_path();
#endif
}

static fs::path SettingsPath()
{
    return ProjectRootPath() / "data" / "settings" / "game_settings.json";
}

static constexpr int kSaveSlotCount = 3;

static std::string DefaultSaveSlotName(int slotIndex)
{
    return U8("Pozice ") + std::to_string(slotIndex + 1);
}

static fs::path SaveSlotPath(int slotIndex)
{
    return ProjectRootPath() / "data" / "saves" / ("slot_" + std::to_string(slotIndex + 1) + ".json");
}

static std::string CurrentSaveTimestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm local{};
    if (std::tm* value = std::localtime(&now))
        local = *value;
    char buffer[32] = "";
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return buffer;
}

static bool IsSavePathLike(const std::string& value)
{
    return value.find(':') != std::string::npos ||
        value.find('/') != std::string::npos ||
        value.find('\\') != std::string::npos ||
        value.ends_with(".json");
}

static std::string ResolveSavedInteriorLocationId(const std::string& interiorId)
{
    if (interiorId.empty())
        return kDefaultHouskaLocation;

    if (interiorId.starts_with("castle:") || IsSavePathLike(interiorId))
        return interiorId;

    std::error_code ec;
    const fs::path legacyInterior = ProjectRootPath() / "data" / "interiors" / (interiorId + ".json");
    if (fs::exists(legacyInterior, ec) && !ec)
        return interiorId;

    ec.clear();
    const fs::path castlesRoot = ProjectRootPath() / "data" / "castles";
    if (!fs::exists(castlesRoot, ec) || ec)
        return interiorId;

    for (fs::recursive_directory_iterator it(castlesRoot, ec), end; it != end && !ec; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;

        const fs::path path = it->path();
        if (path.parent_path().filename() != "maps")
            continue;

        if (path.filename() != fs::path(interiorId + ".map.json"))
            continue;

        const std::string castleId = path.parent_path().parent_path().filename().string();
        if (!castleId.empty())
            return "castle:" + castleId + "/" + interiorId;
    }

    return interiorId;
}

static fs::path ResolveCampaignMapPath(const std::string& mapPath)
{
    fs::path path(mapPath);
    if (path.is_absolute())
        return path;
    if (path.has_parent_path())
        return ProjectRootPath() / path;
    return ProjectRootPath() / "data" / "maps" / path;
}

static ImU32 ColorU32(float r, float g, float b, float a)
{
    return ImGui::GetColorU32(ImVec4(r, g, b, a));
}

static const char* T(const std::string& lang, const char* key)
{
    const bool cs = (lang != "en");

    if (std::strcmp(key, "title") == 0) return U8("Řemeslo středověku");
    if (std::strcmp(key, "subtitle") == 0) return cs ? U8("příběh cizince ve vsi Blatce léta Páně 1400") : "a stranger's story in Blatce, Anno Domini 1400";
    if (std::strcmp(key, "main_menu") == 0) return cs ? U8("HLAVNÍ MENU") : "MAIN MENU";
    if (std::strcmp(key, "choose_path") == 0) return cs ? U8("Vyber cestu dál.") : "Choose your path.";
    if (std::strcmp(key, "start_campaign") == 0) return cs ? U8("Zahájit kampaň") : "Start campaign";
    if (std::strcmp(key, "load_game") == 0) return cs ? U8("Načíst hru") : "Load game";
    if (std::strcmp(key, "level_editor") == 0) return cs ? U8("Editor úrovní") : "Level editor";
    if (std::strcmp(key, "object_editor") == 0) return cs ? U8("Editor objektů") : "Object editor";
    if (std::strcmp(key, "texture_sprite_editor") == 0) return cs ? U8("Editor textur, spritů a animací") : "Texture, sprite & animation editor";
    if (std::strcmp(key, "interior_2d") == 0) return cs ? U8("2.5D interiér") : "2.5D interior";
    if (std::strcmp(key, "interior_editor") == 0) return cs ? U8("Editor 2.5D interiéru") : "2.5D interior editor";
    if (std::strcmp(key, "profile_editor") == 0) return cs ? U8("Editor postavy") : "Character editor";
    if (std::strcmp(key, "settings") == 0) return cs ? U8("Nastavení") : "Settings";
    if (std::strcmp(key, "quit") == 0) return cs ? U8("Konec") : "Quit";
    if (std::strcmp(key, "hotkeys") == 0) return cs ? U8("F11 – celá obrazovka     ESC – zpět / herní menu") : "F11 – fullscreen     ESC – back / game menu";
    if (std::strcmp(key, "game_menu") == 0) return cs ? U8("HERNÍ MENU") : "GAME MENU";
    if (std::strcmp(key, "save_progress") == 0) return cs ? U8("Uložit postup") : "Save progress";
    if (std::strcmp(key, "load_progress") == 0) return cs ? U8("Nahrát postup") : "Load progress";
    if (std::strcmp(key, "main_menu_from_game") == 0) return cs ? U8("Do hlavního menu") : "Main menu";
    if (std::strcmp(key, "quit_game") == 0) return cs ? U8("Ukončit hru") : "Quit game";
    if (std::strcmp(key, "progress_saved") == 0) return cs ? U8("Postup uložen.") : "Progress saved.";
    if (std::strcmp(key, "progress_save_failed") == 0) return cs ? U8("Postup se nepodařilo uložit.") : "Progress could not be saved.";
    if (std::strcmp(key, "progress_missing") == 0) return cs ? U8("Žádný uložený postup nenalezen.") : "No saved progress found.";
    if (std::strcmp(key, "progress_load_failed") == 0) return cs ? U8("Postup se nepodařilo nahrát.") : "Progress could not be loaded.";
    if (std::strcmp(key, "save_slots_title") == 0) return cs ? U8("ULOŽIT POSTUP") : "SAVE PROGRESS";
    if (std::strcmp(key, "load_slots_title") == 0) return cs ? U8("NAHRÁT POSTUP") : "LOAD PROGRESS";
    if (std::strcmp(key, "slot_empty") == 0) return cs ? U8("Prázdná pozice") : "Empty slot";
    if (std::strcmp(key, "slot_name") == 0) return cs ? U8("Název") : "Name";
    if (std::strcmp(key, "slot_save") == 0) return cs ? U8("Uložit") : "Save";
    if (std::strcmp(key, "slot_load") == 0) return cs ? U8("Nahrát") : "Load";
    if (std::strcmp(key, "slot_rename") == 0) return cs ? U8("Přejmenovat") : "Rename";
    if (std::strcmp(key, "slot_renamed") == 0) return cs ? U8("Pozice přejmenována.") : "Slot renamed.";
    if (std::strcmp(key, "slot_rename_failed") == 0) return cs ? U8("Přejmenování se nepodařilo.") : "Rename failed.";
    if (std::strcmp(key, "slot_saved_at") == 0) return cs ? U8("Uloženo") : "Saved";
    if (std::strcmp(key, "slot_location") == 0) return cs ? U8("Lokace") : "Location";
    if (std::strcmp(key, "style_title") == 0) return cs ? U8("Zvol styl průchodu") : "Choose playstyle";
    if (std::strcmp(key, "style_subtitle") == 0) return cs ? U8("Jakým člověkem byl Patrik Němec předtím, než se ocitl zde?") : "Who was Patrik Němec before he arrived here?";
    if (std::strcmp(key, "back") == 0) return cs ? U8("Zpět") : "Back";
    if (std::strcmp(key, "enter_campaign") == 0) return cs ? U8("Vstoupit do kampaně") : "Enter campaign";
    if (std::strcmp(key, "settings_title") == 0) return cs ? U8("Nastavení hry") : "Game settings";
    if (std::strcmp(key, "settings_subtitle") == 0) return cs ? U8("Editor zůstává anglicky. Tohle platí pro vlastní hru.") : "The editor stays in English. These settings apply to gameplay.";
    if (std::strcmp(key, "language") == 0) return cs ? U8("Jazyk hry") : "Game language";
    if (std::strcmp(key, "music") == 0) return cs ? U8("Hudba") : "Music";
    if (std::strcmp(key, "sfx") == 0) return cs ? U8("Zvuky") : "SFX";
    if (std::strcmp(key, "voice") == 0) return cs ? U8("Hlas") : "Voice";
    if (std::strcmp(key, "save") == 0) return cs ? U8("Uložit") : "Save";
    if (std::strcmp(key, "audio_note") == 0) return cs ? U8("Pozn.: zvuky a hlas jsou připravené pro budoucí rozdělení audia.") : "Note: SFX and voice are prepared for a later audio split.";
    if (std::strcmp(key, "controls") == 0) return cs ? U8("Ovládání") : "Controls";
    if (std::strcmp(key, "audio") == 0) return cs ? U8("Zvuk") : "Audio";
    if (std::strcmp(key, "mouse_sensitivity") == 0) return cs ? U8("Citlivost myši") : "Mouse sensitivity";
    if (std::strcmp(key, "invert_mouse_y") == 0) return cs ? U8("Obrátit svislou osu myši") : "Invert mouse Y";
    if (std::strcmp(key, "always_mouse_look") == 0) return cs ? U8("Trvalé rozhlížení myší v 2.5D") : "Always use mouse look in 2.5D";
    if (std::strcmp(key, "move_forward") == 0) return cs ? U8("Pohyb vpřed") : "Move forward";
    if (std::strcmp(key, "move_backward") == 0) return cs ? U8("Pohyb vzad") : "Move backward";
    if (std::strcmp(key, "move_left") == 0) return cs ? U8("Pohyb vlevo") : "Move left";
    if (std::strcmp(key, "move_right") == 0) return cs ? U8("Pohyb vpravo") : "Move right";
    if (std::strcmp(key, "run") == 0) return cs ? U8("Běh") : "Run";
    if (std::strcmp(key, "jump") == 0) return cs ? U8("Skok") : "Jump";
    if (std::strcmp(key, "use") == 0) return cs ? U8("Použít / interakce") : "Use / interact";
    if (std::strcmp(key, "inventory") == 0) return cs ? U8("Inventář") : "Inventory";
    if (std::strcmp(key, "journal") == 0) return cs ? U8("Deník") : "Journal";
    if (std::strcmp(key, "press_key") == 0) return cs ? U8("Stiskni klávesu…") : "Press a key…";
    if (std::strcmp(key, "reset_controls") == 0) return cs ? U8("Výchozí ovládání") : "Reset controls";
    if (std::strcmp(key, "controls_note") == 0) return cs ? U8("Stejné klávesy používá 2D kampaň i 2.5D režim. V 2D se myš používá pro UI; v 2.5D pro rozhlížení.") : "The same bindings are used by the 2D campaign and 2.5D mode. The mouse controls UI in 2D and looking in 2.5D.";
    if (std::strcmp(key, "performance") == 0) return cs ? U8("Výkon") : "Performance";
    if (std::strcmp(key, "show_fps") == 0) return cs ? U8("Zobrazovat FPS") : "Show FPS";
    if (std::strcmp(key, "fps_now") == 0) return cs ? U8("Aktuální výkon") : "Current performance";
    if (std::strcmp(key, "fps_note") == 0) return cs ? U8("FPS je vyhlazené, aby hodnota zbytečně neposkakovala. Frame time pod 16,7 ms odpovídá 60 FPS.") : "FPS is smoothed to avoid excessive flicker. Frame time below 16.7 ms corresponds to 60 FPS.";
    if (std::strcmp(key, "profile_title") == 0) return cs ? U8("Editor postavy / stylu průchodu") : "Character / playstyle editor";
    if (std::strcmp(key, "profile_subtitle") == 0) return cs ? U8("Tady ladíš vlastnosti, které pak používá kampaň i minihry, například rozpoznávání přírodnin.") : "Tune the stats used by campaign systems and minigames, including foraging identification.";
    if (std::strcmp(key, "profile_save") == 0) return cs ? U8("Uložit profil") : "Save profile";
    if (std::strcmp(key, "profile_reset") == 0) return cs ? U8("Vrátit výchozí") : "Reset defaults";
    if (std::strcmp(key, "profile_use") == 0) return cs ? U8("Použít pro kampaň") : "Use for campaign";
    if (std::strcmp(key, "profile_saved") == 0) return cs ? U8("Profil uložen.") : "Profile saved.";
    if (std::strcmp(key, "profile_reset_done") == 0) return cs ? U8("Profil vrácen na výchozí hodnoty.") : "Profile reset to defaults.";
    if (std::strcmp(key, "profile_hint") == 0) return cs ? U8("Změny se ukládají do data/settings/player_profiles.json. Editor zůstává anglicky, ale hodnoty používá přímo hra.") : "Changes are saved to data/settings/player_profiles.json. The editor remains English, but gameplay uses these values.";

    return key;
}

static void DrawMenuBackground(
    SDL_Renderer* renderer,
    SDL_Texture* texture,
    int texW,
    int texH,
    int screenW,
    int screenH)
{
    if (texture && texW > 0 && texH > 0)
    {
        // Keep the whole image visible. This preserves the castle/background composition.
        const float sx = (float)screenW / (float)texW;
        const float sy = (float)screenH / (float)texH;
        const float scale = std::min(sx, sy);

        SDL_Rect dst{};
        dst.w = (int)std::ceil((float)texW * scale);
        dst.h = (int)std::ceil((float)texH * scale);
        dst.x = (screenW - dst.w) / 2;
        dst.y = (screenH - dst.h) / 2;

        SDL_RenderCopy(renderer, texture, nullptr, &dst);
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 min(0.0f, 0.0f);
    const ImVec2 max((float)screenW, (float)screenH);

    dl->AddRectFilled(min, max, ColorU32(0.02f, 0.018f, 0.012f, 0.22f));
    dl->AddRectFilledMultiColor(
        min, max,
        ColorU32(0.01f, 0.009f, 0.006f, 0.52f),
        ColorU32(0.01f, 0.009f, 0.006f, 0.18f),
        ColorU32(0.01f, 0.009f, 0.006f, 0.26f),
        ColorU32(0.01f, 0.009f, 0.006f, 0.62f));
}

static void DrawTextShadow(const ImVec2& pos, const char* text, ImU32 color, float scale = 1.0f)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * scale;

    dl->AddText(font, size, ImVec2(pos.x + 3.0f, pos.y + 4.0f), ColorU32(0.0f, 0.0f, 0.0f, 0.72f), text);
    dl->AddText(font, size, pos, color, text);
}

static void DrawMenuFrame(const ImVec2& pos, const ImVec2& size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);

    dl->AddRectFilled(pos, max, ColorU32(0.055f, 0.040f, 0.026f, 0.78f), 18.0f);
    dl->AddRect(pos, max, ColorU32(0.72f, 0.56f, 0.30f, 0.46f), 18.0f, 0, 1.6f);
    dl->AddRect(ImVec2(pos.x + 5.0f, pos.y + 5.0f), ImVec2(max.x - 5.0f, max.y - 5.0f), ColorU32(0.96f, 0.78f, 0.40f, 0.14f), 14.0f, 0, 1.0f);
}

static bool MedievalMenuButton(const char* label, const ImVec2& size, bool primary = false)
{
    ImGui::PushID(label);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##menu_button", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);

    ImU32 fill = primary
        ? ColorU32(0.34f, 0.20f, 0.08f, 0.92f)
        : ColorU32(0.10f, 0.075f, 0.045f, 0.88f);

    if (hovered)
        fill = primary ? ColorU32(0.46f, 0.28f, 0.10f, 0.96f) : ColorU32(0.18f, 0.12f, 0.06f, 0.94f);
    if (active)
        fill = ColorU32(0.58f, 0.34f, 0.12f, 1.0f);

    dl->AddRectFilled(pos, max, fill, 10.0f);
    dl->AddRect(pos, max, hovered ? ColorU32(1.0f, 0.78f, 0.34f, 0.90f) : ColorU32(0.78f, 0.58f, 0.28f, 0.52f), 10.0f, 0, hovered ? 2.2f : 1.4f);
    dl->AddLine(ImVec2(pos.x + 12.0f, pos.y + 8.0f), ImVec2(max.x - 12.0f, pos.y + 8.0f), ColorU32(1.0f, 0.84f, 0.42f, hovered ? 0.38f : 0.18f), 1.0f);

    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 textPos(
        pos.x + (size.x - textSize.x) * 0.5f,
        pos.y + (size.y - textSize.y) * 0.5f - 1.0f);

    dl->AddText(ImVec2(textPos.x + 2.0f, textPos.y + 2.0f), ColorU32(0.0f, 0.0f, 0.0f, 0.65f), label);
    dl->AddText(textPos, hovered ? ColorU32(1.0f, 0.90f, 0.62f, 1.0f) : ColorU32(0.92f, 0.82f, 0.62f, 1.0f), label);

    ImGui::PopID();
    return clicked;
}

static const char* BackgroundTitle(PlayerStats::Background background)
{
    switch (background)
    {
    case PlayerStats::Background::Survivalist:    return U8("Patrik Němec – Zálesák");
    case PlayerStats::Background::ScholarAthlete: return U8("Patrik Němec – Student historie");
    case PlayerStats::Background::SocialAdaptable:return U8("Patrik Němec – Měšťan");
    }
    return U8("Patrik Němec");
}

static const char* BackgroundShort(PlayerStats::Background background)
{
    switch (background)
    {
    case PlayerStats::Background::Survivalist:
        return U8("Přežije díky rukám, ohni a improvizaci. V přírodě je doma, ale ve středověké společnosti tápe.");
    case PlayerStats::Background::ScholarAthlete:
        return U8("Vytrvalý, vzdělaný a pozorný. Chápe historii a symboly, ale hůř navazuje kontakt s cizími lidmi.");
    case PlayerStats::Background::SocialAdaptable:
        return U8("Přežívá hlavně přes lidi. Snadno získává důvěru, ale v lese a nepohodlí rychle ztrácí jistotu.");
    }
    return "";
}

static const char* BackgroundStrengths(PlayerStats::Background background)
{
    switch (background)
    {
    case PlayerStats::Background::Survivalist:
        return U8("Silné stránky: oheň, dřevo, opravy, voda, spaní venku.");
    case PlayerStats::Background::ScholarAthlete:
        return U8("Silné stránky: výdrž, historie, latina, soustředění, adaptace.");
    case PlayerStats::Background::SocialAdaptable:
        return U8("Silné stránky: důvěra NPC, vyjednávání, čtení situací, hygiena.");
    }
    return "";
}

static const char* BackgroundWeaknesses(PlayerStats::Background background)
{
    switch (background)
    {
    case PlayerStats::Background::Survivalist:
        return U8("Slabiny: latina 0, němčina 0, slabší orientace v historii a sociálních pravidlech.");
    case PlayerStats::Background::ScholarAthlete:
        return U8("Slabiny: introverze, slabší začínání rozhovorů, menší spontánnost mezi lidmi.");
    case PlayerStats::Background::SocialAdaptable:
        return U8("Slabiny: nízká stamina, slabší survival v lese, větší citlivost na nepohodlí.");
    }
    return "";
}

static void WrappedTextAtWidth(const char* text, float wrapWidth, const ImVec4& color)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + wrapWidth);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

static void WrappedBoldTextAtWidth(const char* text, float wrapWidth, const ImVec4& color)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(pos.x + wrapWidth);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();

    // Tiny second pass gives a readable bold-like weight without requiring a separate font file.
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 1.0f, pos.y));
    ImGui::PushTextWrapPos(pos.x + wrapWidth);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}


static float WrappedTextAtWidthScaled(const char* text, float wrapWidth, const ImVec4& color, float scale)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * scale;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImU32 col = ImGui::GetColorU32(color);

    dl->AddText(font, fontSize, pos, col, text, nullptr, wrapWidth);

    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, wrapWidth, text);
    const float height = std::max(fontSize, textSize.y);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height));
    return height;
}

static float WrappedBoldTextAtWidthScaled(const char* text, float wrapWidth, const ImVec4& color, float scale)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * scale;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImU32 col = ImGui::GetColorU32(color);

    dl->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y), col, text, nullptr, wrapWidth);
    dl->AddText(font, fontSize, pos, col, text, nullptr, wrapWidth);

    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, wrapWidth, text);
    const float height = std::max(fontSize, textSize.y);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height));
    return height;
}

static bool StyledLanguageDropdown(const char* id, std::string& language, float width)
{
    ImGui::PushID(id);

    const char* currentLanguage = (language == "en") ? "English" : U8("Čeština");
    const ImVec2 size(width, 40.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##language_dropdown", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);

    ImU32 fill = ColorU32(0.10f, 0.075f, 0.045f, 0.88f);
    if (hovered)
        fill = ColorU32(0.18f, 0.12f, 0.06f, 0.94f);
    if (active)
        fill = ColorU32(0.32f, 0.19f, 0.08f, 0.98f);

    dl->AddRectFilled(pos, max, fill, 9.0f);
    dl->AddRect(pos, max,
        hovered ? ColorU32(1.0f, 0.78f, 0.34f, 0.86f) : ColorU32(0.78f, 0.58f, 0.28f, 0.52f),
        9.0f, 0, hovered ? 1.8f : 1.2f);
    dl->AddLine(ImVec2(pos.x + 10.0f, pos.y + 7.0f), ImVec2(max.x - 10.0f, pos.y + 7.0f), ColorU32(1.0f, 0.84f, 0.42f, hovered ? 0.34f : 0.16f), 1.0f);

    dl->AddText(ImVec2(pos.x + 14.0f, pos.y + 8.0f), ColorU32(0.94f, 0.84f, 0.64f, 1.0f), currentLanguage);

    const float ax = max.x - 28.0f;
    const float ay = pos.y + size.y * 0.5f + 2.0f;
    dl->AddTriangleFilled(
        ImVec2(ax - 8.0f, ay - 6.0f),
        ImVec2(ax + 8.0f, ay - 6.0f),
        ImVec2(ax, ay + 7.0f),
        ColorU32(0.96f, 0.82f, 0.48f, 1.0f));

    bool changed = false;
    if (clicked)
        ImGui::OpenPopup("##language_popup");

    ImGui::SetNextWindowPos(ImVec2(pos.x, max.y + 4.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Appearing);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.055f, 0.040f, 0.026f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.34f, 0.20f, 0.08f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.46f, 0.28f, 0.10f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.58f, 0.34f, 0.12f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 9.0f));

    if (ImGui::BeginPopup("##language_popup"))
    {
        if (ImGui::Selectable(U8("Čeština"), language == "cs"))
        {
            language = "cs";
            changed = true;
        }
        if (ImGui::Selectable("English", language == "en"))
        {
            language = "en";
            changed = true;
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y));
    ImGui::PopID();
    return changed;
}

static bool BackgroundRowCard(PlayerStats::Background background, PlayerStats::Background& selected, const ImVec2& size)
{
    ImGui::PushID((int)background);

    const bool chosen = selected == background;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##background_row", size);
    const bool hovered = ImGui::IsItemHovered();

    if (clicked)
        selected = background;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);

    const ImU32 fill = chosen
        ? ColorU32(0.24f, 0.15f, 0.060f, 0.94f)
        : ColorU32(0.050f, 0.040f, 0.030f, 0.82f);

    dl->AddRectFilled(pos, max, fill, 16.0f);
    dl->AddRect(pos, max,
        chosen ? ColorU32(1.0f, 0.76f, 0.32f, 0.95f) : (hovered ? ColorU32(0.92f, 0.68f, 0.30f, 0.75f) : ColorU32(0.72f, 0.54f, 0.30f, 0.38f)),
        16.0f, 0, chosen ? 2.4f : 1.4f);

    if (chosen)
    {
        dl->AddRectFilled(
            ImVec2(pos.x + 2.0f, pos.y + 2.0f),
            ImVec2(max.x - 2.0f, pos.y + 7.0f),
            ColorU32(1.0f, 0.76f, 0.28f, 0.23f), 14.0f);
    }

    const float padX = 24.0f;
    const float wrapW = size.x - padX * 2.0f;
    float y = pos.y + 14.0f;

    const char* title = BackgroundTitle(background);
    const char* selectedText = U8("  vybráno");
    std::string titleLine = title;
    if (chosen)
        titleLine += selectedText;

    const ImVec2 titleSize = ImGui::CalcTextSize(titleLine.c_str());
    ImGui::SetCursorScreenPos(ImVec2(pos.x + std::max(padX, (size.x - titleSize.x) * 0.5f), y));
    ImGui::TextColored(chosen ? ImVec4(1.0f, 0.82f, 0.38f, 1.0f) : ImVec4(0.88f, 0.78f, 0.58f, 1.0f), "%s", titleLine.c_str());

    y += 30.0f;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + padX, y));
    WrappedBoldTextAtWidthScaled(BackgroundShort(background), wrapW, ImVec4(0.96f, 0.90f, 0.78f, 1.0f), 0.82f);

    y = ImGui::GetCursorScreenPos().y + 6.0f;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + padX, y));
    WrappedTextAtWidthScaled(BackgroundStrengths(background), wrapW, ImVec4(0.82f, 0.76f, 0.60f, 1.0f), 0.78f);

    y = ImGui::GetCursorScreenPos().y + 3.0f;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + padX, y));
    WrappedTextAtWidthScaled(BackgroundWeaknesses(background), wrapW, ImVec4(0.70f, 0.66f, 0.54f, 1.0f), 0.76f);

    ImGui::PopID();
    return clicked;
}

static bool SettingsSliderRow(const char* label, int* value, float width)
{
    ImGui::PushID(label);

    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(rowStart);
    ImGui::TextColored(ImVec4(0.86f, 0.76f, 0.54f, 1.0f), "%s", label);

    ImGui::SetCursorScreenPos(ImVec2(rowStart.x + 155.0f, rowStart.y - 2.0f));
    ImGui::PushItemWidth(std::max(120.0f, width - 155.0f));

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.065f, 0.045f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.16f, 0.11f, 0.055f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.82f, 0.55f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.74f, 0.28f, 1.0f));

    const bool changed = ImGui::SliderInt("##slider", value, 0, 128, "%d");

    ImGui::PopStyleColor(4);
    ImGui::PopItemWidth();
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowStart.y + 40.0f));
    ImGui::PopID();
    return changed;
}

static bool SettingsFloatSliderRow(const char* label, float* value, float width, float minValue, float maxValue)
{
    ImGui::PushID(label);
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    ImGui::TextColored(ImVec4(0.86f, 0.76f, 0.54f, 1.0f), "%s", label);
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x + 210.0f, rowStart.y - 2.0f));
    ImGui::PushItemWidth(std::max(130.0f, width - 210.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.065f, 0.045f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.16f, 0.11f, 0.055f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.82f, 0.55f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.74f, 0.28f, 1.0f));
    const bool changed = ImGui::SliderFloat("##slider", value, minValue, maxValue, "%.2f");
    ImGui::PopStyleColor(4);
    ImGui::PopItemWidth();
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowStart.y + 40.0f));
    ImGui::PopID();
    return changed;
}

static bool ControlBindingRow(const char* label, const char* actionId, SDL_Scancode binding,
                              std::string& pendingAction, float width, const std::string& language)
{
    ImGui::PushID(actionId);
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    ImGui::TextColored(ImVec4(0.86f, 0.76f, 0.54f, 1.0f), "%s", label);
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x + 250.0f, rowStart.y - 4.0f));
    const bool waiting = pendingAction == actionId;
    const char* buttonText = waiting ? T(language, "press_key") : gamecontrols::KeyName(binding);
    const bool clicked = MedievalMenuButton(buttonText, ImVec2(std::max(130.0f, width - 250.0f), 34.0f), waiting);
    if (clicked)
        pendingAction = actionId;
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowStart.y + 42.0f));
    ImGui::PopID();
    return clicked;
}

static const char* ProfileShortName(PlayerStats::Background background)
{
    switch (background)
    {
    case PlayerStats::Background::Survivalist:     return U8("Zálesák");
    case PlayerStats::Background::ScholarAthlete:  return U8("Student historie");
    case PlayerStats::Background::SocialAdaptable: return U8("Měšťan");
    }
    return U8("Profil");
}

static bool ProfileBackgroundButton(PlayerStats::Background background, PlayerStats::Background& selected, const ImVec2& size)
{
    const bool isSelected = (selected == background);
    if (MedievalMenuButton(ProfileShortName(background), size, isSelected))
    {
        selected = background;
        return true;
    }
    return false;
}

static bool ProfileSlider(const char* label, float& value, float width = 210.0f)
{
    ImGui::PushID(label);
    ImGui::TextColored(ImVec4(0.84f, 0.77f, 0.60f, 1.0f), "%s", label);
    ImGui::SameLine(185.0f);
    ImGui::PushItemWidth(width);
    bool changed = ImGui::SliderFloat("##value", &value, 0.0f, 100.0f, "%.0f");
    ImGui::PopItemWidth();
    ImGui::PopID();
    return changed;
}

static bool ProfileSliderInt(const char* label, int& value, int minValue, int maxValue, float width = 210.0f)
{
    ImGui::PushID(label);
    ImGui::TextColored(ImVec4(0.84f, 0.77f, 0.60f, 1.0f), "%s", label);
    ImGui::SameLine(185.0f);
    ImGui::PushItemWidth(width);
    bool changed = ImGui::SliderInt("##value", &value, minValue, maxValue, "%d");
    ImGui::PopItemWidth();
    ImGui::PopID();
    return changed;
}

static bool ProfileCheckbox(const char* label, bool& value)
{
    ImGui::PushID(label);
    bool changed = ImGui::Checkbox("##check", &value);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.84f, 0.77f, 0.60f, 1.0f), "%s", label);
    ImGui::PopID();
    return changed;
}

static void ProfileDerivedPreview(const PlayerStats& s)
{
    ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.48f, 1.0f), U8("Odvozené hodnoty"));
    ImGui::Text(U8("Rychlost pohybu: %.1f px/s"), s.getMoveSpeed());
    ImGui::Text(U8("Nosnost: %.1f kg"), s.carryCapacity);
    ImGui::Text(U8("Objem batohu: %.1f"), s.carryVolumeCapacity);
    ImGui::Text(U8("Morálka / hygiena: %.0f / %.0f"), s.condition.morale, s.condition.hygiene);
}

static bool DrawProfileEditorStats(PlayerStats& draft)
{
    bool changed = false;

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.13f, 0.05f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.34f, 0.21f, 0.08f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.46f, 0.28f, 0.10f, 1.0f));

    if (ImGui::CollapsingHeader(U8("Základní vlastnosti"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ProfileSlider(U8("Síla"), draft.attributes.strength);
        changed |= ProfileSlider(U8("Výdrž"), draft.attributes.endurance);
        changed |= ProfileSlider(U8("Obratnost"), draft.attributes.dexterity);
        changed |= ProfileSlider(U8("Vnímání"), draft.attributes.perception);
        changed |= ProfileSlider(U8("Inteligence"), draft.attributes.intelligence);
        changed |= ProfileSlider(U8("Charisma"), draft.attributes.charisma);
        changed |= ProfileSlider(U8("Vůle"), draft.attributes.willpower);
    }

    if (ImGui::CollapsingHeader(U8("Survival a řemeslo"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ProfileSlider(U8("Rozdělání ohně"), draft.survival.fireMaking);
        changed |= ProfileSlider(U8("Práce se dřevem"), draft.survival.woodProcessing);
        changed |= ProfileSlider(U8("Přístřešek"), draft.survival.shelterBuilding);
        changed |= ProfileSlider(U8("Úprava vody"), draft.survival.waterPurification);
        changed |= ProfileSlider(U8("Stopování"), draft.survival.tracking);
        changed |= ProfileSlider(U8("Navigace"), draft.survival.navigation);
        changed |= ProfileSlider(U8("Sběr přírodnin"), draft.survival.foraging);
        changed |= ProfileSlider(U8("Vaření"), draft.survival.cooking);
        changed |= ProfileSlider(U8("Odolnost vůči chladu"), draft.survival.coldResistance);
        changed |= ProfileSlider(U8("Odolnost vůči horku"), draft.survival.heatResistance);
        changed |= ProfileSlider(U8("Tolerance jídla"), draft.survival.foodTolerance);
        changed |= ProfileSlider(U8("Odolnost vůči nemocem"), draft.survival.diseaseResistance);
        ImGui::Separator();
        changed |= ProfileSlider(U8("Oprava nástrojů"), draft.craft.toolRepair);
        changed |= ProfileSlider(U8("Oprava oděvu"), draft.craft.clothingRepair);
        changed |= ProfileSlider(U8("Práce s lanem"), draft.craft.ropeWork);
        changed |= ProfileSlider(U8("Dřevařina"), draft.craft.woodcraft);
        changed |= ProfileSlider(U8("Improvizace"), draft.craft.improvisation);
    }

    if (ImGui::CollapsingHeader(U8("Sociální a znalostní dovednosti")))
    {
        changed |= ProfileSlider(U8("Začínání rozhovorů"), draft.social.conversationInitiation);
        changed |= ProfileSlider(U8("Přesvědčování"), draft.social.persuasion);
        changed |= ProfileSlider(U8("Vyjednávání"), draft.social.negotiation);
        changed |= ProfileSlider(U8("Empatie"), draft.social.empathy);
        changed |= ProfileSlider(U8("Etiketa"), draft.social.etiquette);
        changed |= ProfileSlider(U8("Pohoda v davu"), draft.social.crowdComfort);
        changed |= ProfileSlider(U8("Dobový protokol"), draft.social.socialProtocol);
        ImGui::Separator();
        changed |= ProfileSlider(U8("Historie"), draft.knowledge.history);
        changed |= ProfileSlider(U8("Náboženství"), draft.knowledge.religionKnowledge);
        changed |= ProfileSlider(U8("Symboly"), draft.knowledge.symbolRecognition);
        changed |= ProfileSlider(U8("Gramotnost"), draft.knowledge.literacy);
        changed |= ProfileSlider(U8("Latina"), draft.knowledge.latin);
        changed |= ProfileSlider(U8("Němčina"), draft.knowledge.german);
        changed |= ProfileSlider(U8("Středověká čeština"), draft.knowledge.medievalCzech);
    }

    if (ImGui::CollapsingHeader(U8("Tělo, mysl a komfort")))
    {
        changed |= ProfileSlider(U8("Běh"), draft.physical.running);
        changed |= ProfileSlider(U8("Nošení břemen"), draft.physical.loadHandling);
        changed |= ProfileSlider(U8("Lezení"), draft.physical.climbing);
        changed |= ProfileSlider(U8("Plavání"), draft.physical.swimming);
        changed |= ProfileSlider(U8("Regenerace spánkem"), draft.physical.sleepRecovery);
        changed |= ProfileSlider(U8("Efektivita staminy"), draft.physical.staminaEfficiency);
        ImGui::Separator();
        changed |= ProfileSlider(U8("Soustředění"), draft.mental.focus);
        changed |= ProfileSlider(U8("Paměť"), draft.mental.memory);
        changed |= ProfileSlider(U8("Odolnost stresu"), draft.mental.stressResistance);
        changed |= ProfileSlider(U8("Adaptace"), draft.mental.adaptation);
        changed |= ProfileSlider(U8("Pozorování"), draft.mental.observation);
        changed |= ProfileSlider(U8("Samota"), draft.mental.lonelinessTolerance);
        ImGui::Separator();
        changed |= ProfileSlider(U8("Pohoda v přírodě"), draft.comfort.natureComfort);
        changed |= ProfileSlider(U8("Pohoda ve vsi/městě"), draft.comfort.urbanComfort);
        changed |= ProfileSlider(U8("Sociální komfort"), draft.comfort.socialComfort);
        changed |= ProfileSlider(U8("Citlivost na hygienu"), draft.comfort.hygieneSensitivity);
        changed |= ProfileSlider(U8("Adaptace na středověk"), draft.comfort.medievalAdaptation);
    }

    if (ImGui::CollapsingHeader(U8("Startovní výbava")))
    {
        changed |= ProfileCheckbox(U8("Hamaka"), draft.loadout.hammock);
        changed |= ProfileCheckbox(U8("Plachta / tarp"), draft.loadout.tarp);
        changed |= ProfileCheckbox(U8("Vodní filtr"), draft.loadout.waterFilter);
        changed |= ProfileCheckbox(U8("Dřívkáč"), draft.loadout.woodStove);
        changed |= ProfileCheckbox(U8("Plynový vařič"), draft.loadout.gasStove);
        changed |= ProfileCheckbox(U8("Chemický ohřev"), draft.loadout.rationHeater);
        changed |= ProfileCheckbox(U8("Kvalitní ešus"), draft.loadout.qualityCookPot);
        changed |= ProfileCheckbox(U8("Jednoduchý ešus"), draft.loadout.simpleCookPot);
        changed |= ProfileCheckbox(U8("Silný nůž"), draft.loadout.strongKnife);
        changed |= ProfileCheckbox(U8("Malý nůž"), draft.loadout.simpleKnife);
        changed |= ProfileCheckbox(U8("Lano"), draft.loadout.rope);
        changed |= ProfileCheckbox(U8("Powerbanka"), draft.loadout.powerBank);
        changed |= ProfileCheckbox(U8("Solární panel"), draft.loadout.solarPanel);
        changed |= ProfileCheckbox(U8("Offline mobil"), draft.loadout.smartphoneOffline);
        changed |= ProfileCheckbox(U8("Cestovní sprcha"), draft.loadout.travelShower);
        changed |= ProfileCheckbox(U8("Hygienická sada"), draft.loadout.hygieneKit);
        changed |= ProfileSliderInt(U8("Nádoby na vodu"), draft.loadout.waterContainers, 0, 8);
        changed |= ProfileSlider(U8("Teplo spaní"), draft.loadout.beddingWarmth);
        changed |= ProfileSlider(U8("Kvalita tarpu"), draft.loadout.tarpQuality);
    }

    ImGui::PopStyleColor(3);

    if (changed)
    {
        draft.recomputeDerivedStats();
        draft.refillVitals();
    }

    return changed;
}

Game::Game() = default;

Game::~Game()
{
    shutdown();
}

void Game::loadSettings()
{
    m_settings = GameSettings{};

    const fs::path p = SettingsPath();
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return;

    try
    {
        json j;
        f >> j;
        m_settings.language = j.value("language", m_settings.language);
        m_settings.musicVolume = std::clamp(j.value("music_volume", m_settings.musicVolume), 0, 128);
        m_settings.sfxVolume = std::clamp(j.value("sfx_volume", m_settings.sfxVolume), 0, 128);
        m_settings.voiceVolume = std::clamp(j.value("voice_volume", m_settings.voiceVolume), 0, 128);
        m_settings.showFps = j.value("show_fps", m_settings.showFps);
        gamecontrols::LoadFromJson(j, m_settings.controls);
        gamecontrols::Set(m_settings.controls);

        if (m_settings.language != "cs" && m_settings.language != "en")
            m_settings.language = "cs";
    }
    catch (...)
    {
        m_settings = GameSettings{};
    }
    gamecontrols::Set(m_settings.controls);
}

void Game::saveSettings()
{
    const fs::path p = SettingsPath();
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    json j;
    j["language"] = m_settings.language;
    j["music_volume"] = std::clamp(m_settings.musicVolume, 0, 128);
    j["sfx_volume"] = std::clamp(m_settings.sfxVolume, 0, 128);
    j["voice_volume"] = std::clamp(m_settings.voiceVolume, 0, 128);
    j["show_fps"] = m_settings.showFps;
    gamecontrols::SaveToJson(j, m_settings.controls);
    gamecontrols::Set(m_settings.controls);

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (f)
        f << j.dump(2);
}

void Game::applySettingsToRuntime()
{
    m_settings.musicVolume = std::clamp(m_settings.musicVolume, 0, 128);
    m_settings.sfxVolume = std::clamp(m_settings.sfxVolume, 0, 128);
    m_settings.voiceVolume = std::clamp(m_settings.voiceVolume, 0, 128);
    m_audio.setMusicVolume(m_settings.musicVolume);
    m_settings.controls.mouseSensitivity = std::clamp(m_settings.controls.mouseSensitivity, 0.15f, 4.0f);
    gamecontrols::Set(m_settings.controls);
}

void Game::loadProfileDraft(PlayerStats::Background background)
{
    m_profileEditBackground = background;
    m_profileDraft = playerprofile::LoadProfileForBackground(background);
    m_profileDraft.background = background;
    m_profileDraft.recomputeDerivedStats();
    m_profileDraft.refillVitals();
    m_profileDraftLoaded = true;
    m_profileDirty = false;
    m_profileStatus.clear();
}

void Game::saveProfileDraft()
{
    if (!m_profileDraftLoaded)
        loadProfileDraft(m_profileEditBackground);

    m_profileDraft.background = m_profileEditBackground;
    m_profileDraft.recomputeDerivedStats();
    m_profileDraft.refillVitals();

    if (playerprofile::SaveProfileOverride(m_profileDraft))
    {
        m_profileStatus = T(m_settings.language, "profile_saved");
        m_profileDirty = false;
    }
    else
    {
        m_profileStatus = "Save failed.";
    }
}

void Game::resetProfileDraft()
{
    playerprofile::ResetProfileOverride(m_profileEditBackground);
    m_profileDraft = PlayerStats{};
    m_profileDraft.applyBackgroundPreset(m_profileEditBackground);
    m_profileDraftLoaded = true;
    m_profileDirty = false;
    m_profileStatus = T(m_settings.language, "profile_reset_done");
}

bool Game::isGameplayMode() const
{
    return m_mode == Mode::Campaign || m_mode == Mode::Interior2D;
}

void Game::syncInteriorMouseLookSuppression()
{
    if (!m_interior2D)
        return;

    const bool sharedUiOpen =
        m_mode == Mode::Interior2D &&
        m_campaign &&
        m_campaign->sharedUiBlocksMouseLook();

    m_interior2D->setMouseLookSuppressed(m_gameMenuOpen || sharedUiOpen);
}

void Game::openGameMenu()
{
    if (!isGameplayMode())
        return;

    m_gameMenuOpen = true;
    m_saveSlotMode = SaveSlotMode::None;
    m_gameMenuStatus.clear();
    m_saveSlotStatus.clear();
    resetGameMenuButtonRects();
    releaseMouseForGameMenu();
    syncInteriorMouseLookSuppression();
}

void Game::closeGameMenu()
{
    m_gameMenuOpen = false;
    m_saveSlotMode = SaveSlotMode::None;
    m_gameMenuStatus.clear();
    m_saveSlotStatus.clear();
    resetGameMenuButtonRects();
    syncInteriorMouseLookSuppression();
}

void Game::releaseMouseForGameMenu()
{
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_CaptureMouse(SDL_FALSE);
    if (m_window)
        SDL_SetWindowGrab(m_window, SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
}

void Game::resetGameMenuButtonRects()
{
    for (UiRect& rect : m_gameMenuButtonRects)
        rect = UiRect{};
    m_gameMenuPressedButton = -1;
    m_gameMenuClickHandled = false;
}

void Game::storeGameMenuButtonRect(int index)
{
    if (index < 0 || index >= 4)
        return;

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    m_gameMenuButtonRects[index] = {min.x, min.y, max.x, max.y, true};
}

int Game::hitGameMenuButton(float x, float y) const
{
    for (int i = 0; i < 4; ++i)
    {
        const UiRect& rect = m_gameMenuButtonRects[i];
        if (rect.valid && x >= rect.x0 && x <= rect.x1 && y >= rect.y0 && y <= rect.y1)
            return i;
    }
    return -1;
}

void Game::triggerGameMenuButton(int index)
{
    switch (index)
    {
    case 0:
        requestGameMenuAction(GameMenuAction::SaveProgress);
        break;
    case 1:
        requestGameMenuAction(GameMenuAction::LoadProgress);
        break;
    case 2:
        requestGameMenuAction(GameMenuAction::MainMenu);
        break;
    case 3:
        requestGameMenuAction(GameMenuAction::QuitGame);
        break;
    default:
        break;
    }
}

void Game::requestGameMenuAction(GameMenuAction action)
{
    if (m_pendingGameMenuAction == GameMenuAction::None)
        m_pendingGameMenuAction = action;
}

void Game::processPendingGameMenuAction()
{
    if (m_pendingSaveSlotIndex >= 0)
    {
        const int slotIndex = m_pendingSaveSlotIndex;
        m_pendingSaveSlotIndex = -1;
        saveProgressToSlot(slotIndex);
    }

    if (m_pendingLoadSlotIndex >= 0)
    {
        const int slotIndex = m_pendingLoadSlotIndex;
        m_pendingLoadSlotIndex = -1;
        loadProgressFromSlot(slotIndex);
    }

    const GameMenuAction action = m_pendingGameMenuAction;
    m_pendingGameMenuAction = GameMenuAction::None;

    switch (action)
    {
    case GameMenuAction::SaveProgress:
        openSaveSlotMenu(SaveSlotMode::Save);
        break;
    case GameMenuAction::LoadProgress:
        openSaveSlotMenu(SaveSlotMode::Load);
        break;
    case GameMenuAction::MainMenu:
        returnToMainMenuFromGame();
        break;
    case GameMenuAction::QuitGame:
        m_running = false;
        break;
    case GameMenuAction::None:
        break;
    }
}

bool Game::handleGameMenuMouseEvent(const SDL_Event& e)
{
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        m_gameMenuPressedButton = hitGameMenuButton((float)e.button.x, (float)e.button.y);
        return m_gameMenuPressedButton >= 0;
    }

    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
    {
        const int releasedButton = hitGameMenuButton((float)e.button.x, (float)e.button.y);
        const int pressedButton = m_gameMenuPressedButton;
        m_gameMenuPressedButton = -1;

        if (pressedButton >= 0 && pressedButton == releasedButton)
        {
            m_gameMenuClickHandled = true;
            triggerGameMenuButton(pressedButton);
            return true;
        }
    }

    return false;
}

void Game::openSaveSlotMenu(SaveSlotMode mode)
{
    if (mode == SaveSlotMode::Save && !isGameplayMode())
        return;

    m_saveSlotMode = mode;
    m_saveSlotStatus.clear();
    loadSaveSlotNameBuffers();

    if (isGameplayMode())
    {
        m_gameMenuOpen = true;
        releaseMouseForGameMenu();
        syncInteriorMouseLookSuppression();
    }
}

void Game::closeSaveSlotMenu()
{
    m_saveSlotMode = SaveSlotMode::None;
    m_saveSlotStatus.clear();
    m_pendingSaveSlotIndex = -1;
    m_pendingLoadSlotIndex = -1;
}

void Game::loadSaveSlotNameBuffers()
{
    for (int i = 0; i < kSaveSlotCount; ++i)
    {
        std::string name = DefaultSaveSlotName(i);
        std::ifstream f(SaveSlotPath(i), std::ios::binary);
        if (f)
        {
            try
            {
                json j;
                f >> j;
                name = j.value("slot_name", name);
            }
            catch (...) {}
        }

        std::snprintf(m_saveSlotNameBuffers[i], sizeof(m_saveSlotNameBuffers[i]), "%s", name.c_str());
    }
}

Game::SaveSlotSummary Game::readSaveSlotSummary(int slotIndex) const
{
    SaveSlotSummary summary;
    summary.name = DefaultSaveSlotName(slotIndex);

    std::ifstream f(SaveSlotPath(slotIndex), std::ios::binary);
    if (!f)
        return summary;

    try
    {
        json j;
        f >> j;
        summary.exists = true;
        summary.name = j.value("slot_name", summary.name);
        summary.savedAt = j.value("saved_at", std::string());

        const std::string saveMode = j.value("mode", std::string("campaign"));
        if (saveMode == "interior_2d")
        {
            const std::string interiorId = ResolveSavedInteriorLocationId(
                j.value("interior_id", std::string(kDefaultHouskaLocation)));
            summary.location = std::string("2.5D: ") + interiorId;
        }
        else
        {
            const std::string campaignMap = j.value("campaign_map", std::string());
            const std::string mapName = campaignMap.empty()
                ? std::string("campaign")
                : fs::path(campaignMap).filename().string();
            summary.location = std::string("Kampaň: ") + mapName;
        }
    }
    catch (...)
    {
        summary.exists = false;
    }

    return summary;
}

std::string Game::saveSlotName(int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kSaveSlotCount)
        return DefaultSaveSlotName(0);

    std::string name = m_saveSlotNameBuffers[slotIndex];
    if (name.empty())
        name = DefaultSaveSlotName(slotIndex);
    return name;
}

void Game::requestSaveSlotAction(int slotIndex)
{
    if (slotIndex >= 0 && slotIndex < kSaveSlotCount)
        m_pendingSaveSlotIndex = slotIndex;
}

void Game::requestLoadSlotAction(int slotIndex)
{
    if (slotIndex >= 0 && slotIndex < kSaveSlotCount)
        m_pendingLoadSlotIndex = slotIndex;
}

bool Game::saveProgressToSlot(int slotIndex)
{
    if (!isGameplayMode())
        return false;
    if (slotIndex < 0 || slotIndex >= kSaveSlotCount)
        return false;

    json j;
    j["version"] = 1;
    j["slot"] = slotIndex + 1;
    j["slot_name"] = saveSlotName(slotIndex);
    j["saved_at"] = CurrentSaveTimestamp();
    j["mode"] = (m_mode == Mode::Interior2D) ? "interior_2d" : "campaign";

    if (m_campaign)
    {
        j["campaign_map"] = m_campaign->currentMapPath();
        j["campaign_player"] = {
            {"x", m_campaign->playerX()},
            {"y", m_campaign->playerY()}
        };
    }

    if (m_mode == Mode::Interior2D && m_interior2D)
    {
        double x = 0.0;
        double y = 0.0;
        double angle = 0.0;
        double pitch = 0.0;
        m_interior2D->getPlayerPose(x, y, angle, pitch);

        j["interior_id"] = m_interior2D->currentInteriorLocationId();
        j["interior_player"] = {
            {"x", x},
            {"y", y},
            {"angle", angle},
            {"pitch", pitch}
        };
    }

    const fs::path p = SaveSlotPath(slotIndex);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        m_saveSlotStatus = T(m_settings.language, "progress_save_failed");
        return false;
    }

    f << j.dump(2);
    const bool saved = f.good();
    m_saveSlotStatus = saveSlotName(slotIndex) + ": " + T(m_settings.language, saved ? "progress_saved" : "progress_save_failed");
    return saved;
}

bool Game::loadProgressFromSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kSaveSlotCount)
        return false;

    const fs::path p = SaveSlotPath(slotIndex);
    std::ifstream f(p, std::ios::binary);
    if (!f)
    {
        m_saveSlotStatus = T(m_settings.language, "progress_missing");
        return false;
    }

    json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        m_saveSlotStatus = T(m_settings.language, "progress_load_failed");
        return false;
    }

    const std::string saveMode = j.value("mode", std::string("campaign"));
    const std::string campaignMap = j.value("campaign_map", std::string());
    const std::string interiorId = ResolveSavedInteriorLocationId(
        j.value("interior_id", std::string(kDefaultHouskaLocation)));
    const json campaignPlayer = j.value("campaign_player", json::object());
    const json interiorPlayer = j.value("interior_player", json::object());

    m_gameMenuOpen = false;
    m_saveSlotMode = SaveSlotMode::None;
    if (m_interior2D)
        leaveInterior2D();
    if (m_campaign)
        leaveCampaign();

    if (saveMode == "interior_2d")
    {
        if (!campaignMap.empty())
            enterCampaign(campaignMap);

        if (m_campaign && campaignPlayer.is_object())
        {
            const float x = campaignPlayer.value("x", m_campaign->playerX());
            const float y = campaignPlayer.value("y", m_campaign->playerY());
            m_campaign->setPlayerPosition(x, y);
        }

        enterInterior2D(interiorId);
        if (!m_interior2D)
        {
            m_gameMenuOpen = isGameplayMode();
            m_saveSlotStatus = T(m_settings.language, "progress_load_failed");
            syncInteriorMouseLookSuppression();
            return false;
        }

        if (interiorPlayer.is_object())
        {
            double x = 0.0;
            double y = 0.0;
            double angle = 0.0;
            double pitch = 0.0;
            m_interior2D->getPlayerPose(x, y, angle, pitch);
            x = interiorPlayer.value("x", x);
            y = interiorPlayer.value("y", y);
            angle = interiorPlayer.value("angle", angle);
            pitch = interiorPlayer.value("pitch", pitch);
            m_interior2D->setPlayerPose(x, y, angle, pitch);
        }
    }
    else
    {
        enterCampaign(campaignMap);
        if (!m_campaign)
        {
            m_gameMenuOpen = false;
            m_saveSlotStatus = T(m_settings.language, "progress_load_failed");
            syncInteriorMouseLookSuppression();
            return false;
        }

        if (campaignPlayer.is_object())
        {
            const float x = campaignPlayer.value("x", m_campaign->playerX());
            const float y = campaignPlayer.value("y", m_campaign->playerY());
            m_campaign->setPlayerPosition(x, y);
        }
    }

    m_gameMenuStatus.clear();
    m_saveSlotStatus.clear();
    syncInteriorMouseLookSuppression();
    return true;
}

bool Game::renameSaveSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kSaveSlotCount)
        return false;

    const fs::path p = SaveSlotPath(slotIndex);
    std::ifstream in(p, std::ios::binary);
    if (!in)
    {
        m_saveSlotStatus = T(m_settings.language, "progress_missing");
        return false;
    }

    json j;
    try
    {
        in >> j;
    }
    catch (...)
    {
        m_saveSlotStatus = T(m_settings.language, "slot_rename_failed");
        return false;
    }

    j["slot_name"] = saveSlotName(slotIndex);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        m_saveSlotStatus = T(m_settings.language, "slot_rename_failed");
        return false;
    }

    out << j.dump(2);
    const bool renamed = out.good();
    m_saveSlotStatus = T(m_settings.language, renamed ? "slot_renamed" : "slot_rename_failed");
    return renamed;
}

void Game::returnToMainMenuFromGame()
{
    m_gameMenuOpen = false;
    m_saveSlotMode = SaveSlotMode::None;
    m_saveSlotStatus.clear();
    if (m_interior2D)
        leaveInterior2D();
    if (m_campaign)
        leaveCampaign();

    m_mode = Mode::Menu;
    syncInteriorMouseLookSuppression();
    m_audio.playMusic("assets/audio/menu.ogg", -1, 400);
}

void Game::renderGameMenuOverlay(int screenW, int screenH)
{
    releaseMouseForGameMenu();
    ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 vp(
        io.DisplaySize.x > 0.0f ? io.DisplaySize.x : (float)screenW,
        io.DisplaySize.y > 0.0f ? io.DisplaySize.y : (float)screenH);

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp, ImGuiCond_Always);
    ImGui::SetNextWindowFocus();

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("##GameMenuOverlay", nullptr, flags);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0.0f, 0.0f), vp, ColorU32(0.0f, 0.0f, 0.0f, 0.66f));

    const float panelW = std::min(500.0f, std::max(330.0f, vp.x - 56.0f));
    const float panelH = m_gameMenuStatus.empty() ? 380.0f : 425.0f;
    const ImVec2 panelSize(panelW, std::min(panelH, std::max(340.0f, vp.y - 56.0f)));
    const ImVec2 panelPos(
        (vp.x - panelSize.x) * 0.5f,
        (vp.y - panelSize.y) * 0.5f);

    DrawMenuFrame(panelPos, panelSize);

    const float innerX = panelPos.x + 38.0f;
    const float innerW = panelSize.x - 76.0f;
    ImGui::SetCursorScreenPos(ImVec2(innerX, panelPos.y + 34.0f));
    ImGui::BeginGroup();

    ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.48f, 1.0f), "%s", T(m_settings.language, "game_menu"));
    ImGui::Dummy(ImVec2(1.0f, 18.0f));

    const ImVec2 btnSize(innerW, 48.0f);
    if (MedievalMenuButton(T(m_settings.language, "save_progress"), btnSize, true) && !m_gameMenuClickHandled)
        triggerGameMenuButton(0);
    storeGameMenuButtonRect(0);

    ImGui::Spacing();
    if (MedievalMenuButton(T(m_settings.language, "load_progress"), btnSize) && !m_gameMenuClickHandled)
        triggerGameMenuButton(1);
    storeGameMenuButtonRect(1);

    ImGui::Spacing();
    if (MedievalMenuButton(T(m_settings.language, "main_menu_from_game"), btnSize) && !m_gameMenuClickHandled)
        triggerGameMenuButton(2);
    storeGameMenuButtonRect(2);

    ImGui::Spacing();
    if (MedievalMenuButton(T(m_settings.language, "quit_game"), btnSize) && !m_gameMenuClickHandled)
        triggerGameMenuButton(3);
    storeGameMenuButtonRect(3);

    if (!m_gameMenuStatus.empty())
    {
        ImGui::Dummy(ImVec2(1.0f, 14.0f));
        WrappedTextAtWidth(m_gameMenuStatus.c_str(), innerW, ImVec4(0.78f, 0.88f, 0.62f, 1.0f));
    }

    ImGui::EndGroup();
    ImGui::End();
    m_gameMenuClickHandled = false;
}

void Game::renderSaveSlotMenu(int screenW, int screenH)
{
    if (m_gameMenuOpen)
        releaseMouseForGameMenu();
    ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 vp(
        io.DisplaySize.x > 0.0f ? io.DisplaySize.x : (float)screenW,
        io.DisplaySize.y > 0.0f ? io.DisplaySize.y : (float)screenH);

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp, ImGuiCond_Always);
    ImGui::SetNextWindowFocus();

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("##SaveSlotOverlay", nullptr, flags);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0.0f, 0.0f), vp, ColorU32(0.0f, 0.0f, 0.0f, 0.68f));

    const float panelW = std::min(900.0f, std::max(620.0f, vp.x - 72.0f));
    const float panelH = std::min(650.0f, std::max(560.0f, vp.y - 72.0f));
    const ImVec2 panelSize(panelW, panelH);
    const ImVec2 panelPos((vp.x - panelSize.x) * 0.5f, (vp.y - panelSize.y) * 0.5f);
    DrawMenuFrame(panelPos, panelSize);

    const float innerX = panelPos.x + 38.0f;
    const float innerY = panelPos.y + 32.0f;
    const float innerW = panelSize.x - 76.0f;
    const char* title = m_saveSlotMode == SaveSlotMode::Save
        ? T(m_settings.language, "save_slots_title")
        : T(m_settings.language, "load_slots_title");

    ImGui::SetCursorScreenPos(ImVec2(innerX, innerY));
    ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.48f, 1.0f), "%s", title);

    const float rowH = 112.0f;
    const float rowGap = 14.0f;
    float rowY = innerY + 58.0f;

    for (int i = 0; i < kSaveSlotCount; ++i)
    {
        const SaveSlotSummary summary = readSaveSlotSummary(i);
        const ImVec2 rowPos(innerX, rowY);
        const ImVec2 rowSize(innerW, rowH);
        dl->AddRectFilled(rowPos, ImVec2(rowPos.x + rowSize.x, rowPos.y + rowSize.y),
            ColorU32(0.07f, 0.052f, 0.034f, 0.82f), 8.0f);
        dl->AddRect(rowPos, ImVec2(rowPos.x + rowSize.x, rowPos.y + rowSize.y),
            summary.exists ? ColorU32(0.82f, 0.64f, 0.34f, 0.42f) : ColorU32(0.58f, 0.48f, 0.34f, 0.24f),
            8.0f, 0, 1.0f);

        ImGui::PushID(i);

        ImGui::SetCursorScreenPos(ImVec2(rowPos.x + 18.0f, rowPos.y + 16.0f));
        ImGui::TextColored(ImVec4(0.82f, 0.74f, 0.56f, 1.0f), "%s %d", U8("Pozice"), i + 1);

        const float inputX = rowPos.x + 118.0f;
        const float actionW = 150.0f;
        const float inputW = std::max(220.0f, rowSize.x - 118.0f - actionW - 42.0f);
        ImGui::SetCursorScreenPos(ImVec2(inputX, rowPos.y + 12.0f));
        ImGui::PushItemWidth(inputW);
        ImGui::InputText("##slotName", m_saveSlotNameBuffers[i], sizeof(m_saveSlotNameBuffers[i]));
        ImGui::PopItemWidth();

        ImGui::SetCursorScreenPos(ImVec2(inputX, rowPos.y + 52.0f));
        if (summary.exists)
        {
            ImGui::TextColored(ImVec4(0.68f, 0.84f, 0.56f, 1.0f), "%s: %s",
                T(m_settings.language, "slot_saved_at"), summary.savedAt.empty() ? "-" : summary.savedAt.c_str());
            ImGui::SetCursorScreenPos(ImVec2(inputX, rowPos.y + 76.0f));
            ImGui::TextColored(ImVec4(0.76f, 0.70f, 0.58f, 1.0f), "%s: %s",
                T(m_settings.language, "slot_location"), summary.location.c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(0.62f, 0.58f, 0.48f, 1.0f), "%s", T(m_settings.language, "slot_empty"));
        }

        ImGui::SetCursorScreenPos(ImVec2(rowPos.x + rowSize.x - actionW - 18.0f, rowPos.y + 18.0f));
        if (m_saveSlotMode == SaveSlotMode::Save)
        {
            if (MedievalMenuButton(T(m_settings.language, "slot_save"), ImVec2(actionW, 44.0f), true))
                requestSaveSlotAction(i);
        }
        else
        {
            const char* loadLabel = summary.exists ? T(m_settings.language, "slot_load") : T(m_settings.language, "slot_empty");
            if (MedievalMenuButton(loadLabel, ImVec2(actionW, 44.0f), summary.exists) && summary.exists)
                requestLoadSlotAction(i);

            if (summary.exists)
            {
                ImGui::SetCursorScreenPos(ImVec2(rowPos.x + rowSize.x - actionW - 18.0f, rowPos.y + 70.0f));
                if (ImGui::Button(T(m_settings.language, "slot_rename"), ImVec2(actionW, 28.0f)))
                    renameSaveSlot(i);
            }
        }

        ImGui::PopID();
        rowY += rowH + rowGap;
    }

    ImGui::SetCursorScreenPos(ImVec2(innerX, panelPos.y + panelSize.y - 66.0f));
    if (MedievalMenuButton(T(m_settings.language, "back"), ImVec2(170.0f, 46.0f)))
        closeSaveSlotMenu();

    if (!m_saveSlotStatus.empty())
    {
        ImGui::SetCursorScreenPos(ImVec2(innerX + 190.0f, panelPos.y + panelSize.y - 54.0f));
        WrappedTextAtWidth(m_saveSlotStatus.c_str(), innerW - 210.0f, ImVec4(0.78f, 0.88f, 0.62f, 1.0f));
    }

    ImGui::End();
}

bool Game::init(SDL_Window* window)
{
    m_window = window;

    toggleFullscreen();

    if (!m_window) return false;

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) return false;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetWindowMinimumSize(m_window, 1024, 1024);

    const int imgFlags = IMG_INIT_PNG;
    if ((IMG_Init(imgFlags) & imgFlags) != imgFlags) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "IMG_Init failed", IMG_GetError(), m_window);
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer);
    ImGui_ImplSDLRenderer2_Init(m_renderer);

    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,
        0x0100, 0x017F,
        0x0180, 0x024F,
        0
    };
    io.Fonts->AddFontFromFileTTF("assets/Fonts/NotoSans-Regular.ttf", 28.0f, nullptr, ranges);

    m_menuBg = loadTexture(m_renderer, "assets/Images/MainMenuImg.png", m_menuBgW, m_menuBgH);

    m_running = true;
    m_mode = Mode::Menu;

    loadSettings();
    loadProfileDraft(m_pendingBackground);

    if (!m_audio.init()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Audio init failed",
            m_audio.lastError().c_str(), m_window);
    }

    applySettingsToRuntime();
    m_audio.playMusic("assets/audio/menu.ogg", -1, 400);

    return true;
}

void Game::shutdown()
{
    leaveInterior2D();
    leaveTextureSpriteEditor();
    leaveObjectEditor();
    leaveEditor();
    leaveCampaign();
    m_audio.shutdown();

    if (m_menuBg) {
        SDL_DestroyTexture(m_menuBg);
        m_menuBg = nullptr;
    }

    if (m_renderer) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }

    IMG_Quit();

    m_window = nullptr;
    m_running = false;
}

void Game::enterEditor()
{
    if (m_editor) return;

    m_editor = new Editor();
    if (!m_editor->init(m_window, m_renderer)) {
        delete m_editor;
        m_editor = nullptr;

        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Editor init failed",
            "Editor::init failed", m_window);
        return;
    }

    m_mode = Mode::Editor;
}

void Game::leaveEditor()
{
    if (!m_editor) return;
    m_editor->shutdown();
    delete m_editor;
    m_editor = nullptr;
    m_mode = Mode::Menu;
}

void Game::enterObjectEditor()
{
    if (m_objectEditor) return;

    m_objectEditor = new ObjectEditor();
    if (!m_objectEditor->init(m_window, m_renderer)) {
        delete m_objectEditor;
        m_objectEditor = nullptr;

        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Object editor init failed",
            "ObjectEditor::init failed", m_window);
        return;
    }

    m_mode = Mode::ObjectEditor;
}

void Game::leaveObjectEditor()
{
    if (!m_objectEditor) return;
    m_objectEditor->shutdown();
    delete m_objectEditor;
    m_objectEditor = nullptr;
    m_mode = Mode::Menu;
}

void Game::enterTextureSpriteEditor()
{
    if (m_textureSpriteEditor) return;

    m_textureSpriteEditor = new TextureSpriteEditor();
    if (!m_textureSpriteEditor->init(m_renderer, ProjectRootPath()))
    {
        delete m_textureSpriteEditor;
        m_textureSpriteEditor = nullptr;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
            "Texture/sprite editor init failed",
            "TextureSpriteEditor::init failed", m_window);
        return;
    }

    m_textureSpriteEditor->open();
    m_mode = Mode::TextureSpriteEditor;
}

void Game::leaveTextureSpriteEditor()
{
    if (!m_textureSpriteEditor) return;
    m_textureSpriteEditor->shutdown();
    delete m_textureSpriteEditor;
    m_textureSpriteEditor = nullptr;
    m_mode = Mode::Menu;
}

void Game::enterInterior2D(const std::string& interiorId, const std::string& spawnId)
{
    if (m_interior2D) return;

    m_interior2D = new BuildInteriorEngine();
    if (!m_interior2D->init(m_window, m_renderer)) {
        const std::string error = m_interior2D->lastError().empty()
            ? std::string("BuildInteriorEngine::init failed")
            : m_interior2D->lastError();
        delete m_interior2D;
        m_interior2D = nullptr;

        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "2.5D interior init failed",
            error.c_str(), m_window);
        return;
    }

    const std::string requestedInterior = interiorId.empty()
        ? std::string(kDefaultHouskaLocation)
        : interiorId;
    if (!m_interior2D->loadInteriorAtSpawn(requestedInterior, spawnId))
    {
        const std::string error = m_interior2D->lastError().empty()
            ? std::string("Cannot load requested 2.5D map: ") + requestedInterior
            : m_interior2D->lastError();
        m_interior2D->shutdown();
        delete m_interior2D;
        m_interior2D = nullptr;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "2.5D map load failed",
            error.c_str(), m_window);
        return;
    }

    m_interior2D->setEditorMode(false);
    m_interior2D->setRuntimeOverlayVisible(m_campaign == nullptr);
    m_mode = Mode::Interior2D;
    syncInteriorMouseLookSuppression();
}

void Game::enterInteriorEditor(const std::string& interiorId)
{
    if (m_interior2D) return;

    m_interior2D = new BuildInteriorEngine();
    if (!m_interior2D->init(m_window, m_renderer)) {
        const std::string error = m_interior2D->lastError().empty()
            ? std::string("BuildInteriorEngine::init failed")
            : m_interior2D->lastError();
        delete m_interior2D;
        m_interior2D = nullptr;

        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "2.5D interior editor init failed",
            error.c_str(), m_window);
        return;
    }

    const std::string requestedInterior = interiorId.empty()
        ? std::string(kDefaultHouskaLocation)
        : interiorId;
    if (!m_interior2D->loadInterior(requestedInterior))
    {
        const std::string error = m_interior2D->lastError().empty()
            ? std::string("Cannot load requested 2.5D editor map: ") + requestedInterior
            : m_interior2D->lastError();
        m_interior2D->shutdown();
        delete m_interior2D;
        m_interior2D = nullptr;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "2.5D editor map load failed",
            error.c_str(), m_window);
        return;
    }

    m_interior2D->setEditorMode(true);
    m_gameMenuOpen = false;
    m_mode = Mode::InteriorEditor;
    syncInteriorMouseLookSuppression();
}

void Game::leaveInterior2D()
{
    if (!m_interior2D) return;
    m_gameMenuOpen = false;
    m_interior2D->shutdown();
    delete m_interior2D;
    m_interior2D = nullptr;
    m_mode = m_campaign ? Mode::Campaign : Mode::Menu;
}

void Game::enterCampaign(const std::string& mapPath, const std::string& spawnId)
{
    if (m_campaign) return;

    m_gameMenuOpen = false;
    campaignflow::SetSelectedBackground(m_pendingBackground);

    m_campaign = new Campaign();
    if (!m_campaign->init(m_window, m_renderer)) {
        delete m_campaign;
        m_campaign = nullptr;

        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Campaign init failed",
            "Campaign::init failed", m_window);
        return;
    }

    if (!mapPath.empty() && !loadCampaignMap(mapPath, spawnId))
    {
        m_campaign->shutdown();
        delete m_campaign;
        m_campaign = nullptr;
        return;
    }

    m_mode = Mode::Campaign;
}

bool Game::loadCampaignMap(const std::string& mapPath, const std::string& spawnId)
{
    if (!m_campaign)
        return false;

    const fs::path target = ResolveCampaignMapPath(mapPath);
    if (m_campaign->loadMap(target.string(), spawnId))
        return true;

    const std::string error = "Cannot load campaign map: " + target.string();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Campaign map load failed",
        error.c_str(), m_window);
    return false;
}

void Game::leaveCampaign()
{
    if (!m_campaign) return;
    m_gameMenuOpen = false;
    m_campaign->shutdown();
    delete m_campaign;
    m_campaign = nullptr;
    m_mode = Mode::Menu;
}

void Game::toggleFullscreen()
{
    if (!m_window) return;

    Uint32 flags = SDL_GetWindowFlags(m_window);
    bool isFullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;

    SDL_SetWindowFullscreen(m_window, isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void Game::handleEvent(const SDL_Event& e)
{
    ImGui_ImplSDL2_ProcessEvent(&e);

    if (e.type == SDL_QUIT) {
        m_running = false;
        return;
    }

    if (m_mode == Mode::Settings && !m_pendingControlBinding.empty() &&
        e.type == SDL_KEYDOWN && e.key.repeat == 0)
    {
        if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
        {
            m_pendingControlBinding.clear();
            return;
        }

        if (SDL_Scancode* binding = gamecontrols::BindingByAction(
                m_settings.controls, m_pendingControlBinding))
        {
            *binding = e.key.keysym.scancode;
            gamecontrols::Set(m_settings.controls);
        }
        m_pendingControlBinding.clear();
        return;
    }

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
        toggleFullscreen();
        return;
    }

    if (e.type == SDL_KEYDOWN && e.key.repeat == 0 && e.key.keysym.sym == SDLK_ESCAPE) {
        if (m_saveSlotMode != SaveSlotMode::None) {
            closeSaveSlotMenu();
        }
        else if (m_gameMenuOpen) {
            closeGameMenu();
        }
        else if (m_mode == Mode::Campaign || m_mode == Mode::Interior2D) {
            openGameMenu();
        }
        else if (m_mode == Mode::Editor) {
            leaveEditor();
        }
        else if (m_mode == Mode::ObjectEditor) {
            leaveObjectEditor();
        }
        else if (m_mode == Mode::TextureSpriteEditor) {
            leaveTextureSpriteEditor();
        }
        else if (m_mode == Mode::InteriorEditor) {
            leaveInterior2D();
        }
        else if (m_mode == Mode::StyleSelect || m_mode == Mode::Settings || m_mode == Mode::ProfileEditor) {
            m_mode = Mode::Menu;
        }
        else {
            m_mode = Mode::Menu;
        }
        return;
    }

    if (m_saveSlotMode != SaveSlotMode::None)
        return;

    if (m_gameMenuOpen)
    {
        handleGameMenuMouseEvent(e);
        return;
    }

    if (m_mode == Mode::Campaign && m_campaign)
    {
        m_campaign->handleEvent(e);
        return;
    }
    else if (m_mode == Mode::Interior2D && m_campaign)
    {
        if (m_campaign->handleSharedUiEvent(e))
        {
            syncInteriorMouseLookSuppression();
            return;
        }

        syncInteriorMouseLookSuppression();
        if (m_campaign->sharedUiBlocksMouseLook())
            return;
    }

    if (m_mode == Mode::Editor && m_editor) {
        m_editor->handleEvent(e);
    }
    else if (m_mode == Mode::ObjectEditor && m_objectEditor) {
        m_objectEditor->handleEvent(e);
    }
    else if (m_mode == Mode::TextureSpriteEditor && m_textureSpriteEditor) {
        m_textureSpriteEditor->handleEvent(e);
    }
    else if ((m_mode == Mode::Interior2D || m_mode == Mode::InteriorEditor) && m_interior2D) {
        m_interior2D->handleEvent(e);
    }
}

void Game::update(float dt)
{
    if (dt > 0.000001f)
    {
        const float instantFps = 1.0f / dt;
        const float instantMs = dt * 1000.0f;
        const float blend = 1.0f - std::exp(-std::max(0.0f, dt) * 5.0f);
        if (m_smoothedFps <= 0.0f)
        {
            m_smoothedFps = instantFps;
            m_smoothedFrameMs = instantMs;
        }
        else
        {
            m_smoothedFps += (instantFps - m_smoothedFps) * blend;
            m_smoothedFrameMs += (instantMs - m_smoothedFrameMs) * blend;
        }
    }

    processPendingGameMenuAction();

    syncInteriorMouseLookSuppression();
    if (m_gameMenuOpen && isGameplayMode())
        return;

    if (m_mode == Mode::Editor && m_editor) {
        m_editor->update(dt);
    }
    else if (m_mode == Mode::ObjectEditor && m_objectEditor) {
        m_objectEditor->update(dt);
    }
    else if (m_mode == Mode::TextureSpriteEditor && m_textureSpriteEditor) {
        m_textureSpriteEditor->update(dt);
    }
    else if ((m_mode == Mode::Interior2D || m_mode == Mode::InteriorEditor) && m_interior2D) {
        m_interior2D->update(dt);

        if (m_mode == Mode::Interior2D)
        {
            if (m_campaign)
                m_campaign->updateSharedRuntime(dt);

            std::string campaignMap;
            std::string campaignSpawn;
            if (m_interior2D->consumePendingCampaignTransition(campaignMap, campaignSpawn))
            {
                if (m_campaign)
                {
                    if (loadCampaignMap(campaignMap, campaignSpawn))
                        leaveInterior2D();
                }
                else
                {
                    leaveInterior2D();
                    enterCampaign(campaignMap, campaignSpawn);
                }
            }
        }
    }
    else if (m_mode == Mode::Campaign && m_campaign) {
        m_campaign->update(dt);

        std::string interiorId;
        std::string interiorSpawn;
        if (m_campaign->consumePendingInteriorTransition(interiorId, interiorSpawn))
            enterInterior2D(interiorId, interiorSpawn);
    }
}

void Game::render()
{
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Gameplay/editor UI uses a smaller readable workspace scale.
    // Main menu/settings stay at 100 % so the styled menu keeps its composition.
    ImGui::GetIO().FontGlobalScale =
        (m_saveSlotMode != SaveSlotMode::None || m_gameMenuOpen || m_mode == Mode::Menu || m_mode == Mode::StyleSelect || m_mode == Mode::Settings || m_mode == Mode::ProfileEditor || m_mode == Mode::TextureSpriteEditor)
        ? 1.0f
        : 0.70f;

    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);

    int screenW = 0, screenH = 0;
    SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);

    if (m_mode == Mode::Menu || m_mode == Mode::StyleSelect || m_mode == Mode::Settings || m_mode == Mode::ProfileEditor)
    {
        DrawMenuBackground(m_renderer, m_menuBg, m_menuBgW, m_menuBgH, screenW, screenH);

        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 vp = io.DisplaySize;

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(vp, ImGuiCond_Always);

        ImGuiWindowFlags wflags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoBackground;

        ImGui::Begin("##MainMenuOverlay", nullptr, wflags);

        const float titleX = std::max(52.0f, vp.x * 0.055f);
        const float titleY = std::max(34.0f, vp.y * 0.050f);

        DrawTextShadow(ImVec2(titleX, titleY), T(m_settings.language, "title"), ColorU32(0.94f, 0.78f, 0.43f, 1.0f), 2.05f);
        ImGui::SetCursorPos(ImVec2(titleX + 4.0f, titleY + 74.0f));
        ImGui::TextColored(ImVec4(0.82f, 0.74f, 0.58f, 0.92f), "%s", T(m_settings.language, "subtitle"));

        if (m_mode == Mode::Menu)
        {
            const ImVec2 panelSize(450.0f, 790.0f);
            const ImVec2 panelPos(
                (vp.x - panelSize.x) * 0.5f,
                std::max(170.0f, (vp.y - panelSize.y) * 0.52f));

            DrawMenuFrame(panelPos, panelSize);
            ImGui::SetCursorScreenPos(ImVec2(panelPos.x + 38.0f, panelPos.y + 32.0f));
            ImGui::BeginGroup();

            ImGui::TextColored(ImVec4(0.86f, 0.76f, 0.54f, 1.0f), "%s", T(m_settings.language, "main_menu"));
            ImGui::TextColored(ImVec4(0.62f, 0.58f, 0.48f, 1.0f), "%s", T(m_settings.language, "choose_path"));
            ImGui::Spacing();
            ImGui::Spacing();

            const ImVec2 btnSize(374.0f, 48.0f);
            if (MedievalMenuButton(T(m_settings.language, "start_campaign"), btnSize, true))
                m_mode = Mode::StyleSelect;

            ImGui::Spacing();
            if (MedievalMenuButton(T(m_settings.language, "load_game"), btnSize, true))
                openSaveSlotMenu(SaveSlotMode::Load);

            ImGui::Spacing();
            if (MedievalMenuButton(T(m_settings.language, "level_editor"), btnSize))
                enterEditor();

            ImGui::Spacing();
            if (MedievalMenuButton(T(m_settings.language, "object_editor"), btnSize))
                enterObjectEditor();

            ImGui::Spacing();
            if (MedievalMenuButton(T(m_settings.language, "texture_sprite_editor"), btnSize))
                enterTextureSpriteEditor();

            ImGui::Spacing();
            if (MedievalMenuButton(T(m_settings.language, "interior_2d"), btnSize))
                enterInterior2D(kDefaultHouskaLocation);

            ImGui::Spacing();
            if (MedievalMenuButton(T(m_settings.language, "interior_editor"), btnSize))
                enterInteriorEditor(kDefaultHouskaLocation);

            ImGui::Spacing();
            if (MedievalMenuButton(T(m_settings.language, "profile_editor"), btnSize))
            {
                loadProfileDraft(m_pendingBackground);
                m_mode = Mode::ProfileEditor;
            }

            ImGui::Spacing();
            if (MedievalMenuButton(T(m_settings.language, "settings"), btnSize))
                m_mode = Mode::Settings;

            ImGui::Spacing();
            if (MedievalMenuButton(T(m_settings.language, "quit"), btnSize))
                m_running = false;

            ImGui::Dummy(ImVec2(1.0f, 8.0f));
            ImGui::TextColored(ImVec4(0.62f, 0.58f, 0.48f, 0.92f), "%s", T(m_settings.language, "hotkeys"));

            ImGui::EndGroup();
        }
        else if (m_mode == Mode::StyleSelect)
        {
            const ImVec2 panelSize(std::min(980.0f, vp.x - 90.0f), std::min(720.0f, vp.y - 145.0f));
            const ImVec2 panelPos((vp.x - panelSize.x) * 0.5f, std::max(120.0f, (vp.y - panelSize.y) * 0.58f));
            DrawMenuFrame(panelPos, panelSize);

            ImGui::SetCursorScreenPos(ImVec2(panelPos.x + 34.0f, panelPos.y + 28.0f));
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.48f, 1.0f), "%s", T(m_settings.language, "style_title"));
            ImGui::TextColored(ImVec4(0.70f, 0.65f, 0.52f, 1.0f), "%s", T(m_settings.language, "style_subtitle"));
            ImGui::EndGroup();

            const PlayerStats::Background options[3] = {
                PlayerStats::Background::Survivalist,
                PlayerStats::Background::ScholarAthlete,
                PlayerStats::Background::SocialAdaptable
            };

            const float rowW = panelSize.x - 68.0f;
            const float rowH = 158.0f;
            const float gap = 14.0f;
            float y = panelPos.y + 92.0f;

            for (int i = 0; i < 3; ++i)
            {
                ImGui::SetCursorScreenPos(ImVec2(panelPos.x + 34.0f, y));
                BackgroundRowCard(options[i], m_pendingBackground, ImVec2(rowW, rowH));
                y += rowH + gap;
            }

            ImGui::SetCursorScreenPos(ImVec2(panelPos.x + 34.0f, panelPos.y + panelSize.y - 70.0f));
            if (MedievalMenuButton(T(m_settings.language, "back"), ImVec2(180.0f, 52.0f)))
                m_mode = Mode::Menu;

            ImGui::SameLine();
            if (MedievalMenuButton(T(m_settings.language, "enter_campaign"), ImVec2(320.0f, 52.0f), true))
                enterCampaign();
        }
        else if (m_mode == Mode::ProfileEditor)
        {
            if (!m_profileDraftLoaded)
                loadProfileDraft(m_pendingBackground);

            const ImVec2 panelSize(std::min(1080.0f, vp.x - 90.0f), std::min(790.0f, vp.y - 120.0f));
            const ImVec2 panelPos((vp.x - panelSize.x) * 0.5f, std::max(96.0f, (vp.y - panelSize.y) * 0.56f));
            DrawMenuFrame(panelPos, panelSize);

            const float innerX = panelPos.x + 36.0f;
            const float innerY = panelPos.y + 30.0f;
            const float innerW = panelSize.x - 72.0f;
            const float innerH = panelSize.y - 60.0f;

            ImGui::SetCursorScreenPos(ImVec2(innerX, innerY));
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.48f, 1.0f), "%s", T(m_settings.language, "profile_title"));
            WrappedTextAtWidth(T(m_settings.language, "profile_subtitle"), innerW, ImVec4(0.70f, 0.65f, 0.52f, 1.0f));
            ImGui::EndGroup();

            const float topY = innerY + 74.0f;
            const float leftW = 250.0f;
            const float rightX = innerX + leftW + 28.0f;
            const float rightW = innerW - leftW - 28.0f;
            const float contentH = innerH - 150.0f;

            ImGui::SetCursorScreenPos(ImVec2(innerX, topY));
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.86f, 0.76f, 0.54f, 1.0f), U8("Profil"));
            ImGui::Dummy(ImVec2(1.0f, 8.0f));

            const PlayerStats::Background profileOptions[3] = {
                PlayerStats::Background::Survivalist,
                PlayerStats::Background::ScholarAthlete,
                PlayerStats::Background::SocialAdaptable
            };

            for (PlayerStats::Background bg : profileOptions)
            {
                const PlayerStats::Background before = m_profileEditBackground;
                ProfileBackgroundButton(bg, m_profileEditBackground, ImVec2(leftW, 48.0f));
                if (before != m_profileEditBackground)
                    loadProfileDraft(m_profileEditBackground);
                ImGui::Dummy(ImVec2(1.0f, 8.0f));
            }

            ImGui::Dummy(ImVec2(1.0f, 14.0f));
            ProfileDerivedPreview(m_profileDraft);
            ImGui::Dummy(ImVec2(1.0f, 14.0f));
            WrappedTextAtWidth(T(m_settings.language, "profile_hint"), leftW, ImVec4(0.70f, 0.66f, 0.55f, 1.0f));

            if (!m_profileStatus.empty())
            {
                ImGui::Dummy(ImVec2(1.0f, 8.0f));
                ImGui::TextColored(ImVec4(0.66f, 0.92f, 0.58f, 1.0f), "%s", m_profileStatus.c_str());
            }
            ImGui::EndGroup();

            ImGui::SetCursorScreenPos(ImVec2(rightX, topY));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.035f, 0.027f, 0.020f, 0.62f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.72f, 0.56f, 0.30f, 0.26f));
            ImGui::BeginChild("##profile_stats_child", ImVec2(rightW, contentH), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

            if (DrawProfileEditorStats(m_profileDraft))
            {
                m_profileDirty = true;
                m_profileStatus.clear();
            }

            ImGui::EndChild();
            ImGui::PopStyleColor(2);

            ImGui::SetCursorScreenPos(ImVec2(innerX, panelPos.y + panelSize.y - 72.0f));
            if (MedievalMenuButton(T(m_settings.language, "back"), ImVec2(190.0f, 52.0f)))
                m_mode = Mode::Menu;

            ImGui::SameLine();
            if (MedievalMenuButton(T(m_settings.language, "profile_reset"), ImVec2(210.0f, 52.0f)))
                resetProfileDraft();

            ImGui::SameLine();
            if (MedievalMenuButton(T(m_settings.language, "profile_save"), ImVec2(210.0f, 52.0f), true))
                saveProfileDraft();

            ImGui::SameLine();
            if (MedievalMenuButton(T(m_settings.language, "profile_use"), ImVec2(230.0f, 52.0f), true))
            {
                if (m_profileDirty)
                    saveProfileDraft();
                m_pendingBackground = m_profileEditBackground;
                campaignflow::SetSelectedBackground(m_pendingBackground);
                m_profileStatus = U8("Profil je připravený pro novou kampaň.");
            }
        }

        else if (m_mode == Mode::Settings)
        {
            const float panelHeight = std::max(620.0f, std::min(790.0f, vp.y - 100.0f));
            const ImVec2 panelSize(760.0f, panelHeight);
            const ImVec2 panelPos((vp.x - panelSize.x) * 0.5f, std::max(55.0f, (vp.y - panelSize.y) * 0.55f));
            DrawMenuFrame(panelPos, panelSize);

            const float innerX = panelPos.x + 46.0f;
            const float innerW = panelSize.x - 92.0f;
            ImGui::SetCursorScreenPos(ImVec2(innerX, panelPos.y + 34.0f));
            ImGui::BeginGroup();

            ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.48f, 1.0f), "%s", T(m_settings.language, "settings_title"));
            WrappedTextAtWidth(T(m_settings.language, "settings_subtitle"), innerW, ImVec4(0.70f, 0.65f, 0.52f, 1.0f));
            ImGui::Dummy(ImVec2(1.0f, 14.0f));

            {
                const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                ImGui::TextColored(ImVec4(0.86f, 0.76f, 0.54f, 1.0f), "%s", T(m_settings.language, "language"));
                ImGui::SetCursorScreenPos(ImVec2(rowStart.x + 155.0f, rowStart.y - 2.0f));
                StyledLanguageDropdown("game_language", m_settings.language, std::max(120.0f, innerW - 155.0f));
                ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowStart.y + 44.0f));
            }

            if (ImGui::BeginTabBar("##settings_tabs"))
            {
                if (ImGui::BeginTabItem(T(m_settings.language, "audio")))
                {
                    ImGui::Dummy(ImVec2(1.0f, 12.0f));
                    if (SettingsSliderRow(T(m_settings.language, "music"), &m_settings.musicVolume, innerW))
                        applySettingsToRuntime();
                    SettingsSliderRow(T(m_settings.language, "sfx"), &m_settings.sfxVolume, innerW);
                    SettingsSliderRow(T(m_settings.language, "voice"), &m_settings.voiceVolume, innerW);
                    ImGui::Dummy(ImVec2(1.0f, 16.0f));
                    WrappedTextAtWidth(T(m_settings.language, "audio_note"), innerW, ImVec4(0.86f, 0.82f, 0.74f, 1.0f));
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(T(m_settings.language, "controls")))
                {
                    ImGui::BeginChild("##controls_scroll", ImVec2(innerW, panelSize.y - 300.0f), false);
                    ImGui::Dummy(ImVec2(1.0f, 8.0f));
                    ControlBindingRow(T(m_settings.language, "move_forward"), "move_forward", m_settings.controls.moveForward, m_pendingControlBinding, innerW, m_settings.language);
                    ControlBindingRow(T(m_settings.language, "move_backward"), "move_backward", m_settings.controls.moveBackward, m_pendingControlBinding, innerW, m_settings.language);
                    ControlBindingRow(T(m_settings.language, "move_left"), "move_left", m_settings.controls.moveLeft, m_pendingControlBinding, innerW, m_settings.language);
                    ControlBindingRow(T(m_settings.language, "move_right"), "move_right", m_settings.controls.moveRight, m_pendingControlBinding, innerW, m_settings.language);
                    ControlBindingRow(T(m_settings.language, "run"), "run", m_settings.controls.run, m_pendingControlBinding, innerW, m_settings.language);
                    ControlBindingRow(T(m_settings.language, "jump"), "jump", m_settings.controls.jump, m_pendingControlBinding, innerW, m_settings.language);
                    ControlBindingRow(T(m_settings.language, "use"), "use", m_settings.controls.use, m_pendingControlBinding, innerW, m_settings.language);
                    ControlBindingRow(T(m_settings.language, "inventory"), "inventory", m_settings.controls.inventory, m_pendingControlBinding, innerW, m_settings.language);
                    ControlBindingRow(T(m_settings.language, "journal"), "journal", m_settings.controls.journal, m_pendingControlBinding, innerW, m_settings.language);

                    ImGui::Separator();
                    SettingsFloatSliderRow(T(m_settings.language, "mouse_sensitivity"), &m_settings.controls.mouseSensitivity, innerW, 0.15f, 4.0f);
                    ImGui::Checkbox(T(m_settings.language, "invert_mouse_y"), &m_settings.controls.invertMouseY);
                    ImGui::Checkbox(T(m_settings.language, "always_mouse_look"), &m_settings.controls.alwaysMouseLook);
                    ImGui::Dummy(ImVec2(1.0f, 10.0f));
                    WrappedTextAtWidth(T(m_settings.language, "controls_note"), innerW, ImVec4(0.86f, 0.82f, 0.74f, 1.0f));
                    ImGui::Dummy(ImVec2(1.0f, 12.0f));
                    if (MedievalMenuButton(T(m_settings.language, "reset_controls"), ImVec2(220.0f, 42.0f)))
                    {
                        m_settings.controls = gamecontrols::Settings{};
                        m_pendingControlBinding.clear();
                        gamecontrols::Set(m_settings.controls);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(T(m_settings.language, "performance")))
                {
                    ImGui::Dummy(ImVec2(1.0f, 12.0f));
                    ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.48f, 1.0f), "%s", T(m_settings.language, "fps_now"));
                    ImGui::Dummy(ImVec2(1.0f, 8.0f));
                    ImGui::Text("FPS: %.1f", m_smoothedFps);
                    ImGui::Text("Frame time: %.2f ms", m_smoothedFrameMs);
                    ImGui::Dummy(ImVec2(1.0f, 10.0f));
                    ImGui::Checkbox(T(m_settings.language, "show_fps"), &m_settings.showFps);
                    ImGui::Dummy(ImVec2(1.0f, 12.0f));
                    WrappedTextAtWidth(T(m_settings.language, "fps_note"), innerW, ImVec4(0.86f, 0.82f, 0.74f, 1.0f));
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::Dummy(ImVec2(1.0f, 18.0f));
            if (MedievalMenuButton(T(m_settings.language, "back"), ImVec2(230.0f, 52.0f)))
            {
                m_pendingControlBinding.clear();
                m_mode = Mode::Menu;
            }

            ImGui::SameLine();
            if (MedievalMenuButton(T(m_settings.language, "save"), ImVec2(230.0f, 52.0f), true))
            {
                applySettingsToRuntime();
                saveSettings();
                m_pendingControlBinding.clear();
                m_mode = Mode::Menu;
            }

            ImGui::EndGroup();
        }

        ImGui::End();
    }
    else if (m_mode == Mode::Editor)
    {
        if (m_editor)
            m_editor->render();
    }
    else if (m_mode == Mode::ObjectEditor)
    {
        if (m_objectEditor)
            m_objectEditor->render();
    }
    else if (m_mode == Mode::TextureSpriteEditor)
    {
        if (m_textureSpriteEditor)
        {
            m_textureSpriteEditor->render();
            if (!m_textureSpriteEditor->isOpen())
                leaveTextureSpriteEditor();
        }
    }
    else if (m_mode == Mode::Interior2D || m_mode == Mode::InteriorEditor)
    {
        if (m_interior2D)
        {
            m_interior2D->render();
            if (m_mode == Mode::Interior2D && m_campaign)
                m_campaign->renderSharedHudOverlay();
        }
    }
    else if (m_mode == Mode::Campaign)
    {
        if (m_campaign)
            m_campaign->render();
    }

    if (m_saveSlotMode != SaveSlotMode::None)
        renderSaveSlotMenu(screenW, screenH);
    else if (m_gameMenuOpen)
        renderGameMenuOverlay(screenW, screenH);

    if (m_settings.showFps)
    {
        ImGuiIO& fpsIo = ImGui::GetIO();
        char fpsText[96];
        std::snprintf(fpsText, sizeof(fpsText), "FPS %.1f  |  %.2f ms", m_smoothedFps, m_smoothedFrameMs);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const ImVec2 textSize = ImGui::CalcTextSize(fpsText);
        const ImVec2 pos(std::max(8.0f, fpsIo.DisplaySize.x - textSize.x - 18.0f), 12.0f);
        fg->AddRectFilled(ImVec2(pos.x - 8.0f, pos.y - 5.0f),
                          ImVec2(pos.x + textSize.x + 8.0f, pos.y + textSize.y + 5.0f),
                          IM_COL32(10, 10, 12, 178), 5.0f);
        fg->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 220), fpsText);
        fg->AddText(pos, IM_COL32(230, 210, 155, 255), fpsText);
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);

    SDL_RenderPresent(m_renderer);
}
