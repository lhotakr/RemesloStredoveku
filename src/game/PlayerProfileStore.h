#pragma once

#include "PlayerStats.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace playerprofile
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    inline fs::path ProjectRootPath()
    {
    #ifdef REMESLO_PROJECT_ROOT
        return fs::path(REMESLO_PROJECT_ROOT);
    #else
        return fs::current_path();
    #endif
    }

    inline fs::path ProfilesPath()
    {
        return ProjectRootPath() / "data" / "settings" / "player_profiles.json";
    }

    inline const char* BackgroundKey(PlayerStats::Background bg)
    {
        switch (bg)
        {
        case PlayerStats::Background::Survivalist:     return "survivalist";
        case PlayerStats::Background::ScholarAthlete:  return "scholar_athlete";
        case PlayerStats::Background::SocialAdaptable: return "social_adaptable";
        }
        return "scholar_athlete";
    }

    inline json LoadRoot()
    {
        const fs::path p = ProfilesPath();
        std::ifstream f(p, std::ios::binary);
        if (!f)
            return json::object({ {"profiles", json::object()} });

        try
        {
            json root;
            f >> root;
            if (!root.is_object())
                root = json::object();
            if (!root.contains("profiles") || !root["profiles"].is_object())
                root["profiles"] = json::object();
            return root;
        }
        catch (...)
        {
            return json::object({ {"profiles", json::object()} });
        }
    }

    inline bool SaveRoot(const json& root)
    {
        const fs::path p = ProfilesPath();
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);

        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;

        f << root.dump(2);
        return true;
    }

    inline float ClampStat(float value)
    {
        return std::clamp(value, 0.0f, 100.0f);
    }

    inline int ClampContainers(int value)
    {
        return std::clamp(value, 0, 8);
    }

    inline json SaveAttributes(const PlayerStats::CoreAttributes& v)
    {
        return json{
            {"strength", ClampStat(v.strength)},
            {"endurance", ClampStat(v.endurance)},
            {"dexterity", ClampStat(v.dexterity)},
            {"perception", ClampStat(v.perception)},
            {"intelligence", ClampStat(v.intelligence)},
            {"charisma", ClampStat(v.charisma)},
            {"willpower", ClampStat(v.willpower)}
        };
    }

    inline void LoadAttributes(const json& j, PlayerStats::CoreAttributes& v)
    {
        if (!j.is_object()) return;
        v.strength = ClampStat(j.value("strength", v.strength));
        v.endurance = ClampStat(j.value("endurance", v.endurance));
        v.dexterity = ClampStat(j.value("dexterity", v.dexterity));
        v.perception = ClampStat(j.value("perception", v.perception));
        v.intelligence = ClampStat(j.value("intelligence", v.intelligence));
        v.charisma = ClampStat(j.value("charisma", v.charisma));
        v.willpower = ClampStat(j.value("willpower", v.willpower));
    }

    inline json SaveSurvival(const PlayerStats::SurvivalSkills& v)
    {
        return json{
            {"fire_making", ClampStat(v.fireMaking)},
            {"wood_processing", ClampStat(v.woodProcessing)},
            {"shelter_building", ClampStat(v.shelterBuilding)},
            {"water_purification", ClampStat(v.waterPurification)},
            {"tracking", ClampStat(v.tracking)},
            {"navigation", ClampStat(v.navigation)},
            {"foraging", ClampStat(v.foraging)},
            {"cooking", ClampStat(v.cooking)},
            {"cold_resistance", ClampStat(v.coldResistance)},
            {"heat_resistance", ClampStat(v.heatResistance)},
            {"food_tolerance", ClampStat(v.foodTolerance)},
            {"disease_resistance", ClampStat(v.diseaseResistance)}
        };
    }

    inline void LoadSurvival(const json& j, PlayerStats::SurvivalSkills& v)
    {
        if (!j.is_object()) return;
        v.fireMaking = ClampStat(j.value("fire_making", v.fireMaking));
        v.woodProcessing = ClampStat(j.value("wood_processing", v.woodProcessing));
        v.shelterBuilding = ClampStat(j.value("shelter_building", v.shelterBuilding));
        v.waterPurification = ClampStat(j.value("water_purification", v.waterPurification));
        v.tracking = ClampStat(j.value("tracking", v.tracking));
        v.navigation = ClampStat(j.value("navigation", v.navigation));
        v.foraging = ClampStat(j.value("foraging", v.foraging));
        v.cooking = ClampStat(j.value("cooking", v.cooking));
        v.coldResistance = ClampStat(j.value("cold_resistance", v.coldResistance));
        v.heatResistance = ClampStat(j.value("heat_resistance", v.heatResistance));
        v.foodTolerance = ClampStat(j.value("food_tolerance", v.foodTolerance));
        v.diseaseResistance = ClampStat(j.value("disease_resistance", v.diseaseResistance));
    }

    inline json SaveCraft(const PlayerStats::CraftSkills& v)
    {
        return json{
            {"tool_repair", ClampStat(v.toolRepair)},
            {"clothing_repair", ClampStat(v.clothingRepair)},
            {"rope_work", ClampStat(v.ropeWork)},
            {"woodcraft", ClampStat(v.woodcraft)},
            {"improvisation", ClampStat(v.improvisation)}
        };
    }

    inline void LoadCraft(const json& j, PlayerStats::CraftSkills& v)
    {
        if (!j.is_object()) return;
        v.toolRepair = ClampStat(j.value("tool_repair", v.toolRepair));
        v.clothingRepair = ClampStat(j.value("clothing_repair", v.clothingRepair));
        v.ropeWork = ClampStat(j.value("rope_work", v.ropeWork));
        v.woodcraft = ClampStat(j.value("woodcraft", v.woodcraft));
        v.improvisation = ClampStat(j.value("improvisation", v.improvisation));
    }

    inline json SaveSocial(const PlayerStats::SocialSkills& v)
    {
        return json{
            {"conversation_initiation", ClampStat(v.conversationInitiation)},
            {"persuasion", ClampStat(v.persuasion)},
            {"negotiation", ClampStat(v.negotiation)},
            {"empathy", ClampStat(v.empathy)},
            {"etiquette", ClampStat(v.etiquette)},
            {"crowd_comfort", ClampStat(v.crowdComfort)},
            {"social_protocol", ClampStat(v.socialProtocol)}
        };
    }

    inline void LoadSocial(const json& j, PlayerStats::SocialSkills& v)
    {
        if (!j.is_object()) return;
        v.conversationInitiation = ClampStat(j.value("conversation_initiation", v.conversationInitiation));
        v.persuasion = ClampStat(j.value("persuasion", v.persuasion));
        v.negotiation = ClampStat(j.value("negotiation", v.negotiation));
        v.empathy = ClampStat(j.value("empathy", v.empathy));
        v.etiquette = ClampStat(j.value("etiquette", v.etiquette));
        v.crowdComfort = ClampStat(j.value("crowd_comfort", v.crowdComfort));
        v.socialProtocol = ClampStat(j.value("social_protocol", v.socialProtocol));
    }

    inline json SaveKnowledge(const PlayerStats::KnowledgeSkills& v)
    {
        return json{
            {"history", ClampStat(v.history)},
            {"religion_knowledge", ClampStat(v.religionKnowledge)},
            {"symbol_recognition", ClampStat(v.symbolRecognition)},
            {"literacy", ClampStat(v.literacy)},
            {"latin", ClampStat(v.latin)},
            {"german", ClampStat(v.german)},
            {"medieval_czech", ClampStat(v.medievalCzech)}
        };
    }

    inline void LoadKnowledge(const json& j, PlayerStats::KnowledgeSkills& v)
    {
        if (!j.is_object()) return;
        v.history = ClampStat(j.value("history", v.history));
        v.religionKnowledge = ClampStat(j.value("religion_knowledge", v.religionKnowledge));
        v.symbolRecognition = ClampStat(j.value("symbol_recognition", v.symbolRecognition));
        v.literacy = ClampStat(j.value("literacy", v.literacy));
        v.latin = ClampStat(j.value("latin", v.latin));
        v.german = ClampStat(j.value("german", v.german));
        v.medievalCzech = ClampStat(j.value("medieval_czech", v.medievalCzech));
    }

    inline json SavePhysical(const PlayerStats::PhysicalSkills& v)
    {
        return json{
            {"running", ClampStat(v.running)},
            {"load_handling", ClampStat(v.loadHandling)},
            {"climbing", ClampStat(v.climbing)},
            {"swimming", ClampStat(v.swimming)},
            {"sleep_recovery", ClampStat(v.sleepRecovery)},
            {"stamina_efficiency", ClampStat(v.staminaEfficiency)}
        };
    }

    inline void LoadPhysical(const json& j, PlayerStats::PhysicalSkills& v)
    {
        if (!j.is_object()) return;
        v.running = ClampStat(j.value("running", v.running));
        v.loadHandling = ClampStat(j.value("load_handling", v.loadHandling));
        v.climbing = ClampStat(j.value("climbing", v.climbing));
        v.swimming = ClampStat(j.value("swimming", v.swimming));
        v.sleepRecovery = ClampStat(j.value("sleep_recovery", v.sleepRecovery));
        v.staminaEfficiency = ClampStat(j.value("stamina_efficiency", v.staminaEfficiency));
    }

    inline json SaveMental(const PlayerStats::MentalSkills& v)
    {
        return json{
            {"focus", ClampStat(v.focus)},
            {"memory", ClampStat(v.memory)},
            {"stress_resistance", ClampStat(v.stressResistance)},
            {"adaptation", ClampStat(v.adaptation)},
            {"observation", ClampStat(v.observation)},
            {"loneliness_tolerance", ClampStat(v.lonelinessTolerance)}
        };
    }

    inline void LoadMental(const json& j, PlayerStats::MentalSkills& v)
    {
        if (!j.is_object()) return;
        v.focus = ClampStat(j.value("focus", v.focus));
        v.memory = ClampStat(j.value("memory", v.memory));
        v.stressResistance = ClampStat(j.value("stress_resistance", v.stressResistance));
        v.adaptation = ClampStat(j.value("adaptation", v.adaptation));
        v.observation = ClampStat(j.value("observation", v.observation));
        v.lonelinessTolerance = ClampStat(j.value("loneliness_tolerance", v.lonelinessTolerance));
    }

    inline json SaveComfort(const PlayerStats::ComfortProfile& v)
    {
        return json{
            {"nature_comfort", ClampStat(v.natureComfort)},
            {"urban_comfort", ClampStat(v.urbanComfort)},
            {"social_comfort", ClampStat(v.socialComfort)},
            {"hygiene_sensitivity", ClampStat(v.hygieneSensitivity)},
            {"medieval_adaptation", ClampStat(v.medievalAdaptation)}
        };
    }

    inline void LoadComfort(const json& j, PlayerStats::ComfortProfile& v)
    {
        if (!j.is_object()) return;
        v.natureComfort = ClampStat(j.value("nature_comfort", v.natureComfort));
        v.urbanComfort = ClampStat(j.value("urban_comfort", v.urbanComfort));
        v.socialComfort = ClampStat(j.value("social_comfort", v.socialComfort));
        v.hygieneSensitivity = ClampStat(j.value("hygiene_sensitivity", v.hygieneSensitivity));
        v.medievalAdaptation = ClampStat(j.value("medieval_adaptation", v.medievalAdaptation));
    }

    inline json SaveLoadout(const PlayerStats::StartingLoadout& v)
    {
        return json{
            {"hammock", v.hammock},
            {"tarp", v.tarp},
            {"water_filter", v.waterFilter},
            {"wood_stove", v.woodStove},
            {"gas_stove", v.gasStove},
            {"ration_heater", v.rationHeater},
            {"quality_cook_pot", v.qualityCookPot},
            {"simple_cook_pot", v.simpleCookPot},
            {"strong_knife", v.strongKnife},
            {"simple_knife", v.simpleKnife},
            {"rope", v.rope},
            {"power_bank", v.powerBank},
            {"solar_panel", v.solarPanel},
            {"smartphone_offline", v.smartphoneOffline},
            {"travel_shower", v.travelShower},
            {"hygiene_kit", v.hygieneKit},
            {"water_containers", ClampContainers(v.waterContainers)},
            {"bedding_warmth", ClampStat(v.beddingWarmth)},
            {"tarp_quality", ClampStat(v.tarpQuality)}
        };
    }

    inline void LoadLoadout(const json& j, PlayerStats::StartingLoadout& v)
    {
        if (!j.is_object()) return;
        v.hammock = j.value("hammock", v.hammock);
        v.tarp = j.value("tarp", v.tarp);
        v.waterFilter = j.value("water_filter", v.waterFilter);
        v.woodStove = j.value("wood_stove", v.woodStove);
        v.gasStove = j.value("gas_stove", v.gasStove);
        v.rationHeater = j.value("ration_heater", v.rationHeater);
        v.qualityCookPot = j.value("quality_cook_pot", v.qualityCookPot);
        v.simpleCookPot = j.value("simple_cook_pot", v.simpleCookPot);
        v.strongKnife = j.value("strong_knife", v.strongKnife);
        v.simpleKnife = j.value("simple_knife", v.simpleKnife);
        v.rope = j.value("rope", v.rope);
        v.powerBank = j.value("power_bank", v.powerBank);
        v.solarPanel = j.value("solar_panel", v.solarPanel);
        v.smartphoneOffline = j.value("smartphone_offline", v.smartphoneOffline);
        v.travelShower = j.value("travel_shower", v.travelShower);
        v.hygieneKit = j.value("hygiene_kit", v.hygieneKit);
        v.waterContainers = ClampContainers(j.value("water_containers", v.waterContainers));
        v.beddingWarmth = ClampStat(j.value("bedding_warmth", v.beddingWarmth));
        v.tarpQuality = ClampStat(j.value("tarp_quality", v.tarpQuality));
    }

    inline json SaveProfile(const PlayerStats& stats)
    {
        return json{
            {"attributes", SaveAttributes(stats.attributes)},
            {"condition", json{
                {"morale", ClampStat(stats.condition.morale)},
                {"hygiene", ClampStat(stats.condition.hygiene)}
            }},
            {"survival", SaveSurvival(stats.survival)},
            {"craft", SaveCraft(stats.craft)},
            {"social", SaveSocial(stats.social)},
            {"knowledge", SaveKnowledge(stats.knowledge)},
            {"physical", SavePhysical(stats.physical)},
            {"mental", SaveMental(stats.mental)},
            {"comfort", SaveComfort(stats.comfort)},
            {"loadout", SaveLoadout(stats.loadout)}
        };
    }

    inline void ApplyProfileJson(PlayerStats& stats, const json& profile)
    {
        if (!profile.is_object())
            return;

        LoadAttributes(profile.value("attributes", json::object()), stats.attributes);

        if (profile.contains("condition") && profile["condition"].is_object())
        {
            const auto& c = profile["condition"];
            stats.condition.morale = ClampStat(c.value("morale", stats.condition.morale));
            stats.condition.hygiene = ClampStat(c.value("hygiene", stats.condition.hygiene));
        }

        LoadSurvival(profile.value("survival", json::object()), stats.survival);
        LoadCraft(profile.value("craft", json::object()), stats.craft);
        LoadSocial(profile.value("social", json::object()), stats.social);
        LoadKnowledge(profile.value("knowledge", json::object()), stats.knowledge);
        LoadPhysical(profile.value("physical", json::object()), stats.physical);
        LoadMental(profile.value("mental", json::object()), stats.mental);
        LoadComfort(profile.value("comfort", json::object()), stats.comfort);
        LoadLoadout(profile.value("loadout", json::object()), stats.loadout);

        stats.recomputeDerivedStats();
        stats.refillVitals();
    }

    inline bool ApplySavedOverride(PlayerStats& stats)
    {
        const json root = LoadRoot();
        const std::string key = BackgroundKey(stats.background);

        if (!root.contains("profiles") || !root["profiles"].is_object())
            return false;

        const auto& profiles = root["profiles"];
        if (!profiles.contains(key))
            return false;

        ApplyProfileJson(stats, profiles.at(key));
        return true;
    }

    inline PlayerStats LoadProfileForBackground(PlayerStats::Background background)
    {
        PlayerStats stats;
        stats.applyBackgroundPreset(background);
        ApplySavedOverride(stats);
        return stats;
    }

    inline bool SaveProfileOverride(const PlayerStats& stats)
    {
        json root = LoadRoot();
        root["profiles"][BackgroundKey(stats.background)] = SaveProfile(stats);
        return SaveRoot(root);
    }

    inline bool ResetProfileOverride(PlayerStats::Background background)
    {
        json root = LoadRoot();
        const std::string key = BackgroundKey(background);
        if (root.contains("profiles") && root["profiles"].is_object())
            root["profiles"].erase(key);
        return SaveRoot(root);
    }
}
