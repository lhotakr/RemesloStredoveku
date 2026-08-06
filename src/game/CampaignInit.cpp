#include "Campaign.h"
#include "../PathUtils.h"
#include "../JsonUtils.h"
#include <nlohmann/json.hpp>

#include <SDL_image.h>
#include <filesystem>
#include <string>
#include <initializer_list>

namespace fs = std::filesystem;

using json = nlohmann::json;

static float JsonFloatAny(const json& j, float def, std::initializer_list<const char*> keys)
{
    for (const char* key : keys)
    {
        try
        {
            if (!j.contains(key))
                continue;

            const auto& v = j.at(key);
            if (v.is_number())
                return v.get<float>();
            if (v.is_string())
                return std::stof(v.get<std::string>());
        }
        catch (...)
        {
        }
    }
    return def;
}


static ItemCategory ItemCategoryFromString(const std::string& s)
{
    if (s == "Clothing")       return ItemCategory::Clothing;
    if (s == "BeltItem")       return ItemCategory::BeltItem;
    if (s == "Tool")           return ItemCategory::Tool;
    if (s == "WeaponTool")     return ItemCategory::WeaponTool;
    if (s == "Food")           return ItemCategory::Food;
    if (s == "Drink")          return ItemCategory::Drink;
    if (s == "WaterContainer") return ItemCategory::WaterContainer;
    if (s == "Container")      return ItemCategory::Container;
    if (s == "Material")       return ItemCategory::Material;
    if (s == "Utility")        return ItemCategory::Utility;
    if (s == "Medicine")       return ItemCategory::Medicine;
    if (s == "Quest")          return ItemCategory::Quest;
    return ItemCategory::Utility;
}

static EquipSlot EquipSlotFromString(const std::string& s)
{
    if (s == "Head")       return EquipSlot::Head;
    if (s == "TorsoInner") return EquipSlot::TorsoInner;
    if (s == "TorsoOuter") return EquipSlot::TorsoOuter;
    if (s == "Legs")       return EquipSlot::Legs;
    if (s == "Feet")       return EquipSlot::Feet;
    if (s == "Cloak")      return EquipSlot::Cloak;
    if (s == "Belt")       return EquipSlot::Belt;
    if (s == "Back")       return EquipSlot::Back;
    if (s == "MainHand")   return EquipSlot::MainHand;
    if (s == "OffHand")    return EquipSlot::OffHand;
    return EquipSlot::None;
}

static BeltSlot BeltSlotFromString(const std::string& s)
{
    if (s == "Knife")    return BeltSlot::Knife;
    if (s == "Pouch")    return BeltSlot::Pouch;
    if (s == "Utility1") return BeltSlot::Utility1;
    if (s == "Utility2") return BeltSlot::Utility2;
    return BeltSlot::None;
}

static void GiveItemToBackpack(
    PlayerInventory& inv,
    const std::unordered_map<std::string, ItemDef>& defs,
    const std::string& itemId,
    int count = 1)
{
    auto it = defs.find(itemId);
    if (it == defs.end())
        return;

    inv.addItem(it->second, count, defs);
}

static void EquipDirect(
    ItemStack& slot,
    const std::string& itemId)
{
    slot.itemId = itemId;
    slot.count = 1;
    slot.durability = 100.0f;
    slot.wetness = 0.0f;
}

static void ApplyStartingLoadout(
    Player& player,
    const std::unordered_map<std::string, ItemDef>& defs,
    PlayerStats::Background background)
{
    player.inventory = PlayerInventory{};
    player.inventory.initDefaults();

    auto it = defs.find("artifact_hoe");
    if (it != defs.end())
        player.inventory.pockets.addItem(it->second, 1, defs);

    switch (background)
    {
    case PlayerStats::Background::Survivalist:
        EquipDirect(player.inventory.equipped.head, "hat");
        EquipDirect(player.inventory.equipped.torsoInner, "shirt_outdoor_warm");
        EquipDirect(player.inventory.equipped.torsoOuter, "jacket_outdoor");
        EquipDirect(player.inventory.equipped.legs, "pants_outdoor");
        EquipDirect(player.inventory.equipped.feet, "boots_outdoor");
        EquipDirect(player.inventory.equipped.belt, "belt_leather_strong");
        EquipDirect(player.inventory.equipped.back, "backpack_modern_large");

        player.inventory.syncBackpackFromEquippedItem(defs);

        EquipDirect(player.inventory.beltSlots.knife, "knife_bushcraft");
        EquipDirect(player.inventory.beltSlots.pouch, "pouch_small");

        GiveItemToBackpack(player.inventory, defs, "wood_stove", 1);
        GiveItemToBackpack(player.inventory, defs, "flint_and_steel", 1);
        GiveItemToBackpack(player.inventory, defs, "military_scum", 1);
        GiveItemToBackpack(player.inventory, defs, "hammock_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "tarp_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "water_filter_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "water_bottle_metal", 3);
        GiveItemToBackpack(player.inventory, defs, "food_ration_basic", 2);
        break;

    case PlayerStats::Background::ScholarAthlete:
        EquipDirect(player.inventory.equipped.head, "baseball_black_cap");
        EquipDirect(player.inventory.equipped.torsoInner, "shirt_modern_basic");
        EquipDirect(player.inventory.equipped.torsoOuter, "hoodie_basic");
        EquipDirect(player.inventory.equipped.legs, "pants_modern_basic");
        EquipDirect(player.inventory.equipped.feet, "sneakers");
        EquipDirect(player.inventory.equipped.belt, "belt_basic");
        EquipDirect(player.inventory.equipped.back, "backpack_modern_medium");

        player.inventory.syncBackpackFromEquippedItem(defs);

        GiveItemToBackpack(player.inventory, defs, "gas_stove", 1);
        GiveItemToBackpack(player.inventory, defs, "gas_can", 1);
        GiveItemToBackpack(player.inventory, defs, "lighter", 1);
        EquipDirect(player.inventory.beltSlots.knife, "knife_small");
        EquipDirect(player.inventory.beltSlots.pouch, "pouch_small");

        GiveItemToBackpack(player.inventory, defs, "hammock_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "tarp_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "water_filter_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "water_bottle_metal", 1);
        GiveItemToBackpack(player.inventory, defs, "powerbank_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "solar_panel_folded", 1);
        GiveItemToBackpack(player.inventory, defs, "smartphone_offline", 1);
        GiveItemToBackpack(player.inventory, defs, "food_ration_basic", 2);
        break;

    case PlayerStats::Background::SocialAdaptable:
        EquipDirect(player.inventory.equipped.head, "sport_cap");
        EquipDirect(player.inventory.equipped.torsoInner, "shirt_modern_basic");
        EquipDirect(player.inventory.equipped.legs, "pants_modern_basic");
        EquipDirect(player.inventory.equipped.feet, "shoes_hiking_light");
        EquipDirect(player.inventory.equipped.belt, "belt_basic");
        EquipDirect(player.inventory.equipped.back, "backpack_modern_small");

        player.inventory.syncBackpackFromEquippedItem(defs);

        EquipDirect(player.inventory.beltSlots.pouch, "pouch_small");

        GiveItemToBackpack(player.inventory, defs, "hammock_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "tarp_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "water_bottle_plastic", 2);
        GiveItemToBackpack(player.inventory, defs, "powerbank_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "solar_panel_folded", 1);
        GiveItemToBackpack(player.inventory, defs, "smartphone_offline", 1);
        GiveItemToBackpack(player.inventory, defs, "hygiene_kit_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "notebook_small", 1);
        GiveItemToBackpack(player.inventory, defs, "pencil_basic", 1);
        GiveItemToBackpack(player.inventory, defs, "MRE", 2);
        break;
    }
}

static SDL_Texture* loadTexture(SDL_Renderer* r, const char* path, int& outW, int& outH)
{
    outW = outH = 0;

    SDL_Surface* surf = IMG_Load(path);
    if (!surf)
        return nullptr;

    outW = surf->w;
    outH = surf->h;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);

    return tex;
}

bool Campaign::loadObjectAtlases(std::string* outError)
{
    destroyObjectAtlases();

    std::unordered_map<std::string, bool> seen;

    for (const auto& def : m_objCatalog.Objects())
    {
        if (!def.has_sprite)
            continue;

        if (def.image.empty())
            continue;

        if (seen.find(def.image) != seen.end())
            continue;

        const std::string fullPath =
            (pathutils::ProjectRoot() / "assets" / "Objects" / def.image).string();

        SDL_Texture* tex = IMG_LoadTexture(m_renderer, fullPath.c_str());
        if (!tex)
        {
            if (outError)
                *outError = "IMG_LoadTexture failed: " + fullPath;
            destroyObjectAtlases();
            return false;
        }

        m_objAtlases[def.image] = tex;
        seen[def.image] = true;
    }

    return true;
}

void Campaign::destroyObjectAtlases()
{
    for (auto& kv : m_objAtlases)
    {
        if (kv.second)
            SDL_DestroyTexture(kv.second);
    }
    m_objAtlases.clear();
}

SDL_Texture* Campaign::textureForObject(const gameobj::ObjectDef& def) const
{
    auto it = m_objAtlases.find(def.image);
    if (it == m_objAtlases.end())
        return nullptr;
    return it->second;
}


void Campaign::destroyForageAtlases()
{
    for (auto& kv : m_forageAtlases)
    {
        if (kv.second)
            SDL_DestroyTexture(kv.second);
    }
    m_forageAtlases.clear();
}

SDL_Texture* Campaign::textureForForageSprite(const ForageSpriteRuntimeDef& sprite) const
{
    auto it = m_forageAtlases.find(sprite.image);
    if (it == m_forageAtlases.end())
        return nullptr;
    return it->second;
}

bool Campaign::loadForageSpriteData(std::string* outError)
{
    destroyForageAtlases();
    m_forageSprites.clear();

    const fs::path foragingDataDir = pathutils::DataDir() / "foraging";
    const fs::path spritesPath = foragingDataDir / "forage_sprites.json";
    const fs::path archetypesPath = foragingDataDir / "forage_archetypes.json";

    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(spritesPath.string(), root, err))
    {
        if (outError) *outError = "Forage sprites: " + err;
        return false;
    }

    if (!root.contains("sprites") || !root["sprites"].is_array())
    {
        if (outError) *outError = "Forage sprites: missing 'sprites' array";
        return false;
    }

    std::unordered_map<std::string, bool> seenImages;

    for (const auto& js : root["sprites"])
    {
        ForageSpriteRuntimeDef sp;
        sp.id = js.value("id", "");
        sp.image = js.value("image", "");

        if (js.contains("src") && js["src"].is_object())
        {
            const auto& r = js["src"];
            sp.src.x = r.value("x", 0);
            sp.src.y = r.value("y", 0);
            sp.src.w = r.value("w", 0);
            sp.src.h = r.value("h", 0);
        }

        if (js.contains("pivot") && js["pivot"].is_object())
        {
            const auto& pv = js["pivot"];
            sp.pivotX = pv.value("x", sp.src.w / 2);
            sp.pivotY = pv.value("y", sp.src.h);
        }
        else
        {
            sp.pivotX = sp.src.w / 2;
            sp.pivotY = sp.src.h;
        }

        if (sp.id.empty() || sp.image.empty() || sp.src.w <= 0 || sp.src.h <= 0)
            continue;

        if (!seenImages[sp.image])
        {
            const fs::path imagePath = pathutils::ProjectRoot() / "assets" / "Foraging" / sp.image;
            SDL_Texture* tex = IMG_LoadTexture(m_renderer, imagePath.string().c_str());
            if (!tex)
            {
                SDL_Log("Forage texture load failed: %s | %s", imagePath.string().c_str(), IMG_GetError());
            }
            else
            {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                m_forageAtlases[sp.image] = tex;
            }

            seenImages[sp.image] = true;
        }

        m_forageSprites[sp.id] = std::move(sp);
    }

    SDL_Log("Forage sprites loaded: %d atlas textures=%d", (int)m_forageSprites.size(), (int)m_forageAtlases.size());
    return true;
}

bool Campaign::init(SDL_Window* window, SDL_Renderer* renderer)
{
    m_window = window;
    m_renderer = renderer;

    if (!m_window || !m_renderer)
        return false;

    m_tileSize = 32;

    // character manager
    if (!m_characterManager.init(m_renderer, "assets/characters"))
    {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "CharacterManager error",
            m_characterManager.lastError().c_str(),
            m_window
        );
        return false;
    }

    // terrain tileset
    if (!m_tileset.loadFromJson(
        m_renderer,
        "assets/Tileset/terrain_tiles.json",
        "assets/Tileset/terrain_atlas.png"))
    {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Tileset error",
            "terrain_tiles.json / terrain_atlas.png load failed",
            m_window
        );
        return false;
    }

    // objects catalog
    {
        std::string err;

        if (!m_objCatalog.LoadFromFile(
            "assets/Objects/TreeAndStoneSprites.json",
            &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Objects JSON error",
                err.c_str(),
                m_window
            );
            return false;
        }

        if (!m_objCatalog.AppendFromFile(
            "assets/Objects/TechObjects.json",
            &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Objects JSON error (TechObjects)",
                err.c_str(),
                m_window
            );
            return false;
        }

        if (!m_objCatalog.AppendFromFile(
            "assets/Objects/CastleObjects.json",
            &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Objects JSON error (CastleObjects)",
                err.c_str(),
                m_window
            );
            return false;
        }

        if (!m_objCatalog.AppendFromFile(
            "assets/Objects/Houses.json",
            &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Objects JSON error (Houses)",
                err.c_str(),
                m_window
            );
            return false;
        }

        if (!m_objCatalog.AppendFromFile(
    "assets/Objects/Decoration.json",
            &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Objects JSON error (Decoration)",
                err.c_str(),
                m_window
            );
            return false;
        }

        if (!loadObjectAtlases(&err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Objects atlas error",
                err.c_str(),
                m_window
            );
            return false;
        }
    }

    const std::string mapPath =
        (pathutils::MapsDir() / "blatce.rvm").string();

    if (!loadMap(mapPath, ""))
    {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Map load error",
            mapPath.c_str(),
            m_window
        );
        return false;
    }

    // NPC types + spawns
    {
        std::string err;

        const std::string npcTypesPath =
            (pathutils::NpcsDir()
                / "NpcTypes.json").string();

        if (!m_npcManager.loadTypes(
            npcTypesPath,
            &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "NPC Types error",
                err.c_str(),
                m_window
            );

            return false;
        }

        fs::path npcSpawnPath =
            fs::path(mapPath);

        npcSpawnPath.replace_extension(".npcs.json");

        if (!m_npcManager.loadSpawns(
            npcSpawnPath.string(),
            m_tileSize,
            &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "NPC Spawns error",
                err.c_str(),
                m_window
            );

            return false;
        }
		m_npcManager.loadNpcVoices(m_audioManager);
    }

    {
        const std::string schedulesPath = (pathutils::NpcsDir() / "schedules.json").string();

        fs::path zonesPath = fs::path(mapPath);
        zonesPath.replace_extension(".zones.json");

        if (!m_npcManager.loadZones(zonesPath.string())) {
            SDL_Log("NPC zones not loaded: %s", zonesPath.string().c_str());
        }

        if (!m_npcManager.loadSchedules(schedulesPath)) {
            SDL_Log("NPC schedules not loaded: %s", schedulesPath.c_str());
        }
    }

    std::string playerId = "Character_2_char_05";

    if (!m_characterManager.getCharacter(playerId))
    {
        const auto ids = m_characterManager.characterIds();
        if (!ids.empty())
            playerId = ids.front();
    }

    SDL_Log("Selected player character: %s", playerId.c_str());

    if (!m_player.selectCharacter(playerId, m_characterManager))
    {
        std::string msg =
            "Nepodarilo se vybrat postavu: " + playerId;

        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Player character error",
            msg.c_str(),
            m_window
        );
        return false;
    }
    
    if (!m_player.selectCharacter(
        playerId,
        m_characterManager))
    {
        std::string msg =
            "Nepodarilo se vybrat postavu: "
            + playerId;

        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Player character error",
            msg.c_str(),
            m_window
        );

        return false;
    }

    if (!spawnPlayerFromMap())
    {
        m_player.x = 10.5f * m_tileSize;
        m_player.y = 10.0f * m_tileSize;
    }

    m_player.stats.condition.health = 100.0f;

    m_player.stats.condition.nutrition = 95.0f;   // hunger 5 ? nutrition 95
    m_player.stats.condition.hydration = 92.0f;   // thirst 8 ? hydration 92

    m_player.stats.condition.fatigue = 10.0f;

    m_player.stats.condition.hygiene = 100.0f;    // hygiene 0 (�p�na) ? �ist� = 100

    m_player.stats.comfort.medievalAdaptation = 0.0f; // m�sto medievalFeel

    m_player.stats.condition.bodyTemperature = 50.0f; // neutr�ln� stav (d��v 36.5)

    m_player.stats.carryWeight = 0.0f;
    m_player.stats.carryCapacity = 25.0f;

    m_gameTime.setStartDateTime(
        29, 3, 1400, 8, 0
    );
    
	// load NPC definitions (e.g. for dialog, quests, etc.)
    {
        std::string err;

        const std::string npcDefinitionsPath =
            (pathutils::NpcsDir() / "NpcDefinitions.json").string();

        if (!m_npcDefinitions.loadFromFile(npcDefinitionsPath, &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "NPC Definitions error",
                err.c_str(),
                m_window
            );
            return false;
        }
    }

    applyNpcDefinitionsToInstances();

    // liturgical calendar
    {
        std::string err;

        const std::string calPath =
            (pathutils::DataDir()
                / "calendar"
                / "LiturgicalCalendar.json").string();

        if (!m_liturgicalCalendar.loadFromFile(
            calPath,
            &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Liturgicky kalendar",
                err.c_str(),
                m_window
            );

            return false;
        }
    }

    // sun cycle
    {
        std::string err;

        const std::string sunPath =
            (pathutils::DataDir()
                / "calendar"
                / "SunCycle.json").string();

        if (!m_sunCycle.loadFromFile(
            sunPath,
            &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "SunCycle error",
                err.c_str(),
                m_window
            );

            return false;
        }
    }

    // moon cycle
    {
        std::string err;

        const std::string moonPath =
            (pathutils::DataDir()
                / "calendar"
                / "MoonCycle.json").string();

        if (!m_moonCycle.loadFromFile(
            moonPath,
            &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "MoonCycle error",
                err.c_str(),
                m_window
            );

            return false;
        }
    }

    // weather
    {
        const auto& now = m_gameTime.now();
        const auto sun = m_sunCycle.getDayInfo(now.day, now.month, now.hour, now.minute);

        m_todayWeather = m_weatherSystem.getDayProfile(now.day, now.month, now.year);
        m_runtimeWeather = m_weatherSystem.getRuntimeState(
            m_todayWeather,
            now.hour,
            now.minute,
            sun.sunriseMinutes,
            sun.sunsetMinutes);
    }

    // sky overlay
    {
        int w = 0;
        int h = 0;

        m_skyOverlay = loadTexture(
            m_renderer,
            "assets/overlays/NightOverlay.png",
            w,
            h
        );

        if (!m_skyOverlay)
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Overlay error",
                "Nepodarilo se nacist NightOverlay.png",
                m_window
            );

            return false;
        }
    }

    // light mask texture
    {
        int w = 0;
        int h = 0;

        m_lightSoftTex = loadTexture(
            m_renderer,
            "assets/overlays/LightSoft.png",
            w,
            h
        );

        if (!m_lightSoftTex)
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Light texture error",
                "Nepodarilo se nacist LightSoft.png",
                m_window
            );

            return false;
        }
    }

    // fog of war overlay
    {
        int w = 0;
        int h = 0;

        m_fowOverlay = loadTexture(
            m_renderer,
            "assets/overlays/FogOfWar.png",
            w,
            h
        );

        if (!m_fowOverlay)
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Fog of War error",
                "Nepodarilo se nacist assets/overlays/FogOfWar.png",
                m_window
            );
            return false;
        }
    }

	// dialogs
    {
        std::string err;
        const std::string dialogPath =
            (pathutils::DataDir() / "dialogs" / "dialogs.json").string();

        if (!m_dialogManager.loadFromFile(dialogPath, &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Dialogs error",
                err.c_str(),
                m_window
            );
            return false;
        }
    }
    // items
    {
        std::string itemErr;

        const std::string itemsPath =
            (pathutils::DataDir()
                / "items"
                / "Items.json").string();

        if (!loadItemDefs(itemsPath, &itemErr))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Items error",
                itemErr.c_str(),
                m_window
            );
            return false;
        }
    }

    std::string itemErr;
    if (!loadItemDefs("assets/data/items.json", &itemErr))
    {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "ItemDefs load failed",
            itemErr.c_str(),
            m_window);
        return false;
    }

    m_player.applyBackground(campaignflow::GetSelectedBackground());
    ApplyStartingLoadout(m_player, m_itemDefs, m_player.stats.background);

	// default item icon (used when no specific sprite is found for an item)
    {
        int iconW = 0;
        int iconH = 0;
        m_defaultItemIcon = loadTexture(
            m_renderer,
            "assets/data/default.png",
            iconW,
            iconH);
    }

    // HUD
    if (!loadHudAssets())
    {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "HUD assets error",
            "Nepodarilo se nacist HUD atlas nebo metadata.",
            m_window
        );
        return false;
    }

	// weather system
    {
        std::string err;

        const std::string climatePath =
            (pathutils::DataDir()
                / "weather"
                / "ClimateProfile.json").string();

        if (!m_weatherSystem.loadClimateProfile(
            climatePath,
            &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Weather load error",
                err.c_str(),
                m_window);

            return false;
        }

        m_weatherSystem.setBaseSeed(1400u);
    }

    // Quests
    {
        std::string err;
        const std::string questsPath =
            (pathutils::DataDir() / "quests" / "quests.json").string();

        if (!loadQuestDefs(questsPath, &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "QuestDefs error",
                (err + "\nPath: " + questsPath).c_str(),
                m_window
            );
            return false;
        }
    }

    loadInspectCursor();
    loadForagingData();

    return true;
}

bool Campaign::loadItemDefs(const std::string& path, std::string* outError)
{
    json root;
    std::string err;

    if (!jsonutils::LoadJsonFileSafe(path, root, err))
    {
        if (outError) *outError = "ItemDefs: " + err;
        return false;
    }

    if (!root.contains("items") || !root["items"].is_array())
    {
        if (outError) *outError = "ItemDefs: chybi pole 'items' v " + path;
        return false;
    }

    m_itemDefs.clear();

    for (const auto& ji : root["items"])
    {
        ItemDef def;

        def.id = ji.value("id", "");
        def.name = ji.value("name", "");
		def.lockedInInventory = ji.value("lockedInInventory", false);
        def.description = ji.value("description", "");
        def.flavorText = ji.value("flavorText", "");
        def.spriteId = ji.value("spriteId", "item_default");
		def.audioNoteSfx = ji.value("audioNoteSfx", "");

        def.category = ItemCategoryFromString(
            ji.value("category", "Utility"));

        def.weight = ji.value("weight", 0.0f);
        def.volume = ji.value("volume", 0.0f);

        def.stackable = ji.value("stackable", false);
        def.maxStack = ji.value("maxStack", 1);

        def.equippable = ji.value("equippable", false);
        def.equipSlot = EquipSlotFromString(
            ji.value("equipSlot", "None"));

        def.beltCompatible = ji.value("beltCompatible", false);
        def.preferredBeltSlot = BeltSlotFromString(
            ji.value("preferredBeltSlot", "None"));

        def.isContainer = ji.value("isContainer", false);
        def.containerWeightCapacity = ji.value("containerWeightCapacity", 0.0f);
        def.containerVolumeCapacity = ji.value("containerVolumeCapacity", 0.0f);

        def.warmth = ji.value("warmth", 0.0f);
        def.rainProtection = ji.value("rainProtection", 0.0f);
        def.outfitAuthenticity = ji.value("outfitAuthenticity", 0.0f);
        def.socialValue = ji.value("socialValue", 0.0f);

        def.waterCapacity = ji.value("waterCapacity", 0.0f);
        def.toolPower = ji.value("toolPower", 0.0f);

        def.quickUsable = ji.value("quickUsable", false);

        if (def.id.empty())
            continue;

        if (def.name.empty())
            def.name = def.id;

        if (def.spriteId.empty())
            def.spriteId = "item_default";

        if (def.maxStack < 1)
            def.maxStack = 1;

        if (!def.stackable)
            def.maxStack = 1;

        m_itemDefs[def.id] = std::move(def);
    }

    if (m_itemDefs.empty())
    {
        if (outError) *outError = "ItemDefs: nenacten zadny item z " + path;
        return false;
    }

    for (const auto& [id, def] : m_itemDefs)
    {
        if (!def.audioNoteSfx.empty())
        {
            const std::string path = "assets/audio/items/" + def.audioNoteSfx + ".ogg";
            m_audioManager.loadSfx(def.audioNoteSfx, path);
        }
    }

    SDL_Surface* surfDefault = IMG_Load("assets/ui/cursor_default.png");
    if (surfDefault)
    {
        m_defaultCursor = SDL_CreateColorCursor(surfDefault, 0, 0);
        SDL_FreeSurface(surfDefault);
    }

    SDL_Surface* surfInspect = IMG_Load("assets/ui/cursor_inspect.png");
    if (surfInspect)
    {
        m_inspectCursor = SDL_CreateColorCursor(surfInspect, 8, 8);
        SDL_FreeSurface(surfInspect);
    }

    if (m_defaultCursor)
        SDL_SetCursor(m_defaultCursor);

    SDL_Log("ItemDefs loaded: %d", (int)m_itemDefs.size());
    return true;
}


void Campaign::shutdown()
{
    m_npcManager.clear();
    m_characterManager.shutdown();

	destroyObjectAtlases();
    destroyForageAtlases();

    if (m_skyOverlay)
    {
        SDL_DestroyTexture(m_skyOverlay);
        m_skyOverlay = nullptr;
    }

    if (m_lightMask)
    {
        SDL_DestroyTexture(m_lightMask);
        m_lightMask = nullptr;
    }

    if (m_lightSoftTex)
    {
        SDL_DestroyTexture(m_lightSoftTex);
        m_lightSoftTex = nullptr;
    }

    if (m_fowOverlay)
    {
        SDL_DestroyTexture(m_fowOverlay);
        m_fowOverlay = nullptr;
    }

    if (m_fowMask)
    {
        SDL_DestroyTexture(m_fowMask);
        m_fowMask = nullptr;
    }

    if (m_defaultItemIcon)
    {
        SDL_DestroyTexture(m_defaultItemIcon);
        m_defaultItemIcon = nullptr;
    }

    unloadItemIcons();

    m_tileset.destroyAtlas();

    if(m_defaultCursor)
    {
        SDL_FreeCursor(m_defaultCursor);
        m_defaultCursor = nullptr;
	}   

    if (m_inspectCursor)
    {
        SDL_FreeCursor(m_inspectCursor);
        m_inspectCursor = nullptr;
    }
    destroyHudAssets();

    m_renderer = nullptr;
    m_window = nullptr;
}

bool Campaign::loadMapLinksForCurrentMap()
{
    m_currentMapLinks.clear();

    std::filesystem::path p = std::filesystem::path(m_currentMapPath);
    p.replace_extension(".links.json");

    nlohmann::json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(p.string(), root, err))
        return false;

    if (!root.contains("links") || !root["links"].is_array())
        return false;

    for (const auto& jl : root["links"])
    {
        MapLinkDef link;
        link.id = jl.value("id", 0);
        link.targetMap = jl.value("target_map", "");
        link.targetLocation = jl.value("target_location", jl.value("target_interior", ""));
        link.targetSpawnId = jl.value("target_spawn_id", "");
        if (link.targetSpawnId.empty())
            link.targetSpawnId = jl.value("target_spawn", "");

        if (link.id > 0 && (!link.targetMap.empty() || !link.targetLocation.empty()))
            m_currentMapLinks.push_back(std::move(link));
    }

    return true;
}

bool Campaign::loadMap(const std::string& path, const std::string& spawnId)
{
    if (!m_map.loadFromFile(path))
        return false;

    m_currentMapPath = path;
    m_tileSize = m_map.tileSize();

    if (!spawnId.empty())
    {
        if (!spawnPlayerFromMapAt(spawnId))
            spawnPlayerFromMap();
    }
    else
    {
        spawnPlayerFromMap();
    }

    loadMapLinksForCurrentMap();

    // Forage data are map-specific (*.forage.json), so reload them on map transitions too.
    if (!m_forageDb.archetypes().empty())
        loadForagingData();

    m_npcManager.loadNpcVoices(m_audioManager);
    return true;
}

bool Campaign::saveMap(const std::string& path)
{
    return m_map.saveToFile(path);
}

bool Campaign::consumePendingInteriorTransition(std::string& outInteriorId, std::string& outSpawnId)
{
    if (m_pendingInteriorTransitionId.empty())
        return false;

    outInteriorId = m_pendingInteriorTransitionId;
    outSpawnId = m_pendingInteriorTransitionSpawnId;
    m_pendingInteriorTransitionId.clear();
    m_pendingInteriorTransitionSpawnId.clear();
    return true;
}

void Campaign::loadInspectCursor()
{
    SDL_Surface* surf = IMG_Load("assets/ui/cursor_inspect.png");
    if (!surf)
        return;

    m_inspectCursor = SDL_CreateColorCursor(surf, 0, 0);
    SDL_FreeSurface(surf);
}

void Campaign::updateInspectCursor()
{
    if (m_dragItem.active)
        return; // drag item si ��d� kurzor s�m

    if (isInspectHeld() && m_inspectCursor)
        SDL_SetCursor(m_inspectCursor);
    else
        //SDL_SetCursor(SDL_GetDefaultCursor());
		SDL_SetCursor(m_defaultCursor);
}

bool Campaign::spawnPlayerFromMap()
{
    for (int y = 0; y < m_map.height(); ++y)
    {
        for (int x = 0; x < m_map.width(); ++x)
        {
            const auto* def =
                m_map.getObjDefAt(
                    m_objCatalog,
                    x,
                    y
                );

            if (!def)
                continue;

            if (def->HasTag("spawn"))
            {
                int wx = 0;
                int wy = 0;

                m_map.getObjPivotWorld(
                    x,
                    y,
                    wx,
                    wy
                );

                m_player.x = (float)wx;
                m_player.y = (float)wy;

                return true;
            }
        }
    }

    return false;
}

bool Campaign::spawnPlayerFromMapAt(const std::string& spawnId)
{
    for (int y = 0; y < m_map.height(); ++y)
    {
        for (int x = 0; x < m_map.width(); ++x)
        {
            const auto* def = m_map.getObjDefAt(m_objCatalog, x, y);
            if (!def)
                continue;

            if (def->id != spawnId)
                continue;

            int wx = 0, wy = 0;
            m_map.getObjPivotWorld(x, y, wx, wy);

            m_player.x = (float)wx;
            m_player.y = (float)wy;
            return true;
        }
    }

    return false;
}

bool Campaign::loadForagingData()
{
    std::string err;

    const std::string base =
        (pathutils::DataDir() / "foraging").string() + "/";

    if (!loadForageSpriteData(&err))
    {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Foraging sprite error",
            err.c_str(),
            m_window
        );
        return false;
    }

    if (!m_forageDb.loadAll(
        base + "forage_archetypes.json",
        base + "forage_species.json",
        base + "forage_traits.json",
        &err))
    {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Foraging error",
            err.c_str(),
            m_window
        );
        return false;
    }

    // Runtime scale must be loaded from the same JSON that the editor saves.
    // Older patches used a separate cache loaded too early, so Campaign could silently
    // fall back to the giant default scale even after saving Map scale in the editor.
    m_forageArchetypeMapScale.clear();
    {
        json archeRoot;
        const fs::path archetypesPath = pathutils::DataDir() / "foraging" / "forage_archetypes.json";
        if (jsonutils::LoadJsonFileSafe(archetypesPath.string(), archeRoot, err) &&
            archeRoot.contains("archetypes") && archeRoot["archetypes"].is_array())
        {
            for (const auto& ja : archeRoot["archetypes"])
            {
                const std::string id = ja.value("id", "");
                if (id.empty())
                    continue;

                const float scale = JsonFloatAny(ja, 0.20f, { "map_scale", "scale", "mapScale" });
                m_forageArchetypeMapScale[id] = std::clamp(scale, 0.03f, 2.0f);
            }
        }
    }

    m_forageSpawnScaleOverride.clear();
    m_depletedForageSpawnIds.clear();
    m_forageRespawnAvailableDay.clear();
    m_activeForageSpawnId.clear();
    m_activeForageSpeciesId.clear();
    m_activeForageAnswers.clear();
    m_forageWindowOpen = false;

    fs::path foragePath = fs::path(m_currentMapPath);
    foragePath.replace_extension(".forage.json");

    if (fs::exists(foragePath))
    {
        if (!m_forageSystem.loadSpawnsForMap(foragePath.string(), &err))
        {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Foraging spawn error",
                err.c_str(),
                m_window
            );
            return false;
        }

        json spawnRoot;
        if (jsonutils::LoadJsonFileSafe(foragePath.string(), spawnRoot, err) &&
            spawnRoot.contains("forage_spawns") && spawnRoot["forage_spawns"].is_array())
        {
            for (const auto& js : spawnRoot["forage_spawns"])
            {
                const std::string id = js.value("id", "");
                if (id.empty())
                    continue;

                const float scale = JsonFloatAny(js, 0.0f, { "map_scale_override", "scale_override", "mapScaleOverride" });
                if (scale > 0.0f)
                    m_forageSpawnScaleOverride[id] = std::clamp(scale, 0.03f, 2.0f);
            }
        }
    }
    else
    {
        m_forageSystem.clear();
    }

    SDL_Log("Forage runtime scales: archetypes=%d spawn_overrides=%d spawns=%d",
        (int)m_forageArchetypeMapScale.size(),
        (int)m_forageSpawnScaleOverride.size(),
        (int)m_forageSystem.spawns().size());

    return true;
}
