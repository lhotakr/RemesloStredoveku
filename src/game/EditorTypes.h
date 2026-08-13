#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include "terrain/TileMap.h"


// NPCs
struct NpcSpawn
{
    std::string id;
    std::string typeId;

    std::string name;
    std::string surname;
    std::string greeting;

    std::string scriptId;
    std::string characterId;

    int tileX = 0;
    int tileY = 0;

    int hp = 100;
    int mood = 50;
};

struct EditorNpcType
{
    std::string typeId;
    std::string name;
    std::string characterId;
    std::string defaultScriptId;
    int defaultHP = 100;
    int defaultMood = 50;
};

struct EditorNpcZone
{
    std::string id;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
};

struct EditorNpcPhaseEntry
{
    std::string phase;
    std::string zoneId;
};

struct EditorNpcSchedule
{
    std::string npcId;

    std::vector<EditorNpcPhaseEntry> spring;
    std::vector<EditorNpcPhaseEntry> summer;
    std::vector<EditorNpcPhaseEntry> autumn;
    std::vector<EditorNpcPhaseEntry> winter;

    std::vector<EditorNpcPhaseEntry> sunday;
    std::vector<EditorNpcPhaseEntry> feastDefault;
};

// Dialogs
struct EditorDialogChoice
{
    std::string text;
    std::string next;
    std::string style;
    int npcMoodDelta = 0;
    std::string setFlag;
    std::vector<std::string> setFlags;

    std::string requireFlag;
    std::vector<std::string> requireFlags;
    std::string forbidFlag;
    std::vector<std::string> forbidFlags;
    int requireMoodMin = 0;
    bool closeDialog = false;
    std::string setNpcScript;
    std::string setNpcGreeting;
};

struct EditorDialogNode
{
    std::string id;
    std::string speaker;
    std::string text;
    std::string requireFlag;
    std::vector<std::string> requireFlags;
    std::string forbidFlag;
    std::vector<std::string> forbidFlags;
    std::vector<EditorDialogChoice> choices;
};

struct EditorDialogDef
{
    std::string dialogId;
    std::string startNode;
    std::vector<EditorDialogNode> nodes;
};

// Undo
struct EditorUndoState
{
    TileMap map{ 64, 64, 32 };
    std::vector<NpcSpawn> npcSpawns;
    std::vector<EditorNpcZone> npcZones;
    std::vector<EditorNpcSchedule> npcSchedules;
    int selectedNpcSpawnIndex = -1;
    int selectedNpcZoneIndex = -1;
    int selectedNpcScheduleIndex = -1;
    std::string label;
};

// Quests
struct EditorQuestDef
{
    std::string id;
    std::string title;
    std::string description;
    std::string startedFlag;
    std::string readyFlag;
    std::string doneFlag;
};

// Foraging - herbs & fungi

struct EditorForageArchetype
{
    std::string id;
    std::string category;
    std::string displayUnknown;
    std::string displayPartial;
    std::string genericMapSprite;
    std::string detailPlaceholderSprite;
    std::string herbariumPlaceholderSprite;
    std::vector<std::string> examinationSlots;
};

struct EditorForageSpecies
{
    std::string id;
    std::string archetypeId;
    std::string trueName;
    std::vector<std::string> folkNames;
    std::string detailSprite;
    std::string inventorySprite;
    std::string herbariumSprite;
    int difficulty = 25;
    std::unordered_map<std::string, std::vector<std::string>> traits;
    std::string edibility;
    std::string medicinalValue;
    int toxicityLevel = 0;
    std::vector<std::string> effectsOnEat;
    std::vector<std::string> effectsOnUse;
    std::vector<std::string> season;
};

struct EditorForageSpawn
{
    std::string id;
    int tileX = 0;
    int tileY = 0;
    std::string archetypeId;
    std::vector<std::string> speciesPool;
    std::string genericMapSpriteOverride;
    std::vector<std::string> seasonMask;
    int respawnDays = 0;
    bool gatherOnce = false;
    int quantityMin = 1;
    int quantityMax = 1;
    int rarity = 50;
    bool requiresExamination = true;
};
