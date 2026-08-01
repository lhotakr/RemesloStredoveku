#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct NpcIdentity
{
    std::string name;
    std::string surname;
    int age = 0;
    std::string gender;
    std::string profession;
    std::string socialStatus;
    std::string authorityLevel;
    std::string temperament;
};

struct NpcHouseholdLink
{
    std::string householdId;
    std::string roleInHousehold;
};

struct NpcScheduleProfile
{
    // napø. "morning" -> "farmyard"
    std::unordered_map<std::string, std::string> defaultPhaseZones;

    // napø. "morning" -> "church"
    std::unordered_map<std::string, std::string> sundayPhaseZones;

    // napø. "spring" -> "sowing"
    std::unordered_map<std::string, std::string> seasonalFocus;
};

struct NpcRelationships
{
    std::vector<std::string> family;
    std::vector<std::string> respects;
    std::vector<std::string> trusts;
    std::vector<std::string> friends;
    std::vector<std::string> tensions;
    std::vector<std::string> distance;
    std::vector<std::string> practicalLinks;
    std::vector<std::string> watches;
    std::vector<std::string> skepticalOf;
    std::vector<std::string> curiousAbout;
    std::vector<std::string> observedBy;
};

struct NpcReputationSensitivity
{
    // napø. "work_ethic" -> 0.95
    std::unordered_map<std::string, float> weights;
};

struct NpcLanguageProfile
{
    std::string primaryLanguage;
    std::string secondaryLanguage;
    std::string learnedLanguage;
    std::string speechStyle;
    float errorTolerance = 0.5f;
};

struct NpcAccessProfile
{
    bool allowsPublicTalk = true;
    bool allowsYardTalk = true;
    bool allowsHouseTalk = false;
    bool requiresTrustForHouse = true;
    float minTrustForHouse = 20.0f;
};

struct NpcDefinition
{
    std::string npcId;
    std::string typeId;
    std::string scriptId;
    std::string characterId;

    NpcIdentity identity;
    NpcHouseholdLink household;
    std::vector<std::string> systemRoles;
    NpcScheduleProfile scheduleProfile;
    NpcRelationships relationships;
    NpcReputationSensitivity reputationSensitivity;
    NpcLanguageProfile languageProfile;
    NpcAccessProfile accessProfile;
    std::vector<std::string> dialogueHooks;
    std::vector<std::string> questHooks;

    std::string displayName() const
    {
        if (!identity.name.empty() && !identity.surname.empty())
            return identity.name + " " + identity.surname;
        if (!identity.name.empty())
            return identity.name;
        return npcId;
    }
};

class NpcDefinitionRegistry
{
public:
    bool loadFromFile(const std::string& path, std::string* outError = nullptr);
    void clear();

    const NpcDefinition* findByNpcId(const std::string& npcId) const;
    const std::unordered_map<std::string, NpcDefinition>& all() const { return m_definitions; }

private:
    std::unordered_map<std::string, NpcDefinition> m_definitions;
};