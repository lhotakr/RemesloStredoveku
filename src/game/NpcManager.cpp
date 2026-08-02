#include "NpcManager.h"
#include "CharacterManager.h"
#include "../JsonUtils.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <cmath>
#include <sstream>
#include <random>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

namespace
{
std::mt19937& NpcRandomEngine()
{
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

int RandomInt(int minInclusive, int maxInclusive)
{
    std::uniform_int_distribution<int> distribution(minInclusive, maxInclusive);
    return distribution(NpcRandomEngine());
}
}

static float computeFacing(float npcX, float npcY, float playerX, float playerY)
{
    const float dx = playerX - npcX;
    const float dy = playerY - npcY;

    if (std::abs(dx) > std::abs(dy))
        return (dx > 0.0f) ? 1.0f : 3.0f;

    return (dy > 0.0f) ? 2.0f : 0.0f;
}

static bool playerInRange(float npcX, float npcY, float playerX, float playerY, float radius)
{
    const float dx = playerX - npcX;
    const float dy = playerY - npcY;
    return (dx * dx + dy * dy) <= (radius * radius);
}

static void LoadPhaseEntries(const json& arr, std::vector<NpcPhaseScheduleEntry>& outEntries)
{
    outEntries.clear();

    if (!arr.is_array())
        return;

    for (const auto& e : arr)
    {
        NpcPhaseScheduleEntry entry;
        entry.phase = e.value("phase", "");
        entry.zoneId = e.value("zone", "");

        if (!entry.phase.empty() && !entry.zoneId.empty())
            outEntries.push_back(std::move(entry));
    }
}

static const std::vector<NpcPhaseScheduleEntry>* SelectActiveSchedule(
    const NpcInstance& npc,
    const std::string& season,
    bool isSunday,
    const std::string& feastId,
    const std::vector<std::string>& feastTags)
{
    if (!feastId.empty())
    {
        auto itId = npc.specialSchedule.feastById.find(feastId);
        if (itId != npc.specialSchedule.feastById.end() && !itId->second.empty())
            return &itId->second;

        for (const auto& tag : feastTags)
        {
            auto itTag = npc.specialSchedule.feastByTag.find(tag);
            if (itTag != npc.specialSchedule.feastByTag.end() && !itTag->second.empty())
                return &itTag->second;
        }

        if (!npc.specialSchedule.feastDefault.empty())
            return &npc.specialSchedule.feastDefault;
    }

    if (isSunday && !npc.specialSchedule.sunday.empty())
        return &npc.specialSchedule.sunday;

    if (season == "spring")
        return &npc.seasonSchedule.spring;
    if (season == "summer")
        return &npc.seasonSchedule.summer;
    if (season == "autumn")
        return &npc.seasonSchedule.autumn;

    return &npc.seasonSchedule.winter;
}

void NpcManager::clear()
{
    m_types.clear();
    m_npcs.clear();
    m_zones.clear();
}

bool NpcManager::loadTypes(const std::string& path, std::string* outError)
{
    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(path, root, err)) {
        if (outError) *outError = "NpcManager: " + err;
        return false;
    }

    if (!root.contains("types") || !root["types"].is_array()) {
        if (outError) *outError = "NpcManager: chybi pole 'types' v " + path;
        return false;
    }

    m_types.clear();

    for (const auto& jt : root["types"])
    {
        NpcTypeDef t;
        t.typeId = jt.value("type_id", "");
        t.name = jt.value("name", "");
        t.characterId = jt.value("character_id", "");
        t.defaultHP = jt.value("default_hp", 100);
        t.defaultMood = jt.value("default_mood", 50);
        t.defaultScriptId = jt.value("default_script_id", "");

        if (t.typeId.empty())
            continue;

        m_types[t.typeId] = std::move(t);
    }

    if (m_types.empty()) {
        if (outError) *outError = "NpcManager: nenacten zadny NPC typ z " + path;
        return false;
    }

    return true;
}

bool NpcManager::loadSpawns(const std::string& path, int tileSize, std::string* outError)
{
    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(path, root, err)) {
        if (outError) *outError = "NpcManager: " + err;
        return false;
    }

    if (!root.contains("npcs") || !root["npcs"].is_array()) {
        if (outError) *outError = "NpcManager: chybi pole 'npcs' v " + path;
        return false;
    }

    m_npcs.clear();

    for (const auto& jn : root["npcs"])
    {
        const std::string typeId = jn.value("type_id", "");
        if (typeId.empty())
            continue;

        auto itType = m_types.find(typeId);
        if (itType == m_types.end()) {
            if (outError) *outError = "NpcManager: neznamy type_id: " + typeId;
            return false;
        }

        const NpcTypeDef& t = itType->second;

        const int tileX = jn.value("tile_x", 0);
        const int tileY = jn.value("tile_y", 0);

        NpcInstance npc;
        npc.id = jn.value("id", "");
		npc.npcId = jn.value("npc_id", "");
        npc.typeId = typeId;

		// Debug log pro naèítaní NPC spawnù
        SDL_Log("Loaded NPC spawn id=%s npc_id=%s", npc.id.c_str(), npc.npcId.c_str());
        npc.name = jn.value("name", "");
        npc.surname = jn.value("surname", "");
        npc.greeting = jn.value("greeting", "");

        npc.scriptId = jn.value("script_id", t.defaultScriptId);
        npc.characterId = jn.value("character_id", t.characterId);
        npc.hp = jn.value("hp", t.defaultHP);
        npc.mood = jn.value("mood", t.defaultMood);

        npc.x = tileX * tileSize + tileSize * 0.5f;
        npc.y = tileY * tileSize + (float)tileSize;

        npc.targetX = npc.x;
        npc.targetY = npc.y;
        npc.currentZone.clear();
        npc.idleTimer = 0.5f;
        npc.animTime = 0.0f;
        npc.facing = 2.0f;
        npc.greetedPlayer = false;
		npc.nextGreetingAllowedTime = 0;
		npc.playerNearby = false;
		npc.playerInDialogRange = false;

        m_npcs.push_back(std::move(npc));
    }

    return true;
}

void NpcManager::loadNpcVoices(AudioManager& audio)
{
    for (const auto& [id, type] : m_types)
    {
        const std::string base = "assets/audio/voice/" + id;

        const bool a = audio.loadSfx(id + "_greeting_morning", base + "_greeting_morning.ogg");
        const bool b = audio.loadSfx(id + "_greeting_day", base + "_greeting_day.ogg");
        const bool c = audio.loadSfx(id + "_greeting_evening", base + "_greeting_evening.ogg");
        const bool d = audio.loadSfx(id + "_greeting_night", base + "_greeting_night.ogg");

        SDL_Log("NPC voice load [%s] morning=%d day=%d evening=%d night=%d",
            id.c_str(), (int)a, (int)b, (int)c, (int)d);
    }
}

void NpcManager::playGreeting(NpcInstance& npc, AudioManager& audio, int dayPhase)
{
    std::string clipSuffix;

    switch (dayPhase)
    {
    case 1: // Dawn
    case 2: // Morning
        clipSuffix = "greeting_morning";
        break;

    case 3: // Forenoon
    case 4: // Noon
    case 5: // Afternoon
    case 6: // LateDay
        clipSuffix = "greeting_day";
        break;

    case 7: // Evening
        clipSuffix = "greeting_evening";
        break;

    case 0: // Night
    default:
        clipSuffix = "greeting_night";
        break;
    }

    const std::string sfxId = npc.typeId + "_" + clipSuffix;
    SDL_Log("Play NPC greeting: %s", sfxId.c_str());
    audio.playVoice(sfxId);
}

bool NpcManager::loadZones(const std::string& path)
{
    json root;
    std::string err;

    if (!jsonutils::LoadJsonFileSafe(path, root, err))
    {
        SDL_Log("loadZones failed: %s", err.c_str());
        return false;
    }

    if (!root.contains("zones") || !root["zones"].is_array())
    {
        SDL_Log("loadZones INVALID JSON structure: %s", path.c_str());
        return false;
    }

    m_zones.clear();

    for (const auto& z : root["zones"])
    {
        NpcZone zone;
        zone.id = z.value("id", "");

        if (!z.contains("rect") || !z["rect"].is_array() || z["rect"].size() < 4)
            continue;

        const auto& r = z["rect"];
        zone.minX = r[0].get<int>();
        zone.minY = r[1].get<int>();
        zone.maxX = r[2].get<int>();
        zone.maxY = r[3].get<int>();

        if (!zone.id.empty())
        {
            m_zones[zone.id] = zone;
            SDL_Log("Zone loaded: %s [%d,%d -> %d,%d]",
                zone.id.c_str(), zone.minX, zone.minY, zone.maxX, zone.maxY);
        }
    }

    return true;
}

bool NpcManager::loadSchedules(const std::string& path)
{
    json root;
    std::string err;

    if (!jsonutils::LoadJsonFileSafe(path, root, err))
    {
        SDL_Log("loadSchedules failed: %s", err.c_str());
        return false;
    }

    if (!root.contains("schedules") || !root["schedules"].is_array())
    {
        SDL_Log("loadSchedules INVALID JSON structure: %s", path.c_str());
        return false;
    }

    for (const auto& sch : root["schedules"])
    {
        const std::string npcId = sch.value("npc_id", "");
        auto* npc = findNpcById(npcId);

        if (!npc)
        {
            SDL_Log("Schedule skipped, NPC not found: %s", npcId.c_str());
            continue;
        }

        npc->seasonSchedule = {};
        npc->specialSchedule = {};

        if (sch.contains("seasonal_schedule") && sch["seasonal_schedule"].is_object())
        {
            const auto& ss = sch["seasonal_schedule"];

            if (ss.contains("spring")) LoadPhaseEntries(ss["spring"], npc->seasonSchedule.spring);
            if (ss.contains("summer")) LoadPhaseEntries(ss["summer"], npc->seasonSchedule.summer);
            if (ss.contains("autumn")) LoadPhaseEntries(ss["autumn"], npc->seasonSchedule.autumn);
            if (ss.contains("winter")) LoadPhaseEntries(ss["winter"], npc->seasonSchedule.winter);
        }

        if (sch.contains("sunday_schedule"))
            LoadPhaseEntries(sch["sunday_schedule"], npc->specialSchedule.sunday);

        if (sch.contains("feast_schedule") && sch["feast_schedule"].is_object())
        {
            const auto& fs = sch["feast_schedule"];

            if (fs.contains("default"))
                LoadPhaseEntries(fs["default"], npc->specialSchedule.feastDefault);

            if (fs.contains("by_id") && fs["by_id"].is_object())
            {
                for (auto it = fs["by_id"].begin(); it != fs["by_id"].end(); ++it)
                {
                    std::vector<NpcPhaseScheduleEntry> entries;
                    LoadPhaseEntries(it.value(), entries);
                    npc->specialSchedule.feastById[it.key()] = std::move(entries);
                }
            }

            if (fs.contains("by_tag") && fs["by_tag"].is_object())
            {
                for (auto it = fs["by_tag"].begin(); it != fs["by_tag"].end(); ++it)
                {
                    std::vector<NpcPhaseScheduleEntry> entries;
                    LoadPhaseEntries(it.value(), entries);
                    npc->specialSchedule.feastByTag[it.key()] = std::move(entries);
                }
            }
        }

        SDL_Log("Seasonal schedule loaded for %s", npc->id.c_str());
    }

    return true;
}

NpcInstance* NpcManager::findNpcById(const std::string& id)
{
    for (auto& n : m_npcs)
    {
        if (n.id == id)
            return &n;
    }

    return nullptr;
}

void NpcManager::updateSchedules(
    const std::string& season,
    const std::string& phase,
    bool isSunday,
    const std::string& feastId,
    const std::vector<std::string>& feastTags)
{
    for (auto& npc : m_npcs)
    {
        const std::string oldZone = npc.currentZone;
        npc.currentZone.clear();

        const auto* active = SelectActiveSchedule(npc, season, isSunday, feastId, feastTags);
        if (!active)
            continue;

        for (const auto& e : *active)
        {
            if (e.phase == phase)
            {
                npc.currentZone = e.zoneId;
                break;
            }
        }

        if (npc.currentZone != oldZone)
        {
            npc.idleTimer = 0.0f;
        }
    }
}

void NpcManager::updateMovement(float dt, int tileSize)
{
    for (auto& npc : m_npcs)
    {
        npc.idleTimer -= dt;

        if (npc.idleTimer > 0.0f)
            continue;

        npc.idleTimer = 2.0f + static_cast<float>(RandomInt(0, 299)) / 100.0f;

        if (npc.currentZone.empty())
        {
            npc.targetX = npc.x;
            npc.targetY = npc.y;
            continue;
        }

        auto it = m_zones.find(npc.currentZone);
        if (it == m_zones.end())
        {
            npc.targetX = npc.x;
            npc.targetY = npc.y;
            continue;
        }

        const auto& zone = it->second;

        if (zone.maxX < zone.minX || zone.maxY < zone.minY)
        {
            npc.targetX = npc.x;
            npc.targetY = npc.y;
            continue;
        }

        const int tx = RandomInt(zone.minX, zone.maxX);
        const int ty = RandomInt(zone.minY, zone.maxY);

        npc.targetX = tx * tileSize + tileSize * 0.5f;
        npc.targetY = ty * tileSize + (float)tileSize;
    }
}

void NpcManager::stepMovement(float dt)
{
    const float speed = 30.0f;

    for (auto& npc : m_npcs)
    {
        const float dx = npc.targetX - npc.x;
        const float dy = npc.targetY - npc.y;
        const float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < 1.0f)
        {
            npc.x = npc.targetX;
            npc.y = npc.targetY;
            continue;
        }

        if (std::fabs(dx) > std::fabs(dy))
            npc.facing = (dx > 0.0f) ? 1.0f : 3.0f;
        else
            npc.facing = (dy > 0.0f) ? 2.0f : 0.0f;

        const float step = speed * dt;
        if (step >= dist)
        {
            npc.x = npc.targetX;
            npc.y = npc.targetY;
        }
        else
        {
            npc.x += dx / dist * step;
            npc.y += dy / dist * step;
        }

        npc.animTime += dt * 6.0f;
    }
}

void NpcManager::updateReactions(
    float playerX,
    float playerY,
    AudioManager& audio,
    int dayPhase)
{
    constexpr float awarenessRadius = 96.0f;
    constexpr float greetRadius = 64.0f;
    constexpr float dialogRadius = 42.0f;
    constexpr uint32_t greetingCooldownMs = 12000; // 12 sekund

    const uint32_t nowTicks = SDL_GetTicks();

    for (auto& npc : m_npcs)
    {
        npc.playerNearby = false;
        npc.playerInDialogRange = false;

        const bool inAwareness =
            playerInRange(npc.x, npc.y, playerX, playerY, awarenessRadius);

        const bool inGreet =
            playerInRange(npc.x, npc.y, playerX, playerY, greetRadius);

        const bool inDialog =
            playerInRange(npc.x, npc.y, playerX, playerY, dialogRadius);

        // mimo awareness radius: NPC hráèe ignoruje
        if (!inAwareness)
        {
            // po vypršení cooldownu dovol znovu budoucí pozdrav
            if (nowTicks >= npc.nextGreetingAllowedTime)
                npc.greetedPlayer = false;

            continue;
        }

        npc.playerNearby = true;
        npc.facing = computeFacing(npc.x, npc.y, playerX, playerY);

        if (inDialog)
            npc.playerInDialogRange = true;

        // v awareness, ale ne v greet radiusu -> sleduje hráèe, ale nezdraví
        if (!inGreet)
        {
            if (nowTicks >= npc.nextGreetingAllowedTime)
                npc.greetedPlayer = false;

            continue;
        }

        // pokud cooldown ještì bìží, nic nedìlej
        if (nowTicks < npc.nextGreetingAllowedTime)
            continue;

        // greeting jen jednou za cooldown
        if (!npc.greetedPlayer)
        {
            playGreeting(npc, audio, dayPhase);
            npc.greetedPlayer = true;
            npc.nextGreetingAllowedTime = nowTicks + greetingCooldownMs;
        }
    }
}

void NpcManager::render(SDL_Renderer* renderer, int camX, int camY, const CharacterManager& characterManager) const
{
    for (const auto& npc : m_npcs)
    {
        const CharacterDef* ch = characterManager.getCharacter(npc.characterId);
        SDL_Texture* tex = characterManager.getTextureForCharacter(npc.characterId);

        if (!ch || !tex)
            continue;

        std::string animName;

        const float dx = npc.targetX - npc.x;
        const float dy = npc.targetY - npc.y;
        const bool moving = (std::fabs(dx) > 2.0f || std::fabs(dy) > 2.0f);

        if (moving)
        {
            switch ((int)npc.facing)
            {
            case 0: animName = "walk_up"; break;
            case 1: animName = "walk_right"; break;
            case 2: animName = "walk_down"; break;
            case 3: animName = "walk_left"; break;
            default: animName = "walk_down"; break;
            }
        }
        else
        {
            switch ((int)npc.facing)
            {
            case 0: animName = "idle_up"; break;
            case 1: animName = "idle_right"; break;
            case 2: animName = "idle_down"; break;
            case 3: animName = "idle_left"; break;
            default: animName = "idle_down"; break;
            }
        }

        auto itAnim = ch->animations.find(animName);

        if (itAnim == ch->animations.end() || itAnim->second.frames.empty())
        {
            itAnim = ch->animations.find("idle_down");
            if (itAnim == ch->animations.end() || itAnim->second.frames.empty())
                continue;
        }

        const auto& frames = itAnim->second.frames;
        const int frameIndex = moving
            ? ((int)npc.animTime % (int)frames.size())
            : 0;

        const AnimFrame& fr = frames[frameIndex];

        SDL_Rect src{ fr.x, fr.y, fr.w, fr.h };
        SDL_Rect dst;
        dst.w = fr.w;
        dst.h = fr.h;
        dst.x = (int)std::lround(npc.x - fr.w * 0.5f) - camX;
        dst.y = (int)std::lround(npc.y - fr.h) - camY;

        SDL_RenderCopy(renderer, tex, &src, &dst);
    }
}