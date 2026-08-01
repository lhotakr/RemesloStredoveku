#include "NpcDefinition.h"
#include "JsonUtils.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
    static void LoadStringVector(const json& arr, std::vector<std::string>& out)
    {
        out.clear();

        if (!arr.is_array())
            return;

        for (const auto& v : arr)
        {
            if (v.is_string())
                out.push_back(v.get<std::string>());
        }
    }

    static void LoadStringMap(const json& obj, std::unordered_map<std::string, std::string>& out)
    {
        out.clear();

        if (!obj.is_object())
            return;

        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            if (it.value().is_string())
                out[it.key()] = it.value().get<std::string>();
        }
    }

    static void LoadFloatMap(const json& obj, std::unordered_map<std::string, float>& out)
    {
        out.clear();

        if (!obj.is_object())
            return;

        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            if (it.value().is_number())
                out[it.key()] = it.value().get<float>();
        }
    }
}

void NpcDefinitionRegistry::clear()
{
    m_definitions.clear();
}

bool NpcDefinitionRegistry::loadFromFile(const std::string& path, std::string* outError)
{
    json root;
    std::string err;

    if (!jsonutils::LoadJsonFileSafe(path, root, err))
    {
        if (outError) *outError = "NpcDefinitionRegistry: " + err;
        return false;
    }

    if (!root.contains("npcs") || !root["npcs"].is_array())
    {
        if (outError) *outError = "NpcDefinitionRegistry: chybi pole 'npcs' v " + path;
        return false;
    }

    m_definitions.clear();

    for (const auto& jNpc : root["npcs"])
    {
        NpcDefinition def;

        def.npcId = jNpc.value("npc_id", "");
        def.typeId = jNpc.value("type_id", "");
        def.scriptId = jNpc.value("script_id", "");
        def.characterId = jNpc.value("character_id", "");

        if (def.npcId.empty())
            continue;

        if (jNpc.contains("identity") && jNpc["identity"].is_object())
        {
            const auto& ji = jNpc["identity"];
            def.identity.name = ji.value("name", "");
            def.identity.surname = ji.value("surname", "");
            def.identity.age = ji.value("age", 0);
            def.identity.gender = ji.value("gender", "");
            def.identity.profession = ji.value("profession", "");
            def.identity.socialStatus = ji.value("social_status", "");
            def.identity.authorityLevel = ji.value("authority_level", "");
            def.identity.temperament = ji.value("temperament", "");
        }

        if (jNpc.contains("household") && jNpc["household"].is_object())
        {
            const auto& jh = jNpc["household"];
            def.household.householdId = jh.value("household_id", "");
            def.household.roleInHousehold = jh.value("role_in_household", "");
        }

        if (jNpc.contains("system_roles"))
            LoadStringVector(jNpc["system_roles"], def.systemRoles);

        if (jNpc.contains("schedule_profile") && jNpc["schedule_profile"].is_object())
        {
            const auto& js = jNpc["schedule_profile"];

            if (js.contains("default"))
                LoadStringMap(js["default"], def.scheduleProfile.defaultPhaseZones);

            if (js.contains("sunday"))
                LoadStringMap(js["sunday"], def.scheduleProfile.sundayPhaseZones);

            if (js.contains("seasonal_focus"))
                LoadStringMap(js["seasonal_focus"], def.scheduleProfile.seasonalFocus);
        }

        if (jNpc.contains("relationships") && jNpc["relationships"].is_object())
        {
            const auto& jr = jNpc["relationships"];

            if (jr.contains("family"))        LoadStringVector(jr["family"], def.relationships.family);
            if (jr.contains("respects"))      LoadStringVector(jr["respects"], def.relationships.respects);
            if (jr.contains("trusts"))        LoadStringVector(jr["trusts"], def.relationships.trusts);
            if (jr.contains("friends"))       LoadStringVector(jr["friends"], def.relationships.friends);
            if (jr.contains("tensions"))      LoadStringVector(jr["tensions"], def.relationships.tensions);
            if (jr.contains("distance"))      LoadStringVector(jr["distance"], def.relationships.distance);
            if (jr.contains("practical_links")) LoadStringVector(jr["practical_links"], def.relationships.practicalLinks);
            if (jr.contains("watches"))       LoadStringVector(jr["watches"], def.relationships.watches);
            if (jr.contains("skeptical_of"))  LoadStringVector(jr["skeptical_of"], def.relationships.skepticalOf);
            if (jr.contains("curious_about")) LoadStringVector(jr["curious_about"], def.relationships.curiousAbout);
            if (jr.contains("observed_by"))   LoadStringVector(jr["observed_by"], def.relationships.observedBy);
        }

        if (jNpc.contains("reputation_sensitivity") && jNpc["reputation_sensitivity"].is_object())
        {
            LoadFloatMap(jNpc["reputation_sensitivity"], def.reputationSensitivity.weights);
        }

        if (jNpc.contains("language_profile") && jNpc["language_profile"].is_object())
        {
            const auto& jl = jNpc["language_profile"];
            def.languageProfile.primaryLanguage = jl.value("primary_language", "");
            def.languageProfile.secondaryLanguage = jl.value("secondary_language", "");
            def.languageProfile.learnedLanguage = jl.value("learned_language", "");
            def.languageProfile.speechStyle = jl.value("speech_style", "");
            def.languageProfile.errorTolerance = jl.value("error_tolerance", 0.5f);
        }

        if (jNpc.contains("access_profile") && jNpc["access_profile"].is_object())
        {
            const auto& ja = jNpc["access_profile"];

            def.accessProfile.allowsPublicTalk =
                ja.value("allows_public_talk", true);

            def.accessProfile.allowsYardTalk =
                ja.value("allows_yard_talk", true);

            def.accessProfile.allowsHouseTalk =
                ja.value("allows_house_talk", false);

            def.accessProfile.requiresTrustForHouse =
                ja.value("requires_trust_for_house", true);

            def.accessProfile.minTrustForHouse =
                ja.value("min_trust_for_house", 20.0f);
        }

        if (jNpc.contains("dialogue_hooks"))
            LoadStringVector(jNpc["dialogue_hooks"], def.dialogueHooks);

        if (jNpc.contains("quest_hooks"))
            LoadStringVector(jNpc["quest_hooks"], def.questHooks);

        m_definitions[def.npcId] = std::move(def);
    }

    if (m_definitions.empty())
    {
        if (outError) *outError = "NpcDefinitionRegistry: nenactena zadna NPC definice z " + path;
        return false;
    }

    return true;
}

const NpcDefinition* NpcDefinitionRegistry::findByNpcId(const std::string& npcId) const
{
    auto it = m_definitions.find(npcId);
    if (it == m_definitions.end())
        return nullptr;

    return &it->second;
}