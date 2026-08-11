#include "Campaign.h"

#include "PlayerProfileStore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
template <typename T>
T JsonValue(const json& source, const char* key, const T& fallback)
{
    if (!source.is_object() || !source.contains(key))
        return fallback;

    try
    {
        return source.at(key).get<T>();
    }
    catch (...)
    {
        return fallback;
    }
}

PlayerStats::Background BackgroundFromString(const std::string& value, PlayerStats::Background fallback)
{
    if (value == "survivalist")
        return PlayerStats::Background::Survivalist;
    if (value == "scholar_athlete")
        return PlayerStats::Background::ScholarAthlete;
    if (value == "social_adaptable")
        return PlayerStats::Background::SocialAdaptable;
    return fallback;
}

json SaveStringSet(const std::unordered_set<std::string>& values)
{
    json out = json::array();
    std::vector<std::string> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    for (const std::string& value : sorted)
        out.push_back(value);
    return out;
}

void LoadStringSet(const json& source, std::unordered_set<std::string>& out)
{
    out.clear();
    if (!source.is_array())
        return;

    for (const auto& item : source)
    {
        if (item.is_string())
            out.insert(item.get<std::string>());
    }
}

std::vector<std::string> LoadStringVector(const json& source)
{
    std::vector<std::string> out;
    if (!source.is_array())
        return out;

    for (const auto& item : source)
    {
        if (item.is_string())
            out.push_back(item.get<std::string>());
    }
    return out;
}

json SaveStringVector(const std::vector<std::string>& values)
{
    json out = json::array();
    for (const std::string& value : values)
        out.push_back(value);
    return out;
}

json SaveDateTime(const GameTime::DateTime& value)
{
    return json{
        {"day", value.day},
        {"month", value.month},
        {"year", value.year},
        {"hour", value.hour},
        {"minute", value.minute}
    };
}

template <std::size_t N>
std::string FixedBufferString(const char (&buffer)[N])
{
    std::size_t len = 0;
    while (len < N && buffer[len] != '\0')
        ++len;
    return std::string(buffer, len);
}

template <std::size_t N>
std::string FixedBufferString(const std::array<char, N>& buffer)
{
    std::size_t len = 0;
    while (len < N && buffer[len] != '\0')
        ++len;
    return std::string(buffer.data(), len);
}

bool IsLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int DaysInMonth(int month, int year)
{
    switch (month)
    {
    case 1: return 31;
    case 2: return IsLeapYear(year) ? 29 : 28;
    case 3: return 31;
    case 4: return 30;
    case 5: return 31;
    case 6: return 30;
    case 7: return 31;
    case 8: return 31;
    case 9: return 30;
    case 10: return 31;
    case 11: return 30;
    case 12: return 31;
    default: return 30;
    }
}

json SaveCondition(const PlayerStats::ConditionState& c)
{
    return json{
        {"health", PlayerStats::Clamp01To100(c.health)},
        {"stamina", PlayerStats::Clamp01To100(c.stamina)},
        {"fatigue", PlayerStats::Clamp01To100(c.fatigue)},
        {"nutrition", PlayerStats::Clamp01To100(c.nutrition)},
        {"hydration", PlayerStats::Clamp01To100(c.hydration)},
        {"body_temperature", PlayerStats::Clamp01To100(c.bodyTemperature)},
        {"hygiene", PlayerStats::Clamp01To100(c.hygiene)},
        {"morale", PlayerStats::Clamp01To100(c.morale)},
        {"stress", PlayerStats::Clamp01To100(c.stress)},
        {"wetness", PlayerStats::Clamp01To100(c.wetness)},
        {"disease_load", PlayerStats::Clamp01To100(c.diseaseLoad)},
        {"pain", PlayerStats::Clamp01To100(c.pain)},
        {"poisoned", c.poisoned},
        {"injured", c.injured},
        {"fracture", c.fracture},
        {"bleeding", c.bleeding},
        {"treated_wound", c.treatedWound}
    };
}

void LoadCondition(const json& source, PlayerStats::ConditionState& c)
{
    if (!source.is_object())
        return;

    c.health = PlayerStats::Clamp01To100(JsonValue(source, "health", c.health));
    c.stamina = PlayerStats::Clamp01To100(JsonValue(source, "stamina", c.stamina));
    c.fatigue = PlayerStats::Clamp01To100(JsonValue(source, "fatigue", c.fatigue));
    c.nutrition = PlayerStats::Clamp01To100(JsonValue(source, "nutrition", c.nutrition));
    c.hydration = PlayerStats::Clamp01To100(JsonValue(source, "hydration", c.hydration));
    c.bodyTemperature = PlayerStats::Clamp01To100(JsonValue(source, "body_temperature", c.bodyTemperature));
    c.hygiene = PlayerStats::Clamp01To100(JsonValue(source, "hygiene", c.hygiene));
    c.morale = PlayerStats::Clamp01To100(JsonValue(source, "morale", c.morale));
    c.stress = PlayerStats::Clamp01To100(JsonValue(source, "stress", c.stress));
    c.wetness = PlayerStats::Clamp01To100(JsonValue(source, "wetness", c.wetness));
    c.diseaseLoad = PlayerStats::Clamp01To100(JsonValue(source, "disease_load", c.diseaseLoad));
    c.pain = PlayerStats::Clamp01To100(JsonValue(source, "pain", c.pain));
    c.poisoned = JsonValue(source, "poisoned", c.poisoned);
    c.injured = JsonValue(source, "injured", c.injured);
    c.fracture = JsonValue(source, "fracture", c.fracture);
    c.bleeding = JsonValue(source, "bleeding", c.bleeding);
    c.treatedWound = JsonValue(source, "treated_wound", c.treatedWound);
}

json SavePlayerStats(const PlayerStats& s)
{
    return json{
        {"background", s.backgroundId()},
        {"attributes", playerprofile::SaveAttributes(s.attributes)},
        {"condition", SaveCondition(s.condition)},
        {"survival", playerprofile::SaveSurvival(s.survival)},
        {"craft", playerprofile::SaveCraft(s.craft)},
        {"social", playerprofile::SaveSocial(s.social)},
        {"knowledge", playerprofile::SaveKnowledge(s.knowledge)},
        {"physical", playerprofile::SavePhysical(s.physical)},
        {"mental", playerprofile::SaveMental(s.mental)},
        {"comfort", playerprofile::SaveComfort(s.comfort)},
        {"loadout", playerprofile::SaveLoadout(s.loadout)},
        {"derived", json{
            {"move_speed_base", s.moveSpeedBase},
            {"carry_weight", s.carryWeight},
            {"carry_capacity", s.carryCapacity},
            {"carry_volume", s.carryVolume},
            {"carry_volume_capacity", s.carryVolumeCapacity},
            {"outfit_authenticity", s.outfitAuthenticity},
            {"village_acceptance", s.villageAcceptance},
            {"church_trust", s.churchTrust},
            {"reeve_trust", s.reeveTrust},
            {"fear", s.fear},
            {"walk_speed_factor", s.walkSpeedFactor},
            {"run_speed_factor", s.runSpeedFactor}
        }}
    };
}

void LoadPlayerStats(const json& source, PlayerStats& s)
{
    if (!source.is_object())
        return;

    s.background = BackgroundFromString(JsonValue(source, "background", std::string(s.backgroundId())), s.background);
    playerprofile::LoadAttributes(source.value("attributes", json::object()), s.attributes);
    LoadCondition(source.value("condition", json::object()), s.condition);
    playerprofile::LoadSurvival(source.value("survival", json::object()), s.survival);
    playerprofile::LoadCraft(source.value("craft", json::object()), s.craft);
    playerprofile::LoadSocial(source.value("social", json::object()), s.social);
    playerprofile::LoadKnowledge(source.value("knowledge", json::object()), s.knowledge);
    playerprofile::LoadPhysical(source.value("physical", json::object()), s.physical);
    playerprofile::LoadMental(source.value("mental", json::object()), s.mental);
    playerprofile::LoadComfort(source.value("comfort", json::object()), s.comfort);
    playerprofile::LoadLoadout(source.value("loadout", json::object()), s.loadout);

    const json derived = source.value("derived", json::object());
    if (derived.is_object())
    {
        s.moveSpeedBase = JsonValue(derived, "move_speed_base", s.moveSpeedBase);
        s.carryWeight = std::max(0.0f, JsonValue(derived, "carry_weight", s.carryWeight));
        s.carryCapacity = std::max(0.0f, JsonValue(derived, "carry_capacity", s.carryCapacity));
        s.carryVolume = std::max(0.0f, JsonValue(derived, "carry_volume", s.carryVolume));
        s.carryVolumeCapacity = std::max(0.0f, JsonValue(derived, "carry_volume_capacity", s.carryVolumeCapacity));
        s.outfitAuthenticity = JsonValue(derived, "outfit_authenticity", s.outfitAuthenticity);
        s.villageAcceptance = JsonValue(derived, "village_acceptance", s.villageAcceptance);
        s.churchTrust = JsonValue(derived, "church_trust", s.churchTrust);
        s.reeveTrust = JsonValue(derived, "reeve_trust", s.reeveTrust);
        s.fear = JsonValue(derived, "fear", s.fear);
        s.walkSpeedFactor = JsonValue(derived, "walk_speed_factor", s.walkSpeedFactor);
        s.runSpeedFactor = JsonValue(derived, "run_speed_factor", s.runSpeedFactor);
    }
}

json SaveItemStack(const ItemStack& stack)
{
    return json{
        {"item_id", stack.itemId},
        {"count", stack.count},
        {"durability", stack.durability},
        {"wetness", stack.wetness}
    };
}

void LoadItemStack(const json& source, ItemStack& stack)
{
    if (!source.is_object())
        return;

    stack.itemId = JsonValue(source, "item_id", stack.itemId);
    stack.count = std::max(0, JsonValue(source, "count", stack.count));
    stack.durability = PlayerStats::Clamp01To100(JsonValue(source, "durability", stack.durability));
    stack.wetness = PlayerStats::Clamp01To100(JsonValue(source, "wetness", stack.wetness));
    if (stack.count <= 0 || stack.itemId.empty())
        stack.clear();
}

json SaveContainerInventory(const ContainerInventory& container)
{
    json items = json::array();
    for (const ItemStack& stack : container.items)
        items.push_back(SaveItemStack(stack));

    return json{
        {"type", static_cast<int>(container.type)},
        {"max_weight", container.maxWeight},
        {"max_volume", container.maxVolume},
        {"items", items}
    };
}

void LoadContainerInventory(const json& source, ContainerInventory& container)
{
    if (!source.is_object())
        return;

    container.type = static_cast<ContainerType>(
        std::clamp(JsonValue(source, "type", static_cast<int>(container.type)), 0, 3));
    container.maxWeight = std::max(0.0f, JsonValue(source, "max_weight", container.maxWeight));
    container.maxVolume = std::max(0.0f, JsonValue(source, "max_volume", container.maxVolume));

    if (source.contains("items") && source["items"].is_array())
    {
        container.items.clear();
        for (const auto& itemJson : source["items"])
        {
            ItemStack stack;
            LoadItemStack(itemJson, stack);
            if (!stack.empty())
                container.items.push_back(stack);
        }
    }
}

json SaveInventory(const PlayerInventory& inv)
{
    return json{
        {"equipped", json{
            {"head", SaveItemStack(inv.equipped.head)},
            {"torso_inner", SaveItemStack(inv.equipped.torsoInner)},
            {"torso_outer", SaveItemStack(inv.equipped.torsoOuter)},
            {"legs", SaveItemStack(inv.equipped.legs)},
            {"feet", SaveItemStack(inv.equipped.feet)},
            {"cloak", SaveItemStack(inv.equipped.cloak)},
            {"belt", SaveItemStack(inv.equipped.belt)},
            {"back", SaveItemStack(inv.equipped.back)},
            {"main_hand", SaveItemStack(inv.equipped.mainHand)},
            {"off_hand", SaveItemStack(inv.equipped.offHand)}
        }},
        {"belt_slots", json{
            {"knife", SaveItemStack(inv.beltSlots.knife)},
            {"pouch", SaveItemStack(inv.beltSlots.pouch)},
            {"utility1", SaveItemStack(inv.beltSlots.utility1)},
            {"utility2", SaveItemStack(inv.beltSlots.utility2)}
        }},
        {"pockets", SaveContainerInventory(inv.pockets)},
        {"backpack", SaveContainerInventory(inv.backpack)}
    };
}

void LoadInventory(const json& source, PlayerInventory& inv)
{
    if (!source.is_object())
        return;

    const json equipped = source.value("equipped", json::object());
    if (equipped.is_object())
    {
        LoadItemStack(equipped.value("head", json::object()), inv.equipped.head);
        LoadItemStack(equipped.value("torso_inner", json::object()), inv.equipped.torsoInner);
        LoadItemStack(equipped.value("torso_outer", json::object()), inv.equipped.torsoOuter);
        LoadItemStack(equipped.value("legs", json::object()), inv.equipped.legs);
        LoadItemStack(equipped.value("feet", json::object()), inv.equipped.feet);
        LoadItemStack(equipped.value("cloak", json::object()), inv.equipped.cloak);
        LoadItemStack(equipped.value("belt", json::object()), inv.equipped.belt);
        LoadItemStack(equipped.value("back", json::object()), inv.equipped.back);
        LoadItemStack(equipped.value("main_hand", json::object()), inv.equipped.mainHand);
        LoadItemStack(equipped.value("off_hand", json::object()), inv.equipped.offHand);
    }

    const json beltSlots = source.value("belt_slots", json::object());
    if (beltSlots.is_object())
    {
        LoadItemStack(beltSlots.value("knife", json::object()), inv.beltSlots.knife);
        LoadItemStack(beltSlots.value("pouch", json::object()), inv.beltSlots.pouch);
        LoadItemStack(beltSlots.value("utility1", json::object()), inv.beltSlots.utility1);
        LoadItemStack(beltSlots.value("utility2", json::object()), inv.beltSlots.utility2);
    }

    LoadContainerInventory(source.value("pockets", json::object()), inv.pockets);
    LoadContainerInventory(source.value("backpack", json::object()), inv.backpack);
}

json SavePlayer(const Player& player)
{
    return json{
        {"x", player.x},
        {"y", player.y},
        {"vx", player.vx},
        {"vy", player.vy},
        {"col_w", player.colW},
        {"col_h", player.colH},
        {"character_id", player.characterId},
        {"given_name", player.givenName},
        {"family_name", player.familyName},
        {"note_profile_id", player.noteProfileId},
        {"is_sprinting", player.isSprinting},
        {"is_moving", player.isMoving},
        {"facing", static_cast<int>(player.facing)},
        {"current_anim", player.currentAnim},
        {"current_frame", player.currentFrame},
        {"anim_time", player.animTime},
        {"stats", SavePlayerStats(player.stats)},
        {"inventory", SaveInventory(player.inventory)}
    };
}

void LoadPlayer(const json& source, Player& player, const CharacterManager& characters)
{
    if (!source.is_object())
        return;

    const std::string characterId = JsonValue(source, "character_id", player.characterId);
    if (!characterId.empty())
    {
        if (!player.selectCharacter(characterId, characters))
            player.characterId = characterId;
    }

    player.x = JsonValue(source, "x", player.x);
    player.y = JsonValue(source, "y", player.y);
    player.vx = JsonValue(source, "vx", 0.0f);
    player.vy = JsonValue(source, "vy", 0.0f);
    player.colW = std::max(1, JsonValue(source, "col_w", player.colW));
    player.colH = std::max(1, JsonValue(source, "col_h", player.colH));
    player.givenName = JsonValue(source, "given_name", player.givenName);
    player.familyName = JsonValue(source, "family_name", player.familyName);
    player.noteProfileId = JsonValue(source, "note_profile_id", player.noteProfileId);
    player.isSprinting = JsonValue(source, "is_sprinting", player.isSprinting);
    player.isMoving = JsonValue(source, "is_moving", player.isMoving);
    player.facing = static_cast<Player::Facing>(std::clamp(JsonValue(source, "facing", static_cast<int>(player.facing)), 0, 3));
    player.currentAnim = JsonValue(source, "current_anim", player.currentAnim);
    player.currentFrame = std::max(0, JsonValue(source, "current_frame", player.currentFrame));
    player.animTime = std::max(0.0f, JsonValue(source, "anim_time", player.animTime));
    LoadPlayerStats(source.value("stats", json::object()), player.stats);
    LoadInventory(source.value("inventory", json::object()), player.inventory);
}

json SaveNpcState(const NpcInstance& npc)
{
    const uint32_t nowTicks = SDL_GetTicks();
    const uint32_t greetingRemaining =
        npc.nextGreetingAllowedTime > nowTicks
            ? npc.nextGreetingAllowedTime - nowTicks
            : 0u;

    return json{
        {"id", npc.id},
        {"npc_id", npc.npcId},
        {"type_id", npc.typeId},
        {"name", npc.name},
        {"surname", npc.surname},
        {"greeting", npc.greeting},
        {"script_id", npc.scriptId},
        {"character_id", npc.characterId},
        {"x", npc.x},
        {"y", npc.y},
        {"hp", npc.hp},
        {"mood", npc.mood},
        {"facing", npc.facing},
        {"greeted_player", npc.greetedPlayer},
        {"greeting_cooldown_remaining_ms", greetingRemaining},
        {"player_nearby", npc.playerNearby},
        {"player_in_dialog_range", npc.playerInDialogRange},
        {"current_zone", npc.currentZone},
        {"target_x", npc.targetX},
        {"target_y", npc.targetY},
        {"anim_time", npc.animTime},
        {"idle_timer", npc.idleTimer},
        {"awareness_radius", npc.awarenessRadius},
        {"greeting_radius", npc.greetingRadius},
        {"dialog_radius", npc.dialogRadius}
    };
}

void LoadNpcState(const json& source, NpcInstance& npc)
{
    if (!source.is_object())
        return;

    npc.npcId = JsonValue(source, "npc_id", npc.npcId);
    npc.typeId = JsonValue(source, "type_id", npc.typeId);
    npc.name = JsonValue(source, "name", npc.name);
    npc.surname = JsonValue(source, "surname", npc.surname);
    npc.greeting = JsonValue(source, "greeting", npc.greeting);
    npc.scriptId = JsonValue(source, "script_id", npc.scriptId);
    npc.characterId = JsonValue(source, "character_id", npc.characterId);
    npc.x = JsonValue(source, "x", npc.x);
    npc.y = JsonValue(source, "y", npc.y);
    npc.hp = JsonValue(source, "hp", npc.hp);
    npc.mood = std::clamp(JsonValue(source, "mood", npc.mood), 0, 100);
    npc.facing = JsonValue(source, "facing", npc.facing);
    npc.greetedPlayer = JsonValue(source, "greeted_player", npc.greetedPlayer);
    npc.nextGreetingAllowedTime = SDL_GetTicks() +
        JsonValue(source, "greeting_cooldown_remaining_ms", 0u);
    npc.playerNearby = JsonValue(source, "player_nearby", npc.playerNearby);
    npc.playerInDialogRange = JsonValue(source, "player_in_dialog_range", npc.playerInDialogRange);
    npc.currentZone = JsonValue(source, "current_zone", npc.currentZone);
    npc.targetX = JsonValue(source, "target_x", npc.targetX);
    npc.targetY = JsonValue(source, "target_y", npc.targetY);
    npc.animTime = std::max(0.0f, JsonValue(source, "anim_time", npc.animTime));
    npc.idleTimer = JsonValue(source, "idle_timer", npc.idleTimer);
    npc.awarenessRadius = std::max(0.0f, JsonValue(source, "awareness_radius", npc.awarenessRadius));
    npc.greetingRadius = std::max(0.0f, JsonValue(source, "greeting_radius", npc.greetingRadius));
    npc.dialogRadius = std::max(0.0f, JsonValue(source, "dialog_radius", npc.dialogRadius));
}

json SaveWeatherDay(const WeatherDayProfile& w)
{
    return json{
        {"day", w.day},
        {"month", w.month},
        {"year", w.year},
        {"min_temp", w.minTemp},
        {"max_temp", w.maxTemp},
        {"cloudiness", w.cloudiness},
        {"precipitation_chance", w.precipitationChance},
        {"precipitation_type", WeatherSystem::precipitationTypeToString(w.precipitationType)},
        {"precipitation_intensity", w.precipitationIntensity},
        {"wind_avg", w.wind.avg},
        {"wind_gust_chance", w.wind.gustChance},
        {"fog_morning", w.fogMorning},
        {"ground_wetness", w.groundWetness},
        {"front_type", w.frontType}
    };
}

void LoadWeatherDay(const json& source, WeatherDayProfile& w)
{
    if (!source.is_object())
        return;

    w.day = JsonValue(source, "day", w.day);
    w.month = JsonValue(source, "month", w.month);
    w.year = JsonValue(source, "year", w.year);
    w.minTemp = JsonValue(source, "min_temp", w.minTemp);
    w.maxTemp = JsonValue(source, "max_temp", w.maxTemp);
    w.cloudiness = PlayerStats::Clamp01To100(JsonValue(source, "cloudiness", w.cloudiness));
    w.precipitationChance = PlayerStats::Clamp01To100(JsonValue(source, "precipitation_chance", w.precipitationChance));
    w.precipitationType = WeatherSystem::precipitationTypeFromString(
        JsonValue(source, "precipitation_type", std::string(WeatherSystem::precipitationTypeToString(w.precipitationType))));
    w.precipitationIntensity = std::clamp(JsonValue(source, "precipitation_intensity", w.precipitationIntensity), 0.0f, 1.0f);
    w.wind.avg = std::max(0.0f, JsonValue(source, "wind_avg", w.wind.avg));
    w.wind.gustChance = PlayerStats::Clamp01To100(JsonValue(source, "wind_gust_chance", w.wind.gustChance));
    w.fogMorning = PlayerStats::Clamp01To100(JsonValue(source, "fog_morning", w.fogMorning));
    w.groundWetness = PlayerStats::Clamp01To100(JsonValue(source, "ground_wetness", w.groundWetness));
    w.frontType = JsonValue(source, "front_type", w.frontType);
}

json SaveWeatherRuntime(const WeatherRuntimeState& w)
{
    return json{
        {"current_temp", w.currentTemp},
        {"is_raining", w.isRaining},
        {"rain_intensity", w.rainIntensity},
        {"is_foggy", w.isFoggy},
        {"wind_now", w.windNow},
        {"discomfort_index", w.discomfortIndex}
    };
}

void LoadWeatherRuntime(const json& source, WeatherRuntimeState& w)
{
    if (!source.is_object())
        return;

    w.currentTemp = JsonValue(source, "current_temp", w.currentTemp);
    w.isRaining = JsonValue(source, "is_raining", w.isRaining);
    w.rainIntensity = std::clamp(JsonValue(source, "rain_intensity", w.rainIntensity), 0.0f, 1.0f);
    w.isFoggy = JsonValue(source, "is_foggy", w.isFoggy);
    w.windNow = std::max(0.0f, JsonValue(source, "wind_now", w.windNow));
    w.discomfortIndex = JsonValue(source, "discomfort_index", w.discomfortIndex);
}

json SaveForageKnowledge(const PlayerForageKnowledgeState& state)
{
    json entries = json::object();
    for (const auto& [speciesId, entry] : state.entries)
    {
        entries[speciesId] = json{
            {"knowledge_level", static_cast<int>(entry.knowledgeLevel)},
            {"times_seen", entry.timesSeen},
            {"times_examined", entry.timesExamined},
            {"times_verified", entry.timesVerified},
            {"known_traits", SaveStringSet(entry.knownTraits)}
        };
    }
    return entries;
}

void LoadForageKnowledge(const json& source, PlayerForageKnowledgeState& state)
{
    state.entries.clear();
    if (!source.is_object())
        return;

    for (auto it = source.begin(); it != source.end(); ++it)
    {
        if (!it.value().is_object())
            continue;

        PlayerForageKnowledgeEntry entry;
        entry.knowledgeLevel = static_cast<KnowledgeLevel>(
            std::clamp(JsonValue(it.value(), "knowledge_level", 0), 0, 3));
        entry.timesSeen = std::max(0, JsonValue(it.value(), "times_seen", 0));
        entry.timesExamined = std::max(0, JsonValue(it.value(), "times_examined", 0));
        entry.timesVerified = std::max(0, JsonValue(it.value(), "times_verified", 0));
        LoadStringSet(it.value().value("known_traits", json::array()), entry.knownTraits);
        state.entries[it.key()] = std::move(entry);
    }
}

json SaveForageResult(const ForageExaminationResult& r)
{
    return json{
        {"success", r.success},
        {"revealed_new_knowledge", r.revealedNewKnowledge},
        {"verified_species", r.verifiedSpecies},
        {"correct_traits", r.correctTraits},
        {"total_traits", r.totalTraits},
        {"score_percent", r.scorePercent},
        {"trait_score", r.traitScore},
        {"description_score", r.descriptionScore},
        {"name_score", r.nameScore},
        {"skill_bonus", r.skillBonus},
        {"nature_bonus", r.natureBonus},
        {"difficulty_modifier", r.difficultyModifier},
        {"difficulty_penalty", r.difficultyPenalty},
        {"newly_known_traits", SaveStringVector(r.newlyKnownTraits)},
        {"feedback_lines", SaveStringVector(r.feedbackLines)},
        {"feedback_text", r.feedbackText}
    };
}

void LoadForageResult(const json& source, ForageExaminationResult& r)
{
    if (!source.is_object())
        return;

    r.success = JsonValue(source, "success", r.success);
    r.revealedNewKnowledge = JsonValue(source, "revealed_new_knowledge", r.revealedNewKnowledge);
    r.verifiedSpecies = JsonValue(source, "verified_species", r.verifiedSpecies);
    r.correctTraits = std::max(0, JsonValue(source, "correct_traits", r.correctTraits));
    r.totalTraits = std::max(0, JsonValue(source, "total_traits", r.totalTraits));
    r.scorePercent = std::clamp(JsonValue(source, "score_percent", r.scorePercent), 0, 100);
    r.traitScore = JsonValue(source, "trait_score", r.traitScore);
    r.descriptionScore = JsonValue(source, "description_score", r.descriptionScore);
    r.nameScore = JsonValue(source, "name_score", r.nameScore);
    r.skillBonus = JsonValue(source, "skill_bonus", r.skillBonus);
    r.natureBonus = JsonValue(source, "nature_bonus", r.natureBonus);
    r.difficultyModifier = JsonValue(source, "difficulty_modifier", r.difficultyModifier);
    r.difficultyPenalty = JsonValue(source, "difficulty_penalty", r.difficultyPenalty);
    r.newlyKnownTraits = LoadStringVector(source.value("newly_known_traits", json::array()));
    r.feedbackLines = LoadStringVector(source.value("feedback_lines", json::array()));
    r.feedbackText = JsonValue(source, "feedback_text", r.feedbackText);
}

template <typename T>
json SavePoisoning(const T& p)
{
    return json{
        {"active", p.active},
        {"fatal", p.fatal},
        {"source_name", p.sourceName},
        {"elapsed_hours", p.elapsedHours},
        {"onset_hours", p.onsetHours},
        {"duration_hours", p.durationHours},
        {"health_drain_per_hour", p.healthDrainPerHour},
        {"hydration_drain_per_hour", p.hydrationDrainPerHour},
        {"onset_logged", p.onsetLogged},
        {"end_logged", p.endLogged},
        {"last_logged_hour", p.lastLoggedHour},
        {"last_logged_milestone", p.lastLoggedMilestone}
    };
}

template <typename T>
void LoadPoisoning(const json& source, T& p)
{
    if (!source.is_object())
        return;

    p.active = JsonValue(source, "active", p.active);
    p.fatal = JsonValue(source, "fatal", p.fatal);
    p.sourceName = JsonValue(source, "source_name", p.sourceName);
    p.elapsedHours = std::max(0.0f, JsonValue(source, "elapsed_hours", p.elapsedHours));
    p.onsetHours = std::max(0.0f, JsonValue(source, "onset_hours", p.onsetHours));
    p.durationHours = std::max(0.0f, JsonValue(source, "duration_hours", p.durationHours));
    p.healthDrainPerHour = std::max(0.0f, JsonValue(source, "health_drain_per_hour", p.healthDrainPerHour));
    p.hydrationDrainPerHour = std::max(0.0f, JsonValue(source, "hydration_drain_per_hour", p.hydrationDrainPerHour));
    p.onsetLogged = JsonValue(source, "onset_logged", p.onsetLogged);
    p.endLogged = JsonValue(source, "end_logged", p.endLogged);
    p.lastLoggedHour = JsonValue(source, "last_logged_hour", p.lastLoggedHour);
    p.lastLoggedMilestone = JsonValue(source, "last_logged_milestone", p.lastLoggedMilestone);
}
}

bool Campaign::handleSharedUiEvent(const SDL_Event& e)
{
    if (e.type != SDL_KEYDOWN || e.key.repeat != 0)
        return false;

    const SDL_Keycode key = e.key.keysym.sym;

    if (key == SDLK_SEMICOLON)
    {
        m_consoleOpen = !m_consoleOpen;
        if (m_consoleOpen)
            m_consoleFocusInput = true;
        return true;
    }

    if (shouldBlockLetterHotkeys())
        return false;

    if (key == SDLK_i)
    {
        m_inventoryOpen = !m_inventoryOpen;
        m_inventoryFocus = m_inventoryOpen;
        return true;
    }

    if (key == SDLK_q)
    {
        m_questJournalOpen = !m_questJournalOpen;
        m_questJournalFocus = m_questJournalOpen;
        return true;
    }

    if (key == SDLK_p)
    {
        m_playerOverviewOpen = !m_playerOverviewOpen;
        m_playerOverviewFocus = m_playerOverviewOpen;
        return true;
    }

    return false;
}

bool Campaign::sharedUiBlocksMouseLook() const
{
    return m_consoleOpen ||
        m_inventoryOpen ||
        m_playerOverviewOpen ||
        m_questJournalOpen ||
        m_npcDialogOpen ||
        m_forageWindowOpen;
}

float Campaign::playerX() const
{
    return m_player.x;
}

float Campaign::playerY() const
{
    return m_player.y;
}

void Campaign::setPlayerPosition(float x, float y)
{
    if (!std::isfinite(x) || !std::isfinite(y))
        return;

    m_player.setPosition(x, y);
    m_player.vx = 0.0f;
    m_player.vy = 0.0f;
    clampPlayerToMap();
    updateFogOfWar();
}

void Campaign::setGameDateTime(int day, int month, int year, int hour, int minute)
{
    year = std::max(1, year);
    month = std::clamp(month, 1, 12);
    day = std::clamp(day, 1, DaysInMonth(month, year));
    hour = std::clamp(hour, 0, 23);
    minute = std::clamp(minute, 0, 59);

    m_gameTime.setStartDateTime(day, month, year, hour, minute);
    m_cachedWeatherDay = -1;
    m_cachedWeatherMonth = -1;
    m_cachedWeatherYear = -1;
    updateWeather();
    updateSkyOverlay(0.0f);
}

json Campaign::saveRuntimeState() const
{
    json npcs = json::array();
    for (const NpcInstance& npc : m_npcManager.npcs())
        npcs.push_back(SaveNpcState(npc));

    json fowValues = json::array();
    for (uint8_t value : m_fowVisited)
        fowValues.push_back(static_cast<int>(value));

    json respawns = json::object();
    for (const auto& [spawnId, day] : m_forageRespawnAvailableDay)
        respawns[spawnId] = day;

    json collected = json::array();
    for (const ForageInventoryStack& stack : m_collectedForageItems)
    {
        collected.push_back(json{
            {"species_id", stack.speciesId},
            {"display_name", stack.displayName},
            {"sprite_id", stack.spriteId},
            {"count", stack.count}
        });
    }

    json activeAnswers = json::array();
    for (const ForageExaminationAnswer& answer : m_activeForageAnswers)
    {
        activeAnswers.push_back(json{
            {"trait_group_id", answer.traitGroupId},
            {"option_id", answer.optionId},
            {"free_text", answer.freeText}
        });
    }

    json activeTraitText = json::object();
    for (const auto& [traitId, text] : m_activeForageTraitText)
        activeTraitText[traitId] = FixedBufferString(text);

    std::string dialogNpcId;
    if (m_dialogNpcIndex >= 0 && m_dialogNpcIndex < static_cast<int>(m_npcManager.npcs().size()))
        dialogNpcId = m_npcManager.npcs()[m_dialogNpcIndex].id;

    return json{
        {"version", 2},
        {"map", m_currentMapPath},
        {"game_time", SaveDateTime(m_gameTime.now())},
        {"player", SavePlayer(m_player)},
        {"npcs", npcs},
        {"story_flags", SaveStringSet(m_storyFlags)},
        {"fog_of_war", json{
            {"map_w", m_map.width()},
            {"map_h", m_map.height()},
            {"visited", fowValues}
        }},
        {"weather", json{
            {"today", SaveWeatherDay(m_todayWeather)},
            {"runtime", SaveWeatherRuntime(m_runtimeWeather)},
            {"cached_day", m_cachedWeatherDay},
            {"cached_month", m_cachedWeatherMonth},
            {"cached_year", m_cachedWeatherYear},
            {"debug", json{
                {"enabled", m_weatherDebug.enabled},
                {"override_rain", m_weatherDebug.overrideRain},
                {"forced_rain", m_weatherDebug.forcedRain},
                {"forced_rain_intensity", m_weatherDebug.forcedRainIntensity},
                {"override_fog", m_weatherDebug.overrideFog},
                {"forced_fog", m_weatherDebug.forcedFog},
                {"override_temp", m_weatherDebug.overrideTemp},
                {"forced_temp", m_weatherDebug.forcedTemp},
                {"override_wind", m_weatherDebug.overrideWind},
                {"forced_wind", m_weatherDebug.forcedWind},
                {"override_cloudiness", m_weatherDebug.overrideCloudiness},
                {"forced_cloudiness", m_weatherDebug.forcedCloudiness},
                {"override_ground_wetness", m_weatherDebug.overrideGroundWetness},
                {"forced_ground_wetness", m_weatherDebug.forcedGroundWetness}
            }}
        }},
        {"forage", json{
            {"knowledge", SaveForageKnowledge(m_forageKnowledge)},
            {"depleted_spawns", SaveStringSet(m_depletedForageSpawnIds)},
            {"respawn_available_day", respawns},
            {"collected_items", collected},
            {"window_open", m_forageWindowOpen},
            {"active_spawn_id", m_activeForageSpawnId},
            {"active_tile_x", m_activeForageTileX},
            {"active_tile_y", m_activeForageTileY},
            {"active_species_id", m_activeForageSpeciesId},
            {"active_answers", activeAnswers},
            {"active_trait_text", activeTraitText},
            {"active_description_text", FixedBufferString(m_activeForageDescriptionText)},
            {"active_name_guess", FixedBufferString(m_activeForageNameGuess)},
            {"has_last_result", m_hasLastForageExaminationResult},
            {"last_result", SaveForageResult(m_lastForageExaminationResult)}
        }},
        {"food_poisoning", SavePoisoning(m_activeFoodPoisoning)},
        {"dialog", json{
            {"open", m_npcDialogOpen},
            {"npc_instance_id", dialogNpcId},
            {"node_id", m_activeDialogNodeId}
        }},
        {"ui", json{
            {"inventory_open", m_inventoryOpen},
            {"player_overview_open", m_playerOverviewOpen},
            {"quest_journal_open", m_questJournalOpen},
            {"console_open", m_consoleOpen},
            {"show_debug_hud", m_showDebugHud},
            {"god_mode", m_godMode},
            {"console_input", FixedBufferString(m_consoleInput)},
            {"console_log", SaveStringVector(m_consoleLog)},
            {"console_history", SaveStringVector(m_consoleHistory)}
        }},
        {"transitions", json{
            {"pending_interior_id", m_pendingInteriorTransitionId},
            {"pending_interior_spawn_id", m_pendingInteriorTransitionSpawnId}
        }},
        {"hud", json{
            {"day_anim_time", m_dayHudAnimTime},
            {"sky_overlay_scroll_x", m_skyOverlayScrollX},
            {"sky_overlay_scroll_y", m_skyOverlayScrollY},
            {"cloud_time", m_cloudTime}
        }}
    };
}

void Campaign::loadRuntimeState(const json& state)
{
    if (!state.is_object())
        return;

    try
    {
        const json time = state.value("game_time", json::object());
        if (time.is_object())
        {
            setGameDateTime(
                JsonValue(time, "day", m_gameTime.now().day),
                JsonValue(time, "month", m_gameTime.now().month),
                JsonValue(time, "year", m_gameTime.now().year),
                JsonValue(time, "hour", m_gameTime.now().hour),
                JsonValue(time, "minute", m_gameTime.now().minute));
        }

        LoadPlayer(state.value("player", json::object()), m_player, m_characterManager);
        clampPlayerToMap();
        refreshInventoryDerivedStats();

        if (state.contains("npcs") && state["npcs"].is_array())
        {
            for (const auto& npcJson : state["npcs"])
            {
                const std::string id = JsonValue(npcJson, "id", std::string());
                if (id.empty())
                    continue;

                if (NpcInstance* npc = m_npcManager.findNpcById(id))
                    LoadNpcState(npcJson, *npc);
            }
        }

        LoadStringSet(state.value("story_flags", json::array()), m_storyFlags);

        const json fow = state.value("fog_of_war", json::object());
        if (fow.is_object())
        {
            const int mapW = JsonValue(fow, "map_w", 0);
            const int mapH = JsonValue(fow, "map_h", 0);
            const json visited = fow.value("visited", json::array());
            if (mapW == m_map.width() && mapH == m_map.height() && visited.is_array() &&
                visited.size() == static_cast<std::size_t>(mapW * mapH))
            {
                m_fowVisited.clear();
                m_fowVisited.reserve(visited.size());
                for (const auto& item : visited)
                {
                    const int value = item.is_number_integer() ? item.get<int>() : 0;
                    m_fowVisited.push_back(static_cast<uint8_t>(std::clamp(value, 0, 1)));
                }
            }
        }

        const json weather = state.value("weather", json::object());
        if (weather.is_object())
        {
            LoadWeatherDay(weather.value("today", json::object()), m_todayWeather);
            LoadWeatherRuntime(weather.value("runtime", json::object()), m_runtimeWeather);
            m_cachedWeatherDay = JsonValue(weather, "cached_day", m_cachedWeatherDay);
            m_cachedWeatherMonth = JsonValue(weather, "cached_month", m_cachedWeatherMonth);
            m_cachedWeatherYear = JsonValue(weather, "cached_year", m_cachedWeatherYear);

            const json debug = weather.value("debug", json::object());
            if (debug.is_object())
            {
                m_weatherDebug.enabled = JsonValue(debug, "enabled", m_weatherDebug.enabled);
                m_weatherDebug.overrideRain = JsonValue(debug, "override_rain", m_weatherDebug.overrideRain);
                m_weatherDebug.forcedRain = JsonValue(debug, "forced_rain", m_weatherDebug.forcedRain);
                m_weatherDebug.forcedRainIntensity = std::clamp(JsonValue(debug, "forced_rain_intensity", m_weatherDebug.forcedRainIntensity), 0.0f, 1.0f);
                m_weatherDebug.overrideFog = JsonValue(debug, "override_fog", m_weatherDebug.overrideFog);
                m_weatherDebug.forcedFog = JsonValue(debug, "forced_fog", m_weatherDebug.forcedFog);
                m_weatherDebug.overrideTemp = JsonValue(debug, "override_temp", m_weatherDebug.overrideTemp);
                m_weatherDebug.forcedTemp = JsonValue(debug, "forced_temp", m_weatherDebug.forcedTemp);
                m_weatherDebug.overrideWind = JsonValue(debug, "override_wind", m_weatherDebug.overrideWind);
                m_weatherDebug.forcedWind = JsonValue(debug, "forced_wind", m_weatherDebug.forcedWind);
                m_weatherDebug.overrideCloudiness = JsonValue(debug, "override_cloudiness", m_weatherDebug.overrideCloudiness);
                m_weatherDebug.forcedCloudiness = PlayerStats::Clamp01To100(JsonValue(debug, "forced_cloudiness", m_weatherDebug.forcedCloudiness));
                m_weatherDebug.overrideGroundWetness = JsonValue(debug, "override_ground_wetness", m_weatherDebug.overrideGroundWetness);
                m_weatherDebug.forcedGroundWetness = PlayerStats::Clamp01To100(JsonValue(debug, "forced_ground_wetness", m_weatherDebug.forcedGroundWetness));
            }
        }

        const json forage = state.value("forage", json::object());
        if (forage.is_object())
        {
            LoadForageKnowledge(forage.value("knowledge", json::object()), m_forageKnowledge);
            LoadStringSet(forage.value("depleted_spawns", json::array()), m_depletedForageSpawnIds);

            m_forageRespawnAvailableDay.clear();
            const json respawns = forage.value("respawn_available_day", json::object());
            if (respawns.is_object())
            {
                for (auto it = respawns.begin(); it != respawns.end(); ++it)
                {
                    if (it.value().is_number_integer())
                        m_forageRespawnAvailableDay[it.key()] = it.value().get<int>();
                }
            }

            m_collectedForageItems.clear();
            const json collected = forage.value("collected_items", json::array());
            if (collected.is_array())
            {
                for (const auto& stackJson : collected)
                {
                    if (!stackJson.is_object())
                        continue;

                    ForageInventoryStack stack;
                    stack.speciesId = JsonValue(stackJson, "species_id", std::string());
                    stack.displayName = JsonValue(stackJson, "display_name", std::string());
                    stack.spriteId = JsonValue(stackJson, "sprite_id", std::string());
                    stack.count = std::max(0, JsonValue(stackJson, "count", 0));
                    if (!stack.speciesId.empty() && stack.count > 0)
                        m_collectedForageItems.push_back(std::move(stack));
                }
            }

            m_forageWindowOpen = JsonValue(forage, "window_open", m_forageWindowOpen);
            m_forageWindowFocus = m_forageWindowOpen;
            m_activeForageSpawnId = JsonValue(forage, "active_spawn_id", m_activeForageSpawnId);
            m_activeForageTileX = JsonValue(forage, "active_tile_x", m_activeForageTileX);
            m_activeForageTileY = JsonValue(forage, "active_tile_y", m_activeForageTileY);
            m_activeForageSpeciesId = JsonValue(forage, "active_species_id", m_activeForageSpeciesId);

            m_activeForageAnswers.clear();
            const json activeAnswers = forage.value("active_answers", json::array());
            if (activeAnswers.is_array())
            {
                for (const auto& answerJson : activeAnswers)
                {
                    if (!answerJson.is_object())
                        continue;

                    ForageExaminationAnswer answer;
                    answer.traitGroupId = JsonValue(answerJson, "trait_group_id", std::string());
                    answer.optionId = JsonValue(answerJson, "option_id", std::string());
                    answer.freeText = JsonValue(answerJson, "free_text", std::string());
                    m_activeForageAnswers.push_back(std::move(answer));
                }
            }

            m_activeForageTraitText.clear();
            const json traitText = forage.value("active_trait_text", json::object());
            if (traitText.is_object())
            {
                for (auto it = traitText.begin(); it != traitText.end(); ++it)
                {
                    if (!it.value().is_string())
                        continue;

                    std::array<char, 160> buffer{};
                    const std::string text = it.value().get<std::string>();
                    std::snprintf(buffer.data(), buffer.size(), "%s", text.c_str());
                    m_activeForageTraitText[it.key()] = buffer;
                }
            }

            const std::string desc = JsonValue(forage, "active_description_text", std::string());
            std::snprintf(m_activeForageDescriptionText, sizeof(m_activeForageDescriptionText), "%s", desc.c_str());
            const std::string guess = JsonValue(forage, "active_name_guess", std::string());
            std::snprintf(m_activeForageNameGuess, sizeof(m_activeForageNameGuess), "%s", guess.c_str());

            m_hasLastForageExaminationResult = JsonValue(forage, "has_last_result", m_hasLastForageExaminationResult);
            LoadForageResult(forage.value("last_result", json::object()), m_lastForageExaminationResult);
        }

        LoadPoisoning(state.value("food_poisoning", json::object()), m_activeFoodPoisoning);

        const json dialog = state.value("dialog", json::object());
        if (dialog.is_object())
        {
            m_npcDialogOpen = false;
            m_dialogNpcIndex = -1;
            m_activeDialog = nullptr;
            m_activeDialogNodeId.clear();

            if (JsonValue(dialog, "open", false))
            {
                const std::string npcId = JsonValue(dialog, "npc_instance_id", std::string());
                const auto& npcs = m_npcManager.npcs();
                for (int i = 0; i < static_cast<int>(npcs.size()); ++i)
                {
                    if (npcs[i].id != npcId)
                        continue;

                    m_dialogNpcIndex = i;
                    m_npcDialogOpen = true;
                    m_activeDialogNodeId = JsonValue(dialog, "node_id", std::string());
                    if (!npcs[i].scriptId.empty())
                        m_activeDialog = m_dialogManager.findDialog(npcs[i].scriptId);
                    break;
                }
            }
        }

        const json ui = state.value("ui", json::object());
        if (ui.is_object())
        {
            m_inventoryOpen = JsonValue(ui, "inventory_open", m_inventoryOpen);
            m_inventoryFocus = m_inventoryOpen;
            m_playerOverviewOpen = JsonValue(ui, "player_overview_open", m_playerOverviewOpen);
            m_playerOverviewFocus = m_playerOverviewOpen;
            m_questJournalOpen = JsonValue(ui, "quest_journal_open", m_questJournalOpen);
            m_questJournalFocus = m_questJournalOpen;
            m_consoleOpen = JsonValue(ui, "console_open", m_consoleOpen);
            m_consoleFocusInput = m_consoleOpen;
            m_showDebugHud = JsonValue(ui, "show_debug_hud", m_showDebugHud);
            m_godMode = JsonValue(ui, "god_mode", m_godMode);
            const std::string consoleInput = JsonValue(ui, "console_input", std::string());
            std::snprintf(m_consoleInput, sizeof(m_consoleInput), "%s", consoleInput.c_str());
            m_consoleLog = LoadStringVector(ui.value("console_log", json::array()));
            m_consoleHistory = LoadStringVector(ui.value("console_history", json::array()));
            m_consoleHistoryIndex = -1;
            m_consoleAutocompleteBase.clear();
            m_consoleAutocompleteMatches.clear();
            m_consoleAutocompleteIndex = -1;
        }

        const json transitions = state.value("transitions", json::object());
        if (transitions.is_object())
        {
            m_pendingInteriorTransitionId = JsonValue(transitions, "pending_interior_id", m_pendingInteriorTransitionId);
            m_pendingInteriorTransitionSpawnId = JsonValue(transitions, "pending_interior_spawn_id", m_pendingInteriorTransitionSpawnId);
        }

        const json hud = state.value("hud", json::object());
        if (hud.is_object())
        {
            m_dayHudAnimTime = JsonValue(hud, "day_anim_time", m_dayHudAnimTime);
            m_skyOverlayScrollX = JsonValue(hud, "sky_overlay_scroll_x", m_skyOverlayScrollX);
            m_skyOverlayScrollY = JsonValue(hud, "sky_overlay_scroll_y", m_skyOverlayScrollY);
            m_cloudTime = JsonValue(hud, "cloud_time", m_cloudTime);
        }

        m_dragItem = InventoryDragState{};
        updateNearbyNpc();
    }
    catch (...)
    {
        consoleLog("Ulozena pozice obsahuje cast neplatnych dat. Co slo, zustalo nactene.");
    }
}

void Campaign::updateSharedRuntime(float dt, bool playerMoving, bool playerRunning)
{
    m_player.isMoving = playerMoving;
    m_player.isSprinting = playerRunning;

    m_gameTime.setPaused(m_consoleOpen);

    const int elapsedGameMinutes = m_gameTime.update(dt);

    updateWeather();
    updatePlayerNeeds(elapsedGameMinutes, playerMoving, playerRunning);
    updateFoodPoisoning(elapsedGameMinutes);

    m_dayHudAnimTime += dt;
    if (m_dayHudAnimTime > 1000.0f)
        m_dayHudAnimTime = std::fmod(m_dayHudAnimTime, 1000.0f);

    updateSkyOverlay(dt);
}
