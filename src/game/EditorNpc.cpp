#include <filesystem>
#include <fstream>

#include "Editor.h"
#include "EditorHelpers.h"
#include "JsonUtils.h"
#include "PathUtils.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

bool Editor::loadNpcTypes()
{
    m_npcTypes.clear();

    std::filesystem::path p = pathutils::NpcsDir() / "NpcTypes.json";

    nlohmann::json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(p.string(), root, err)) {
        m_lastIoStatus = "NpcTypes parse failed: " + err;
        return false;
    }

    if (!root.contains("types") || !root["types"].is_array()) {
        m_lastIoStatus = "NpcTypes missing 'types' array";
        return false;
    }

    for (const auto& jt : root["types"])
    {
        EditorNpcType t;
        t.typeId = jt.value("type_id", "");
        t.name = jt.value("name", "");
        t.characterId = jt.value("character_id", "");
        t.defaultHP = jt.value("default_hp", 100);
        t.defaultMood = jt.value("default_mood", 50);
        t.defaultScriptId = jt.value("default_script_id", "");

        if (!t.typeId.empty())
            m_npcTypes.push_back(std::move(t));
    }

    if (m_npcTypes.empty()) {
        m_lastIoStatus = "NpcTypes loaded, but no entries found";
        return false;
    }

    m_selectedNpcTypeIndex = 0;
    applySelectedNpcTypeToBrush();
    m_lastIoStatus = "NpcTypes loaded";
    return true;
}

void Editor::applySelectedNpcTypeToBrush()
{
    if (m_selectedNpcTypeIndex < 0 || m_selectedNpcTypeIndex >= (int)m_npcTypes.size())
        return;

    const auto& t = m_npcTypes[m_selectedNpcTypeIndex];

    strncpy_s(m_npcTypeId, t.typeId.c_str(), _TRUNCATE);
    strncpy_s(m_npcCharacterId, t.characterId.c_str(), _TRUNCATE);
    strncpy_s(m_npcScriptId, t.defaultScriptId.c_str(), _TRUNCATE);

    m_npcHP = t.defaultHP;
    m_npcMood = t.defaultMood;

    m_npcName[0] = '\0';
    m_npcSurname[0] = '\0';
    m_npcGreeting[0] = '\0';

    const auto ids = m_characterManager.characterIds();
    for (int i = 0; i < (int)ids.size(); ++i) {
        if (ids[i] == t.characterId) {
            m_selectedCharacterIndex = i;
            break;
        }
    }
}

NpcSpawn* Editor::findNpcAt(int tileX, int tileY)
{
    for (auto& npc : m_npcSpawns) {
        if (npc.tileX == tileX && npc.tileY == tileY)
            return &npc;
    }
    return nullptr;
}

const NpcSpawn* Editor::findNpcAt(int tileX, int tileY) const
{
    for (const auto& npc : m_npcSpawns) {
        if (npc.tileX == tileX && npc.tileY == tileY)
            return &npc;
    }
    return nullptr;
}

int Editor::findNpcSpawnIndexAt(int tileX, int tileY) const
{
    for (int i = 0; i < (int)m_npcSpawns.size(); ++i)
    {
        if (m_npcSpawns[i].tileX == tileX && m_npcSpawns[i].tileY == tileY)
            return i;
    }

    return -1;
}

bool Editor::saveNpcsForMap(const std::string& mapPath)
{
    std::filesystem::path p(mapPath);
    p.replace_extension(".npcs.json");

    nlohmann::json root;
    root["npcs"] = nlohmann::json::array();

    for (const auto& npc : m_npcSpawns)
    {
        nlohmann::json jn = {
            {"id", npc.id},
            {"type_id", npc.typeId},
            {"script_id", npc.scriptId},
            {"character_id", npc.characterId},
            {"tile_x", npc.tileX},
            {"tile_y", npc.tileY},
            {"hp", npc.hp},
            {"mood", npc.mood}
        };

        if (!npc.name.empty())
            jn["name"] = npc.name;

        if (!npc.surname.empty())
            jn["surname"] = npc.surname;

        if (!npc.greeting.empty())
            jn["greeting"] = npc.greeting;

        root["npcs"].push_back(std::move(jn));
    }

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        m_lastIoStatus = "NPC save failed: " + p.string();
        return false;
    }

    f << root.dump(2);
    return true;
}

bool Editor::loadNpcsForMap(const std::string& mapPath)
{
    m_npcSpawns.clear();

    fs::path p(mapPath);
    p.replace_extension(".npcs.json");

    if (!fs::exists(p))
        return true;

    nlohmann::json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(p.string(), root, err)) {
        m_lastIoStatus = "NPC json parse failed: " + err;
        return false;
    }

    if (!root.contains("npcs") || !root["npcs"].is_array())
        return true;

    for (const auto& jn : root["npcs"])
    {
        NpcSpawn npc;
        npc.id = jn.value("id", "");
        npc.typeId = jn.value("type_id", "");

        npc.name = jn.value("name", "");
        npc.surname = jn.value("surname", "");
        npc.greeting = jn.value("greeting", "");

        npc.scriptId = jn.value("script_id", "");
        npc.characterId = jn.value("character_id", "Character_2_char_01");
        npc.tileX = jn.value("tile_x", 0);
        npc.tileY = jn.value("tile_y", 0);
        npc.hp = jn.value("hp", 100);
        npc.mood = jn.value("mood", 50);

        m_npcSpawns.push_back(std::move(npc));
    }

    return true;
}

void Editor::loadNpcSpawnToBrush(const NpcSpawn& npc)
{
    strncpy_s(m_npcId, npc.id.c_str(), _TRUNCATE);
    strncpy_s(m_npcTypeId, npc.typeId.c_str(), _TRUNCATE);
    strncpy_s(m_npcName, npc.name.c_str(), _TRUNCATE);
    strncpy_s(m_npcSurname, npc.surname.c_str(), _TRUNCATE);
    strncpy_s(m_npcGreeting, npc.greeting.c_str(), _TRUNCATE);
    strncpy_s(m_npcScriptId, npc.scriptId.c_str(), _TRUNCATE);
    strncpy_s(m_npcCharacterId, npc.characterId.c_str(), _TRUNCATE);

    m_npcHP = npc.hp;
    m_npcMood = npc.mood;

    for (int i = 0; i < (int)m_npcTypes.size(); ++i)
    {
        if (m_npcTypes[i].typeId == npc.typeId)
        {
            m_selectedNpcTypeIndex = i;
            break;
        }
    }

    const auto ids = m_characterManager.characterIds();
    for (int i = 0; i < (int)ids.size(); ++i)
    {
        if (ids[i] == npc.characterId)
        {
            m_selectedCharacterIndex = i;
            break;
        }
    }
}

void Editor::saveBrushToSelectedNpc()
{
    if (m_selectedNpcSpawnIndex < 0 || m_selectedNpcSpawnIndex >= (int)m_npcSpawns.size())
        return;

    auto& npc = m_npcSpawns[m_selectedNpcSpawnIndex];

    npc.id = m_npcId;
    npc.typeId = m_npcTypeId;
    npc.name = m_npcName;
    npc.surname = m_npcSurname;
    npc.greeting = m_npcGreeting;
    npc.scriptId = m_npcScriptId;
    npc.characterId = m_npcCharacterId;
    npc.hp = m_npcHP;
    npc.mood = m_npcMood;

    m_mapDirty = true;
    m_lastIoStatus = "NPC updated.";
}

void Editor::deleteSelectedNpc()
{
    pushUndoState("delete npc");
    if (m_selectedNpcSpawnIndex < 0 || m_selectedNpcSpawnIndex >= (int)m_npcSpawns.size())
        return;

    m_npcSpawns.erase(m_npcSpawns.begin() + m_selectedNpcSpawnIndex);
    m_selectedNpcSpawnIndex = -1;

    m_mapDirty = true;
    m_lastIoStatus = "NPC deleted.";
}

bool Editor::loadObjectAtlases(std::string* outError)
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

        SDL_Texture* tex = loadTextureFile(m_renderer, fullPath);
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

void Editor::destroyObjectAtlases()
{
    for (auto& kv : m_objAtlases)
    {
        if (kv.second)
            SDL_DestroyTexture(kv.second);
    }
    m_objAtlases.clear();
}

SDL_Texture* Editor::textureForObject(const gameobj::ObjectDef& def) const
{
    auto it = m_objAtlases.find(def.image);
    if (it == m_objAtlases.end())
        return nullptr;
    return it->second;
}


void Editor::toggleFullscreen()
{
    if (!m_window) return;
    m_fullscreen = !m_fullscreen;
    SDL_SetWindowFullscreen(m_window, m_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void Editor::newMap()
{
    constexpr int newW = 128;
    constexpr int newH = 128;

    m_map = TileMap(newW, newH, m_tileSize);
    m_npcSpawns.clear();
    m_mapDirty = false;

    const int grassCount = std::max(1, m_tileset.fillCount("grass"));

    for (int y = 0; y < m_map.height(); ++y) {
        for (int x = 0; x < m_map.width(); ++x) {
            const uint32_t h = hash2d(x, y);
            m_map.set(x, y, 1);
            m_map.setVar(x, y, (uint8_t)(h % (uint32_t)grassCount));
        }
    }
    loadZonesForMap(m_mapPath);
}

void Editor::rebuildCompositeGroups()
{
    m_compositeGroups = m_tileset.buildCompositeGroups();
}

void Editor::applySelectedCharacterToBrush()
{
    const auto& ids = m_characterManager.characterIds();
    if (ids.empty()) return;

    if (m_selectedCharacterIndex < 0) m_selectedCharacterIndex = 0;
    if (m_selectedCharacterIndex >= (int)ids.size())
        m_selectedCharacterIndex = (int)ids.size() - 1;

    strncpy_s(m_npcCharacterId, ids[m_selectedCharacterIndex].c_str(), _TRUNCATE);
}

bool Editor::saveMap(const char* path)
{
    if (!path || !path[0]) {
        m_lastIoStatus = "Save failed: empty path";
        return false;
    }

    fs::path p(path);

    if (!p.is_absolute())
        p = fs::path(m_mapsDir) / p;

    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    const bool ok = m_map.saveToFile(p.string());
    if (!ok) {
        m_lastIoStatus = std::string("Save failed: ") + p.string();
        return false;
    }

    if (!saveNpcsForMap(p.string())) {
        return false;
    }

    if (!saveZonesForMap(p.string())) {
        return false;
    }

    if (!saveNpcSchedules()) {
        return false;
    }

    m_mapDirty = false;
    m_mapPath = fs::weakly_canonical(p).string();
    m_lastIoStatus = std::string("Saved: ") + m_mapPath;
    return true;
}

bool Editor::loadMap(const char* path)
{
    if (!path || !path[0]) {
        m_lastIoStatus = "Load failed: empty path";
        return false;
    }

    fs::path p(path);

    if (!p.is_absolute())
        p = fs::path(m_mapsDir) / p;

    const bool ok = m_map.loadFromFile(p.string());
    if (!ok) {
        m_lastIoStatus = std::string("Load failed: ") + p.string();
        return false;
    }

    if (!loadNpcsForMap(p.string())) {
        return false;
    }

    if (!loadZonesForMap(p.string())) {
        return false;
    }

    m_mapDirty = false;
    m_mapPath = fs::weakly_canonical(p).string();
    m_tileSize = m_map.tileSize();

    m_lastIoStatus = std::string("Loaded: ") + m_mapPath;
    return true;
}

void Editor::renderNpcInspector()
{
    if (!ImGui::Begin("NPC Inspector"))
    {
        ImGui::End();
        return;
    }

    renderNpcInspectorContents();
    ImGui::End();
}

void Editor::renderNpcInspectorContents()
{
    if (ImGui::BeginListBox("NPCs on map", ImVec2(-1, 140)))
    {
        for (int i = 0; i < (int)m_npcSpawns.size(); ++i)
        {
            const auto& npc = m_npcSpawns[i];

            std::string label = npc.id;
            if (!npc.name.empty())
                label += " - " + npc.name;

            bool selected = (m_selectedNpcSpawnIndex == i);
            if (ImGui::Selectable(label.c_str(), selected))
            {
                m_selectedNpcSpawnIndex = i;
                loadNpcSpawnToBrush(npc);
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    ImGui::Separator();

    ImGui::InputText("NPC ID", m_npcId, IM_ARRAYSIZE(m_npcId));
    ImGui::InputText("Name", m_npcName, IM_ARRAYSIZE(m_npcName));
    ImGui::InputText("Surname", m_npcSurname, IM_ARRAYSIZE(m_npcSurname));
    ImGui::InputText("Greeting", m_npcGreeting, IM_ARRAYSIZE(m_npcGreeting));

    if (!m_npcTypes.empty())
    {
        std::vector<const char*> npcTypeLabels;
        npcTypeLabels.reserve(m_npcTypes.size());
        for (auto& t : m_npcTypes)
            npcTypeLabels.push_back(t.name.c_str());

        int oldNpcTypeIndex = m_selectedNpcTypeIndex;
        ImGui::Combo("NPC Type", &m_selectedNpcTypeIndex, npcTypeLabels.data(), (int)npcTypeLabels.size());
        if (m_selectedNpcTypeIndex != oldNpcTypeIndex)
            applySelectedNpcTypeToBrush();

        ImGui::Text("Type ID: %s", m_npcTypeId);
    }
    else
    {
        ImGui::TextColored(ImVec4(1, 0.6f, 0, 1), "NpcTypes.json not loaded");
        ImGui::InputText("Type ID", m_npcTypeId, IM_ARRAYSIZE(m_npcTypeId));
    }

    const auto charIds = m_characterManager.characterIds();
    if (!charIds.empty())
    {
        if (m_selectedCharacterIndex < 0) m_selectedCharacterIndex = 0;
        if (m_selectedCharacterIndex >= (int)charIds.size())
            m_selectedCharacterIndex = (int)charIds.size() - 1;

        std::vector<const char*> charLabels;
        charLabels.reserve(charIds.size());
        for (auto& id : charIds)
            charLabels.push_back(id.c_str());

        int oldCharIndex = m_selectedCharacterIndex;
        ImGui::Combo("Character", &m_selectedCharacterIndex, charLabels.data(), (int)charLabels.size());
        if (m_selectedCharacterIndex != oldCharIndex)
            applySelectedCharacterToBrush();

        ImGui::Text("Character ID: %s", m_npcCharacterId);

        const CharacterDef* ch = m_characterManager.getCharacter(m_npcCharacterId);
        SDL_Texture* tex = m_characterManager.getTextureForCharacter(m_npcCharacterId);

        if (ch && tex) {
            auto itAnim = ch->animations.find("idle_down");
            if (itAnim != ch->animations.end() && !itAnim->second.frames.empty()) {
                const auto& fr = itAnim->second.frames[0];

                ImVec2 uv0(
                    (float)fr.x / (float)ch->sheetW,
                    (float)fr.y / (float)ch->sheetH
                );
                ImVec2 uv1(
                    (float)(fr.x + fr.w) / (float)ch->sheetW,
                    (float)(fr.y + fr.h) / (float)ch->sheetH
                );

                ImGui::Text("Preview:");
                ImGui::Image((ImTextureID)tex, ImVec2((float)fr.w, (float)fr.h), uv0, uv1);
            }
        }
    }

    if (!m_dialogDefs.empty())
    {
        std::vector<const char*> dialogLabels;
        dialogLabels.reserve(m_dialogDefs.size());
        int currentDialogIndex = -1;

        for (int i = 0; i < (int)m_dialogDefs.size(); ++i)
        {
            dialogLabels.push_back(m_dialogDefs[i].dialogId.c_str());
            if (m_dialogDefs[i].dialogId == m_npcScriptId)
                currentDialogIndex = i;
        }

        if (currentDialogIndex < 0)
            currentDialogIndex = 0;

        int oldDialogIndex = currentDialogIndex;
        ImGui::Combo("Script ID", &currentDialogIndex, dialogLabels.data(), (int)dialogLabels.size());

        if (currentDialogIndex != oldDialogIndex || std::string(m_npcScriptId).empty())
            strncpy_s(m_npcScriptId, m_dialogDefs[currentDialogIndex].dialogId.c_str(), _TRUNCATE);
    }
    else
    {
        ImGui::InputText("Script ID", m_npcScriptId, IM_ARRAYSIZE(m_npcScriptId));
    }

    if (dialogIdExists(m_npcScriptId))
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Dialog OK");
    else
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Dialog nenalezen");

    ImGui::SliderInt("HP", &m_npcHP, 0, 100);
    ImGui::SliderInt("Mood", &m_npcMood, 0, 100);

    ImGui::Separator();

    if (ImGui::Button("New NPC", ImVec2(110, 0)))
    {
        m_selectedNpcSpawnIndex = -1;
        m_npcId[0] = '\0';
        m_npcName[0] = '\0';
        m_npcSurname[0] = '\0';
        m_npcGreeting[0] = '\0';
        m_npcScriptId[0] = '\0';
        m_lastIoStatus = "New NPC form ready.";
    }

    ImGui::SameLine();

    if (ImGui::Button("Update selected", ImVec2(120, 0)))
        saveBrushToSelectedNpc();

    ImGui::SameLine();

    if (ImGui::Button("Delete selected", ImVec2(120, 0)))
        deleteSelectedNpc();

    if (ImGui::Button("Open schedule"))
    {
        if (m_npcId[0] == '\0')
        {
            m_lastIoStatus = "Vyber nebo zadej NPC ID.";
        }
        else
        {
            EditorNpcSchedule* sched = findScheduleForNpc(m_npcId);
            if (!sched)
            {
                EditorNpcSchedule s;
                s.npcId = m_npcId;
                m_npcSchedules.push_back(std::move(s));
                m_selectedNpcScheduleIndex = (int)m_npcSchedules.size() - 1;
                m_lastIoStatus = "Schedule created for NPC.";
            }
            else
            {
                for (int i = 0; i < (int)m_npcSchedules.size(); ++i)
                {
                    if (m_npcSchedules[i].npcId == m_npcId)
                    {
                        m_selectedNpcScheduleIndex = i;
                        break;
                    }
                }
                m_lastIoStatus = "Schedule opened.";
            }
        }
    }
}