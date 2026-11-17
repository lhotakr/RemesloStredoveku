#include "Editor.h"
#include "game/sequence/SequenceTimeline.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr double Pi = 3.14159265358979323846;
    using Point = BuildInteriorEngine::ScenePoint;

    void CameraIcon(ImDrawList* draw, ImVec2 p, ImU32 color)
    {
        draw->AddRectFilled(ImVec2(p.x - 8, p.y - 6), ImVec2(p.x + 4, p.y + 5), IM_COL32(25, 30, 38, 230), 2);
        draw->AddRect(ImVec2(p.x - 8, p.y - 6), ImVec2(p.x + 4, p.y + 5), color, 2, 0, 2);
        draw->AddQuadFilled(ImVec2(p.x + 4, p.y - 2), ImVec2(p.x + 10, p.y - 6),
            ImVec2(p.x + 10, p.y + 5), ImVec2(p.x + 4, p.y + 2), color);
        draw->AddLine(ImVec2(p.x - 3, p.y + 5), ImVec2(p.x - 7, p.y + 10), color, 1.5f);
        draw->AddLine(ImVec2(p.x - 3, p.y + 5), ImVec2(p.x + 1, p.y + 10), color, 1.5f);
    }


}

nlohmann::json Editor::sequencePreviewSceneDocument() const
{
    if (!m_sequenceSceneDocument.is_object()) return nullptr;
    auto document = m_sequenceSceneDocument;
    document["markers"] = nlohmann::json::array();
    for (const auto& marker : m_sequenceSceneMarkers) document["markers"].push_back(sequence::MarkerToJson(marker));
    return document;
}

void Editor::syncSequenceMarkerScene(bool reload)
{
    if (!m_sequencePreviewSceneLoaded) return;
    if (reload)
    {
        stopSequencePreview();
        const auto pose = m_sequencePreviewScene.cameraPose();
        const auto document = sequencePreviewSceneDocument();
        m_sequencePreviewSceneLoaded = m_sequencePreviewScene.loadScene(selectedSequenceSceneId(),
            [&](const std::string& message) { m_lastIoStatus = message; }, &document);
        m_sequencePreviewScene.setCameraPose(pose);
    }
    else m_sequencePreviewScene.setEditorMarkers(m_sequenceSceneMarkers);
}

void Editor::assignSequenceMarkerToAction(const std::string& id, bool recordUndo)
{
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= static_cast<int>(m_sequences.size())) return;
    if (m_sequencePickMarkerMode && (m_sequencePickNodeIndex != m_selectedSequenceNodeIndex ||
        m_sequencePickSequenceIndex != m_selectedSequenceIndex)) { m_sequencePickMarkerMode = false; return; }
    auto& seq = m_sequences[m_selectedSequenceIndex];
    if (m_selectedSequenceNodeIndex < 0 || m_selectedSequenceNodeIndex >= static_cast<int>(seq.nodes.size())) return;
    auto path = m_sequencePickMarkerMode ? m_sequencePickActionPath : m_sequenceTimelineSelection;
    if (!m_sequencePickMarkerMode && (m_sequenceTimelineSequence != m_selectedSequenceIndex ||
        m_sequenceTimelineNode != m_selectedSequenceNodeIndex || path.empty() ||
        path.front() != m_selectedSequenceActionIndex)) path = {m_selectedSequenceActionIndex};
    auto* action = sequence::FindTimelineAction(seq.nodes[m_selectedSequenceNodeIndex], path);
    if (!action) return;
    const bool target = action->type == "move_actor" || action->type == "camera_move";
    if (!target && action->type != "set_camera" && action->type != "spawn_actor" && action->type != "teleport_player") return;
    if (recordUndo) markSequenceDirty("assign sequence marker");
    action->data[target ? "target_marker" : "marker"] = id;
    if (action->type == "camera_move") action->data.erase("target_actor");
    m_sequencePickMarkerMode = false;
    m_sequenceMarkerTool = 0;
}

void Editor::createSequenceMarker(Point point, bool camera)
{
    markSequenceDirty(camera ? "add camera marker" : "add spatial marker");
    const auto& environment = m_sequencePreviewScene.environment();
    sequence::SequenceMarker marker;
    marker.id = sequence::UniqueMarkerId(m_sequenceSceneMarkers,
        m_sequencePickMarkerMode && m_sequenceMarkerId[0] ? m_sequenceMarkerId : (camera ? "camera" : "marker"));
    marker.x = static_cast<float>(point.x);
    marker.y = static_cast<float>(point.y);
    marker.z = isSequenceImageScene() ? 0.0f : static_cast<float>(std::max(0.0, point.z - environment.sceneFloorHeight(point.x, point.y)));
    if (m_sequenceMarkerSnap)
    {
        marker.x = std::round(marker.x * 4) / 4;
        marker.y = std::round(marker.y * 4) / 4;
        marker.z = std::round(marker.z * 4) / 4;
    }
    if (camera)
    {
        const auto pose = m_sequencePreviewScene.cameraPose();
        marker.type = "camera";
        if (m_sequenceMarkerFloorPlacement || marker.z < 0.05f) marker.z = std::max(0.05f, pose.height);
        marker.angleDegrees = pose.angleDegrees;
        marker.pitch = pose.pitch;
    }
    m_sequenceSceneMarkers.push_back(marker);
    m_selectedSequenceMarkerIndex = static_cast<int>(m_sequenceSceneMarkers.size()) - 1;
    m_sequenceSelectionKind = SequenceEditorSelectionKind::Marker;
    if (m_sequencePickMarkerMode) assignSequenceMarkerToAction(marker.id, false);
    m_sequenceMarkerTool = 0;
    syncSequenceMarkerScene();
}

void Editor::createSequenceFloorDecoration(Point point)
{
    if (!m_sequenceSceneDocument.is_object() || isSequenceImageScene()) return;
    markSequenceDirty("add floor decoration");
    stopSequencePreview();
    auto& props = m_sequenceSceneDocument["props"];
    if (!props.is_array()) props = nlohmann::json::array();
    std::string id;
    for (int suffix = 1;; ++suffix)
    {
        id = "floor_decoration_" + std::to_string(suffix);
        bool used = false;
        for (const char* key : {"props", "actors"})
            if (m_sequenceSceneDocument.contains(key) && m_sequenceSceneDocument[key].is_array())
                for (const auto& item : m_sequenceSceneDocument[key])
                    if (item.is_object() && item.value("id", std::string()) == id) used = true;
        if (!used) break;
    }
    sequence::SequenceMarker marker;
    marker.id = sequence::UniqueMarkerId(m_sequenceSceneMarkers, id + "_position");
    marker.x = static_cast<float>(point.x);
    marker.y = static_cast<float>(point.y);
    if (m_sequenceMarkerSnap)
    { marker.x = std::round(marker.x * 4) / 4; marker.y = std::round(marker.y * 4) / 4; }
    m_sequenceSceneMarkers.push_back(marker);
    m_selectedSequenceMarkerIndex = static_cast<int>(m_sequenceSceneMarkers.size()) - 1;
    props.push_back({{"id", id}, {"marker", marker.id}, {"render_mode", "floor"},
        {"texture", ""}, {"scale", 1.0}, {"floor_width", 0.0}, {"floor_depth", 0.0},
        {"yaw_degrees", 0.0}, {"z_offset", 0.0}, {"visible", true}});
    m_selectedSequenceEntityIndex = static_cast<int>(props.size()) - 1;
    m_sequenceSelectionKind = SequenceEditorSelectionKind::Prop;
    m_sequenceMarkerTool = 0;
    syncSequenceMarkerScene();
    m_lastIoStatus = "Dekorace vložena. Vyberte její texturu v inspektoru; bod umístění lze posouvat myší.";
}

void Editor::deleteSelectedSequenceMarker()
{
    if (m_selectedSequenceMarkerIndex < 0 || m_selectedSequenceMarkerIndex >= static_cast<int>(m_sequenceSceneMarkers.size())) return;
    markSequenceDirty("delete sequence marker");
    m_sequenceSceneMarkers.erase(m_sequenceSceneMarkers.begin() + m_selectedSequenceMarkerIndex);
    m_selectedSequenceMarkerIndex = -1;
    m_sequenceDraggingMarker = -1;
    m_sequenceSelectionKind = SequenceEditorSelectionKind::Node;
    syncSequenceMarkerScene();
}

bool Editor::renameSelectedSequenceMarker(const std::string& id)
{
    if (m_selectedSequenceMarkerIndex < 0 || m_selectedSequenceMarkerIndex >= static_cast<int>(m_sequenceSceneMarkers.size())) return false;
    auto& marker = m_sequenceSceneMarkers[m_selectedSequenceMarkerIndex];
    if (id == marker.id) return true;
    if (id.empty() || sequence::FindMarker(m_sequenceSceneMarkers, id))
    {
        m_lastIoStatus = "Marker potřebuje neprázdné, jedinečné ID.";
        return false;
    }
    markSequenceDirty("rename sequence marker");
    const auto oldId = marker.id;
    if (m_selectedSequenceIndex >= 0 && m_selectedSequenceIndex < static_cast<int>(m_sequences.size()))
        sequence::RenameMarkerReferences(m_sequences[m_selectedSequenceIndex], oldId, id);
    sequence::RenameMarkerReferences(m_sequenceSceneDocument, oldId, id);
    marker.id = id;
    syncSequenceMarkerScene(true);
    return true;
}

void Editor::renderSequenceMarkerTools()
{
    if (m_sequencePickMarkerMode && (m_sequencePickNodeIndex != m_selectedSequenceNodeIndex ||
        m_sequencePickSequenceIndex != m_selectedSequenceIndex)) m_sequencePickMarkerMode = false;
    if (m_sequencePreviewCampaign)
    {
        if (ImGui::Button("Upravit markery")) stopSequencePreview();
        ImGui::SameLine();
        ImGui::TextDisabled("Pro úpravy zastavte přehrávání.");
        return;
    }
    if (ImGui::RadioButton("Prostor", !m_sequenceMarkerPlanView)) m_sequenceMarkerPlanView = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Půdorys", m_sequenceMarkerPlanView)) m_sequenceMarkerPlanView = true;
    ImGui::SameLine();
    if (ImGui::Button("+ Marker")) { m_sequenceMarkerTool = 1; m_sequencePickMarkerMode = false; }
    ImGui::SameLine();
    if (ImGui::Button("+ Kamera")) { m_sequenceMarkerTool = 2; m_sequencePickMarkerMode = false; }
    ImGui::SameLine();
    ImGui::Checkbox("Značky", &m_sequencePreviewOverlays);
    if (ImGui::Button("+ Dekorace na podlahu")) { m_sequenceMarkerTool = 3; m_sequencePickMarkerMode = false; }
    ImGui::Checkbox("Na podlahu", &m_sequenceMarkerFloorPlacement);
    ImGui::SameLine();
    ImGui::Checkbox("Krok 0,25", &m_sequenceMarkerSnap);
    ImGui::SameLine();
    if (ImGui::Button("Původní pohled"))
    {
        const auto document = sequencePreviewSceneDocument();
        if (document.is_object())
            m_sequencePreviewSceneLoaded = m_sequencePreviewScene.loadScene(selectedSequenceSceneId(),
                [&](const std::string& message) { m_lastIoStatus = message; }, &document);
    }
    if (m_sequenceMarkerTool || m_sequencePickMarkerMode)
    {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Klikněte do scény%s. Pravé tlačítko zruší vložení.",
            m_sequencePickMarkerMode ? " nebo na existující marker" : "");
    }
    else
    {
        ImGui::TextDisabled("Tažení: XY | Shift / modrá osa: výška | pravé tlačítko + WASD: pohled");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Názvy bodů se zobrazí po najetí myší nebo výběru.\nQ / E: výška pohledu | Ctrl při tažení: bez přichytávání | Esc: zrušit vložení");
    }
}

void Editor::renderSequenceMarkerBrowser()
{
    if (!ImGui::CollapsingHeader("Markery scény", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##marker_filter", "Hledat marker...", m_sequenceMarkerFilter, sizeof(m_sequenceMarkerFilter));
    ImGui::BeginChild("SceneMarkerList", ImVec2(0, 140), true);
    for (int i = 0; i < static_cast<int>(m_sequenceSceneMarkers.size()); ++i)
    {
        const auto& marker = m_sequenceSceneMarkers[i];
        if (marker.id.find(m_sequenceMarkerFilter) == std::string::npos) continue;
        ImGui::PushID(i);
        const auto p = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable(("    " + marker.id).c_str(), i == m_selectedSequenceMarkerIndex))
        {
            if (m_sequencePickMarkerMode) assignSequenceMarkerToAction(marker.id);
            m_selectedSequenceMarkerIndex = i;
            m_sequenceSelectionKind = SequenceEditorSelectionKind::Marker;
        }
        if (marker.type == "camera") CameraIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + 11, p.y + 9), IM_COL32(200, 150, 255, 255));
        else ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 11, p.y + 8), 4, IM_COL32(80, 190, 255, 255));
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void Editor::renderSequenceMarkerInspector()
{
    auto& marker = m_sequenceSceneMarkers[m_selectedSequenceMarkerIndex];
    char id[256]{};
    std::strncpy(id, marker.id.c_str(), sizeof(id) - 1);
    if (ImGui::InputText("Marker ID", id, sizeof(id), ImGuiInputTextFlags_EnterReturnsTrue)) renameSelectedSequenceMarker(id);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Potvrďte Enterem. Odkazy v aktuální sekvenci a scéně se přejmenují spolu s markerem.");
    const auto before = captureUndoState("edit spatial marker");
    bool changed = false;
    int type = marker.type == "camera" ? 1 : 0;
    const char* types[] = {"Bod", "Kamera"};
    const bool imageScene = isSequenceImageScene();
    if (!imageScene && ImGui::Combo("Typ", &type, types, 2))
    {
        marker.type = type ? "camera" : "sequence";
        if (type && marker.z <= 0) marker.z = 1.6f;
        changed = true;
    }
    auto field = [&](const char* label, float& value, float speed, float low, float high)
    {
        ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x - 12));
        const bool edited = ImGui::DragFloat(label, &value, speed, low, high, "%.2f");
        if (ImGui::IsItemActivated()) m_sequenceMarkerInspectorUndo = false;
        changed |= edited;
    };
    field("X", marker.x, imageScene ? 1.0f : 0.02f, -32768, 32768);
    field("Y", marker.y, imageScene ? 1.0f : 0.02f, -32768, 32768);
    if (!imageScene) field("Výška nad podlahou", marker.z, 0.02f, 0, 128);
    if (type && !imageScene)
    {
        field("Směr (stupně)", marker.angleDegrees, 0.5f, -360, 360);
        field("Sklon", marker.pitch, 0.005f, -0.52f, 0.52f);
    }
    if (changed)
    {
        if (!m_sequenceMarkerInspectorUndo)
        {
            m_undoStack.push_back(before);
            if (m_undoStack.size() > kMaxUndoStates) m_undoStack.erase(m_undoStack.begin());
            m_redoStack.clear();
            m_sequenceMarkerInspectorUndo = true;
        }
        m_sequenceEditorDirty = true;
        stopSequencePreview();
        syncSequenceMarkerScene();
    }
    if (!ImGui::IsAnyItemActive()) m_sequenceMarkerInspectorUndo = false;
    if (type && !imageScene)
    {
        if (ImGui::Button("Náhled z této kamery"))
        {
            stopSequencePreview();
            m_sequenceMarkerPlanView = false;
            m_sequencePreviewScene.setCameraPose({{marker.x, marker.y}, marker.z, marker.angleDegrees, marker.pitch});
        }
        if (ImGui::Button("Převzít aktuální pohled"))
        {
            markSequenceDirty("capture camera marker pose");
            const auto pose = m_sequencePreviewScene.cameraPose();
            marker.x = pose.position.x; marker.y = pose.position.y; marker.z = pose.height;
            marker.angleDegrees = pose.angleDegrees; marker.pitch = pose.pitch;
            syncSequenceMarkerScene();
        }
    }
    if (ImGui::Button("Přiřadit vybrané akci")) assignSequenceMarkerToAction(marker.id);
    if (ImGui::Button("Duplikovat marker"))
    {
        markSequenceDirty("duplicate spatial marker");
        auto copy = marker;
        copy.id = sequence::UniqueMarkerId(m_sequenceSceneMarkers, marker.id);
        copy.x += imageScene ? 24.0f : 0.5f;
        m_sequenceSceneMarkers.push_back(copy);
        m_selectedSequenceMarkerIndex = static_cast<int>(m_sequenceSceneMarkers.size()) - 1;
        syncSequenceMarkerScene();
    }
    ImGui::SameLine();
    if (ImGui::Button("Smazat marker")) deleteSelectedSequenceMarker();
    // Resolve the marker again: duplication above can reallocate the vector.
    if (m_selectedSequenceMarkerIndex >= 0 && m_selectedSequenceMarkerIndex < static_cast<int>(m_sequenceSceneMarkers.size()))
        renderSequenceMarkerEntities(m_sequenceSceneMarkers[m_selectedSequenceMarkerIndex].id);
    ImGui::TextWrapped("%s", m_lastIoStatus.c_str());
}

void Editor::renderSequenceMarkerViewport(ImVec2 pos, ImVec2 size)
{
    if (!m_sequencePreviewSceneLoaded) { ImGui::Dummy(size); return; }
    auto* draw = ImGui::GetWindowDrawList();
    const auto& environment = m_sequencePreviewScene.environment();
    const auto boundaries = environment.sceneBoundaries();
    ImVec4 bounds(-2, -2, 16, 12);
    if (!boundaries.empty() && !boundaries.front().empty())
    {
        bounds = ImVec4(boundaries[0][0][0], boundaries[0][0][1], boundaries[0][0][0], boundaries[0][0][1]);
        for (const auto& polygon : boundaries) for (const auto& p : polygon)
        {
            bounds.x = std::min(bounds.x, static_cast<float>(p[0])); bounds.y = std::min(bounds.y, static_cast<float>(p[1]));
            bounds.z = std::max(bounds.z, static_cast<float>(p[0])); bounds.w = std::max(bounds.w, static_cast<float>(p[1]));
        }
    }
    for (const auto& marker : m_sequenceSceneMarkers)
    {
        bounds.x = std::min(bounds.x, marker.x); bounds.y = std::min(bounds.y, marker.y);
        bounds.z = std::max(bounds.z, marker.x); bounds.w = std::max(bounds.w, marker.y);
    }
    if (m_sequenceDraggingMarker >= 0) bounds = m_sequenceMarkerPlanBounds;
    const float scale = std::max(0.01f, std::min((size.x - 50) / std::max(1.0f, bounds.z - bounds.x), (size.y - 50) / std::max(1.0f, bounds.w - bounds.y)));
    const ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    auto plan = [&](double x, double y) { return ImVec2(center.x + (x - (bounds.x + bounds.z) * 0.5) * scale,
        center.y + (y - (bounds.y + bounds.w) * 0.5) * scale); };
    auto world = [&](const sequence::SequenceMarker& marker) { return Point{marker.x, marker.y, environment.sceneFloorHeight(marker.x, marker.y) + marker.z}; };
    auto project = [&](Point point, ImVec2& screen, bool& occluded, bool clip = true)
    {
        occluded = false;
        if (m_sequenceMarkerPlanView) { screen = plan(point.x, point.y); return true; }
        double u, v;
        if (!environment.projectScenePoint(point, u, v, occluded)) return false;
        if (clip && (u < 0 || u >= 1 || v < 0 || v >= 1)) return false;
        screen = ImVec2(pos.x + u * size.x, pos.y + v * size.y);
        return true;
    };
    if (m_sequenceMarkerPlanView)
    {
        draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(22, 28, 33, 255));
        for (const auto& polygon : boundaries)
            for (size_t i = 0; i < polygon.size(); ++i)
                draw->AddLine(plan(polygon[i][0], polygon[i][1]), plan(polygon[(i + 1) % polygon.size()][0], polygon[(i + 1) % polygon.size()][1]), IM_COL32(126, 145, 157, 255), 2);
    }
    ImGui::InvisibleButton("##spatial_marker_input", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetMousePos();
    const double u = (mouse.x - pos.x) / size.x, v = (mouse.y - pos.y) / size.y;
    int hit = -1, axisHit = 0;
    float best = 15 * 15;
    draw->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
    if (m_sequencePreviewOverlays || m_sequenceMarkerPlanView)
    {
        for (int i = 0; i < static_cast<int>(m_sequenceSceneMarkers.size()); ++i)
        {
            const auto& marker = m_sequenceSceneMarkers[i];
            const bool selected = i == m_selectedSequenceMarkerIndex;
            ImVec2 p;
            bool occluded;
            if (!project(world(marker), p, occluded)) continue;
            if (occluded && !selected) continue;
            const ImU32 color = selected ? IM_COL32(255, 211, 78, 255) : (marker.type == "camera" ? IM_COL32(196, 148, 255, 255) : IM_COL32(83, 193, 249, 255));
            ImVec2 foot;
            bool ignored;
            if (!m_sequenceMarkerPlanView && project({marker.x, marker.y, environment.sceneFloorHeight(marker.x, marker.y)}, foot, ignored, false))
            {
                draw->AddLine(foot, p, color, 1);
                draw->AddCircle(foot, 4, color);
            }
            if (selected) draw->AddCircle(p, 15, color, 20, 1.5f);
            if (marker.type == "camera") CameraIcon(draw, p, color);
            else { draw->AddCircleFilled(p, 6, color); draw->AddCircle(p, 8, IM_COL32(20, 25, 30, 220), 16, 2); }
            const float d = (p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y);
            if (hovered && d < best) { best = d; hit = i; }
            if (marker.type == "camera")
            {
                const double a = marker.angleDegrees * Pi / 180;
                for (double offset : {-0.35, 0.0, 0.35})
                {
                    auto target = world(marker);
                    target.x += 1.2 * std::cos(a + offset); target.y += 1.2 * std::sin(a + offset);
                    target.z += 1.2 * marker.pitch * 0.6;
                    ImVec2 end;
                    if (project(target, end, ignored)) draw->AddLine(p, end, color, offset == 0 ? 2 : 1);
                }
            }
            if (selected && !m_sequenceMarkerPlanView)
                for (int axis = 1; axis <= 3; ++axis)
                {
                    auto endPoint = world(marker);
                    if (axis == 1) endPoint.x += 0.6;
                    if (axis == 2) endPoint.y += 0.6;
                    if (axis == 3) endPoint.z += 0.6;
                    ImVec2 end;
                    if (!project(endPoint, end, ignored)) continue;
                    const ImU32 axisColor = axis == 1 ? IM_COL32(245, 90, 80, 255) : axis == 2 ? IM_COL32(100, 225, 120, 255) : IM_COL32(95, 155, 255, 255);
                    draw->AddLine(p, end, axisColor, 2);
                    draw->AddCircleFilled(end, 4, axisColor);
                    const float distance = (mouse.x - end.x) * (mouse.x - end.x) + (mouse.y - end.y) * (mouse.y - end.y);
                    if (hovered && distance < 9 * 9 && distance < best) { hit = i; axisHit = axis; best = distance; }
                }
        }
        // Keep clusters readable: identify the hovered or selected point above
        // the symbols, instead of drawing every name on top of its neighbors.
        const int labeled = hit >= 0 ? hit : m_selectedSequenceMarkerIndex;
        if (labeled >= 0 && labeled < static_cast<int>(m_sequenceSceneMarkers.size()))
        {
            const auto& marker = m_sequenceSceneMarkers[labeled];
            bool ignored;
            ImVec2 p;
            if (project(world(marker), p, ignored))
            {
                const auto textSize = ImGui::CalcTextSize(marker.id.c_str());
                p.x = std::clamp(p.x + 18, pos.x + 4, std::max(pos.x + 4, pos.x + size.x - textSize.x - 8));
                p.y = std::clamp(p.y - 22, pos.y + 4, std::max(pos.y + 4, pos.y + size.y - textSize.y - 8));
                draw->AddRectFilled(ImVec2(p.x - 4, p.y - 3), ImVec2(p.x + textSize.x + 4, p.y + textSize.y + 3), IM_COL32(15, 20, 27, 235), 3);
                draw->AddText(p, IM_COL32(255, 226, 140, 255), marker.id.c_str());
            }
        }
        if (m_sequenceMarkerPlanView)
        {
            const auto camera = m_sequencePreviewScene.cameraPose();
            const auto p = plan(camera.position.x, camera.position.y);
            CameraIcon(draw, p, IM_COL32(255, 255, 255, 255));
            const double a = camera.angleDegrees * Pi / 180;
            draw->AddLine(p, plan(camera.position.x + std::cos(a) * 2, camera.position.y + std::sin(a) * 2), IM_COL32(255, 255, 255, 200), 2);
        }
    }
    const bool cancelPlacement = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && (m_sequenceMarkerTool || m_sequencePickMarkerMode);
    if (cancelPlacement)
    {
        m_sequenceMarkerTool = 0; m_sequencePickMarkerMode = false;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (hit >= 0 && !m_sequenceMarkerTool)
        {
            const bool picking = m_sequencePickMarkerMode;
            if (picking) assignSequenceMarkerToAction(m_sequenceSceneMarkers[hit].id);
            m_selectedSequenceMarkerIndex = hit;
            m_sequenceSelectionKind = SequenceEditorSelectionKind::Marker;
            if (!picking)
            {
                m_sequenceDraggingMarker = hit;
                m_sequenceMarkerDragStart = m_sequenceSceneMarkers[hit];
                m_sequenceMarkerDragMouse = mouse;
                m_sequenceMarkerDragChanged = false;
                m_sequenceMarkerDragAxis = axisHit;
                m_sequenceMarkerPlanBounds = bounds;
                ImVec2 foot;
                bool ignored;
                const auto& start = m_sequenceMarkerDragStart;
                m_sequenceMarkerDragScreenOffset = ImVec2(0, 0);
                if (!m_sequenceMarkerPlanView && project({start.x, start.y, environment.sceneFloorHeight(start.x, start.y)}, foot, ignored, false))
                    m_sequenceMarkerDragScreenOffset = ImVec2((foot.x - mouse.x) / size.x, (foot.y - mouse.y) / size.y);
            }
        }
        else if (m_sequenceMarkerTool || m_sequencePickMarkerMode)
        {
            Point point;
            bool valid = false;
            if (m_sequenceMarkerPlanView)
            {
                point.x = (mouse.x - center.x) / scale + (bounds.x + bounds.z) * 0.5;
                point.y = (mouse.y - center.y) / scale + (bounds.y + bounds.w) * 0.5;
                point.z = environment.sceneFloorHeight(point.x, point.y);
                valid = true;
            }
            else valid = environment.pickScenePoint(u, v, point);
            if (valid)
            {
                if (m_sequenceMarkerFloorPlacement) point.z = environment.sceneFloorHeight(point.x, point.y);
                bool camera = m_sequenceMarkerTool == 2;
                if (m_sequencePickMarkerMode && m_selectedSequenceIndex >= 0 && m_selectedSequenceNodeIndex >= 0)
                    if (auto* action = sequence::FindTimelineAction(m_sequences[m_selectedSequenceIndex].nodes[m_selectedSequenceNodeIndex], m_sequencePickActionPath))
                        camera = action->type == "set_camera" || action->type == "camera_move";
                if (m_sequenceMarkerTool == 3) createSequenceFloorDecoration(point);
                else createSequenceMarker(point, camera);
            }
            else m_lastIoStatus = "Klikněte na podlahu, stěnu nebo objekt; na oblohu marker umístit nelze.";
        }
    }
    if (m_sequenceDraggingMarker >= 0 && m_sequenceDraggingMarker < static_cast<int>(m_sequenceSceneMarkers.size()))
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            auto next = m_sequenceMarkerDragStart;
            const bool vertical = ImGui::GetIO().KeyShift || m_sequenceMarkerDragAxis == 3;
            if (vertical) next.z = std::max(0.0f, next.z - (mouse.y - m_sequenceMarkerDragMouse.y) * 0.02f);
            else if (m_sequenceMarkerPlanView)
            {
                next.x += (mouse.x - m_sequenceMarkerDragMouse.x) / scale;
                next.y += (mouse.y - m_sequenceMarkerDragMouse.y) / scale;
            }
            else
            {
                Point target;
                if (environment.intersectSceneHeight(u + m_sequenceMarkerDragScreenOffset.x, v + m_sequenceMarkerDragScreenOffset.y,
                    environment.sceneFloorHeight(next.x, next.y), target))
                { next.x = target.x; next.y = target.y; }
            }
            if (m_sequenceMarkerDragAxis == 1) next.y = m_sequenceMarkerDragStart.y;
            if (m_sequenceMarkerDragAxis == 2) next.x = m_sequenceMarkerDragStart.x;
            if (m_sequenceMarkerSnap && !ImGui::GetIO().KeyCtrl)
            { next.x = std::round(next.x * 4) / 4; next.y = std::round(next.y * 4) / 4; next.z = std::round(next.z * 4) / 4; }
            if (sequence::MarkerToJson(next) != sequence::MarkerToJson(m_sequenceSceneMarkers[m_sequenceDraggingMarker]))
            {
                if (!m_sequenceMarkerDragChanged) { markSequenceDirty("move spatial marker"); m_sequenceMarkerDragChanged = true; }
                m_sequenceSceneMarkers[m_sequenceDraggingMarker] = next;
                syncSequenceMarkerScene();
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_sequenceDraggingMarker = -1;
    }
    if (hovered && hit >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (hovered && !cancelPlacement && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !m_sequenceMarkerPlanView && !m_sequenceMarkerTool && !m_sequencePickMarkerMode)
    {
        auto camera = m_sequencePreviewScene.cameraPose();
        camera.angleDegrees += ImGui::GetIO().MouseDelta.x * 0.25f;
        camera.pitch = std::clamp(camera.pitch - ImGui::GetIO().MouseDelta.y * 0.0028f, -0.52f, 0.52f);
        const float speed = std::min(ImGui::GetIO().DeltaTime, 0.1f) * (ImGui::GetIO().KeyShift ? 8.0f : 3.0f);
        const float a = camera.angleDegrees * Pi / 180;
        if (!ImGui::GetIO().WantTextInput)
        {
            const float forward = (ImGui::IsKeyDown(ImGuiKey_W) ? 1 : 0) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1 : 0);
            const float strafe = (ImGui::IsKeyDown(ImGuiKey_D) ? 1 : 0) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1 : 0);
            camera.position.x += (std::cos(a) * forward - std::sin(a) * strafe) * speed;
            camera.position.y += (std::sin(a) * forward + std::cos(a) * strafe) * speed;
            camera.height += ((ImGui::IsKeyDown(ImGuiKey_E) ? 1 : 0) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1 : 0)) * speed;
        }
        m_sequencePreviewScene.setCameraPose(camera);
    }
    draw->PopClipRect();
}
