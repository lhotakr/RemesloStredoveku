#include "Campaign.h"
#include "../JsonUtils.h"
#include <nlohmann/json.hpp>
#include <SDL_image.h>
#include "../PathUtils.h"

using json = nlohmann::json;

bool Campaign::loadHudAtlas(const std::string& imagePath, const std::string& jsonPath, HudAtlas& outAtlas)
{
    destroyHudAtlas(outAtlas);

    int w = 0, h = 0;
    SDL_Surface* surf = IMG_Load(imagePath.c_str());
    if (!surf)
    {
        SDL_Log("HUD image load failed: %s | IMG_Load: %s", imagePath.c_str(), IMG_GetError());
        return false;
    }

    w = surf->w;
    h = surf->h;

    outAtlas.texture = SDL_CreateTextureFromSurface(m_renderer, surf);
    SDL_FreeSurface(surf);

    if (!outAtlas.texture)
    {
        SDL_Log("HUD texture create failed: %s | SDL: %s", imagePath.c_str(), SDL_GetError());
        return false;
    }

    outAtlas.imageW = w;
    outAtlas.imageH = h;

    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(jsonPath, root, err))
    {
        SDL_Log("HUD json load failed: %s | %s", jsonPath.c_str(), err.c_str());
        return false;
    }

    outAtlas.cols = root.value("cols", 1);
    outAtlas.rows = root.value("rows", 1);

    if (!root.contains("frames") || !root["frames"].is_array())
    {
        SDL_Log("HUD json missing frames array: %s", jsonPath.c_str());
        return false;
    }

    for (const auto& fr : root["frames"])
    {
        SDL_Rect r{};
        r.x = fr.value("x", 0);
        r.y = fr.value("y", 0);
        r.w = fr.value("w", 0);
        r.h = fr.value("h", 0);

        if (r.w > 0 && r.h > 0)
            outAtlas.frames.push_back(r);
    }

    if (outAtlas.frames.empty())
    {
        SDL_Log("HUD atlas has no valid frames: %s", jsonPath.c_str());
        return false;
    }

    return true;
}

void Campaign::destroyHudAtlas(HudAtlas& atlas)
{
    if (atlas.texture)
        SDL_DestroyTexture(atlas.texture);

    atlas.texture = nullptr;
    atlas.frames.clear();
    atlas.imageW = 0;
    atlas.imageH = 0;
}

bool Campaign::loadHudAssets()
{
    const std::string base =
        (pathutils::ProjectRoot() / "assets" / "gui").string() + "/";

    if (!loadHudAtlas(base + "MoonCycle.png", base + "MoonCycle.json", m_hudMoon))
        return false;

    if (!loadHudAtlas(base + "DayCycle.png", base + "DayCycle.json", m_hudDay))
        return false;

    if (!loadHudAtlas(base + "StatusIcons.png", base + "StatusIcons.json", m_hudStatus))
        return false;

    return true;
}

void Campaign::destroyHudAssets()
{
    destroyHudAtlas(m_hudMoon);
    destroyHudAtlas(m_hudDay);
    destroyHudAtlas(m_hudStatus);
}

int Campaign::hudDayRowIndex() const
{
    switch (getDynamicDayPhase())
    {
    case DayPhase::Dawn:      return 0; // svitani
    case DayPhase::Morning:   return 1; // rano
    case DayPhase::Forenoon:  return 2; // dopoledne
    case DayPhase::Noon:      return 3; // poledne
    case DayPhase::Afternoon: return 4; // odpoledne
    case DayPhase::LateDay:   return 5; // podvecer
    case DayPhase::Evening:   return 6; // vecer
    case DayPhase::Night:     return -1; // noc = pouzij moonCycle
    }
    return -1;
}

int Campaign::hudMoonFrameIndex() const
{
    const auto& now = m_gameTime.now();
    const auto moon = m_moonCycle.getMoonInfo(now.day, now.month, now.year);

    std::string p = moon.phase;
    std::transform(p.begin(), p.end(), p.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });

    if (p == "new_moon" || p == "new moon" || p == "nov")
        return 0;

    if (p == "waxing_crescent" || p == "waxing crescent" ||
        p == "dorustajici_srpek" || p == "dorustajici srpek")
        return 1;

    if (p == "first_quarter" || p == "first quarter" ||
        p == "prvni_ctvrt" || p == "prvni ctvrt" || p == "první čtvrť")
        return 2;

    if (p == "waxing_gibbous" || p == "waxing gibbous" ||
        p == "dorustajici_mesic" || p == "dorustajici mesic" || p == "dorůstající měsíc")
        return 3;

    if (p == "full_moon" || p == "full moon" || p == "uplnek" || p == "úplněk")
        return 4;

    if (p == "waning_gibbous" || p == "waning gibbous" ||
        p == "couvajici_mesic" || p == "couvajici mesic" || p == "couvající měsíc")
        return 5;

    if (p == "last_quarter" || p == "last quarter" ||
        p == "posledni_ctvrt" || p == "posledni ctvrt" || p == "poslední čtvrť")
        return 6;

    if (p == "waning_crescent" || p == "waning crescent" ||
        p == "ubyvajici_srpek" || p == "ubyvajici srpek" ||
        p == "couvajici srpek" || p == "couvající srpek")
        return 7;

    SDL_Log("Unknown moon phase for HUD: %s", moon.phase.c_str());
    return 0;
}

int Campaign::hudStatusFrameIndex(float value01, bool invert) const
{
    value01 = std::clamp(value01, 0.0f, 1.0f);
    if (invert)
        value01 = 1.0f - value01;

    if (value01 >= 0.75f) return 0;
    if (value01 >= 0.50f) return 1;
    if (value01 >= 0.25f) return 2;
    return 3;
}

void Campaign::drawHudFrame(const HudAtlas& atlas, int frameIndex, int x, int y, float scale) const
{
    if (!atlas.texture || frameIndex < 0 || frameIndex >= (int)atlas.frames.size())
        return;

    const SDL_Rect src = atlas.frames[frameIndex];
    SDL_Rect dst{
        x, y,
        (int)std::lround(src.w * scale),
        (int)std::lround(src.h * scale)
    };

    SDL_RenderCopy(m_renderer, atlas.texture, &src, &dst);
}

int Campaign::hudDayAnimatedFrameIndex() const
{
    const int row = hudDayRowIndex();
    if (row < 0)
        return -1;

    const int cols = std::max(1, m_hudDay.cols);
    const int animCol = ((int)(m_dayHudAnimTime * 0.2f)) % cols;
    return row * cols + animCol;
}


static Uint8 ClampAlpha01(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return (Uint8)std::lround(t * 255.0f);
}

void Campaign::drawHudFrameAlpha(const HudAtlas& atlas, int frameIndex, int x, int y, float scale, Uint8 alpha) const
{
    if (!atlas.texture || frameIndex < 0 || frameIndex >= (int)atlas.frames.size())
        return;

    const SDL_Rect src = atlas.frames[frameIndex];
    SDL_Rect dst{
        x, y,
        (int)std::lround(src.w * scale),
        (int)std::lround(src.h * scale)
    };

    SDL_SetTextureBlendMode(atlas.texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(atlas.texture, alpha);
    SDL_RenderCopy(m_renderer, atlas.texture, &src, &dst);
    SDL_SetTextureAlphaMod(atlas.texture, 255);
}

bool Campaign::getHudDayBlendFrames(int& outA, int& outB, float& outT) const
{
    const int row = hudDayRowIndex();
    if (row < 0)
        return false;

    const int cols = std::max(1, m_hudDay.cols);
    const float anim = m_dayHudAnimTime * 0.2f;
    const int baseCol = ((int)std::floor(anim)) % cols;
    const int nextCol = (baseCol + 1) % cols;
    const float t = anim - std::floor(anim);

    outA = row * cols + baseCol;
    outB = row * cols + nextCol;
    outT = t;
    return true;
}

float Campaign::hudHeat01() const
{
    const float t = std::clamp(m_player.stats.condition.bodyTemperature, 0.0f, 100.0f);
    return std::clamp((t - 50.0f) / 50.0f, 0.0f, 1.0f);
}

float Campaign::hudCold01() const
{
    const float t = std::clamp(m_player.stats.condition.bodyTemperature, 0.0f, 100.0f);
    return std::clamp((50.0f - t) / 50.0f, 0.0f, 1.0f);
}

int Campaign::hudStatusColumnIndex(float value01) const
{
    value01 = std::clamp(value01, 0.0f, 1.0f);

    const int cols = std::max(1, m_hudStatus.cols);
    const float inv = 1.0f - value01;

    int col = (int)std::floor(inv * cols);
    if (col >= cols)
        col = cols - 1;

    return col;
}

int Campaign::hudStatusFrameAt(int row, int col) const
{
    const int cols = std::max(1, m_hudStatus.cols);
    const int rows = std::max(1, m_hudStatus.rows);

    row = std::clamp(row, 0, rows - 1);
    col = std::clamp(col, 0, cols - 1);

    return row * cols + col;
}

static const char* DayPhaseToStringCz(DayPhase phase)
{
    switch (phase)
    {
    case DayPhase::Dawn:      return "Svítání";
    case DayPhase::Morning:   return "Ráno";
    case DayPhase::Forenoon:  return "Dopoledne";
    case DayPhase::Noon:      return "Poledne";
    case DayPhase::Afternoon: return "Odpoledne";
    case DayPhase::LateDay:   return "Podvečer";
    case DayPhase::Evening:   return "Večer";
    case DayPhase::Night:     return "Noc";
    }
    return "Chyba v matrixu";
}

static const char* MonthNameCzGenitive(int month)
{
    switch (month)
    {
    case 1:  return "ledna";
    case 2:  return "února";
    case 3:  return "března";
    case 4:  return "dubna";
    case 5:  return "května";
    case 6:  return "června";
    case 7:  return "července";
    case 8:  return "srpna";
    case 9:  return "září";
    case 10: return "října";
    case 11: return "listopadu";
    case 12: return "prosince";
    default: return "";
    }
}

void Campaign::renderHud()
{
    const int pad = 29;
    const float iconScale = 0.75f;

    int screenW = 0, screenH = 0;
    SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);

    bool tooltipShown = false;

    auto drawHudIconWithTooltip = [&](const HudAtlas& atlas,
                                      int frameIndex,
                                      int x,
                                      int y,
                                      float scale,
                                      const std::string& label)
    {
        if (!atlas.texture || frameIndex < 0 || frameIndex >= (int)atlas.frames.size())
            return;

        const SDL_Rect src = atlas.frames[frameIndex];
        SDL_Rect dst{
            x, y,
            (int)std::lround(src.w * scale),
            (int)std::lround(src.h * scale)
        };

        SDL_RenderCopy(m_renderer, atlas.texture, &src, &dst);

        if (!tooltipShown &&
            mouseX >= dst.x && mouseX < dst.x + dst.w &&
            mouseY >= dst.y && mouseY < dst.y + dst.h)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(label.c_str());
            ImGui::EndTooltip();
            tooltipShown = true;
        }
    };

    // ===== STATUSY =====
    const float health01  = std::clamp(m_player.stats.condition.health / 100.0f, 0.0f, 1.0f);
    const float stamina01 = std::clamp(m_player.stats.condition.stamina / 100.0f, 0.0f, 1.0f);
    const float hunger01  = std::clamp(m_player.stats.legacyHunger() / 100.0f, 0.0f, 1.0f);
    const float thirst01  = std::clamp(m_player.stats.legacyThirst() / 100.0f, 0.0f, 1.0f);
    const float hygiene01 = std::clamp(m_player.stats.condition.hygiene / 100.0f, 0.0f, 1.0f);
    const float stress01  = std::clamp(m_player.stats.condition.stress / 100.0f, 0.0f, 1.0f);

    const float temp = std::clamp(m_player.stats.condition.bodyTemperature, 0.0f, 100.0f);
    const float heat01 = std::clamp((temp - 50.0f) / 50.0f, 0.0f, 1.0f);
    const float cold01 = std::clamp((50.0f - temp) / 50.0f, 0.0f, 1.0f);

    const int x = pad;
    const int y = pad;
    const int stepY = 85;

    int rowY = y;

    auto drawStatusRow = [&](int atlasRow, int col, const std::string& label)
    {
        drawHudIconWithTooltip(
            m_hudStatus,
            hudStatusFrameAt(atlasRow, col),
            x,
            rowY,
            iconScale,
            label
        );
        rowY += stepY;
    };

    drawStatusRow(0, hudStatusColumnIndex(health01),
        "Zdravi: " + std::to_string((int)std::lround(m_player.stats.condition.health)) + " %");

    drawStatusRow(1, hudStatusColumnIndex(stamina01),
        "Stamina: " + std::to_string((int)std::lround(m_player.stats.condition.stamina)) + " %");

    drawStatusRow(2, hudStatusColumnIndex(1.0f - hunger01),
        "Hlad: " + std::to_string((int)std::lround(m_player.stats.legacyHunger())) + " %");

    drawStatusRow(3, hudStatusColumnIndex(1.0f - thirst01),
        "Zizen: " + std::to_string((int)std::lround(m_player.stats.legacyThirst())) + " %");

    if (temp > 55.0f)
    {
        drawStatusRow(4, hudStatusColumnIndex(1.0f - heat01),
            "Horko: " + std::to_string((int)std::lround(heat01 * 100.0f)) + " %");
    }
    else if (temp < 45.0f)
    {
        drawStatusRow(5, hudStatusColumnIndex(1.0f - cold01),
            "Zima: " + std::to_string((int)std::lround(cold01 * 100.0f)) + " %");
    }

    drawStatusRow(6, hudStatusColumnIndex(hygiene01),
        "Hygiena: " + std::to_string((int)std::lround(m_player.stats.condition.hygiene)) + " %");

    drawStatusRow(7, hudStatusColumnIndex(1.0f - stress01),
        "Stres: " + std::to_string((int)std::lround(m_player.stats.condition.stress)) + " %");

       // ===== DEN / MESIC =====
    const int rightPad = 20;
    const int topPad = 20;

    int frameA = -1;
    int frameB = -1;
    float blendT = 0.0f;

    int dayBoxW = 0;
    int dayBoxH = 0;

    if (getHudDayBlendFrames(frameA, frameB, blendT))
    {
        if (frameA >= 0 && frameA < (int)m_hudDay.frames.size())
        {
            const SDL_Rect src = m_hudDay.frames[frameA];
            dayBoxW = (int)std::lround(src.w * 0.8f);
            dayBoxH = (int)std::lround(src.h * 0.8f);
        }

        const int hudX = screenW - rightPad - dayBoxW;
        const int hudY = topPad;

        drawHudFrameAlpha(m_hudDay, frameA, hudX, hudY, 0.8f, ClampAlpha01(1.0f - blendT));
        drawHudFrameAlpha(m_hudDay, frameB, hudX, hudY, 0.8f, ClampAlpha01(blendT));

        if (!tooltipShown && frameA >= 0 && frameA < (int)m_hudDay.frames.size())
        {
            SDL_Rect dst{ hudX, hudY, dayBoxW, dayBoxH };

            if (mouseX >= dst.x && mouseX < dst.x + dst.w &&
                mouseY >= dst.y && mouseY < dst.y + dst.h)
            {
                ImGui::BeginTooltip();
                ImGui::Text("Cast dne: %s", DayPhaseToStringCz(getDynamicDayPhase()));
                ImGui::EndTooltip();
                tooltipShown = true;
            }
        }
    }
    else
    {
        const int moonIndex = hudMoonFrameIndex();

        if (moonIndex >= 0 && moonIndex < (int)m_hudMoon.frames.size())
        {
            const SDL_Rect src = m_hudMoon.frames[moonIndex];
            dayBoxW = (int)std::lround(src.w * 0.8f);
            dayBoxH = (int)std::lround(src.h * 0.8f);
        }

        const int hudX = screenW - rightPad - dayBoxW;
        const int hudY = topPad;

        drawHudIconWithTooltip(
            m_hudMoon,
            moonIndex,
            hudX,
            hudY,
            0.8f,
            std::string("Mesic: ") + m_moonCycle.getMoonInfo(
                m_gameTime.now().day,
                m_gameTime.now().month,
                m_gameTime.now().year
            ).phase
        );
    } 

        // ===== STAROČESKÉ DATUM NAHOŘE UPROSTŘED =====
    {
        ImDrawList* fg = ImGui::GetForegroundDrawList();

        const auto& now = m_gameTime.now();
        const int weekDayIndex = m_gameTime.currentWeekDayIndexMondayFirst();
        const std::string weekDay = ui::WeekDayCz(weekDayIndex);

        const std::string line1 =
            "Léta Páně " + std::to_string(now.year);

        const std::string line2 =
            m_liturgicalCalendar.formatMedievalDate(
                now.day,
                now.month,
                now.year,
                weekDay,
                LiturgicalCalendar::Style::Spoken);

        const std::string line3 =
            std::string("měsíce ") + MonthNameCzGenitive(now.month);

        ImVec2 size1 = ImGui::CalcTextSize(line1.c_str());
        ImVec2 size2 = ImGui::CalcTextSize(line2.c_str());
        ImVec2 size3 = ImGui::CalcTextSize(line3.c_str());

        const float boxPadX = 14.0f;
        const float boxPadY = 8.0f;
        const float gapY = 4.0f;

        const float boxW = std::max({ size1.x, size2.x, size3.x }) + boxPadX * 2.0f;
        const float boxH = size1.y + size2.y + size3.y + gapY * 2.0f + boxPadY * 2.0f;

        const ImVec2 boxMin(
            ((float)screenW - boxW) * 0.5f,
            20.0f
        );
        const ImVec2 boxMax(
            boxMin.x + boxW,
            boxMin.y + boxH
        );

        fg->AddRectFilled(boxMin, boxMax, IM_COL32(12, 10, 8, 170), 8.0f);
        fg->AddRect(boxMin, boxMax, IM_COL32(196, 170, 110, 220), 8.0f, 0, 1.5f);

        const float textY1 = boxMin.y + boxPadY;
        const float textY2 = textY1 + size1.y + gapY;
        const float textY3 = textY2 + size2.y + gapY;

        const float textX1 = boxMin.x + (boxW - size1.x) * 0.5f;
        const float textX2 = boxMin.x + (boxW - size2.x) * 0.5f;
        const float textX3 = boxMin.x + (boxW - size3.x) * 0.5f;

        fg->AddText(
            ImVec2(textX1, textY1),
            IM_COL32(235, 220, 180, 255),
            line1.c_str()
        );

        fg->AddText(
            ImVec2(textX2, textY2),
            IM_COL32(230, 230, 220, 255),
            line2.c_str()
        );

        fg->AddText(
            ImVec2(textX3, textY3),
            IM_COL32(190, 190, 170, 255),
            line3.c_str()
        );
    }
}