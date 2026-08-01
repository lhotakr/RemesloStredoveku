#pragma once
#include <SDL.h>
#include "../audio/AudioManager.h"

#include <string>
#include <vector>
#include <unordered_map>

class CharacterManager;

struct NpcTypeDef
{
    std::string typeId;
    std::string name;
    std::string characterId;
    int defaultHP = 100;
    int defaultMood = 50;
    std::string defaultScriptId;
};

struct NpcZone
{
    std::string id;

    int minX = 0;
    int minY = 0;

    int maxX = 0;
    int maxY = 0;
};

struct NpcPhaseScheduleEntry
{
    std::string phase;
    std::string zoneId;
};

struct NpcSeasonSchedule
{
    std::vector<NpcPhaseScheduleEntry> spring;
    std::vector<NpcPhaseScheduleEntry> summer;
    std::vector<NpcPhaseScheduleEntry> autumn;
    std::vector<NpcPhaseScheduleEntry> winter;
};

struct NpcSpecialSchedule
{
    std::vector<NpcPhaseScheduleEntry> sunday;
    std::vector<NpcPhaseScheduleEntry> feastDefault;
    std::unordered_map<std::string, std::vector<NpcPhaseScheduleEntry>> feastById;
    std::unordered_map<std::string, std::vector<NpcPhaseScheduleEntry>> feastByTag;
};

struct NpcInstance
{   
	std::string id;     // unikátní id instance, pro referenci a debug, musí být unikátní mezi všemi NPC v mapì  
	std::string npcId;  // pùvodní id z mapy, pro referenci a debug, nemusí být unikátní
	std::string typeId; // odkaz na typ NPC, pro získání spoleèných vlastností a chování

    std::string name;
    std::string surname;
    std::string greeting;

    std::string scriptId;
    std::string characterId;

    float x = 0.0f;
    float y = 0.0f;

    int hp = 100;
    int mood = 50;

    float facing = 2.0f; // 0=up 1=right 2=down 3=left

    bool greetedPlayer = false;
    uint32_t nextGreetingAllowedTime = 0;

    bool playerNearby = false;
    bool playerInDialogRange = false;

    NpcSeasonSchedule seasonSchedule;
    NpcSpecialSchedule specialSchedule;
    std::string currentZone;

    float targetX = 0.0f;
    float targetY = 0.0f;
    float animTime = 0.0f;
    float idleTimer = 0.0f;

    float awarenessRadius = 96.0f;
    float greetingRadius = 64.0f;
    float dialogRadius = 42.0f;

    std::string displayName() const
    {
        if (!name.empty() && !surname.empty())
            return name + " " + surname;

        if (!name.empty())
            return name;

        if (!npcId.empty())
			return npcId;

        return id;
    }
};

class NpcManager
{
public:
	

    bool loadTypes(const std::string& path, std::string* outError = nullptr);
    bool loadSpawns(const std::string& path, int tileSize, std::string* outError = nullptr);

    void clear();
    void loadNpcVoices(AudioManager& audio);
    void playGreeting(NpcInstance& npc, AudioManager& audio, int dayPhase);

    bool loadZones(const std::string& path);
    bool loadSchedules(const std::string& path);

    NpcInstance* findNpcById(const std::string& id);

    void updateSchedules(const std::string& season, const std::string& phase, bool isSunday, const std::string& feastId, const std::vector<std::string>& feastTags);

    void updateMovement(float dt, int tileSize);
    void stepMovement(float dt);

    void updateReactions(
        float playerX,
        float playerY,
		AudioManager& audio,
        int dayPhase
    );

    void render(SDL_Renderer* renderer, int camX, int camY, const CharacterManager& characterManager) const;

    const std::vector<NpcInstance>& npcs() const { return m_npcs; }
    std::vector<NpcInstance>& npcs() { return m_npcs; }

private:
    std::unordered_map<std::string, NpcTypeDef> m_types;
    std::vector<NpcInstance> m_npcs;
    std::unordered_map<std::string, NpcZone> m_zones;
};