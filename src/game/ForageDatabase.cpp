#include "ForageDatabase.h"
#include "../JsonUtils.h"
#include <nlohmann/json.hpp>
#include <initializer_list>
#include <algorithm>
#include <cctype>
#include <utility>

using json = nlohmann::json;

static ForageCategory ForageCategoryFromString(const std::string& s)
{
    if (s == "mushroom") return ForageCategory::Mushroom;
    return ForageCategory::Herb;
}

static float JsonFloatAny(const json& j, float def, std::initializer_list<const char*> keys)
{
    try
    {
        for (const char* key : keys)
        {
            if (!j.contains(key))
                continue;

            const auto& v = j.at(key);
            if (v.is_number_float() || v.is_number_integer())
                return v.get<float>();

            if (v.is_string())
                return std::stof(v.get<std::string>());
        }
    }
    catch (...)
    {
    }

    return def;
}


static std::string TrimForageDbText(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();

    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;

    return s.substr(b, e - b);
}

static std::vector<std::string> ReadStringArray(const json& j)
{
    std::vector<std::string> out;
    if (!j.is_array())
        return out;

    for (const auto& v : j)
    {
        if (v.is_string())
        {
            std::string value = TrimForageDbText(v.get<std::string>());
            if (!value.empty())
                out.push_back(std::move(value));
        }
    }

    return out;
}

static std::unordered_map<std::string, std::vector<std::string>> ReadTraitsObject(const json& j)
{
    std::unordered_map<std::string, std::vector<std::string>> out;
    if (!j.is_object())
        return out;

    for (auto it = j.begin(); it != j.end(); ++it)
    {
        std::vector<std::string> values = ReadStringArray(it.value());
        if (!values.empty())
            out[it.key()] = std::move(values);
    }

    return out;
}

void ForageDatabase::clear()
{
    m_archetypes.clear();
    m_species.clear();
    m_traitGroups.clear();
}

bool ForageDatabase::loadAll(
    const std::string& archetypesPath,
    const std::string& speciesPath,
    const std::string& traitsPath,
    std::string* outError)
{
    clear();

    if (!loadTraits(traitsPath, outError))
        return false;

    if (!loadArchetypes(archetypesPath, outError))
        return false;

    if (!loadSpecies(speciesPath, outError))
        return false;

    return true;
}

bool ForageDatabase::loadTraits(const std::string& path, std::string* outError)
{
    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(path, root, err))
    {
        if (outError) *outError = "Forage traits: " + err;
        return false;
    }

    if (!root.contains("trait_groups") || !root["trait_groups"].is_array())
    {
        if (outError) *outError = "Forage traits: missing 'trait_groups' array";
        return false;
    }

    for (const auto& jt : root["trait_groups"])
    {
        ForageTraitGroup g;
        g.id = jt.value("id", "");
        g.label = jt.value("label", "");

        if (jt.contains("options") && jt["options"].is_array())
        {
            for (const auto& jo : jt["options"])
            {
                ForageTraitOption opt;
                opt.id = jo.value("id", "");
                opt.label = jo.value("label", "");

                if (!opt.id.empty())
                    g.options.push_back(std::move(opt));
            }
        }

        if (!g.id.empty())
            m_traitGroups[g.id] = std::move(g);
    }

    return true;
}

bool ForageDatabase::loadArchetypes(const std::string& path, std::string* outError)
{
    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(path, root, err))
    {
        if (outError) *outError = "Forage archetypes: " + err;
        return false;
    }

    if (!root.contains("archetypes") || !root["archetypes"].is_array())
    {
        if (outError) *outError = "Forage archetypes: missing 'archetypes' array";
        return false;
    }

    for (const auto& ja : root["archetypes"])
    {
        ForageArchetypeDef a;
        a.id = ja.value("id", "");
        a.category = ForageCategoryFromString(ja.value("category", "herb"));
        a.displayUnknown = ja.value("display_unknown", "");
        a.displayPartial = ja.value("display_partial", "");
        a.genericMapSprite = ja.value("generic_map_sprite", "");
        a.mapScale = JsonFloatAny(ja, 0.35f, { "map_scale", "scale", "mapScale" });
        a.weight = JsonFloatAny(ja, a.category == ForageCategory::Mushroom ? 0.08f : 0.02f, { "weight", "weight_kg", "item_weight", "logistic_weight" });
        a.volume = JsonFloatAny(ja, a.category == ForageCategory::Mushroom ? 0.12f : 0.04f, { "volume", "volume_l", "item_volume", "logistic_volume" });
        a.maxStack = std::clamp(ja.value("max_stack", ja.value("maxStack", 64)), 1, 64);
        a.detailPlaceholderSprite = ja.value("detail_placeholder_sprite", "");
        a.herbariumPlaceholderSprite = ja.value("herbarium_placeholder_sprite", "");

        a.examinationSlots = ReadStringArray(ja.value("examination_slots", json::array()));
        a.defaultTraits = ReadTraitsObject(ja.value("default_traits", json::object()));

        if (!a.id.empty())
            m_archetypes[a.id] = std::move(a);
    }

    return true;
}

bool ForageDatabase::loadSpecies(const std::string& path, std::string* outError)
{
    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(path, root, err))
    {
        if (outError) *outError = "Forage species: " + err;
        return false;
    }

    if (!root.contains("species") || !root["species"].is_array())
    {
        if (outError) *outError = "Forage species: missing 'species' array";
        return false;
    }

    for (const auto& js : root["species"])
    {
        ForageSpeciesDef s;
        s.id = js.value("id", "");
        s.archetypeId = js.value("archetype_id", "");
        s.trueName = js.value("true_name", "");
        s.folkNames = ReadStringArray(js.value("folk_names", json::array()));
        s.detailSprite = js.value("detail_sprite", "");
        s.inventorySprite = js.value("inventory_sprite", "");
        s.herbariumSprite = js.value("herbarium_sprite", "");
        s.difficulty = std::clamp(js.value("difficulty", 0), -100, 100);
        s.description = js.value("description", js.value("free_description", js.value("identification_description", "")));
        s.traits = ReadTraitsObject(js.value("traits", json::object()));
        s.edibility = js.value("edibility", "unknown_safe");
        s.medicinalValue = js.value("medicinal_value", "none");
        s.toxicityLevel = js.value("toxicity_level", 0);
        s.weight = JsonFloatAny(js, 0.0f, { "weight", "weight_kg", "item_weight", "logistic_weight" });
        s.volume = JsonFloatAny(js, 0.0f, { "volume", "volume_l", "item_volume", "logistic_volume" });
        s.maxStack = std::clamp(js.value("max_stack", js.value("maxStack", 64)), 1, 64);
        s.effectsOnEat = ReadStringArray(js.value("effects_on_eat", json::array()));
        s.effectsOnUse = ReadStringArray(js.value("effects_on_use", json::array()));
        s.season = ReadStringArray(js.value("season", json::array()));

        if (!s.id.empty())
            m_species[s.id] = std::move(s);
    }

    return true;
}

const ForageArchetypeDef* ForageDatabase::findArchetype(const std::string& id) const
{
    auto it = m_archetypes.find(id);
    return it != m_archetypes.end() ? &it->second : nullptr;
}

const ForageSpeciesDef* ForageDatabase::findSpecies(const std::string& id) const
{
    auto it = m_species.find(id);
    return it != m_species.end() ? &it->second : nullptr;
}

const ForageTraitGroup* ForageDatabase::findTraitGroup(const std::string& id) const
{
    auto it = m_traitGroups.find(id);
    return it != m_traitGroups.end() ? &it->second : nullptr;
}