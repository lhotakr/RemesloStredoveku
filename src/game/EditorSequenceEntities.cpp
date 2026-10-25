#include "Editor.h"
#include "PathUtils.h"
#include "InventorySpriteCatalog.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace
{
using Json = nlohmann::json;

bool StringField(const char* label, Json& entity, const char* key, const std::string& fallback = {})
{
    char text[2048]{};
    const auto value = entity.value(key, fallback);
    std::strncpy(text, value.c_str(), sizeof(text) - 1);
    if (!ImGui::InputText(label, text, sizeof(text))) return false;
    entity[key] = text;
    return true;
}

bool NumberField(const char* label, Json& entity, const char* key, float fallback,
                 float speed, float low, float high)
{
    float value = entity.value(key, fallback);
    if (!ImGui::DragFloat(label, &value, speed, low, high, "%.2f", ImGuiSliderFlags_AlwaysClamp)) return false;
    entity[key] = value;
    return true;
}
}

bool Editor::renderSequenceCharacterPicker(Json& entity)
{
    const auto before = entity;
    std::string characterId = entity.value("character", entity.value("character_id", std::string()));
    const auto* character = m_characterManager.getCharacter(characterId);
    if (ImGui::BeginCombo("Character", characterId.empty() ? "Bez postavy (textura)" : characterId.c_str()))
    {
        ImGui::InputTextWithHint("##filter", "Hledat postavu...", m_sequenceCharacterFilter, sizeof(m_sequenceCharacterFilter));
        if (ImGui::Selectable("Bez postavy (textura)", characterId.empty()))
        {
            entity["character"] = "";
            entity.erase("character_id");
        }
        for (const auto& id : m_characterManager.characterIds())
        {
            const auto* definition = m_characterManager.getCharacter(id);
            const std::string label = id + (definition && !definition->name.empty() ? " / " + definition->name : "");
            if (label.find(m_sequenceCharacterFilter) == std::string::npos) continue;
            if (ImGui::Selectable(label.c_str(), id == characterId))
            {
                entity["character"] = id;
                entity.erase("character_id");
                if (definition && !definition->animations.contains(entity.value("animation", std::string("idle_down"))))
                    entity["animation"] = "idle_down";
            }
        }
        ImGui::EndCombo();
    }
    characterId = entity.value("character", entity.value("character_id", std::string()));
    character = m_characterManager.getCharacter(characterId);
    if (!characterId.empty() && !character)
        ImGui::TextWrapped("Neznámý Character: %s. Vyberte postavu ze seznamu.", characterId.c_str());
    if (character)
    {
        std::vector<std::string> animations;
        for (const auto& pair : character->animations) animations.push_back(pair.first);
        std::sort(animations.begin(), animations.end());
        const auto animation = entity.value("animation", std::string("idle_down"));
        if (ImGui::BeginCombo("Animace", animation.c_str()))
        {
            for (const auto& id : animations)
                if (ImGui::Selectable(id.c_str(), id == animation)) entity["animation"] = id;
            ImGui::EndCombo();
        }
        auto found = character->animations.find(entity.value("animation", std::string("idle_down")));
        if (found == character->animations.end()) found = character->animations.find("idle_down");
        auto* texture = m_characterManager.getTextureForCharacter(characterId);
        if (texture && character->sheetW > 0 && character->sheetH > 0 &&
            found != character->animations.end() && !found->second.frames.empty())
        {
            const auto& frame = found->second.frames.front();
            if (frame.h > 0)
                ImGui::Image((ImTextureID)(intptr_t)texture, ImVec2(80.0f * frame.w / frame.h, 80.0f),
                    ImVec2(float(frame.x) / character->sheetW, float(frame.y) / character->sheetH),
                    ImVec2(float(frame.x + frame.w) / character->sheetW, float(frame.y + frame.h) / character->sheetH));
        }
    }
    return entity != before;
}

bool Editor::renderSequenceTexturePicker(Json& item, const char* key, bool pathField)
{
    const auto before = item;
    if (pathField) StringField("Cesta obrázku", item, key);
    if (std::string(key) == "texture")
    {
        static std::vector<inventorysprites::Choice> choices;
        static char filter[128]{};
        if (ImGui::Button("Vybrat předmět z inventáře"))
        {
            choices = inventorysprites::Load(pathutils::ProjectRoot());
            filter[0] = '\0';
            ImGui::OpenPopup("InventorySprite");
        }
        if (ImGui::BeginPopup("InventorySprite"))
        {
            ImGui::InputTextWithHint("##item_search", "Hledat název nebo ID (např. motyka)...", filter, sizeof(filter));
            ImGui::TextDisabled("Sprite předmětu jako dekorace scény");
            ImGui::BeginChild("InventoryChoices", ImVec2(440, 260), true);
            ImGuiTextFilter search(filter);
            for (const auto& choice : choices)
            {
                const auto label = choice.name + "  |  " + choice.id;
                const auto keywords = label + " " + choice.spriteId + " " + choice.path;
                if (!search.PassFilter(keywords.c_str())) continue;
                if (ImGui::Selectable(label.c_str(), item.value(key, std::string()) == choice.path))
                {
                    item[key] = choice.path;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", choice.path.c_str());
            }
            if (choices.empty()) ImGui::TextWrapped("Nebyl nalezen žádný předmět s obrázkem v katalogu inventáře.");
            ImGui::EndChild();
            ImGui::EndPopup();
        }
    }
    if (ImGui::Button("Vybrat texturu z projektu"))
    {
        m_sequenceEntityTexturePaths.clear();
        std::error_code ec;
        const auto assets = pathutils::AssetsDir();
        for (std::filesystem::recursive_directory_iterator it(assets, ec), end; it != end && !ec; it.increment(ec))
        {
            if (!it->is_regular_file(ec)) continue;
            auto extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
            if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".bmp" && extension != ".webp") continue;
            m_sequenceEntityTexturePaths.push_back(it->path().lexically_relative(pathutils::ProjectRoot()).generic_string());
        }
        std::sort(m_sequenceEntityTexturePaths.begin(), m_sequenceEntityTexturePaths.end());
        m_sequenceEntityTextureFilter[0] = '\0';
        ImGui::OpenPopup("SceneTexture");
    }
    if (ImGui::BeginPopup("SceneTexture"))
    {
        ImGui::InputTextWithHint("##search", "Hledat texturu...", m_sequenceEntityTextureFilter, sizeof(m_sequenceEntityTextureFilter));
        ImGui::BeginChild("TextureList", ImVec2(360, 220), true);
        for (const auto& path : m_sequenceEntityTexturePaths)
        {
            if (path.find(m_sequenceEntityTextureFilter) == std::string::npos) continue;
            if (ImGui::Selectable(path.c_str(), path == item.value(key, std::string())))
            {
                item[key] = path;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    return item != before;
}

void Editor::renderSequenceEntityBrowser()
{
    if (!m_sequenceSceneDocument.is_object()) return;
    if (isSequenceImageScene())
    {
        if (ImGui::Button("+ Objekt")) addSequenceImageLayer("image");
        ImGui::SameLine();
        if (ImGui::Button("+ Text")) addSequenceImageLayer("text");
        if (ImGui::Button("+ Postava")) addSequenceImageLayer("character");
    }
    if (!isSequenceImageScene() && ImGui::Button("+ Dekorace na podlahu"))
    { m_sequenceMarkerTool = 3; m_sequencePickMarkerMode = false; stopSequencePreview(); }
    for (bool prop : {false, true})
    {
        const char* key = prop ? "props" : "actors";
        if (!ImGui::CollapsingHeader(prop ? "Rekvizity scény" : "Postavy scény", ImGuiTreeNodeFlags_DefaultOpen)) continue;
        const auto found = m_sequenceSceneDocument.find(key);
        if (found == m_sequenceSceneDocument.end() || !found->is_array() || found->empty())
        {
            ImGui::TextDisabled("Žádné položky.");
            continue;
        }
        ImGui::PushID(key);
        for (int i = 0; i < static_cast<int>(found->size()); ++i)
        {
            const auto& entity = (*found)[i];
            if (!entity.is_object()) continue;
            ImGui::PushID(i);
            const auto kind = prop ? SequenceEditorSelectionKind::Prop : SequenceEditorSelectionKind::Actor;
            if (ImGui::Selectable(entity.value("id", std::string("Bez ID")).c_str(),
                m_sequenceSelectionKind == kind && m_selectedSequenceEntityIndex == i))
            {
                m_selectedSequenceEntityIndex = i;
                m_sequenceSelectionKind = kind;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

void Editor::renderSequenceEntityInspector(bool prop, int index)
{
    if (isSequenceImageScene()) { renderSequenceImageLayerInspector(prop, index); return; }
    const char* key = prop ? "props" : "actors";
    if (!m_sequenceSceneDocument.is_object()) return;
    auto found = m_sequenceSceneDocument.find(key);
    if (found == m_sequenceSceneDocument.end() || !found->is_array() || index < 0 || index >= static_cast<int>(found->size())) return;
    const auto& original = (*found)[index];
    if (!original.is_object()) return;
    Json edited = original;
    ImGui::PushID(key);
    ImGui::PushID(index);
    ImGui::Text("%s: %s", prop ? "Rekvizita" : "Postava", edited.value("id", std::string()).c_str());
    if (!prop) renderSequenceCharacterPicker(edited);

    const auto oldTexture = edited.value("texture", edited.value("sprite", edited.value("object", std::string())));
    if (StringField("Textura", edited, "texture", oldTexture))
    {
        edited.erase("sprite");
        edited.erase("object");
    }
    if (renderSequenceTexturePicker(edited, "texture", false))
    {
        edited.erase("sprite"); edited.erase("object");
    }
    if (prop && oldTexture.empty()) ImGui::TextWrapped("Vyberte obrázek rekvizity.");

    std::string markerId = edited.value("marker", std::string());
    if (ImGui::BeginCombo("Bod umístění", markerId.empty() ? "Bez bodu (X/Y)" : markerId.c_str()))
    {
        if (ImGui::Selectable("Bez bodu (X/Y)", markerId.empty()))
        {
            if (const auto* marker = sequence::FindMarker(m_sequenceSceneMarkers, markerId))
            {
                edited["x"] = marker->x; edited["y"] = marker->y;
                edited["z_offset"] = edited.value("z_offset", 0.0f) + marker->z;
            }
            edited["marker"] = "";
            edited.erase("position");
        }
        for (const auto& marker : m_sequenceSceneMarkers)
            if (ImGui::Selectable(marker.id.c_str(), marker.id == markerId)) edited["marker"] = marker.id;
        ImGui::EndCombo();
    }
    markerId = edited.value("marker", std::string());
    const auto* marker = sequence::FindMarker(m_sequenceSceneMarkers, markerId);
    if (!markerId.empty() && !marker)
        ImGui::TextColored(ImVec4(1, 0.55f, 0.3f, 1), "Chybí bod: %s", markerId.c_str());
    if (marker && ImGui::Button("Upravit bod umístění"))
    {
        m_selectedSequenceMarkerIndex = static_cast<int>(marker - m_sequenceSceneMarkers.data());
        m_sequenceSelectionKind = SequenceEditorSelectionKind::Marker;
    }
    if (markerId.empty())
    {
        const auto position = edited.value("position", Json::array());
        const float x = position.is_array() && position.size() >= 2 ? position[0].get<float>() : 2.5f;
        const float y = position.is_array() && position.size() >= 2 ? position[1].get<float>() : 2.5f;
        bool positionChanged = NumberField("X", edited, "x", x, 0.02f, -8192, 8192);
        positionChanged |= NumberField("Y", edited, "y", y, 0.02f, -8192, 8192);
        if (positionChanged)
        {
            // The runtime gives position[] precedence over x/y.
            if (position.is_array() && position.size() >= 2)
            {
                if (!edited.contains("x")) edited["x"] = x;
                if (!edited.contains("y")) edited["y"] = y;
                edited.erase("position");
            }
        }
    }
    NumberField("Měřítko", edited, "scale", prop ? 0.85f : 0.80f, 0.01f, 0.1f, 6.0f);
    NumberField("Výška nad bodem", edited, "z_offset", 0, 0.02f, -128, 128);
    NumberField("Natočení", edited, "yaw_degrees", edited.value("rotation", 0.0f), 0.5f, -360, 360);
    bool visible = edited.value("visible", true);
    if (ImGui::Checkbox("Viditelný na začátku", &visible)) edited["visible"] = visible;
    const auto mode = edited.value("render_mode", edited.value("voxel", false) ? std::string("voxel") : std::string("billboard"));
    int renderMode = mode == "floor" ? 2 : (mode == "voxel" ? 1 : 0);
    const char* modes[] = {"Billboard", "Voxel 3D", "Na podlaze"};
    if (ImGui::Combo("Vykreslení", &renderMode, modes, 3))
    {
        edited["render_mode"] = renderMode == 2 ? "floor" : (renderMode == 1 ? "voxel" : "billboard");
        edited["voxel"] = renderMode == 1;
    }
    if (renderMode == 2)
    {
        NumberField("Šířka (0 = auto)", edited, "floor_width", 0, 0.05f, 0, 128);
        NumberField("Délka (0 = auto)", edited, "floor_depth", 0, 0.05f, 0, 128);
        ImGui::TextWrapped("Obrázek leží naplocho na podlaze. Rozměr 0 se odvodí z měřítka a poměru stran obrázku. Průhlednost PNG zůstává zachovaná.");
    }
    if (edited != original)
    {
        markSequenceDirty("edit scene entity");
        (*found)[index] = edited;
        stopSequencePreview();
        if (m_sequencePreviewSceneLoaded)
            m_sequencePreviewScene.setEditorEntity(edited, prop, [&](const auto& message) { m_lastIoStatus = message; });
    }
    if (ImGui::Button("Smazat objekt")) deleteSequenceEntity(prop, index);
    ImGui::PopID();
    ImGui::PopID();
}

void Editor::renderSequenceMarkerEntities(const std::string& markerId)
{
    if (!m_sequenceSceneDocument.is_object()) return;
    ImGui::Separator();
    ImGui::TextUnformatted("Objekty na tomto bodě");
    bool any = false;
    for (bool prop : {false, true})
    {
        const auto found = m_sequenceSceneDocument.find(prop ? "props" : "actors");
        if (found == m_sequenceSceneDocument.end() || !found->is_array()) continue;
        for (int i = 0; i < static_cast<int>(found->size()); ++i)
        {
            const auto& entity = (*found)[i];
            if (!entity.is_object() || entity.value("marker", std::string()) != markerId) continue;
            any = true;
            renderSequenceEntityInspector(prop, i);
        }
    }
    if (!any) ImGui::TextWrapped("K bodu není přiřazená postava ani rekvizita. Přiřazení změníte v seznamu Postavy scény / Rekvizity scény.");
}
