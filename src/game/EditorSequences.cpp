#include "Editor.h"

#include "PathUtils.h"
#include "game/sequence/SequenceActions.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

#include "imgui.h"

namespace fs = std::filesystem;

namespace
{
    static bool EditString(const char* label, std::string& value, size_t capacity = 128)
    {
        std::vector<char> buf(capacity, '\0');
        std::strncpy(buf.data(), value.c_str(), buf.size() - 1);
        if (ImGui::InputText(label, buf.data(), buf.size()))
        {
            value = buf.data();
            return true;
        }
        return false;
    }

    static std::string ActionLabel(const sequence::ActionDef& action, int index)
    {
        return "[" + std::to_string(index + 1) + "] " + (action.type.empty() ? "<missing>" : action.type);
    }

    static float JsonFloat(const nlohmann::json& j, const char* key, float fallback = 0.0f)
    {
        if (!j.contains(key) || !j.at(key).is_number())
            return fallback;
        return j.at(key).get<float>();
    }

    static void EditJsonString(const char* label, nlohmann::json& data, const char* key)
    {
        std::string value = data.value(key, "");
        if (EditString(label, value))
            data[key] = value;
    }

    static void EditJsonFloat(const char* label, nlohmann::json& data, const char* key, float fallback = 0.0f)
    {
        float value = JsonFloat(data, key, fallback);
        if (ImGui::InputFloat(label, &value))
            data[key] = value;
    }

    static void EditJsonBool(const char* label, nlohmann::json& data, const char* key, bool fallback = false)
    {
        bool value = data.value(key, fallback);
        if (ImGui::Checkbox(label, &value))
            data[key] = value;
    }

    static void EnsureActionDefaults(sequence::ActionDef& action)
    {
        if (!action.data.is_object())
            action.data = nlohmann::json::object();
        action.data["type"] = action.type;
        if (!action.data.contains("blocking"))
            action.data["blocking"] = action.blocking;
        else
            action.blocking = action.data.value("blocking", action.blocking);
        if (!action.data.contains("duration") && action.duration > 0.0f)
            action.data["duration"] = action.duration;
    }
}

bool Editor::loadSequences()
{
    m_sequences.clear();
    m_sequencePaths.clear();

    const fs::path dir = pathutils::AssetsDir() / "sequences";
    std::error_code ec;
    fs::create_directories(dir, ec);

    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;

        auto loaded = sequence::LoadSequenceFromFile(entry.path().string());
        if (loaded.ok)
        {
            m_sequences.push_back(std::move(loaded.sequence));
            m_sequencePaths.push_back(entry.path().string());
        }
        else
        {
            for (const auto& err : loaded.errors)
                m_lastIoStatus = err;
        }
    }

    if (m_selectedSequenceIndex >= (int)m_sequences.size())
        m_selectedSequenceIndex = (int)m_sequences.size() - 1;
    if (m_sequences.empty())
        m_selectedSequenceIndex = -1;

    return true;
}

std::string Editor::sequencePathForId(const std::string& id) const
{
    return (pathutils::AssetsDir() / "sequences" / (id + ".json")).string();
}

bool Editor::saveSelectedSequence()
{
    return saveSequenceAt(m_selectedSequenceIndex);
}

bool Editor::saveSequenceAt(int index)
{
    if (index < 0 || index >= (int)m_sequences.size())
        return false;

    auto& seq = m_sequences[index];
    if (seq.id.empty())
    {
        m_lastIoStatus = "Sequence id is empty.";
        return false;
    }

    if (index >= (int)m_sequencePaths.size())
        m_sequencePaths.resize(m_sequences.size());
    if (m_sequencePaths[index].empty())
        m_sequencePaths[index] = sequencePathForId(seq.id);

    std::string err;
    if (!sequence::SaveSequenceToFile(seq, m_sequencePaths[index], &err))
    {
        m_lastIoStatus = err;
        return false;
    }

    m_lastIoStatus = "Sequence saved: " + m_sequencePaths[index];
    return true;
}

bool Editor::loadSequenceMarkersForMap(const std::string& mapPath)
{
    std::string err;
    if (!sequence::LoadMarkersForMap(mapPath, m_sequenceMarkers, &err))
    {
        m_lastIoStatus = "Sequence markers load failed: " + err;
        return false;
    }
    return true;
}

bool Editor::saveSequenceMarkersForMap(const std::string& mapPath)
{
    std::string err;
    if (!sequence::SaveMarkersForMap(mapPath, m_sequenceMarkers, &err))
    {
        m_lastIoStatus = "Sequence markers save failed: " + err;
        return false;
    }
    return true;
}

int Editor::findSequenceIndexById(const std::string& id) const
{
    for (int i = 0; i < (int)m_sequences.size(); ++i)
    {
        if (m_sequences[i].id == id)
            return i;
    }
    return -1;
}

int Editor::findSequenceNodeIndexById(const sequence::SequenceDefinition& seq, const std::string& id) const
{
    for (int i = 0; i < (int)seq.nodes.size(); ++i)
    {
        if (seq.nodes[i].id == id)
            return i;
    }
    return -1;
}

void Editor::pushSequenceUndo(const char* label)
{
    pushUndoState(label);
}

void Editor::renderSequenceValidation(const sequence::SequenceDefinition& seq) const
{
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::set<std::string> nodeIds;

    if (seq.id.empty())
        errors.push_back("missing sequence id");
    if (seq.start.empty() || findSequenceNodeIndexById(seq, seq.start) < 0)
        errors.push_back("missing start node");

    for (int i = 0; i < (int)m_sequences.size(); ++i)
    {
        if (&m_sequences[i] != &seq && m_sequences[i].id == seq.id)
            errors.push_back("duplicate sequence id: " + seq.id);
    }

    for (const auto& node : seq.nodes)
    {
        if (!nodeIds.insert(node.id).second)
            errors.push_back("duplicate node id: " + node.id);
        if (!node.next.empty() && findSequenceNodeIndexById(seq, node.next) < 0)
            errors.push_back("missing next node: " + node.next);

        for (const auto& action : node.actions)
        {
            if (!sequence::IsKnownActionType(action.type))
                warnings.push_back("unknown action type: " + action.type);
            if (action.type == "goto")
            {
                const std::string target = action.data.value("target", action.data.value("node", ""));
                if (!target.empty() && findSequenceNodeIndexById(seq, target) < 0)
                    errors.push_back("missing goto target: " + target);
            }
            const std::string marker = action.data.value("marker", action.data.value("target_marker", ""));
            if (!marker.empty() && !sequence::FindMarker(m_sequenceMarkers, marker))
                warnings.push_back("unknown marker: " + marker);
            if (action.type == "load_map")
            {
                const std::string map = action.data.value("map", action.data.value("path", ""));
                if (!map.empty())
                {
                    fs::path p = map;
                    if (!p.is_absolute())
                        p = p.has_parent_path() ? (pathutils::ProjectRoot() / p) : (pathutils::MapsDir() / p);
                    if (p.extension().empty())
                        p.replace_extension(".rvm");
                    if (!fs::exists(p))
                        warnings.push_back("unknown map: " + map);
                }
            }
        }
    }

    if (errors.empty() && warnings.empty())
    {
        ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.45f, 1.0f), "Validation: OK");
        return;
    }

    for (const auto& e : errors)
        ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.22f, 1.0f), "Error: %s", e.c_str());
    for (const auto& w : warnings)
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "Warning: %s", w.c_str());
}

void Editor::renderSequenceActionEditor(sequence::ActionDef& action, int actionIndex)
{
    EnsureActionDefaults(action);

    ImGui::PushID(actionIndex);
    EditString("Type", action.type, 64);
    action.data["type"] = action.type;
    EditJsonFloat("Start delay", action.data, "startDelay", action.startDelay);
    action.startDelay = action.data.value("startDelay", action.startDelay);
    EditJsonFloat("Duration", action.data, "duration", action.duration);
    action.duration = action.data.value("duration", action.duration);
    EditJsonBool("Blocking", action.data, "blocking", action.blocking);
    action.blocking = action.data.value("blocking", action.blocking);

    if (action.type == "wait" || action.type == "fade_in" || action.type == "fade_out" || action.type == "screen_flash")
    {
        if (action.type == "screen_flash")
            EditJsonFloat("Intensity", action.data, "intensity", 1.0f);
    }
    else if (action.type == "spawn_actor")
    {
        EditJsonString("Actor", action.data, "actor");
        EditJsonString("Kind", action.data, "kind");
        EditJsonString("Ref/NPC", action.data, "ref");
        EditJsonString("Sprite/Object", action.data, "sprite");
        EditJsonString("Character", action.data, "character");
        EditJsonString("Marker", action.data, "marker");
        EditJsonFloat("X", action.data, "x", 0.0f);
        EditJsonFloat("Y", action.data, "y", 0.0f);
        EditJsonFloat("Scale", action.data, "scale", 1.0f);
    }
    else if (action.type == "move_actor")
    {
        EditJsonString("Actor", action.data, "actor");
        EditJsonString("Target marker", action.data, "target_marker");
        EditJsonFloat("Target X", action.data, "x", 0.0f);
        EditJsonFloat("Target Y", action.data, "y", 0.0f);
    }
    else if (action.type == "set_actor_animation")
    {
        EditJsonString("Actor", action.data, "actor");
        EditJsonString("Animation", action.data, "animation");
    }
    else if (action.type == "face_actor")
    {
        EditJsonString("Actor", action.data, "actor");
        EditJsonString("Direction/target", action.data, "direction");
    }
    else if (action.type == "set_camera" || action.type == "camera_move")
    {
        EditJsonString("Marker", action.data, "marker");
        EditJsonString("Target marker", action.data, "target_marker");
        EditJsonFloat("X", action.data, "x", 0.0f);
        EditJsonFloat("Y", action.data, "y", 0.0f);
    }
    else if (action.type == "camera_follow")
    {
        EditJsonString("Actor", action.data, "actor");
    }
    else if (action.type == "camera_shake")
    {
        EditJsonFloat("Intensity", action.data, "intensity", 1.0f);
    }
    else if (action.type == "show_text")
    {
        EditJsonString("Text", action.data, "text");
    }
    else if (action.type == "play_music" || action.type == "play_sfx" || action.type == "play_voice")
    {
        EditJsonString("Id/path", action.data, "id");
    }
    else if (action.type == "set_time")
    {
        EditJsonString("Time", action.data, "value");
    }
    else if (action.type == "set_weather")
    {
        EditJsonBool("Rain", action.data, "rain", false);
        EditJsonFloat("Rain intensity", action.data, "rain_intensity", 0.0f);
        EditJsonFloat("Wind", action.data, "wind", 0.0f);
        EditJsonFloat("Cloudiness", action.data, "cloudiness", 0.0f);
    }
    else if (action.type == "set_flag" || action.type == "require_flag")
    {
        EditJsonString("Flag", action.data, "flag");
    }
    else if (action.type == "goto")
    {
        EditJsonString("Target node", action.data, "target");
    }
    else if (action.type == "load_map")
    {
        EditJsonString("Map", action.data, "map");
        EditJsonString("Spawn", action.data, "spawn");
    }
    else if (action.type == "teleport_player")
    {
        EditJsonString("Marker", action.data, "marker");
        EditJsonFloat("X", action.data, "x", 0.0f);
        EditJsonFloat("Y", action.data, "y", 0.0f);
    }
    else if (action.type == "start_dialog")
    {
        EditJsonString("NPC/dialog", action.data, "npc");
    }
    else if (action.type == "change_renderer_mode")
    {
        EditJsonString("Mode", action.data, "mode");
    }

    if (ImGui::Button("Pick position"))
    {
        const std::string existing = action.data.value("target_marker", action.data.value("marker", ""));
        std::strncpy(m_sequenceMarkerId, existing.empty() ? "marker" : existing.c_str(), sizeof(m_sequenceMarkerId) - 1);
        m_sequenceMarkerId[sizeof(m_sequenceMarkerId) - 1] = '\0';
        m_sequencePickMarkerMode = true;
        m_sequencePickActionIndex = actionIndex;
        m_lastIoStatus = "Click map to place sequence marker.";
    }
    ImGui::SameLine();
    ImGui::InputText("Marker id", m_sequenceMarkerId, sizeof(m_sequenceMarkerId));

    if (ImGui::TreeNode("Raw JSON"))
    {
        std::string raw = action.data.dump(2);
        ImGui::TextWrapped("%s", raw.c_str());
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void Editor::renderSequenceEditorContents()
{
    ImGui::Columns(3, "SequenceEditorColumns", true);
    ImGui::TextUnformatted("Sequences");
    ImGui::Separator();

    if (ImGui::Button("New"))
    {
        pushSequenceUndo("new sequence");
        sequence::SequenceDefinition seq;
        seq.id = m_sequenceNewId;
        seq.name = "New sequence";
        seq.start = "start";
        sequence::NodeDef node;
        node.id = "start";
        seq.nodes.push_back(std::move(node));
        m_sequences.push_back(std::move(seq));
        m_sequencePaths.push_back(sequencePathForId(m_sequenceNewId));
        m_selectedSequenceIndex = (int)m_sequences.size() - 1;
        m_selectedSequenceNodeIndex = 0;
        m_selectedSequenceActionIndex = -1;
    }
    ImGui::SameLine();
    ImGui::InputText("New id", m_sequenceNewId, sizeof(m_sequenceNewId));

    if (ImGui::Button("Duplicate") && m_selectedSequenceIndex >= 0)
    {
        pushSequenceUndo("duplicate sequence");
        auto copy = m_sequences[m_selectedSequenceIndex];
        copy.id += "_copy";
        copy.name += " copy";
        m_sequences.push_back(std::move(copy));
        m_sequencePaths.push_back(sequencePathForId(m_sequences.back().id));
        m_selectedSequenceIndex = (int)m_sequences.size() - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && m_selectedSequenceIndex >= 0)
    {
        pushSequenceUndo("delete sequence");
        m_sequences.erase(m_sequences.begin() + m_selectedSequenceIndex);
        if (m_selectedSequenceIndex < (int)m_sequencePaths.size())
            m_sequencePaths.erase(m_sequencePaths.begin() + m_selectedSequenceIndex);
        m_selectedSequenceIndex = std::min(m_selectedSequenceIndex, (int)m_sequences.size() - 1);
        m_selectedSequenceNodeIndex = -1;
        m_selectedSequenceActionIndex = -1;
    }
    if (ImGui::Button("Save"))
        saveSelectedSequence();
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
        loadSequences();

    if (ImGui::BeginListBox("##sequence_list", ImVec2(-1, 220)))
    {
        for (int i = 0; i < (int)m_sequences.size(); ++i)
        {
            const bool selected = (i == m_selectedSequenceIndex);
            if (ImGui::Selectable(m_sequences[i].id.c_str(), selected))
            {
                m_selectedSequenceIndex = i;
                m_selectedSequenceNodeIndex = m_sequences[i].nodes.empty() ? -1 : 0;
                m_selectedSequenceActionIndex = -1;
            }
        }
        ImGui::EndListBox();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Markers");
    if (ImGui::Button("Add marker"))
    {
        pushSequenceUndo("add sequence marker");
        sequence::SequenceMarker marker;
        marker.id = m_sequenceMarkerId;
        marker.x = (float)m_camX + 160.0f;
        marker.y = (float)m_camY + 160.0f;
        m_sequenceMarkers.push_back(std::move(marker));
    }
    ImGui::SameLine();
    if (ImGui::Button("Save markers"))
        saveSequenceMarkersForMap(m_mapPath);

    if (ImGui::BeginListBox("##sequence_markers", ImVec2(-1, 160)))
    {
        for (int i = 0; i < (int)m_sequenceMarkers.size(); ++i)
        {
            auto& marker = m_sequenceMarkers[i];
            ImGui::PushID(i);
            ImGui::Text("%s (%.0f, %.0f)", marker.id.c_str(), marker.x, marker.y);
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }

    ImGui::NextColumn();

    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= (int)m_sequences.size())
    {
        ImGui::Columns(1);
        return;
    }

    auto& seq = m_sequences[m_selectedSequenceIndex];
    ImGui::TextUnformatted("Sequence");
    ImGui::Separator();
    if (EditString("Id", seq.id, 96))
        m_sequencePaths[m_selectedSequenceIndex] = sequencePathForId(seq.id);
    EditString("Name", seq.name, 128);
    EditString("Start node", seq.start, 96);

    if (ImGui::Button("Preview Sequence"))
    {
        m_sequencePreviewActive = true;
        m_sequencePreviewTimer = 0.0f;
        m_selectedSequenceNodeIndex = findSequenceNodeIndexById(seq, seq.start);
        m_selectedSequenceActionIndex = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop Preview"))
        m_sequencePreviewActive = false;
    ImGui::SameLine();
    if (ImGui::Button("Restart Preview"))
    {
        m_sequencePreviewActive = true;
        m_sequencePreviewTimer = 0.0f;
        m_selectedSequenceNodeIndex = findSequenceNodeIndexById(seq, seq.start);
        m_selectedSequenceActionIndex = 0;
    }
    if (ImGui::Button("Step Action") && m_selectedSequenceNodeIndex >= 0)
    {
        auto& node = seq.nodes[m_selectedSequenceNodeIndex];
        if (!node.actions.empty())
            m_selectedSequenceActionIndex = std::min((int)node.actions.size() - 1, m_selectedSequenceActionIndex + 1);
    }
    ImGui::SameLine();
    ImGui::Text("Preview timer: %.2f", m_sequencePreviewTimer);

    if (m_sequencePreviewActive)
        m_sequencePreviewTimer += ImGui::GetIO().DeltaTime;

    ImGui::Separator();
    if (ImGui::Button("Add Node"))
    {
        pushSequenceUndo("add sequence node");
        sequence::NodeDef node;
        node.id = "node_" + std::to_string(seq.nodes.size() + 1);
        seq.nodes.push_back(std::move(node));
        m_selectedSequenceNodeIndex = (int)seq.nodes.size() - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate Node") && m_selectedSequenceNodeIndex >= 0)
    {
        pushSequenceUndo("duplicate sequence node");
        auto copy = seq.nodes[m_selectedSequenceNodeIndex];
        copy.id += "_copy";
        seq.nodes.push_back(std::move(copy));
        m_selectedSequenceNodeIndex = (int)seq.nodes.size() - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Node") && m_selectedSequenceNodeIndex >= 0)
    {
        pushSequenceUndo("delete sequence node");
        seq.nodes.erase(seq.nodes.begin() + m_selectedSequenceNodeIndex);
        m_selectedSequenceNodeIndex = std::min(m_selectedSequenceNodeIndex, (int)seq.nodes.size() - 1);
        m_selectedSequenceActionIndex = -1;
    }

    if (ImGui::BeginListBox("Nodes", ImVec2(-1, 180)))
    {
        for (int i = 0; i < (int)seq.nodes.size(); ++i)
        {
            bool selected = i == m_selectedSequenceNodeIndex;
            if (ImGui::Selectable(seq.nodes[i].id.c_str(), selected))
            {
                m_selectedSequenceNodeIndex = i;
                m_selectedSequenceActionIndex = seq.nodes[i].actions.empty() ? -1 : 0;
            }
        }
        ImGui::EndListBox();
    }

    renderSequenceValidation(seq);

    ImGui::NextColumn();

    if (m_selectedSequenceNodeIndex < 0 || m_selectedSequenceNodeIndex >= (int)seq.nodes.size())
    {
        ImGui::Columns(1);
        return;
    }

    auto& node = seq.nodes[m_selectedSequenceNodeIndex];
    ImGui::TextUnformatted("Node");
    ImGui::Separator();
    EditString("Node id", node.id, 96);
    EditString("Next node", node.next, 96);

    ImGui::Separator();
    ImGui::TextUnformatted("Actions");
    if (ImGui::BeginCombo("Add Action", "choose type"))
    {
        std::string currentCategory;
        for (const auto& info : sequence::AllActionTypes())
        {
            if (currentCategory != info.category)
            {
                currentCategory = info.category;
                ImGui::Separator();
                ImGui::TextDisabled("%s", currentCategory.c_str());
            }
            if (ImGui::Selectable(info.type))
            {
                pushSequenceUndo("add sequence action");
                sequence::ActionDef action;
                action.type = info.type;
                action.data = {{"type", info.type}, {"blocking", true}};
                if (action.type == "wait" || action.type == "fade_in" || action.type == "fade_out")
                    action.data["duration"] = 1.0f;
                node.actions.push_back(std::move(action));
                m_selectedSequenceActionIndex = (int)node.actions.size() - 1;
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginListBox("##action_list", ImVec2(-1, 160)))
    {
        for (int i = 0; i < (int)node.actions.size(); ++i)
        {
            const bool selected = i == m_selectedSequenceActionIndex;
            std::string label = ActionLabel(node.actions[i], i);
            if (m_sequencePreviewActive && i == m_selectedSequenceActionIndex)
                label += "  <preview>";
            if (ImGui::Selectable(label.c_str(), selected))
                m_selectedSequenceActionIndex = i;
        }
        ImGui::EndListBox();
    }

    if (m_selectedSequenceActionIndex >= 0 && m_selectedSequenceActionIndex < (int)node.actions.size())
    {
        if (ImGui::Button("Delete Action"))
        {
            pushSequenceUndo("delete sequence action");
            node.actions.erase(node.actions.begin() + m_selectedSequenceActionIndex);
            m_selectedSequenceActionIndex = std::min(m_selectedSequenceActionIndex, (int)node.actions.size() - 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate Action"))
        {
            pushSequenceUndo("duplicate sequence action");
            node.actions.insert(node.actions.begin() + m_selectedSequenceActionIndex + 1, node.actions[m_selectedSequenceActionIndex]);
            ++m_selectedSequenceActionIndex;
        }
        ImGui::SameLine();
        if (ImGui::Button("Move Up") && m_selectedSequenceActionIndex > 0)
        {
            pushSequenceUndo("move sequence action");
            std::swap(node.actions[m_selectedSequenceActionIndex], node.actions[m_selectedSequenceActionIndex - 1]);
            --m_selectedSequenceActionIndex;
        }
        ImGui::SameLine();
        if (ImGui::Button("Move Down") && m_selectedSequenceActionIndex + 1 < (int)node.actions.size())
        {
            pushSequenceUndo("move sequence action");
            std::swap(node.actions[m_selectedSequenceActionIndex], node.actions[m_selectedSequenceActionIndex + 1]);
            ++m_selectedSequenceActionIndex;
        }

        ImGui::Separator();
        renderSequenceActionEditor(node.actions[m_selectedSequenceActionIndex], m_selectedSequenceActionIndex);
    }

    ImGui::Columns(1);
}
