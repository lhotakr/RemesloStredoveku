#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// Runtime forage data model shared by editor-facing JSON loaders and Campaign gameplay.
// Editor/tooling owns authoring UX; these structs are intentionally plain data.

enum class ForageCategory
{
    Mushroom,
    Herb
};

struct ForageTraitOption
{
    std::string id;
    std::string label;
};

struct ForageTraitGroup
{
    std::string id;
    std::string label;
    std::vector<ForageTraitOption> options;
};

struct ForageArchetypeDef
{
    std::string id;
    ForageCategory category = ForageCategory::Herb;

    std::string displayUnknown;
    std::string displayPartial;

    std::string genericMapSprite;
    std::string detailPlaceholderSprite;
    std::string herbariumPlaceholderSprite;

    // Map rendering scale for generic map sprites. Saved by the editor as map_scale.
    float mapScale = 0.35f;

    // Lightweight logistics defaults used when a generic unknown sample is collected.
    // Species can override these values.
    float weight = 0.05f;
    float volume = 0.10f;
    int maxStack = 64;

    std::vector<std::string> examinationSlots;
    std::unordered_map<std::string, std::vector<std::string>> defaultTraits;
};

struct ForageSpeciesDef
{
    std::string id;
    std::string archetypeId;

    std::string trueName;
    std::vector<std::string> folkNames;

    std::string detailSprite;
    std::string inventorySprite;
    std::string herbariumSprite;

    int difficulty = 25;

    // Free authoring text shown to the player during identification.
    // This is used by the minigame as another fuzzy-match source.
    std::string description;

    std::unordered_map<std::string, std::vector<std::string>> traits;

    std::string edibility = "unknown_safe";
    std::string medicinalValue = "none";
    int toxicityLevel = 0;

    // Inventory/logistics. If missing in JSON, defaults are inherited from archetype.
    float weight = 0.0f;
    float volume = 0.0f;
    int maxStack = 64;

    std::vector<std::string> effectsOnEat;
    std::vector<std::string> effectsOnUse;
    std::vector<std::string> season;
};

struct ForageSpawnDef
{
    std::string id;
    int tileX = 0;
    int tileY = 0;

    std::string archetypeId;
    std::vector<std::string> speciesPool;

    std::string genericMapSpriteOverride;

    // 0 = inherit from archetype mapScale.
    float mapScaleOverride = 0.0f;

    std::vector<std::string> seasonMask;

    int respawnDays = 0;
    bool gatherOnce = false;
    int quantityMin = 1;
    int quantityMax = 1;
    int rarity = 50;

    bool requiresExamination = true;
};

struct ForageExaminationAnswer
{
    std::string traitGroupId;

    // Backward-compatible option id for the older combo-box based version.
    std::string optionId;

    // New free-text answer. The minigame evaluates this against species traits.
    std::string freeText;
};

struct ForageIdentificationInput
{
    std::unordered_map<std::string, std::string> traitTexts;
    std::string descriptionText;
    std::string nameGuess;
};

enum class KnowledgeLevel
{
    Unknown,
    Seen,
    Examined,
    Verified
};

struct PlayerForageKnowledgeEntry
{
    KnowledgeLevel knowledgeLevel = KnowledgeLevel::Unknown;
    int timesSeen = 0;
    int timesExamined = 0;
    int timesVerified = 0;
    std::unordered_set<std::string> knownTraits;
};

struct PlayerForageKnowledgeState
{
    std::unordered_map<std::string, PlayerForageKnowledgeEntry> entries;
};

struct ForageExaminationResult
{
    bool success = false;
    bool revealedNewKnowledge = false;
    bool verifiedSpecies = false;

    int correctTraits = 0;
    int totalTraits = 0;

    // 0-100 score for UI feedback.
    int scorePercent = 0;
    float traitScore = 0.0f;
    float descriptionScore = 0.0f;
    float nameScore = 0.0f;
    float skillBonus = 0.0f;
    float natureBonus = 0.0f;

    // Signed modifier from species.difficulty / commonness.
    // difficulty is authored from -100..100: negative = rare / hard to identify, positive = common / easier.
    float difficultyModifier = 0.0f;
    float difficultyPenalty = 0.0f; // legacy mirror for older UI snippets; kept as absolute negative part.

    std::vector<std::string> newlyKnownTraits;
    std::vector<std::string> feedbackLines;
    std::string feedbackText;
};
