#include "Campaign.h"
#include "../Utf8.h"
#include "../ImGuiUtils.h"
#include "imgui.h"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

void Campaign::consoleLog(const std::string& text)
{
    m_consoleLog.push_back(text);
    if (m_consoleLog.size() > 500)
        m_consoleLog.erase(m_consoleLog.begin());
    m_consoleScrollToBottom = true;
}

namespace
{
    static std::string DebugLowerAscii(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    static bool DebugIsLeapYearJulian(int year)
    {
        // Medieval-friendly approximation for the debug clock. Good enough for skip/wait testing.
        return (year % 4) == 0;
    }

    static int DebugDaysInMonth(int month, int year)
    {
        switch (month)
        {
        case 1:  return 31;
        case 2:  return DebugIsLeapYearJulian(year) ? 29 : 28;
        case 3:  return 31;
        case 4:  return 30;
        case 5:  return 31;
        case 6:  return 30;
        case 7:  return 31;
        case 8:  return 31;
        case 9:  return 30;
        case 10: return 31;
        case 11: return 30;
        case 12: return 31;
        default: return 30;
        }
    }

    static bool DebugParseDurationToken(
        std::istringstream& iss,
        const std::string& firstToken,
        int& outMinutes,
        std::string& outError)
    {
        outMinutes = 0;
        outError.clear();

        std::string token = DebugLowerAscii(firstToken);
        float value = 0.0f;
        char unit = 'h'; // default: skip 8 == 8 hours

        auto parseValue = [&](const std::string& raw) -> bool
        {
            if (raw.empty())
                return false;

            try
            {
                size_t consumed = 0;
                value = std::stof(raw, &consumed);
                return consumed == raw.size();
            }
            catch (...)
            {
                return false;
            }
        };

        if (token == "hour" || token == "hours" || token == "h" || token == "hod" || token == "hodin")
        {
            std::string v;
            iss >> v;
            if (!parseValue(v))
            {
                outError = "Pouziti: skip hours 8";
                return false;
            }
            unit = 'h';
        }
        else if (token == "minute" || token == "minutes" || token == "min" || token == "m" || token == "minut")
        {
            std::string v;
            iss >> v;
            if (!parseValue(v))
            {
                outError = "Pouziti: skip minutes 30";
                return false;
            }
            unit = 'm';
        }
        else if (token == "day" || token == "days" || token == "d" || token == "den" || token == "dni")
        {
            std::string v;
            iss >> v;
            if (!parseValue(v))
            {
                outError = "Pouziti: skip days 2";
                return false;
            }
            unit = 'd';
        }
        else
        {
            if (!token.empty())
            {
                const char last = token.back();
                if (last == 'h' || last == 'm' || last == 'd')
                {
                    unit = last;
                    token.pop_back();
                }
            }

            if (!parseValue(token))
            {
                outError = "Pouziti: skip 8 | skip 8h | skip 30m | skip 2d";
                return false;
            }
        }

        if (value <= 0.0f)
        {
            outError = "Hodnota musi byt vetsi nez 0.";
            return false;
        }

        float minutes = value * 60.0f;
        if (unit == 'm') minutes = value;
        if (unit == 'd') minutes = value * 24.0f * 60.0f;

        outMinutes = (int)std::lround(minutes);
        if (outMinutes <= 0)
            outMinutes = 1;

        // Safety guard: this is a debug simulation, not a calendar batch job.
        outMinutes = std::clamp(outMinutes, 1, 60 * 24 * 30);
        return true;
    }
}

void Campaign::debugAdvanceClockOnly(int minutes)
{
    if (minutes <= 0)
        return;

    const auto& now = m_gameTime.now();

    int day = now.day;
    int month = now.month;
    int year = now.year;
    int totalDayMinutes = now.hour * 60 + now.minute + minutes;

    while (totalDayMinutes >= 24 * 60)
    {
        totalDayMinutes -= 24 * 60;
        ++day;

        const int dim = DebugDaysInMonth(month, year);
        if (day > dim)
        {
            day = 1;
            ++month;
            if (month > 12)
            {
                month = 1;
                ++year;
            }
        }
    }

    while (totalDayMinutes < 0)
    {
        totalDayMinutes += 24 * 60;
        --day;
        if (day <= 0)
        {
            --month;
            if (month <= 0)
            {
                month = 12;
                --year;
            }
            day = DebugDaysInMonth(month, year);
        }
    }

    const int hour = totalDayMinutes / 60;
    const int minute = totalDayMinutes % 60;
    m_gameTime.setStartDateTime(day, month, year, hour, minute);
}

void Campaign::debugAdvanceTimeMinutes(int totalMinutes)
{
    if (totalMinutes <= 0)
        return;

    totalMinutes = std::clamp(totalMinutes, 1, 60 * 24 * 30);

    const float oldVx = m_player.vx;
    const float oldVy = m_player.vy;
    const bool oldSprint = m_player.isSprinting;

    int remaining = totalMinutes;
    while (remaining > 0)
    {
        // Hourly chunks keep poisoning onset/messages readable and avoid one giant simulation step.
        const int step = std::min(remaining, 60);

        debugAdvanceClockOnly(step);

        // Debug skip means the player waits/rests. Do not let the last movement vector count as walking.
        m_player.vx = 0.0f;
        m_player.vy = 0.0f;
        m_player.isSprinting = false;

        updatePlayerNeeds(step);
        updateFoodPoisoning(step);
        updateWeather();

        remaining -= step;
    }

    m_player.vx = oldVx;
    m_player.vy = oldVy;
    m_player.isSprinting = oldSprint;

    const auto& now = m_gameTime.now();
    const bool isSunday = (m_gameTime.currentWeekDayIndexMondayFirst() == 6);
    const std::string feastId = m_liturgicalCalendar.primaryTitle(now.day, now.month, now.year);
    const std::vector<std::string> feastTags = m_liturgicalCalendar.tagsForDate(now.day, now.month, now.year);

    m_npcManager.updateSchedules(
        seasonToString(getSeason()),
        dayPhaseToString(getDynamicDayPhase()),
        isSunday,
        feastId,
        feastTags);

    std::ostringstream ss;
    ss << "Debug skip +" << totalMinutes << " min -> "
       << m_gameTime.formatDateCz() << " " << m_gameTime.formatTime()
       << " | HP=" << (int)std::lround(m_player.stats.condition.health)
       << " NUT=" << (int)std::lround(m_player.stats.condition.nutrition)
       << " HYD=" << (int)std::lround(m_player.stats.condition.hydration)
       << " FAT=" << (int)std::lround(m_player.stats.condition.fatigue);
    consoleLog(ss.str());

    if (m_activeFoodPoisoning.active)
    {
        consoleLog(activeFoodPoisoningStatusText());
        if (m_godMode)
            consoleLog("Pozor: IDDQD je zapnuty, takze se poskozeny zdravotni stav muze hned resetovat.");
    }
}

void Campaign::executeConsoleCommand(const std::string& cmd)
{

    if (cmd.empty())
        return;

    if (m_consoleHistory.empty() || m_consoleHistory.back() != cmd)
        m_consoleHistory.push_back(cmd);

    m_consoleHistoryIndex = -1;

    consoleLog("> " + cmd);

    if (cmd == "IDDQD" || cmd == "iddqd")
    {
        m_godMode = !m_godMode;
        if (m_godMode)
            consoleLog(U8("DOOM gamer, huh?"));
        else
            consoleLog("Die like a hero :)");
        return;
    }

    if (cmd == "collision_debug")
    {
        m_drawObjColliders = !m_drawObjColliders;

        if (m_drawObjColliders)
            consoleLog("collision_debug: ON");
        else
            consoleLog("collision_debug: OFF");

        return;
    }

    if (cmd == "clear")
    {
        m_consoleLog.clear();
        return;
    }

    if (cmd == "stats")
    {
        std::ostringstream ss;
        ss << "HP=" << m_player.stats.condition.health
            << " STA=" << m_player.stats.condition.stamina
            << " FAT=" << m_player.stats.condition.fatigue
            << " NUT=" << m_player.stats.condition.nutrition
            << " HYD=" << m_player.stats.condition.hydration
            << " HYG=" << m_player.stats.condition.hygiene
            << " TMP=" << m_player.stats.condition.bodyTemperature
            << " MOR=" << m_player.stats.condition.morale
            << " STR=" << m_player.stats.condition.stress;
        consoleLog(ss.str());

        std::ostringstream ss2;
        ss2 << "Weight=" << m_player.stats.carryWeight
            << "/" << m_player.stats.carryCapacity
            << " Volume=" << m_player.stats.carryVolume
            << "/" << m_player.stats.carryVolumeCapacity
            << " Speed=" << m_player.stats.getMoveSpeed();
        consoleLog(ss2.str());

        if (m_activeFoodPoisoning.active)
        {
            std::ostringstream ps;
            ps << "Poison=" << m_activeFoodPoisoning.sourceName
               << " elapsed=" << m_activeFoodPoisoning.elapsedHours << "h"
               << " onset=" << m_activeFoodPoisoning.onsetHours << "h"
               << " duration=" << m_activeFoodPoisoning.durationHours << "h"
               << " fatal=" << (m_activeFoodPoisoning.fatal ? "yes" : "no");
            consoleLog(ps.str());
        }

        return;
    }

    std::istringstream iss(cmd);
    std::string a, b;
    float value = 0.0f;

    iss >> a;

    if (a == "skip" || a == "advance" || a == "wait")
    {
        std::string durationToken;
        iss >> durationToken;

        if (durationToken.empty())
        {
            consoleLog("Pouziti: skip 8 | skip 8h | skip 30m | skip 2d");
            return;
        }

        int minutes = 0;
        std::string error;
        if (!DebugParseDurationToken(iss, durationToken, minutes, error))
        {
            consoleLog(error);
            return;
        }

        debugAdvanceTimeMinutes(minutes);
        return;
    }

    if (a == "poison")
    {
        std::string mode;
        iss >> mode;
        mode = DebugLowerAscii(mode);

        if (mode == "mild" || mode == "light" || mode == "red" || mode == "cervena" || mode == "červená")
        {
            startFoodPoisoning("debug muchomůrka červená", false);
            consoleLog(activeFoodPoisoningStatusText());
            return;
        }

        if (mode == "fatal" || mode == "deadly" || mode == "green" || mode == "zelena" || mode == "zelená")
        {
            startFoodPoisoning("debug muchomůrka zelená", true);
            consoleLog(activeFoodPoisoningStatusText());
            return;
        }

        if (mode == "clear" || mode == "cure" || mode == "stop")
        {
            clearFoodPoisoning("Debug: otrava zrušena.");
            return;
        }

        if (!m_activeFoodPoisoning.active)
        {
            consoleLog("Poison: none. Test: poison mild | poison fatal | poison clear");
            return;
        }

        consoleLog(activeFoodPoisoningStatusText());
        std::ostringstream ps;
        ps << "Poison detail: elapsed=" << m_activeFoodPoisoning.elapsedHours << "h"
           << " | onset=" << m_activeFoodPoisoning.onsetHours << "h"
           << " | duration=" << m_activeFoodPoisoning.durationHours << "h"
           << " | fatal=" << (m_activeFoodPoisoning.fatal ? "yes" : "no")
           << " | activeDamage=" << (m_player.stats.condition.poisoned ? "yes" : "latent")
           << " | HP=" << (int)std::lround(m_player.stats.condition.health)
           << " HYD=" << (int)std::lround(m_player.stats.condition.hydration);
        consoleLog(ps.str());
        if (m_godMode)
            consoleLog("Pozor: IDDQD je zapnuty, takze nasledky otravy skoro neuvidis.");
        return;
    }

    if (a == "set")
    {
        iss >> b;

        if (b == "time")
        {
            std::string hhmm;
            iss >> hhmm;

            if (m_gameTime.setTimeFromString(hhmm))
                consoleLog("Cas nastaven.");
            else
                consoleLog("Neplatny format casu. Pouzij napr. set time 22:00");
            return;
        }

        if (b == "date")
        {
            std::string ddmmyyyy;
            iss >> ddmmyyyy;

            if (m_gameTime.setDateFromString(ddmmyyyy))
                consoleLog("Datum nastaveno.");
            else
                consoleLog("Neplatny format data. Pouzij napr. set date 14.12.1400");
            return;
        }

        iss >> value;

        if (b == "hp") {
            m_player.stats.condition.health = std::clamp(value, 0.0f, 100.0f);
            consoleLog("Zdravi nastaveno.");
            return;
        }

        if (b == "stamina") {
            m_player.stats.condition.stamina = std::clamp(value, 0.0f, 100.0f);
            consoleLog("Stamina nastavena.");
            return;
        }

        if (b == "hunger") {
            const float hunger = std::clamp(value, 0.0f, 100.0f);
            m_player.stats.condition.nutrition = 100.0f - hunger;
            consoleLog("Hlad nastaven.");
            return;
        }

        if (b == "nutrition") {
            m_player.stats.condition.nutrition = std::clamp(value, 0.0f, 100.0f);
            consoleLog("Vy�iveni nastaveno.");
            return;
        }

        if (b == "thirst") {
            const float thirst = std::clamp(value, 0.0f, 100.0f);
            m_player.stats.condition.hydration = 100.0f - thirst;
            consoleLog("Zizen nastavena.");
            return;
        }

        if (b == "hydration") {
            m_player.stats.condition.hydration = std::clamp(value, 0.0f, 100.0f);
            consoleLog("Hydratace nastavena.");
            return;
        }

        if (b == "fatigue") {
            m_player.stats.condition.fatigue = std::clamp(value, 0.0f, 100.0f);
            consoleLog("Unava nastavena.");
            return;
        }

        if (b == "hygiene") {
            const float dirtiness = std::clamp(value, 0.0f, 100.0f);
            m_player.stats.condition.hygiene = 100.0f - dirtiness;
            consoleLog("Spinavost nastavena.");
            return;
        }

        if (b == "clean") {
            m_player.stats.condition.hygiene = std::clamp(value, 0.0f, 100.0f);
            consoleLog("Cistota nastavena.");
            return;
        }

        if (b == "temp") {
            m_player.stats.condition.bodyTemperature = std::clamp(value, 0.0f, 100.0f);
            consoleLog("Teplotni stav nastaven.");
            return;
        }

        if (b == "morale") {
            m_player.stats.condition.morale = std::clamp(value, 0.0f, 100.0f);
            consoleLog("Moralka nastavena.");
            return;
        }

        if (b == "stress") {
            m_player.stats.condition.stress = std::clamp(value, 0.0f, 100.0f);
            consoleLog("Stres nastaven.");
            return;
        }

        if (b == "weight") {
            m_player.stats.carryWeight = std::max(0.0f, value);
            consoleLog("Nosena vaha nastavena.");
            return;
        }

        if (b == "volume") {
            m_player.stats.carryVolume = std::max(0.0f, value);
            consoleLog("Obsazeny objem nastaven.");
            return;
        }

        consoleLog("Neznamy atribut pro set.");
        return;
    }

    if (a == "weather")
    {
        std::string sub;
        iss >> sub;

        if (sub.empty())
        {
            std::ostringstream ss;
            ss << "Weather: temp=" << m_runtimeWeather.currentTemp
                << "C rain=" << (m_runtimeWeather.isRaining ? "yes" : "no")
                << " rainInt=" << m_runtimeWeather.rainIntensity
                << " fog=" << (m_runtimeWeather.isFoggy ? "yes" : "no")
                << " wind=" << m_runtimeWeather.windNow
                << " cloud=" << m_todayWeather.cloudiness
                << " wet=" << m_todayWeather.groundWetness;
            consoleLog(ss.str());

            consoleLog(std::string("Override: ") + (m_weatherDebug.enabled ? "ON" : "OFF"));
            return;
        }

        if (sub == "clear")
        {
            m_weatherDebug = WeatherDebugOverride{};
            consoleLog("Weather override cleared.");
            return;
        }

        if (sub == "rain")
        {
            std::string arg;
            iss >> arg;

            m_weatherDebug.enabled = true;
            m_weatherDebug.overrideRain = true;

            if (arg == "on")
            {
                m_weatherDebug.forcedRain = true;
                m_weatherDebug.forcedRainIntensity = 0.5f;
                consoleLog("Rain forced ON.");
                return;
            }

            if (arg == "off")
            {
                m_weatherDebug.forcedRain = false;
                m_weatherDebug.forcedRainIntensity = 0.0f;
                consoleLog("Rain forced OFF.");
                return;
            }

            try
            {
                float intensity = std::stof(arg);
                m_weatherDebug.forcedRain = intensity > 0.0f;
                m_weatherDebug.forcedRainIntensity = std::clamp(intensity, 0.0f, 1.0f);
                consoleLog("Rain intensity overridden.");
            }
            catch (...)
            {
                consoleLog("Pouzij: weather rain on | off | <0..1>");
            }
            return;
        }

        if (sub == "fog")
        {
            std::string arg;
            iss >> arg;

            m_weatherDebug.enabled = true;
            m_weatherDebug.overrideFog = true;

            if (arg == "on")
            {
                m_weatherDebug.forcedFog = true;
                consoleLog("Fog forced ON.");
                return;
            }

            if (arg == "off")
            {
                m_weatherDebug.forcedFog = false;
                consoleLog("Fog forced OFF.");
                return;
            }

            consoleLog("Pouzij: weather fog on | off");
            return;
        }

        if (sub == "temp")
        {
            float v = 0.0f;
            if (!(iss >> v))
            {
                consoleLog("Pouzij: weather temp <celsius>");
                return;
            }

            m_weatherDebug.enabled = true;
            m_weatherDebug.overrideTemp = true;
            m_weatherDebug.forcedTemp = v;
            consoleLog("Temperature overridden.");
            return;
        }

        if (sub == "wind")
        {
            float v = 0.0f;
            if (!(iss >> v))
            {
                consoleLog("Pouzij: weather wind <kmh>");
                return;
            }

            m_weatherDebug.enabled = true;
            m_weatherDebug.overrideWind = true;
            m_weatherDebug.forcedWind = std::max(0.0f, v);
            consoleLog("Wind overridden.");
            return;
        }

        if (sub == "clouds")
        {
            float v = 0.0f;
            if (!(iss >> v))
            {
                consoleLog("Pouzij: weather clouds <0..100>");
                return;
            }

            m_weatherDebug.enabled = true;
            m_weatherDebug.overrideCloudiness = true;
            m_weatherDebug.forcedCloudiness = std::clamp(v, 0.0f, 100.0f);
            consoleLog("Cloudiness overridden.");
            return;
        }

        if (sub == "wetness")
        {
            float v = 0.0f;
            if (!(iss >> v))
            {
                consoleLog("Pouzij: weather wetness <0..100>");
                return;
            }

            m_weatherDebug.enabled = true;
            m_weatherDebug.overrideGroundWetness = true;
            m_weatherDebug.forcedGroundWetness = std::clamp(v, 0.0f, 100.0f);
            consoleLog("Ground wetness overridden.");
            return;
        }

        consoleLog("Weather commands: weather, weather clear, weather rain on/off/0.5, weather fog on/off, weather temp 5, weather wind 15, weather clouds 80, weather wetness 70");
        return;
    }

    if (a == "items")
    {
        std::string filter;
        std::getline(iss >> std::ws, filter);

        int shown = 0;
        consoleLog("=== ITEM LIST ===");

        for (const auto& [id, def] : m_itemDefs)
        {
            if (!filter.empty())
            {
                std::string hay = id + " " + def.name;
                std::string hayLower = hay;
                std::string filterLower = filter;

                std::transform(hayLower.begin(), hayLower.end(), hayLower.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });
                std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });

                if (hayLower.find(filterLower) == std::string::npos)
                    continue;
            }

            consoleLog(
                id + " | " + def.name +
                " | stack " + std::to_string(def.maxStack) +
                " | w " + std::to_string(def.weight));
            ++shown;
        }

        if (shown == 0)
            consoleLog("Zadne itemy nenalezeny.");

        return;
    }

    if (a == "give")
    {
        std::string itemId;
        int count = 1;

        iss >> itemId;

        if (itemId.empty())
        {
            consoleLog("Pouziti: give item_id [count]");
            return;
        }

        if (!(iss >> count))
            count = 1;

        if (count <= 0)
        {
            consoleLog("Count musi byt vetsi nez 0.");
            return;
        }

        auto it = m_itemDefs.find(itemId);
        if (it == m_itemDefs.end())
        {
            consoleLog("Neznamy item_id: " + itemId);
            return;
        }

        const ItemDef& def = it->second;

        if (m_player.inventory.addItem(def, count, m_itemDefs))
        {
            m_player.stats.carryWeight =
                m_player.inventory.computeTotalWeight(m_itemDefs);
            m_player.stats.carryVolume =
                m_player.inventory.computeTotalVolume(m_itemDefs);

            consoleLog("Pridan item: " + itemId + " x" + std::to_string(count));
        }
        else
        {
            m_player.stats.carryWeight =
                m_player.inventory.computeTotalWeight(m_itemDefs);
            m_player.stats.carryVolume =
                m_player.inventory.computeTotalVolume(m_itemDefs);

            consoleLog("Item se nepodarilo plne pridat. Malo mista nebo nosnosti.");
        }

        return;
    }

    if (cmd == "help")
    {
        consoleLog("set hp 50");
        consoleLog("set stamina 50");
        consoleLog("set hunger 50");
        consoleLog("set thirst 50");
        consoleLog("set fatigue 50");
        consoleLog("set hygiene 50");
        consoleLog("set clean 80");
        consoleLog("set temp 50");
        consoleLog("set morale 70");
        consoleLog("set stress 20");
        consoleLog("set weight 30");
        consoleLog("set volume 20");
        consoleLog("set time 22:00");
        consoleLog("set date 14.12.1400");
        consoleLog("skip 8          // posune o 8 hodin a spusti potreby/otravu");
        consoleLog("skip 30m        // posune o 30 minut");
        consoleLog("skip 2d         // posune o 2 dny");
        consoleLog("poison          // stav aktivni otravy");
        consoleLog("poison mild     // debug lehka otrava");
        consoleLog("poison fatal    // debug tezka otrava se zpozdenim 8h");
        consoleLog("poison clear    // zrusit debug otravu");
        consoleLog("stats");
        consoleLog("clear");
        consoleLog("IDDQD");
        consoleLog("collision_debug");
        consoleLog("weather");
        consoleLog("weather clear");
        consoleLog("weather rain on");
        consoleLog("weather rain off");
        consoleLog("weather rain 0.75");
        consoleLog("weather fog on");
        consoleLog("weather fog off");
        consoleLog("weather temp 3");
        consoleLog("weather wind 18");
        consoleLog("weather clouds 90");
        consoleLog("weather wetness 80");
        consoleLog("items");
        consoleLog("items nuz");
        consoleLog("give item_id");
        consoleLog("give item_id 3");

        return;
    }

    consoleLog(U8("Neznamy prikaz."));
}

int Campaign::ConsoleInputCallback(ImGuiInputTextCallbackData* data)
{
    Campaign* self = static_cast<Campaign*>(data->UserData);
    if (!self)
        return 0;

    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
    {
        if (self->m_consoleHistory.empty())
            return 0;

        if (data->EventKey == ImGuiKey_UpArrow)
        {
            if (self->m_consoleHistoryIndex == -1)
                self->m_consoleHistoryIndex = (int)self->m_consoleHistory.size() - 1;
            else if (self->m_consoleHistoryIndex > 0)
                --self->m_consoleHistoryIndex;
        }
        else if (data->EventKey == ImGuiKey_DownArrow)
        {
            if (self->m_consoleHistoryIndex != -1)
            {
                ++self->m_consoleHistoryIndex;
                if (self->m_consoleHistoryIndex >= (int)self->m_consoleHistory.size())
                    self->m_consoleHistoryIndex = -1;
            }
        }

        const char* historyText =
            (self->m_consoleHistoryIndex >= 0)
            ? self->m_consoleHistory[self->m_consoleHistoryIndex].c_str()
            : "";

        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, historyText);
    }

    return 0;
}

void Campaign::renderConsole()
{
    if (!m_consoleOpen)
        return;

    ImGui::SetNextWindowPos(ImVec2(20, 80), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_Always);

    if (ImGui::Begin(U8("Debug console"), nullptr))
    {
        ImGui::TextUnformatted(U8("Hints: skip 8, skip 30m, poison, set hp 50, stats, IDDQD, help"));

        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputInt("hodin##debug_skip_hours", &m_debugSkipHours);
        m_debugSkipHours = std::clamp(m_debugSkipHours, 1, 24 * 30);
        ImGui::SameLine();
        if (ImGui::Button("+1h"))
            debugAdvanceTimeMinutes(60);
        ImGui::SameLine();
        if (ImGui::Button("+8h"))
            debugAdvanceTimeMinutes(8 * 60);
        ImGui::SameLine();
        if (ImGui::Button("+24h"))
            debugAdvanceTimeMinutes(24 * 60);
        ImGui::SameLine();
        if (ImGui::Button("Skip X hodin"))
            debugAdvanceTimeMinutes(m_debugSkipHours * 60);

        ImGui::Separator();

        ImGui::BeginChild("ConsoleLog", ImVec2(0, -35), true);
        for (const auto& line : m_consoleLog)
            ImGui::TextWrapped("%s", line.c_str());

        if (m_consoleScrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
            m_consoleScrollToBottom = false;
        }

        ImGui::EndChild();

        ImGuiInputTextFlags inputFlags =
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CallbackHistory;

        if (m_consoleFocusInput)
        {
            ImGui::SetKeyboardFocusHere();
            m_consoleFocusInput = false;
        }

        const bool submitted = ImGui::InputText(
            "##console_input",
            m_consoleInput,
            IM_ARRAYSIZE(m_consoleInput),
            inputFlags,
            &Campaign::ConsoleInputCallback,
            this);

        if (submitted)
        {
            executeConsoleCommand(m_consoleInput);
            m_consoleInput[0] = '\0';
            m_consoleFocusInput = true;
        }
    }
    ImGui::End();
}