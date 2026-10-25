#include "Editor.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cctype>

namespace
{
using Json = nlohmann::json;
bool TextField(const char* label, Json& item, const char* key, bool multiline = false)
{
    char buffer[8192]{};
    const auto text = item.value(key, std::string());
    std::strncpy(buffer, text.c_str(), sizeof(buffer) - 1);
    const bool changed = multiline ? ImGui::InputTextMultiline(label, buffer, sizeof(buffer), ImVec2(-1, 100))
                                   : ImGui::InputText(label, buffer, sizeof(buffer));
    if (changed) item[key] = buffer;
    return changed;
}
void Number(const char* label, Json& item, const char* key, float fallback, float low, float high, float step = 1)
{
    float value = item.value(key, fallback);
    if (ImGui::DragFloat(label, &value, step, low, high, "%.1f", ImGuiSliderFlags_AlwaysClamp)) item[key] = value;
}
bool HasEntityId(const Json& doc, const std::string& id)
{
    for (const auto* key : {"actors", "props"})
        if (doc.contains(key) && doc[key].is_array())
            for (const auto& item : doc[key])
                if (item.is_object() && item.value("id", std::string()) == id) return true;
    return false;
}
std::string NewEntityId(const Json& doc, const std::string& prefix)
{
    int suffix = 1;
    while (HasEntityId(doc, prefix + std::to_string(suffix))) ++suffix;
    return prefix + std::to_string(suffix);
}
}

bool Editor::isSequenceImageScene() const
{
    return m_sequenceSceneDocument.is_object() && m_sequenceSceneDocument.value("type", std::string()) == "image_2d";
}

std::unordered_map<std::string, Json> Editor::sequenceSceneDrafts() const
{
    auto drafts = m_sequenceSceneDrafts;
    if (!m_sequenceEditingSceneId.empty() && m_sequenceSceneDocument.is_object())
        drafts[m_sequenceEditingSceneId] = sequencePreviewSceneDocument();
    return drafts;
}

void Editor::renderSequenceSceneSelector()
{
    const auto selected = selectedSequenceSceneId();
    ImGui::SetNextItemWidth(std::max(120.f, ImGui::GetContentRegionAvail().x - 230));
    if (ImGui::BeginCombo("Scéna", selected.empty() ? "Vyberte scénu" : selected.c_str()))
    {
        std::vector<std::string> ids;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(pathutils::AssetsDir() / "sequences/scenes", ec), end; it != end && !ec; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().extension() == ".json") ids.push_back(it->path().stem().string());
        std::sort(ids.begin(), ids.end());
        for (const auto& id : ids)
            if (ImGui::Selectable(id.c_str(), selected == id) && selected != id)
            {
                stopSequencePreview();
                m_sequenceMarkerTool = 0; m_sequencePickMarkerMode = false;
                if (loadSequenceSceneMetadata(id)) refreshSequenceScenePreview();
                m_sequenceSelectionKind = SequenceEditorSelectionKind::Scene;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ 2D scéna")) ImGui::OpenPopup("NewImageScene");
    if (ImGui::BeginPopup("NewImageScene"))
    {
        ImGui::TextUnformatted("Nová obrázková scéna");
        ImGui::InputText("ID", m_sequenceNewSceneId, sizeof(m_sequenceNewSceneId));
        renderSequenceTexturePicker(m_sequenceNewSceneBackground, "background_image");
        const std::string id = m_sequenceNewSceneId;
        const bool valid = !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
        const auto path = pathutils::AssetsDir() / "sequences/scenes" / (id + ".json");
        const bool exists = std::filesystem::exists(path);
        if (exists) ImGui::TextUnformatted("Toto ID už existuje. Zvolte jiné.");
        ImGui::BeginDisabled(!valid || exists);
        if (ImGui::Button("Vytvořit"))
        {
            Json document = {{"id", id}, {"type", "image_2d"},
                {"background_image", m_sequenceNewSceneBackground.value("background_image", std::string())},
                {"markers", Json::array()}, {"actors", Json::array()}, {"props", Json::array()}};
            std::error_code ec; std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream file(path);
            if (!ec && file && (file << document.dump(2) << '\n'))
            {
                file.close();
                stopSequencePreview();
                if (loadSequenceSceneMetadata(id)) refreshSequenceScenePreview();
                m_sequenceSelectionKind = SequenceEditorSelectionKind::Scene;
                ImGui::CloseCurrentPopup();
            }
            else m_lastIoStatus = "Nelze uložit scénu: " + path.string();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

void Editor::renderSequenceImageTools()
{
    if (ImGui::Button("Pozadí / plátno")) m_sequenceSelectionKind = SequenceEditorSelectionKind::Scene;
    ImGui::SameLine();
    if (ImGui::Button("+ Objekt")) addSequenceImageLayer("image");
    ImGui::SameLine();
    if (ImGui::Button("+ Text")) addSequenceImageLayer("text");
    ImGui::SameLine();
    if (ImGui::Button("+ Bod")) { stopSequencePreview(); m_sequenceMarkerTool = 1; }
    ImGui::TextDisabled(m_sequenceMarkerTool ? "Kliknutím do obrázku vložíte bod. Esc zruší vkládání." : "Pozice a rozměry v pixelech. Vrstvy posunete myší.");
}

void Editor::renderSequenceImageSceneInspector()
{
    if (!isSequenceImageScene()) { ImGui::TextUnformatted("Vyberte 2D scénu v náhledu."); return; }
    ImGui::Text("2D scéna: %s", selectedSequenceSceneId().c_str());
    Json edited = m_sequenceSceneDocument;
    renderSequenceTexturePicker(edited, "background_image");
    Json canvas = edited.value("canvas", Json::object());
    const auto size = m_sequencePreviewScene.imageCanvasSize();
    Number("Šířka plátna", canvas, "width", size.x, 1, 32768);
    Number("Výška plátna", canvas, "height", size.y, 1, 32768);
    if (canvas != edited.value("canvas", Json::object())) edited["canvas"] = canvas;
    ImGui::TextWrapped("Bez vlastního rozměru se plátno přizpůsobí obrázku. X/Y označuje levý horní roh vrstvy.");
    if (edited != m_sequenceSceneDocument)
    {
        markSequenceDirty("edit image scene");
        m_sequenceSceneDocument = edited;
        refreshSequenceScenePreview();
    }
    const bool hasNode = m_selectedSequenceIndex >= 0 && m_selectedSequenceIndex < int(m_sequences.size()) &&
        m_selectedSequenceNodeIndex >= 0 && m_selectedSequenceNodeIndex < int(m_sequences[m_selectedSequenceIndex].nodes.size());
    ImGui::BeginDisabled(!hasNode);
    if (ImGui::Button("Vložit load_scene na začátek uzlu"))
    {
        markSequenceDirty("insert image scene action");
        auto& node = m_sequences[m_selectedSequenceIndex].nodes[m_selectedSequenceNodeIndex];
        sequence::ActionDef action;
        action.type = "load_scene";
        action.data = {{"scene", selectedSequenceSceneId()}};
        node.actions.insert(node.actions.begin(), std::move(action));
        m_lastIoStatus = "Scéna vložena na začátek uzlu " + node.id;
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Délku scény určují akce wait, show_text nebo další load_scene v sekvenci.");
}

void Editor::addSequenceImageLayer(const char* type)
{
    if (!isSequenceImageScene()) return;
    const bool actor = std::string(type) == "character";
    const bool text = std::string(type) == "text";
    const char* key = actor ? "actors" : "props";
    const auto size = m_sequencePreviewScene.imageCanvasSize();
    Json entity = {{"id", NewEntityId(m_sequenceSceneDocument, actor ? "actor_" : (text ? "text_" : "object_"))},
        {"x", size.x * .25f}, {"y", size.y * .25f}, {"scale", 1}, {"visible", true}};
    int layer = 0;
    for (const auto* collection : {"actors", "props"})
        if (m_sequenceSceneDocument.contains(collection))
            for (const auto& item : m_sequenceSceneDocument[collection]) layer = std::max(layer, item.value("layer", 0) + 1);
    entity["layer"] = layer;
    if (text) { entity["type"] = "text"; entity["text"] = "Nový text"; entity["font_size"] = 40; entity["width"] = size.x * .5f; }
    else if (actor)
    {
        const auto ids = m_characterManager.characterIds();
        if (!ids.empty()) entity["character"] = ids.front();
        entity["height"] = 180;
    }
    else { entity["texture"] = "assets/modern/Car.png"; entity["width"] = 240; }
    markSequenceDirty("add image scene layer");
    auto& list = m_sequenceSceneDocument[key];
    if (!list.is_array()) list = Json::array();
    list.push_back(entity);
    m_selectedSequenceEntityIndex = int(list.size()) - 1;
    m_sequenceSelectionKind = actor ? SequenceEditorSelectionKind::Actor : SequenceEditorSelectionKind::Prop;
    stopSequencePreview();
    if (m_sequencePreviewSceneLoaded) m_sequencePreviewScene.setEditorEntity(entity, !actor, [&](const auto& error) { m_lastIoStatus = error; });
}

void Editor::deleteSequenceEntity(bool prop, int index)
{
    const char* key = prop ? "props" : "actors";
    if (!m_sequenceSceneDocument.is_object() || !m_sequenceSceneDocument.contains(key)) return;
    auto& list = m_sequenceSceneDocument[key];
    if (!list.is_array() || index < 0 || index >= int(list.size())) return;
    markSequenceDirty("delete scene entity");
    stopSequencePreview();
    m_sequencePreviewScene.removeActorOrProp(list[index].value("id", std::string()));
    list.erase(list.begin() + index);
    m_selectedSequenceEntityIndex = -1;
    m_sequenceSelectionKind = SequenceEditorSelectionKind::Scene;
}

void Editor::renderSequenceImageLayerInspector(bool prop, int index)
{
    const char* key = prop ? "props" : "actors";
    auto found = m_sequenceSceneDocument.find(key);
    if (found == m_sequenceSceneDocument.end() || !found->is_array() || index < 0 || index >= int(found->size())) return;
    const Json original = (*found)[index];
    Json edited = original;
    ImGui::PushID(key); ImGui::PushID(index);
    const auto id = edited.value("id", std::string());
    ImGui::Text("Vrstva: %s", id.c_str());
    const bool text = edited.value("type", std::string()) == "text";
    if (text)
    {
        TextField("Text", edited, "text", true);
        Number("Velikost písma", edited, "font_size", 32, 1, 512);
        auto color = edited.value("color", Json::array({255,255,255,255}));
        float rgba[4] = {1,1,1,1};
        for (int c = 0; c < 4 && c < int(color.size()); ++c) rgba[c] = color[c].get<float>() / 255;
        if (ImGui::ColorEdit4("Barva", rgba))
            edited["color"] = {int(rgba[0]*255), int(rgba[1]*255), int(rgba[2]*255), int(rgba[3]*255)};
    }
    else
    {
        if (!prop) renderSequenceCharacterPicker(edited);
        renderSequenceTexturePicker(edited, "texture");
    }
    const auto marker = edited.value("marker", std::string());
    if (ImGui::BeginCombo("Bod umístění", marker.empty() ? "Bez bodu (X/Y)" : marker.c_str()))
    {
        if (ImGui::Selectable("Bez bodu (X/Y)", marker.empty()) && !marker.empty())
        {
            sequence::Vec2 position;
            if (m_sequencePreviewScene.getActorPosition(id, position)) { edited["x"] = position.x; edited["y"] = position.y; }
            edited.erase("marker"); edited.erase("position");
        }
        for (const auto& point : m_sequenceSceneMarkers)
            if (ImGui::Selectable(point.id.c_str(), point.id == marker)) edited["marker"] = point.id;
        ImGui::EndCombo();
    }
    if (edited.value("marker", std::string()).empty())
    {
        const auto position = edited.value("position", Json::array({0,0}));
        const float x = position.is_array() && position.size() >= 2 ? position[0].get<float>() : 0;
        const float y = position.is_array() && position.size() >= 2 ? position[1].get<float>() : 0;
        const auto beforePosition = edited;
        Number("X", edited, "x", x, -32768, 32768);
        Number("Y", edited, "y", y, -32768, 32768);
        if (edited != beforePosition && edited.contains("position"))
        {
            if (!edited.contains("x")) edited["x"] = x;
            if (!edited.contains("y")) edited["y"] = y;
            edited.erase("position");
        }
    }
    Number(text ? "Šířka textu (0 = bez zalomení)" : "Šířka (0 = automaticky)", edited, "width", 0, 0, 32768);
    if (!text) Number("Výška (0 = automaticky)", edited, "height", 0, 0, 32768);
    Number("Měřítko", edited, "scale", 1, .01f, 100, .01f);
    if (!text) Number("Natočení", edited, "yaw_degrees", edited.value("rotation", 0.f), -360, 360);
    int layer = edited.value("layer", 0);
    if (ImGui::InputInt("Pořadí vrstvy", &layer)) edited["layer"] = std::clamp(layer, -100000, 100000);
    ImGui::TextDisabled("Vyšší číslo = blíž divákovi.");
    Number("Neprůhlednost", edited, "opacity", 1, 0, 1, .01f);
    bool visible = edited.value("visible", true);
    if (ImGui::Checkbox("Viditelný na začátku", &visible)) edited["visible"] = visible;
    if (edited != original)
    {
        markSequenceDirty("edit image scene layer");
        (*found)[index] = edited;
        stopSequencePreview();
        if (m_sequencePreviewSceneLoaded) m_sequencePreviewScene.setEditorEntity(edited, prop, [&](const auto& error) { m_lastIoStatus = error; });
    }
    if (ImGui::Button("Duplikovat"))
    {
        markSequenceDirty("duplicate image scene layer");
        auto copy = edited;
        copy["id"] = NewEntityId(m_sequenceSceneDocument, id + "_copy_");
        copy.erase("marker"); copy.erase("position");
        sequence::Vec2 pos; m_sequencePreviewScene.getActorPosition(id, pos);
        copy["x"] = pos.x + 24; copy["y"] = pos.y + 24; copy["layer"] = layer + 1;
        found->push_back(copy);
        m_selectedSequenceEntityIndex = int(found->size()) - 1;
        m_sequenceSelectionKind = prop ? SequenceEditorSelectionKind::Prop : SequenceEditorSelectionKind::Actor;
        stopSequencePreview();
        m_sequencePreviewScene.setEditorEntity(copy, prop);
    }
    ImGui::SameLine();
    if (ImGui::Button("Smazat")) deleteSequenceEntity(prop, index);
    ImGui::PopID(); ImGui::PopID();
}

void Editor::renderSequenceImageViewport(ImVec2 pos, ImVec2 size)
{
    ImGui::InvisibleButton("ImageSceneCanvas", size);
    if (!m_sequencePreviewSceneLoaded) return;
    const auto fit = m_sequencePreviewScene.imageViewport(size.x, size.y);
    const float zoom = fit.w / m_sequencePreviewScene.imageCanvasSize().x;
    const ImVec2 origin{pos.x + fit.x, pos.y + fit.y};
    const auto mouse = ImGui::GetIO().MousePos;
    const ImVec2 point{(mouse.x - origin.x) / zoom, (mouse.y - origin.y) / zoom};
    const bool inside = mouse.x >= origin.x && mouse.y >= origin.y && mouse.x < origin.x + fit.w && mouse.y < origin.y + fit.h;
    if (ImGui::IsItemHovered() && inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (m_sequenceMarkerTool || m_sequencePickMarkerMode)
        {
            createSequenceMarker({point.x, point.y, 0}, false);
            return;
        }
        bool picked = false;
        for (int i = int(m_sequenceSceneMarkers.size()) - 1; i >= 0; --i)
        {
            const auto& marker = m_sequenceSceneMarkers[i];
            const float dx = (point.x - marker.x) * zoom, dy = (point.y - marker.y) * zoom;
            if (dx*dx + dy*dy > 81) continue;
            m_selectedSequenceMarkerIndex = i;
            m_sequenceSelectionKind = SequenceEditorSelectionKind::Marker;
            m_sequenceDraggingMarker = i; m_sequenceMarkerDragChanged = false;
            picked = true;
            break;
        }
        auto order = m_sequencePreviewScene.imageLayerOrder();
        for (auto it = order.rbegin(); it != order.rend() && !picked; ++it)
        {
            for (bool prop : {true, false})
            {
                const char* key = prop ? "props" : "actors";
                if (!m_sequenceSceneDocument.contains(key)) continue;
                auto& list = m_sequenceSceneDocument[key];
                for (int i = 0; i < int(list.size()); ++i)
                {
                    const auto& item = list[i];
                    if (item.value("id", std::string()) != *it || !item.value("visible", true) || item.value("opacity", 1.f) <= 0) continue;
                    SDL_FRect bounds;
                    if (!m_sequencePreviewScene.imageEntityBounds(*it, bounds)) continue;
                    // Inverse rotation makes picking follow rotated image layers.
                    const double angle = -item.value("yaw_degrees", item.value("rotation", 0.0)) * 3.14159265358979323846 / 180;
                    const float cx = bounds.x + bounds.w*.5f, cy = bounds.y + bounds.h*.5f;
                    const float px = float((point.x-cx)*std::cos(angle) - (point.y-cy)*std::sin(angle)) + cx;
                    const float py = float((point.x-cx)*std::sin(angle) + (point.y-cy)*std::cos(angle)) + cy;
                    if (px < bounds.x || py < bounds.y || px > bounds.x + bounds.w || py > bounds.y + bounds.h) continue;
                    m_sequenceSelectionKind = prop ? SequenceEditorSelectionKind::Prop : SequenceEditorSelectionKind::Actor;
                    m_selectedSequenceEntityIndex = i;
                    m_sequenceDraggingImageLayer = i; m_sequenceImageDragProp = prop; m_sequenceImageDragUndo = false;
                    m_sequenceImageDragOffset = {point.x - bounds.x, point.y - bounds.y};
                    picked = true;
                    break;
                }
            }
        }
        if (!picked) m_sequenceSelectionKind = SequenceEditorSelectionKind::Scene;
    }
    if (m_sequenceDraggingMarker >= 0)
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_sequenceDraggingMarker >= int(m_sequenceSceneMarkers.size())) m_sequenceDraggingMarker = -1;
        else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            if (!m_sequenceMarkerDragChanged) { markSequenceDirty("move image scene marker"); m_sequenceMarkerDragChanged = true; }
            auto& marker = m_sequenceSceneMarkers[m_sequenceDraggingMarker];
            marker.x = point.x; marker.y = point.y;
            syncSequenceMarkerScene();
        }
    }
    auto* canvasDraw = ImGui::GetWindowDrawList();
    canvasDraw->PushClipRect(origin, {origin.x + fit.w, origin.y + fit.h}, true);
    for (int i = 0; i < int(m_sequenceSceneMarkers.size()); ++i)
    {
        const auto& marker = m_sequenceSceneMarkers[i];
        const ImVec2 screen{origin.x + marker.x * zoom, origin.y + marker.y * zoom};
        canvasDraw->AddCircleFilled(screen, 5, i == m_selectedSequenceMarkerIndex ? IM_COL32(255,210,90,255) : IM_COL32(80,220,255,255));
        canvasDraw->AddText({screen.x + 8, screen.y - 6}, IM_COL32(255,255,255,255), marker.id.c_str());
    }
    canvasDraw->PopClipRect();
    if (m_sequenceDraggingImageLayer >= 0)
    {
        const char* key = m_sequenceImageDragProp ? "props" : "actors";
        auto& list = m_sequenceSceneDocument[key];
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_sequenceDraggingImageLayer >= int(list.size())) m_sequenceDraggingImageLayer = -1;
        else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            if (!m_sequenceImageDragUndo) { markSequenceDirty("move image scene layer"); m_sequenceImageDragUndo = true; }
            auto& item = list[m_sequenceDraggingImageLayer];
            item["x"] = point.x - m_sequenceImageDragOffset.x; item["y"] = point.y - m_sequenceImageDragOffset.y;
            item.erase("marker"); item.erase("position");
            m_sequencePreviewScene.setActorPosition(item.value("id", std::string()), {item["x"].get<float>(), item["y"].get<float>()});
        }
    }
    if (m_sequenceSelectionKind != SequenceEditorSelectionKind::Actor && m_sequenceSelectionKind != SequenceEditorSelectionKind::Prop) return;
    const auto found = m_sequenceSceneDocument.find(m_sequenceSelectionKind == SequenceEditorSelectionKind::Prop ? "props" : "actors");
    if (found == m_sequenceSceneDocument.end() || m_selectedSequenceEntityIndex < 0 || m_selectedSequenceEntityIndex >= int(found->size())) return;
    SDL_FRect bounds;
    if (m_sequencePreviewScene.imageEntityBounds((*found)[m_selectedSequenceEntityIndex].value("id", std::string()), bounds))
    {
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(pos, {pos.x + size.x, pos.y + size.y}, true);
        draw->AddRect({origin.x + bounds.x * zoom, origin.y + bounds.y * zoom},
            {origin.x + (bounds.x + bounds.w)*zoom, origin.y + (bounds.y + bounds.h)*zoom}, IM_COL32(255,210,90,255), 0, 0, 2);
        draw->PopClipRect();
    }
}
