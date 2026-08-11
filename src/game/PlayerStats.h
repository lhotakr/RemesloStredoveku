#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

struct PlayerStats
{
    enum class Background : std::uint8_t
    {
        Survivalist,
        ScholarAthlete,
        SocialAdaptable
    };

    enum class SurvivalTier : std::uint8_t
    {
        Excellent,
        Good,
        Average,
        Poor,
        Critical
    };

    struct CoreAttributes
    {
        float strength = 50.0f;
        float endurance = 50.0f;
        float dexterity = 50.0f;
        float perception = 50.0f;
        float intelligence = 50.0f;
        float charisma = 50.0f;
        float willpower = 50.0f;
    };

    struct ConditionState
    {
        float health = 100.0f;
        float stamina = 100.0f;
        float fatigue = 0.0f;            // 0 = rested, 100 = exhausted
        float nutrition = 100.0f;        // 0 = starving, 100 = full
        float hydration = 100.0f;        // 0 = dehydrated, 100 = hydrated
        float bodyTemperature = 50.0f;   // 0 = freezing, 50 = neutral, 100 = overheated
        float hygiene = 70.0f;
        float morale = 60.0f;
        float stress = 15.0f;
        float wetness = 0.0f;
        float diseaseLoad = 0.0f;
        float pain = 0.0f;

        bool poisoned = false;
        bool injured = false;
        bool fracture = false;
        bool bleeding = false;
        bool treatedWound = false;
    };

    struct SurvivalSkills
    {
        float fireMaking = 0.0f;
        float woodProcessing = 0.0f;
        float shelterBuilding = 0.0f;
        float waterPurification = 0.0f;
        float tracking = 0.0f;
        float navigation = 0.0f;
        float foraging = 0.0f;
        float cooking = 0.0f;
        float coldResistance = 0.0f;
        float heatResistance = 0.0f;
        float foodTolerance = 0.0f;
        float diseaseResistance = 0.0f;
    };

    struct CraftSkills
    {
        float toolRepair = 0.0f;
        float clothingRepair = 0.0f;
        float ropeWork = 0.0f;
        float woodcraft = 0.0f;
        float improvisation = 0.0f;
    };

    struct SocialSkills
    {
        float conversationInitiation = 0.0f;
        float persuasion = 0.0f;
        float negotiation = 0.0f;
        float empathy = 0.0f;
        float etiquette = 0.0f;
        float crowdComfort = 0.0f;
        float socialProtocol = 0.0f;
    };

    struct KnowledgeSkills
    {
        float history = 0.0f;
        float religionKnowledge = 0.0f;
        float symbolRecognition = 0.0f;
        float literacy = 0.0f;
        float latin = 0.0f;
        float german = 0.0f;
        float medievalCzech = 0.0f;
    };

    struct PhysicalSkills
    {
        float running = 0.0f;
        float loadHandling = 0.0f;
        float climbing = 0.0f;
        float swimming = 0.0f;
        float sleepRecovery = 0.0f;
        float staminaEfficiency = 0.0f;
    };

    struct MentalSkills
    {
        float focus = 0.0f;
        float memory = 0.0f;
        float stressResistance = 0.0f;
        float adaptation = 0.0f;
        float observation = 0.0f;
        float lonelinessTolerance = 0.0f;
    };

    struct ComfortProfile
    {
        float natureComfort = 50.0f;
        float urbanComfort = 50.0f;
        float socialComfort = 50.0f;
        float hygieneSensitivity = 50.0f;
        float medievalAdaptation = 50.0f;
    };

    struct StartingLoadout
    {
        bool hammock = true;
        bool tarp = true;
        bool waterFilter = true;
        bool woodStove = false;
        bool gasStove = false;
        bool rationHeater = false;
        bool qualityCookPot = false;
        bool simpleCookPot = false;
        bool strongKnife = false;
        bool simpleKnife = false;
        bool rope = false;
        bool powerBank = false;
        bool solarPanel = false;
        bool smartphoneOffline = false;
        bool travelShower = false;
        bool hygieneKit = false;
        int waterContainers = 1;
        float beddingWarmth = 20.0f;
        float tarpQuality = 30.0f;
    };

    Background background = Background::ScholarAthlete;
    CoreAttributes attributes;
    ConditionState condition;
    SurvivalSkills survival;
    CraftSkills craft;
    SocialSkills social;
    KnowledgeSkills knowledge;
    PhysicalSkills physical;
    MentalSkills mental;
    ComfortProfile comfort;
    StartingLoadout loadout;

	float moveSpeedBase = 60.0f;    // base movement speed in pixels per second
	float carryWeight = 0.0f;       // how much weight the player is currently carrying
	float carryCapacity = 30.0f;    // how much weight the player can carry before being encumbered
	float carryVolume = 0.0f;       // how much volume the player is currently carrying
	float carryVolumeCapacity = 45.0f;  // how much volume the player can carry before being encumbered
	float outfitAuthenticity = 0.0f;    // how authentic the player's clothing and gear are for the medieval setting, which can affect social interactions and comfort
	float villageAcceptance = 0.0f;     // how accepted the player is by the local village community, which can affect access to resources and information
	float churchTrust = 0.0f;       // how much the local church trusts the player, which can affect access to healing and spiritual support
	float reeveTrust = 0.0f;        // how much the local reeve (magistrate) trusts the player, which can affect legal protection and access to certain areas
	float fear = 10.0f;             // how fearful the player is of the dangers in the environment, which can affect decision-making and interactions
	float walkSpeedFactor = 1.0f;   // walking speed is calculated as walkSpeedBase * walkSpeedFactor, and can be modified by conditions like fatigue, encumbrance, injuries, etc.
	float runSpeedFactor = 1.5f;    // running speed is calculated as walkSpeedBase * runSpeedFactor, so it can be used to balance the two speeds against each other

    float getWalkSpeed() const
    {
        return getMoveSpeed() * walkSpeedFactor;
    }

    float getRunSpeed() const
    {
        return getMoveSpeed() * runSpeedFactor;
    }

    float getLimitedMoveSpeed(bool running) const
    {
        float speed = running ? getRunSpeed() : getWalkSpeed();

        if (condition.nutrition <= 40.0f) speed -= 12.0f;
        if (condition.hydration <= 50.0f) speed -= 18.0f;
        if (condition.fatigue >= 70.0f)   speed -= 15.0f;
        if (isOverweight())               speed -= 12.0f;

        return std::max(20.0f, speed);
    }

    static constexpr float kMinValue = 0.0f;
    static constexpr float kMaxValue = 100.0f;

    static float Clamp01To100(float v)
    {
        return std::clamp(v, kMinValue, kMaxValue);
    }

    void applyBackgroundPreset(Background preset)
    {
        *this = PlayerStats{};
        background = preset;

        switch (preset)
        {
        case Background::Survivalist:
            attributes = { 60.0f, 70.0f, 55.0f, 60.0f, 40.0f, 35.0f, 65.0f };
            condition.morale = 62.0f;
            condition.hygiene = 58.0f;
            survival = { 82.0f, 78.0f, 74.0f, 80.0f, 66.0f, 72.0f, 58.0f, 55.0f, 70.0f, 52.0f, 78.0f, 58.0f };
            craft = { 72.0f, 54.0f, 66.0f, 76.0f, 84.0f };
            social = { 28.0f, 34.0f, 32.0f, 44.0f, 26.0f, 30.0f, 22.0f };
            knowledge = { 18.0f, 20.0f, 24.0f, 32.0f, 0.0f, 0.0f, 18.0f };
            physical = { 56.0f, 62.0f, 48.0f, 36.0f, 62.0f, 54.0f };
            mental = { 52.0f, 48.0f, 60.0f, 58.0f, 54.0f, 82.0f };
            comfort = { 88.0f, 28.0f, 34.0f, 22.0f, 66.0f };
            loadout.hammock = true;
            loadout.tarp = true;
            loadout.waterFilter = true;
            loadout.woodStove = true;
            loadout.qualityCookPot = true;
            loadout.strongKnife = true;
            loadout.rope = true;
            loadout.waterContainers = 3;
            loadout.beddingWarmth = 82.0f;
            loadout.tarpQuality = 78.0f;
            break;

        case Background::ScholarAthlete:
            attributes = { 55.0f, 82.0f, 58.0f, 62.0f, 70.0f, 42.0f, 60.0f };
            condition.morale = 58.0f;
            condition.hygiene = 66.0f;
            survival = { 48.0f, 44.0f, 42.0f, 56.0f, 46.0f, 58.0f, 38.0f, 46.0f, 44.0f, 46.0f, 46.0f, 44.0f };
            craft = { 38.0f, 34.0f, 28.0f, 36.0f, 42.0f };
            social = { 26.0f, 42.0f, 40.0f, 48.0f, 40.0f, 26.0f, 42.0f };
            knowledge = { 78.0f, 54.0f, 66.0f, 70.0f, 48.0f, 34.0f, 34.0f };
            physical = { 72.0f, 62.0f, 50.0f, 42.0f, 48.0f, 72.0f };
            mental = { 78.0f, 70.0f, 54.0f, 72.0f, 76.0f, 68.0f };
            comfort = { 60.0f, 54.0f, 42.0f, 48.0f, 58.0f };
            loadout.hammock = true;
            loadout.tarp = true;
            loadout.waterFilter = true;
            loadout.gasStove = true;
            loadout.simpleCookPot = true;
            loadout.simpleKnife = true;
            loadout.powerBank = true;
            loadout.solarPanel = true;
            loadout.smartphoneOffline = true;
            loadout.waterContainers = 1;
            loadout.beddingWarmth = 46.0f;
            loadout.tarpQuality = 58.0f;
            break;

        case Background::SocialAdaptable:
            attributes = { 40.0f, 36.0f, 46.0f, 54.0f, 66.0f, 78.0f, 52.0f };
            condition.morale = 64.0f;
            condition.hygiene = 82.0f;
            survival = { 22.0f, 18.0f, 24.0f, 30.0f, 18.0f, 26.0f, 16.0f, 24.0f, 20.0f, 24.0f, 24.0f, 30.0f };
            craft = { 16.0f, 18.0f, 12.0f, 18.0f, 26.0f };
            social = { 72.0f, 78.0f, 70.0f, 82.0f, 74.0f, 68.0f, 72.0f };
            knowledge = { 46.0f, 48.0f, 50.0f, 64.0f, 20.0f, 22.0f, 30.0f };
            physical = { 28.0f, 30.0f, 24.0f, 20.0f, 42.0f, 34.0f };
            mental = { 56.0f, 62.0f, 42.0f, 52.0f, 58.0f, 28.0f };
            comfort = { 30.0f, 84.0f, 82.0f, 84.0f, 36.0f };
            loadout.hammock = true;
            loadout.tarp = true;
            loadout.waterFilter = false;
            loadout.rationHeater = true;
            loadout.powerBank = true;
            loadout.solarPanel = true;
            loadout.smartphoneOffline = true;
            loadout.travelShower = true;
            loadout.hygieneKit = true;
            loadout.waterContainers = 1;
            loadout.beddingWarmth = 28.0f;
            loadout.tarpQuality = 34.0f;
            break;
        }

        recomputeDerivedStats();
        refillVitals();
    }

    void recomputeDerivedStats()
    {
        carryCapacity = 18.0f
            + attributes.strength * 0.24f
            + attributes.endurance * 0.10f
            + physical.loadHandling * 0.08f;

        if (loadout.rope)
            carryCapacity += 1.0f;

        carryVolumeCapacity = 16.0f
            + attributes.strength * 0.18f
            + attributes.dexterity * 0.10f
            + physical.loadHandling * 0.10f;

        if (loadout.hammock)
            carryVolumeCapacity += 8.0f;

        moveSpeedBase = 74.0f
            + physical.running * 0.20f
            + physical.staminaEfficiency * 0.08f
            + attributes.dexterity * 0.04f;
    }

    void refillVitals()
    {
        condition.health = 100.0f;
        condition.stamina = 100.0f;
        condition.fatigue = 0.0f;
        condition.nutrition = 100.0f;
        condition.hydration = 100.0f;
        condition.bodyTemperature = 50.0f;
        condition.morale = std::max(condition.morale, 55.0f);
        condition.stress = std::min(condition.stress, 20.0f);
        condition.wetness = 0.0f;
        condition.diseaseLoad = 0.0f;
        condition.pain = 0.0f;
    }

    void setCarryState(float weight, float volume)
    {
        carryWeight = std::max(0.0f, weight);
        carryVolume = std::max(0.0f, volume);
    }

    float carryWeightRatio() const
    {
        return (carryCapacity > 0.0f) ? (carryWeight / carryCapacity) : 0.0f;
    }

    float carryVolumeRatio() const
    {
        return (carryVolumeCapacity > 0.0f) ? (carryVolume / carryVolumeCapacity) : 0.0f;
    }

    float encumbrancePenalty() const
    {
        const float ratio = std::max(carryWeightRatio(), carryVolumeRatio());
        if (ratio <= 1.0f)
            return ratio * 0.12f;

        return 0.12f + std::min(0.70f, (ratio - 1.0f) * 0.55f);
    }

    float fatigueMovePenalty() const
    {
        return std::min(0.55f, condition.fatigue / 100.0f * 0.55f);
    }

    float staminaMovePenalty() const
    {
        const float missing = 100.0f - condition.stamina;
        return std::min(0.45f, missing / 100.0f * 0.45f);
    }

    float thermalMovePenalty() const
    {
        const float distanceFromNeutral = std::fabs(condition.bodyTemperature - 50.0f);
        return std::min(0.25f, distanceFromNeutral / 50.0f * 0.25f);
    }

    float moraleBonus() const
    {
        if (condition.morale <= 50.0f)
            return 0.0f;
        return std::min(0.12f, (condition.morale - 50.0f) / 50.0f * 0.12f);
    }

    float getMoveSpeedMultiplier() const
    {
        float value = 1.0f;
        value -= encumbrancePenalty();
        value -= fatigueMovePenalty();
        value -= staminaMovePenalty();
        value -= thermalMovePenalty();
        value += moraleBonus();
        return std::clamp(value, 0.25f, 1.25f);
    }

    float getMoveSpeed() const
    {
        return moveSpeedBase * getMoveSpeedMultiplier();
    }

    float getStaminaDrainPerSecond(bool isMoving) const
    {
        float drain = isMoving ? 2.2f : 0.45f;
        drain += encumbrancePenalty() * 5.0f;
        drain += condition.wetness / 100.0f * 0.8f;
        drain += condition.stress / 100.0f * 0.5f;
        drain -= physical.staminaEfficiency / 100.0f * 0.9f;
        return std::max(0.1f, drain);
    }

    float getStaminaRecoveryPerSecond() const
    {
        float recovery = 4.0f;
        recovery += physical.sleepRecovery / 100.0f * 1.8f;
        recovery += mental.stressResistance / 100.0f * 0.8f;
        recovery -= condition.stress / 100.0f * 1.2f;
        recovery -= condition.wetness / 100.0f * 0.7f;
        return std::max(0.2f, recovery);
    }

    void updateVitals(float dtSeconds, bool isMoving)
    {
        if (isMoving)
            condition.stamina = Clamp01To100(condition.stamina - getStaminaDrainPerSecond(true) * dtSeconds);
        else
            condition.stamina = Clamp01To100(condition.stamina + getStaminaRecoveryPerSecond() * dtSeconds);

        const float fatigueDelta = isMoving ? 0.45f : -0.30f;
        condition.fatigue = Clamp01To100(condition.fatigue + fatigueDelta * dtSeconds);

        condition.nutrition = Clamp01To100(condition.nutrition - (isMoving ? 0.050f : 0.020f) * dtSeconds);
        condition.hydration = Clamp01To100(condition.hydration - (isMoving ? 0.090f : 0.035f) * dtSeconds);
        condition.stress = Clamp01To100(condition.stress + (isMoving ? 0.010f : -0.015f) * dtSeconds);
        condition.morale = Clamp01To100(condition.morale + (isMoving ? -0.004f : 0.003f) * dtSeconds);
    }

    float getLanguageComprehensionScore() const
    {
        return knowledge.medievalCzech * 0.45f + knowledge.german * 0.20f + knowledge.latin * 0.10f + knowledge.history * 0.25f;
    }

    float getSocialEntryScore() const
    {
        return social.conversationInitiation * 0.35f
            + social.etiquette * 0.25f
            + social.socialProtocol * 0.20f
            + attributes.charisma * 0.20f;
    }

    float getPracticalWorkScore() const
    {
        return survival.woodProcessing * 0.20f
            + craft.toolRepair * 0.20f
            + craft.improvisation * 0.20f
            + attributes.strength * 0.15f
            + attributes.dexterity * 0.10f
            + survival.fireMaking * 0.15f;
    }

    SurvivalTier getGeneralSurvivalTier() const
    {
        const float score =
            survival.fireMaking * 0.12f +
            survival.shelterBuilding * 0.12f +
            survival.waterPurification * 0.12f +
            survival.navigation * 0.10f +
            survival.foraging * 0.08f +
            survival.coldResistance * 0.10f +
            survival.foodTolerance * 0.08f +
            physical.staminaEfficiency * 0.08f +
            mental.stressResistance * 0.10f +
            comfort.natureComfort * 0.10f;

        if (score >= 80.0f) return SurvivalTier::Excellent;
        if (score >= 62.0f) return SurvivalTier::Good;
        if (score >= 45.0f) return SurvivalTier::Average;
        if (score >= 28.0f) return SurvivalTier::Poor;
        return SurvivalTier::Critical;
    }

    const char* backgroundId() const
    {
        switch (background)
        {
        case Background::Survivalist:    return "survivalist";
        case Background::ScholarAthlete: return "scholar_athlete";
        case Background::SocialAdaptable:return "social_adaptable";
        }
        return "scholar_athlete";
    }

    bool isOverweight() const
    {
        return carryWeight > carryCapacity;
    }

    float legacyHp() const { return condition.health; }
    float legacyHunger() const { return 100.0f - condition.nutrition; }
    float legacyThirst() const { return 100.0f - condition.hydration; }
    float legacyFatigue() const { return condition.fatigue; }
    float legacyHygiene() const { return 100.0f - condition.hygiene; }
    float legacyTemperature() const { return condition.bodyTemperature; }
    float legacyMedievalFeel() const { return comfort.medievalAdaptation; }
};

