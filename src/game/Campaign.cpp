#define NOMINMAX
#include "Campaign.h"
#include "../PathUtils.h"
#include "../Utf8.h"
#include "../ImGuiUtils.h"
#include "imgui.h"
#include "../JsonUtils.h"

#include <SDL_image.h>
#include <algorithm>
#include <string>
#include <filesystem>
#include <cmath>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

static float Clamp01To100(float v)
{
    return std::clamp(v, 0.0f, 100.0f);
}

static SDL_Rect GetObjectWorldRect(const gameobj::ObjectDef& def, int wx, int wy)
{
    SDL_Rect r{};
    r.w = def.src.w;
    r.h = def.src.h;
    r.x = (int)std::lround(wx - def.pivot.x);
    r.y = (int)std::lround(wy - def.pivot.y);
    return r;
}

const char* Campaign::seasonToString(Season season) const
{
    switch (season)
    {
    case Season::Spring: return "spring";
    case Season::Summer: return "summer";
    case Season::Autumn: return "autumn";
    default:             return "winter";
    }
}

DayPhase Campaign::getDynamicDayPhase() const
{
    const auto& now = m_gameTime.now();
    const auto sun = m_sunCycle.getDayInfo(now.day, now.month, now.hour, now.minute);

    const int nowMin = now.hour * 60 + now.minute;
    const int sunrise = sun.sunriseMinutes;
    const int sunset = sun.sunsetMinutes;

    const int noonStart = 11 * 60;
    const int noonEnd = 13 * 60;

    if (nowMin < sunrise - 30)
        return DayPhase::Night;

    if (nowMin < sunrise + 45)
        return DayPhase::Dawn;

    if (nowMin < noonStart - 90)
        return DayPhase::Morning;

    if (nowMin < noonStart)
        return DayPhase::Forenoon;

    if (nowMin < noonEnd)
        return DayPhase::Noon;

    const int afternoonStart = noonEnd;
    const int lateDayStart = afternoonStart + (sunset - afternoonStart) * 2 / 3;
    const int eveningStart = sunset - 30;
    const int nightStart = sunset + 45;

    if (nowMin < lateDayStart)
        return DayPhase::Afternoon;

    if (nowMin < eveningStart)
        return DayPhase::LateDay;

    if (nowMin < nightStart)
        return DayPhase::Evening;

    return DayPhase::Night;
}

const char* Campaign::dayPhaseToString(DayPhase phase) const
{
    switch (phase)
    {
    case DayPhase::Dawn:      return "dawn";
    case DayPhase::Morning:   return "morning";
    case DayPhase::Forenoon:  return "forenoon";
    case DayPhase::Noon:      return "noon";
    case DayPhase::Afternoon: return "afternoon";
    case DayPhase::LateDay:   return "lateday";
    case DayPhase::Evening:   return "evening";
    default:                  return "night";
    }
}

Season Campaign::getSeason() const
{
    const auto& now = m_gameTime.now();

    if (now.month >= 3 && now.month <= 5)  return Season::Spring;
    if (now.month >= 6 && now.month <= 8)  return Season::Summer;
    if (now.month >= 9 && now.month <= 11) return Season::Autumn;
    return Season::Winter;
}

void Campaign::updateWeather()
{
    const auto& now = m_gameTime.now();

    if (now.day != m_cachedWeatherDay ||
        now.month != m_cachedWeatherMonth ||
        now.year != m_cachedWeatherYear)
    {
        m_todayWeather = m_weatherSystem.getDayProfile(
            now.day,
            now.month,
            now.year);

        m_cachedWeatherDay = now.day;
        m_cachedWeatherMonth = now.month;
        m_cachedWeatherYear = now.year;
    }

    const auto sun = m_sunCycle.getDayInfo(
        now.day,
        now.month,
        now.hour,
        now.minute);

    m_runtimeWeather = m_weatherSystem.getRuntimeState(
        m_todayWeather,
        now.hour,
        now.minute,
        sun.sunriseMinutes,
        sun.sunsetMinutes);

    applyWeatherDebugOverride();
}

void Campaign::applyWeatherDebugOverride()
{
    if (!m_weatherDebug.enabled)
        return;

    if (m_weatherDebug.overrideCloudiness)
        m_todayWeather.cloudiness = std::clamp(m_weatherDebug.forcedCloudiness, 0.0f, 100.0f);

    if (m_weatherDebug.overrideGroundWetness)
        m_todayWeather.groundWetness = std::clamp(m_weatherDebug.forcedGroundWetness, 0.0f, 100.0f);

    if (m_weatherDebug.overrideTemp)
        m_runtimeWeather.currentTemp = m_weatherDebug.forcedTemp;

    if (m_weatherDebug.overrideWind)
        m_runtimeWeather.windNow = std::max(0.0f, m_weatherDebug.forcedWind);

    if (m_weatherDebug.overrideFog)
        m_runtimeWeather.isFoggy = m_weatherDebug.forcedFog;

    if (m_weatherDebug.overrideRain)
    {
        m_runtimeWeather.isRaining = m_weatherDebug.forcedRain;
        m_runtimeWeather.rainIntensity = m_weatherDebug.forcedRain
            ? std::clamp(m_weatherDebug.forcedRainIntensity, 0.0f, 1.0f)
            : 0.0f;
    }
}

bool Campaign::isInspectHeld() const
{
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    return ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT];
}

void Campaign::tryInspectWorld()
{
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    const int worldX = mx + m_camX;
    const int worldY = my + m_camY;

    // 1) NPC
    for (auto& npc : m_npcManager.npcs())
    {
        SDL_Rect npcRect{
            (int)(npc.x - 16.0f),
            (int)(npc.y - 32.0f),
            32,
            48
        };

        if (worldX >= npcRect.x && worldX < npcRect.x + npcRect.w &&
            worldY >= npcRect.y && worldY < npcRect.y + npcRect.h)
        {
            const std::string sfxId = "inspect_npc_" + npc.id;
            m_audioManager.playSfx(sfxId);

            consoleLog("=== NPC INSPECT ===");
            SDL_Log("=== NPC INSPECT ===");

            consoleLog("spawn id: " + npc.id);
            SDL_Log("spawn id: %s", npc.id.c_str());

            consoleLog("npc_id: " + npc.npcId);
            SDL_Log("npc_id: %s", npc.npcId.c_str());

            consoleLog("display name: " + npc.displayName());
            SDL_Log("display name: %s", npc.displayName().c_str());

            if (!npc.npcId.empty())
            {
                const NpcDefinition* def = m_npcDefinitions.findByNpcId(npc.npcId);
                if (def)
                {
                    consoleLog("--- definition ---");
                    consoleLog("profession: " + def->identity.profession);
                    consoleLog("authority: " + def->identity.authorityLevel);
                    consoleLog("household: " + def->household.householdId);

                    SDL_Log("profession: %s", def->identity.profession.c_str());
                    SDL_Log("authority: %s", def->identity.authorityLevel.c_str());
                    SDL_Log("household: %s", def->household.householdId.c_str());
                }
                else
                {
                    consoleLog("definition: NOT FOUND");
                    SDL_Log("definition: NOT FOUND");
                }
            }

            return;
        }
    }

    // 2) Object / item on map – zatím placeholder podle objektu v tile
    const int tx = worldX / m_tileSize;
    const int ty = worldY / m_tileSize;

    const auto* def = m_map.getObjDefAt(m_objCatalog, tx, ty);
    if (def)
    {
        const std::string sfxId = "inspect_obj_" + def->id;
        m_audioManager.playSfx(sfxId);

        consoleLog("=== OBJECT INSPECT ===");
        consoleLog("object id: " + def->id);
        consoleLog("object name: " + def->name);

        return;
    }
}

void Campaign::applyNpcDefinitionsToInstances()
{
    SDL_Log("applyNpcDefinitionsToInstances: npcs=%d",
        (int)m_npcManager.npcs().size());

    for (auto& npc : m_npcManager.npcs())
    {
        SDL_Log("Applying definition for spawn id=%s npc_id=%s",
            npc.id.c_str(), npc.npcId.c_str());

        if (npc.npcId.empty())
        {
            SDL_Log(" -> skipped: empty npcId");
            continue;
        }

        const NpcDefinition* def = m_npcDefinitions.findByNpcId(npc.npcId);
        if (!def)
        {
            SDL_Log(" -> definition NOT FOUND for npc_id=%s", npc.npcId.c_str());
            continue;
        }

        SDL_Log(" -> definition found: %s", def->displayName().c_str());

        if (npc.name.empty())
            npc.name = def->identity.name;

        if (npc.surname.empty())
            npc.surname = def->identity.surname;

        if (npc.scriptId.empty())
            npc.scriptId = def->scriptId;

        if (npc.characterId.empty())
            npc.characterId = def->characterId;

        SDL_Log(" -> result name=%s surname=%s script=%s character=%s",
            npc.name.c_str(),
            npc.surname.c_str(),
            npc.scriptId.c_str(),
            npc.characterId.c_str());
    }
}

static float distanceSq(float x1, float y1, float x2, float y2)
{
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    return dx * dx + dy * dy;
}

void Campaign::updateNearbyNpc()
{
    m_nearNpcIndex = -1;
    float bestDistSq = 99999999.0f;

    const auto& npcs = m_npcManager.npcs();

    for (int i = 0; i < (int)npcs.size(); ++i)
    {
        const auto& npc = npcs[i];

        if (!npc.playerInDialogRange)
            continue;

        const float dx = npc.x - m_player.x;
        const float dy = npc.y - m_player.y;
        const float distSq = dx * dx + dy * dy;

        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            m_nearNpcIndex = i;
        }
    }
}