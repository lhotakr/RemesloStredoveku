#include "Editor.h"

#include "PathUtils.h"
#include "JsonUtils.h"
#include "game/sequence/SequenceActions.h"
#include "game/sequence/SequenceAudio.h"
#include "game/sequence/SequenceTimeline.h"
#include "game/sequence/SequenceEditing.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <set>

#include "imgui.h"

namespace fs = std::filesystem;

namespace
{
    static std::string UniqueNodeId(const sequence::SequenceDefinition& definition, const std::string& prefix)
    {
        if (!definition.findNode(prefix)) return prefix;
        int suffix = 2;
        while (definition.findNode(prefix + "_" + std::to_string(suffix))) ++suffix;
        return prefix + "_" + std::to_string(suffix);
    }

    static int HourForPreviewNamedTime(const std::string& value, int fallback)
    {
        if (value == "dawn") return 5;
        if (value == "morning") return 8;
        if (value == "forenoon") return 10;
        if (value == "noon") return 12;
        if (value == "afternoon") return 15;
        if (value == "late_afternoon" || value == "late afternoon") return 17;
        if (value == "evening") return 20;
        if (value == "night") return 23;
        return fallback;
    }

    class EditorSequencePreviewContext final : public sequence::ISequenceContext
    {
    public:
        explicit EditorSequencePreviewContext(Editor& editor) : m_editor(editor) {}

        void sequenceLog(const std::string& message) override { m_editor.sequenceEditorPreviewLog(message); }
        bool hasStoryFlag(const std::string& flag) const override { return m_editor.sequenceEditorPreviewHasFlag(flag); }
        void setStoryFlag(const std::string& flag) override { m_editor.sequenceEditorPreviewSetFlag(flag); }
        bool loadScene(const std::string& sceneId) override { return m_editor.sequenceEditorPreviewLoadScene(sceneId); }
        bool resolveMarker(const std::string& markerId, sequence::Vec2& out) const override { return m_editor.sequenceEditorPreviewResolveMarker(markerId, out); }
        bool spawnActor(const sequence::ActorSpawnDef& def) override { return m_editor.sequenceEditorPreviewSpawnActor(def); }
        void removeActor(const std::string& actorId) override { m_editor.sequenceEditorPreviewRemoveActor(actorId); }
        void setActorVisible(const std::string& actorId, bool visible) override { m_editor.sequenceEditorPreviewSetActorVisible(actorId, visible); }
        bool getActorPosition(const std::string& actorId, sequence::Vec2& out) const override { return m_editor.sequenceEditorPreviewGetActorPosition(actorId, out); }
        bool setActorPosition(const std::string& actorId, sequence::Vec2 pos) override { return m_editor.sequenceEditorPreviewSetActorPosition(actorId, pos); }
        void setActorAnimation(const std::string& actorId, const std::string& animationId) override { m_editor.sequenceEditorPreviewSetActorAnimation(actorId, animationId); }
        void faceActor(const std::string& actorId, const std::string& directionOrTarget) override { m_editor.sequenceEditorPreviewFaceActor(actorId, directionOrTarget); }
        void setCameraPosition(sequence::Vec2 pos) override { m_editor.sequenceEditorPreviewSetCameraPosition(pos); }
        void setCameraFollow(const std::string& actorId) override { m_editor.sequenceEditorPreviewSetCameraFollow(actorId); }
        void setCameraFollow(const std::string& actorId, const sequence::CameraFollowSettings& settings) override { m_editor.sequenceEditorPreviewSetCameraFollow(actorId, settings); }
        void setCameraShake(float intensity, float duration, float frequency) override { m_editor.sequenceEditorPreviewSetCameraShake(intensity, duration, frequency); }
        void setFadeAlpha(float alpha) override { m_editor.sequenceEditorPreviewSetFadeAlpha(alpha); }
        void setFlashAlpha(float alpha) override { m_editor.sequenceEditorPreviewSetFlashAlpha(alpha); }
        void showSequenceText(const std::string& text, float duration) override { m_editor.sequenceEditorPreviewShowText(text, duration); }
        void hideSequenceText() override { m_editor.sequenceEditorPreviewHideText(); }
        void playMusic(const std::string& id, int, int) override { m_editor.sequenceEditorPreviewLog("PREVIEW: play_music " + id); }
        void stopMusic(int) override { m_editor.sequenceEditorPreviewLog("PREVIEW: stop_music simulated"); }
        void playSfx(const std::string& id) override { m_editor.sequenceEditorPreviewLog("PREVIEW: play_sfx " + id); }
        void playVoice(const std::string& id) override { m_editor.sequenceEditorPreviewLog("PREVIEW: play_voice " + id); }
        void setTimeOfDay(const std::string& value) override { m_editor.sequenceEditorPreviewSetTimeOfDay(value); }
        void setWeather(const nlohmann::json& data) override { m_editor.sequenceEditorPreviewSetWeather(data); }
        bool loadMapForSequence(const std::string& mapIdOrPath, const std::string& spawnId) override { return m_editor.sequenceEditorPreviewLoadMap(mapIdOrPath, spawnId); }
        void teleportPlayer(sequence::Vec2 pos) override { m_editor.sequenceEditorPreviewTeleportPlayer(pos); }
        void enablePlayerControl(bool enabled) override { m_editor.sequenceEditorPreviewEnablePlayerControl(enabled); }
        void startDialog(const std::string& npcOrDialogId) override { m_editor.sequenceEditorPreviewLog("PREVIEW: start_dialog " + npcOrDialogId); }
        void changeRendererMode(const std::string& mode) override { m_editor.sequenceEditorPreviewChangeRendererMode(mode); }

    private:
        Editor& m_editor;
    };

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

    static std::vector<std::string> ListSceneIds()
    {
        std::vector<std::string> ids;
        const fs::path dir = pathutils::AssetsDir() / "sequences" / "scenes";
        std::error_code ec;
        if (!fs::exists(dir, ec))
            return ids;
        for (const auto& entry : fs::directory_iterator(dir, ec))
        {
            if (ec)
                break;
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                ids.push_back(entry.path().stem().string());
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    static bool SceneExists(const std::string& id)
    {
        if (id.empty())
            return false;
        fs::path p(id);
        if (p.is_absolute())
            return fs::exists(p);
        if (p.has_parent_path())
            return fs::exists(pathutils::ProjectRoot() / p);
        if (p.extension().empty())
            p.replace_extension(".json");
        return fs::exists(pathutils::AssetsDir() / "sequences" / "scenes" / p);
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

    static bool ActionHasDuration(const sequence::ActionDef& action)
    {
        return action.type == "wait" ||
            action.type == "move_actor" ||
            action.type == "camera_move" ||
            action.type == "fade_in" ||
            action.type == "fade_out" ||
            action.type == "screen_flash";
    }

    static ImU32 SeverityColor(bool hasErrors, bool hasWarnings)
    {
        if (hasErrors) return IM_COL32(210, 65, 55, 255);
        if (hasWarnings) return IM_COL32(225, 170, 55, 255);
        return IM_COL32(70, 180, 95, 255);
    }

    static std::string TitleFromId(const std::string& id)
    {
        std::string out = id;
        bool cap = true;
        for (char& c : out)
        {
            if (c == '_' || c == '-')
            {
                c = ' ';
                cap = true;
                continue;
            }
            if (cap && c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
            cap = false;
        }
        return out;
    }

    static fs::path ResolveSequenceScenePath(const std::string& sceneIdOrPath)
    {
        fs::path p(sceneIdOrPath);
        if (p.is_absolute())
            return p;
        if (p.has_parent_path())
            return pathutils::ProjectRoot() / p;
        if (p.extension().empty())
            p.replace_extension(".json");
        return pathutils::AssetsDir() / "sequences" / "scenes" / p;
    }

    static bool ContainsId(const std::vector<std::string>& ids, const std::string& id)
    {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }

    static bool DrawStringCombo(const char* label, std::string& value, const std::vector<std::string>& ids)
    {
        bool changed = false;
        if (ImGui::BeginCombo(label, value.empty() ? "<none>" : value.c_str()))
        {
            for (const auto& id : ids)
            {
                if (ImGui::Selectable(id.c_str(), id == value))
                {
                    value = id;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    struct SequenceUiRect
    {
        ImVec2 min;
        ImVec2 max;

        ImVec2 size() const
        {
            return ImVec2(max.x - min.x, max.y - min.y);
        }
    };
}

void Editor::sequenceEditorPreviewLog(const std::string& message)
{
    m_lastIoStatus = message;
}

bool Editor::sequenceEditorPreviewHasFlag(const std::string& flag) const
{
    const auto it = m_sequencePreviewFlags.find(flag);
    return it != m_sequencePreviewFlags.end() && it->second;
}

void Editor::sequenceEditorPreviewSetFlag(const std::string& flag)
{
    if (!flag.empty())
        m_sequencePreviewFlags[flag] = true;
}

bool Editor::sequenceEditorPreviewLoadScene(const std::string& sceneId)
{
    if (!loadSequenceSceneMetadata(sceneId)) return false;
    const auto document = sequencePreviewSceneDocument();
    m_sequencePreviewSceneLoaded = m_sequencePreviewScene.loadScene(sceneId, [&](const std::string& msg)
    {
        m_lastIoStatus = msg;
    }, &document);
    return m_sequencePreviewSceneLoaded;
}

bool Editor::sequenceEditorPreviewResolveMarker(const std::string& markerId, sequence::Vec2& out) const
{
    if (m_sequencePreviewScene.resolveMarker(markerId, out))
        return true;

    for (const auto& marker : m_sequenceSceneMarkers)
    {
        if (marker.id == markerId)
        {
            out.x = marker.x;
            out.y = marker.y;
            return true;
        }
    }

    for (const auto& marker : m_sequenceMarkers)
    {
        if (marker.id == markerId)
        {
            out.x = marker.x;
            out.y = marker.y;
            return true;
        }
    }

    return false;
}

bool Editor::sequenceEditorPreviewSpawnActor(const sequence::ActorSpawnDef& def)
{
    if (def.kind == "prop")
        return m_sequencePreviewScene.upsertProp(def, [&](const std::string& msg)
        {
            m_lastIoStatus = msg;
        });

    return m_sequencePreviewScene.upsertActor(def, [&](const std::string& msg)
    {
        m_lastIoStatus = msg;
    });
}

void Editor::sequenceEditorPreviewRemoveActor(const std::string& actorId)
{
    m_sequencePreviewScene.removeActorOrProp(actorId);
}

void Editor::sequenceEditorPreviewSetActorVisible(const std::string& actorId, bool visible)
{
    m_sequencePreviewScene.setActorVisible(actorId, visible);
}

bool Editor::sequenceEditorPreviewGetActorPosition(const std::string& actorId, sequence::Vec2& out) const
{
    return m_sequencePreviewScene.getActorPosition(actorId, out);
}

bool Editor::sequenceEditorPreviewSetActorPosition(const std::string& actorId, sequence::Vec2 pos)
{
    return m_sequencePreviewScene.setActorPosition(actorId, pos);
}

void Editor::sequenceEditorPreviewSetActorAnimation(const std::string& actorId, const std::string& animationId)
{
    m_sequencePreviewScene.setActorAnimation(actorId, animationId, [&](const std::string& msg)
    {
        m_lastIoStatus = msg;
    });
}

void Editor::sequenceEditorPreviewFaceActor(const std::string& actorId, const std::string& directionOrTarget)
{
    m_sequencePreviewScene.faceActor(actorId, directionOrTarget);
}

void Editor::sequenceEditorPreviewSetCameraPosition(sequence::Vec2 pos)
{
    m_sequencePreviewScene.setCameraPosition(pos);
}

void Editor::sequenceEditorPreviewSetCameraFollow(const std::string& actorId, const sequence::CameraFollowSettings& settings)
{
    m_sequencePreviewScene.setCameraFollow(actorId, settings);
}

void Editor::sequenceEditorPreviewSetCameraShake(float intensity, float duration, float frequency)
{
    m_sequencePreviewScene.setCameraShake(intensity, duration, frequency);
}

void Editor::sequenceEditorPreviewSetFadeAlpha(float alpha)
{
    m_sequencePreviewFadeAlpha = std::clamp(alpha, 0.0f, 1.0f);
}

void Editor::sequenceEditorPreviewSetFlashAlpha(float alpha)
{
    m_sequencePreviewFlashAlpha = std::clamp(alpha, 0.0f, 1.0f);
}

void Editor::sequenceEditorPreviewShowText(const std::string& text, float duration)
{
    m_sequencePreviewText = text;
    m_sequencePreviewTextRemaining = duration;
}

void Editor::sequenceEditorPreviewHideText()
{
    m_sequencePreviewText.clear();
    m_sequencePreviewTextRemaining = 0.0f;
}

void Editor::sequenceEditorPreviewSetTimeOfDay(const std::string& value)
{
    m_sequencePreviewTimeOfDay = value;
}

void Editor::sequenceEditorPreviewSetWeather(const nlohmann::json& data)
{
    m_sequencePreviewWeather = data;
}

bool Editor::sequenceEditorPreviewLoadMap(const std::string& mapIdOrPath, const std::string& spawnId)
{
    (void)spawnId;

    fs::path path = mapIdOrPath;
    if (!path.is_absolute())
        path = path.has_parent_path() ? (pathutils::ProjectRoot() / path) : (pathutils::MapsDir() / path);
    if (path.extension().empty())
        path.replace_extension(".rvm");

    return loadMap(path.string().c_str());
}

void Editor::sequenceEditorPreviewTeleportPlayer(sequence::Vec2 pos)
{
    m_camX = static_cast<int>(pos.x);
    m_camY = static_cast<int>(pos.y);
}

void Editor::sequenceEditorPreviewEnablePlayerControl(bool enabled)
{
    sequenceEditorPreviewLog(std::string("PREVIEW: player control ") + (enabled ? "enabled" : "disabled"));
}

void Editor::sequenceEditorPreviewChangeRendererMode(const std::string& mode)
{
    sequenceEditorPreviewLog("PREVIEW: change_renderer_mode " + mode);
}

bool Editor::loadSequences()
{
    m_sequenceSceneDrafts.clear();
    m_sequenceSceneDocument = nullptr;
    m_sequenceEditingSceneId.clear();
    m_sequenceEditingSceneOwner.clear();
    stopSequencePreview();
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
    if (index == m_selectedSequenceIndex)
        m_sequenceEditorDirty = false;
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
            if (action.type == "load_scene")
            {
                const std::string scene = action.data.value("scene", action.data.value("id", ""));
                if (!SceneExists(scene))
                    warnings.push_back("unknown scene: " + scene);
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
        std::string actor = action.data.value("actor", "");
        if (DrawStringCombo("Actor", actor, selectedSequenceActorIds()))
            action.data["actor"] = actor;
        EditJsonString("Actor fallback", action.data, "actor");
        EditJsonString("Kind", action.data, "kind");
        EditJsonString("Ref/NPC", action.data, "ref");
        EditJsonString("Sprite/Object", action.data, "sprite");
        renderSequenceCharacterPicker(action.data);
        std::string marker = action.data.value("marker", "");
        if (DrawStringCombo("Marker", marker, selectedSequenceMarkerIds()))
            action.data["marker"] = marker;
        EditJsonString("Marker fallback", action.data, "marker");
        EditJsonFloat("X", action.data, "x", 0.0f);
        EditJsonFloat("Y", action.data, "y", 0.0f);
        EditJsonFloat("Scale", action.data, "scale", 1.0f);
    }
    else if (action.type == "move_actor")
    {
        std::string actor = action.data.value("actor", "");
        if (DrawStringCombo("Actor", actor, selectedSequenceActorIds()))
            action.data["actor"] = actor;
        EditJsonString("Actor fallback", action.data, "actor");
        std::string marker = action.data.value("target_marker", "");
        if (DrawStringCombo("Target", marker, selectedSequenceMarkerIds()))
            action.data["target_marker"] = marker;
        EditJsonString("Target fallback", action.data, "target_marker");
        EditJsonFloat("Target X", action.data, "x", 0.0f);
        EditJsonFloat("Target Y", action.data, "y", 0.0f);
        renderSequenceMotionEasing(action.data);
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
    else if (action.type == "set_camera" || action.type == "camera_move" || action.type == "camera_follow")
    {
        renderSequenceCameraAction(action);
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
        const std::string currentAudio = action.data.value("id", action.data.value("path", std::string()));
        if (ImGui::BeginCombo("Audio soubor", currentAudio.empty() ? "<vyberte zvuk>" : currentAudio.c_str()))
        {
            for (const auto& file : sequence::ListAudioFiles(pathutils::ProjectRoot()))
                if (ImGui::Selectable(file.c_str(), file == currentAudio)) action.data["id"] = file;
            ImGui::EndCombo();
        }
        if (ImGui::Button("Audio knihovna / Import"))
        {
            m_sequenceAudioFiles = sequence::ListAudioFiles(pathutils::ProjectRoot());
            m_sequenceAudioLibraryOpen = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Poslechnout")) auditionSequenceAudio(currentAudio);
        if (action.type == "play_music")
        {
            int loops = action.data.value("loops", -1);
            int fade = action.data.value("fade_ms", 500);
            if (ImGui::InputInt("Opakování (-1 = stále)", &loops)) action.data["loops"] = std::max(-1, loops);
            if (ImGui::InputInt("Náběh (ms)", &fade)) action.data["fade_ms"] = std::max(0, fade);
        }
        renderSequenceAudioAction(action);
    }
    else if (action.type == "set_music_volume" || action.type == "set_sfx_volume" ||
        action.type == "duck_music" || action.type == "restore_music" || action.type == "stop_music")
    {
        renderSequenceAudioAction(action);
    }
    else if (action.type == "pickup_item")
    {
        EditJsonString("Předmět (ID inventáře)", action.data, "item");
        int count = action.data.value("count", 1);
        if (ImGui::InputInt("Počet", &count)) action.data["count"] = std::max(1, count);
        std::string prop = action.data.value("prop", "");
        if (DrawStringCombo("Rekvizita", prop, selectedSequencePropIds())) action.data["prop"] = prop;
        EditJsonString("ID objektu v interiéru / rekvizity", action.data, "prop");
        EditJsonString("Příznak jednorázového sebrání", action.data, "once_flag");
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
    else if (action.type == "load_scene")
    {
        std::string scene = action.data.value("scene", "");
        if (ImGui::BeginCombo("Scene", scene.empty() ? "<none>" : scene.c_str()))
        {
            for (const auto& id : ListSceneIds())
            {
                if (ImGui::Selectable(id.c_str(), id == scene))
                {
                    scene = id;
                    action.data["scene"] = scene;
                }
            }
            ImGui::EndCombo();
        }
        EditJsonString("Scene id/path", action.data, "scene");
    }
    else if (action.type == "show_prop" || action.type == "hide_prop")
    {
        std::string prop = action.data.value("prop", "");
        if (DrawStringCombo("Prop", prop, selectedSequencePropIds()))
            action.data["prop"] = prop;
        EditJsonString("Prop fallback", action.data, "prop");
    }
    else if (action.type == "show_actor" || action.type == "hide_actor")
    {
        std::string actor = action.data.value("actor", "");
        if (DrawStringCombo("Actor", actor, selectedSequenceActorIds()))
            action.data["actor"] = actor;
        EditJsonString("Actor fallback", action.data, "actor");
    }
    else if (action.type == "teleport_player")
    {
        std::string marker = action.data.value("marker", "");
        if (DrawStringCombo("Marker", marker, selectedSequenceMarkerIds()))
            action.data["marker"] = marker;
        EditJsonString("Marker fallback", action.data, "marker");
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

    const bool supportsMarker = action.type == "camera_move" || action.type == "set_camera" ||
        action.type == "move_actor" || action.type == "spawn_actor" || action.type == "teleport_player";
    ImGui::BeginDisabled(!supportsMarker);
    if (ImGui::Button("Pick target in preview"))
    {
        const std::string existing = action.data.value("target_marker", action.data.value("marker", ""));
        std::strncpy(m_sequenceMarkerId, existing.empty() ? "marker" : existing.c_str(), sizeof(m_sequenceMarkerId) - 1);
        m_sequenceMarkerId[sizeof(m_sequenceMarkerId) - 1] = '\0';
        m_sequencePickMarkerMode = true;
        m_sequencePickActionIndex = actionIndex;
        m_sequencePickNodeIndex = m_selectedSequenceNodeIndex;
        m_sequencePickSequenceIndex = m_selectedSequenceIndex;
        m_sequencePickActionPath = m_sequenceTimelineSelection;
        if (m_sequencePickActionPath.empty() || m_sequencePickActionPath.front() != actionIndex)
            m_sequencePickActionPath = {actionIndex};
        m_lastIoStatus = "Klikněte na marker nebo na povrch scény pro vytvoření cíle.";
    }
    ImGui::SameLine();
    ImGui::InputText("Marker id", m_sequenceMarkerId, sizeof(m_sequenceMarkerId));
    ImGui::EndDisabled();

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
        playSelectedSequencePreview(false);
    ImGui::SameLine();
    if (ImGui::Button("Stop Preview"))
        stopSequencePreview();
    ImGui::SameLine();
    if (ImGui::Button("Restart Preview"))
        restartSequencePreview(false);
    if (ImGui::Button("Step Action"))
        stepSequencePreview();
    ImGui::SameLine();
    ImGui::Text("Preview timer: %.2f", m_sequencePreviewTimer);

    ImGui::Separator();
    if (ImGui::Button("Add Node"))
    {
        pushSequenceUndo("add sequence node");
        sequence::NodeDef node;
        node.id = UniqueNodeId(seq, "node_" + std::to_string(seq.nodes.size() + 1));
        if (seq.start.empty()) seq.start = node.id;
        seq.nodes.push_back(std::move(node));
        m_selectedSequenceNodeIndex = (int)seq.nodes.size() - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate Node") && m_selectedSequenceNodeIndex >= 0)
    {
        pushSequenceUndo("duplicate sequence node");
        auto copy = seq.nodes[m_selectedSequenceNodeIndex];
        copy.id = UniqueNodeId(seq, copy.id + "_copy");
        seq.nodes.push_back(std::move(copy));
        m_selectedSequenceNodeIndex = (int)seq.nodes.size() - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Node")) deleteSelectedSequenceNode();

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
            if (m_sequencePreviewCampaign && m_sequencePreviewCampaign->sequenceStatus().nodeId == node.id &&
                m_sequencePreviewCampaign->sequenceStatus().actionIndex == i)
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
        auto* editedAction = &node.actions[m_selectedSequenceActionIndex];
        if (m_sequenceTimelineSequence == m_selectedSequenceIndex && m_sequenceTimelineNode == m_selectedSequenceNodeIndex &&
            !m_sequenceTimelineSelection.empty() && m_sequenceTimelineSelection.front() == m_selectedSequenceActionIndex)
            if (auto* child = sequence::FindTimelineAction(node, m_sequenceTimelineSelection)) editedAction = child;
        renderSequenceActionEditor(*editedAction, m_selectedSequenceActionIndex);
    }

    ImGui::Columns(1);
}

void Editor::resetSequencePreviewState()
{
    stopSequencePreview();
    m_sequencePreviewTimer = 0.0f;
}

bool Editor::playSelectedSequencePreview(bool fromSelectedNode)
{
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= (int)m_sequences.size())
        return false;

    resetSequencePreviewState();
    m_sequenceAudioPlayer.stopSfxChannel(30);
    m_sequencePreviewNodeTimer = 0.0f;
    const auto& seq = m_sequences[m_selectedSequenceIndex];
    std::string nodeId;
    if (fromSelectedNode && m_selectedSequenceNodeIndex >= 0 && m_selectedSequenceNodeIndex < (int)seq.nodes.size())
        nodeId = seq.nodes[m_selectedSequenceNodeIndex].id;
    else
    {
        m_selectedSequenceNodeIndex = findSequenceNodeIndexById(seq, seq.start);
        m_selectedSequenceActionIndex = -1;
    }

    m_sequencePreviewCampaign = std::make_unique<Campaign>();
    if (!m_sequencePreviewCampaign->init(m_window, m_renderer) ||
        !m_sequencePreviewCampaign->startSequencePreview(seq, nodeId, selectedSequenceSceneId(), -1.0f, sequencePreviewSceneDocument(), sequenceSceneDrafts()))
    {
        stopSequencePreview();
        m_lastIoStatus = "Cannot start sequence preview: " + seq.id;
        return false;
    }
    m_sequencePreviewNodeId = m_sequencePreviewCampaign->sequenceStatus().nodeId;
    m_sequencePreviewActive = m_sequencePreviewCampaign->sequenceStatus().running;
    m_sequenceEditorOpen = true;
    m_lastIoStatus = "Playing preview: " + seq.id;
    return true;
}

void Editor::stopSequencePreview()
{
    if (m_sequencePreviewCampaign)
    {
        m_sequencePreviewCampaign->shutdown();
        m_sequencePreviewCampaign.reset();
    }
    m_sequencePreviewActive = false;
    m_sequencePreviewPaused = false;
}

void Editor::restartSequencePreview(bool fromSelectedNode)
{
    playSelectedSequencePreview(fromSelectedNode);
}

void Editor::pauseSequencePreview(bool paused)
{
    if (!m_sequencePreviewCampaign)
        return;
    m_sequencePreviewCampaign->pauseSequencePreview(paused);
    m_sequencePreviewPaused = paused;
}

void Editor::stepSequencePreview()
{
    if (!m_sequencePreviewActive && !playSelectedSequencePreview(true))
        return;
    m_sequencePreviewCampaign->stepSequencePreview();
    const auto status = m_sequencePreviewCampaign->sequenceStatus();
    m_sequencePreviewActive = status.running;
    m_sequencePreviewPaused = status.paused;
    m_sequencePreviewTimer = status.elapsed;
}

void Editor::updateSequencePreview(float dt)
{
    if (!m_sequencePreviewActive || m_sequencePreviewPaused || !m_sequencePreviewCampaign)
        return;
    m_sequencePreviewCampaign->updateSequencePreview(dt);
    const auto status = m_sequencePreviewCampaign->sequenceStatus();
    m_sequencePreviewNodeTimer = status.nodeId == m_sequencePreviewNodeId ? m_sequencePreviewNodeTimer + dt : 0.0f;
    m_sequencePreviewNodeId = status.nodeId;
    m_sequencePreviewTimer = status.elapsed;
    m_sequencePreviewActive = status.running;
    m_sequencePreviewPaused = status.paused;
    if (status.completed)
        m_lastIoStatus = "Preview finished: " + status.sequenceId;
}

void Editor::markSequenceDirty(const char* undoLabel)
{
    if (undoLabel && undoLabel[0] != '\0')
        pushSequenceUndo(undoLabel);
    m_sequenceEditorDirty = true;
}

std::string Editor::sequenceLayoutPathForId(const std::string& id) const
{
    return (pathutils::AssetsDir() / "sequences" / "editor" / (id + ".layout.json")).string();
}

bool Editor::loadSelectedSequenceLayout()
{
    m_sequenceGraphLayout.clear();
    m_sequenceGraphLinkSource = -1;
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= (int)m_sequences.size())
        return false;

    const auto& seq = m_sequences[m_selectedSequenceIndex];
    nlohmann::json root;
    std::string err;
    const std::string path = sequenceLayoutPathForId(seq.id);
    if (fs::exists(path) && jsonutils::LoadJsonFileSafe(path, root, err) && root.contains("nodes"))
    {
        for (auto it = root["nodes"].begin(); it != root["nodes"].end(); ++it)
        {
            SequenceGraphNodeLayout layout;
            layout.x = it.value().value("x", 0.0f);
            layout.y = it.value().value("y", 0.0f);
            m_sequenceGraphLayout[it.key()] = layout;
        }
    }

    for (int i = 0; i < (int)seq.nodes.size(); ++i)
    {
        auto& layout = m_sequenceGraphLayout[seq.nodes[i].id];
        if (layout.x == 0.0f && layout.y == 0.0f)
        {
            layout.x = 40.0f;
            layout.y = 40.0f + i * 112.0f;
        }
    }
    m_sequenceLayoutDirty = false;
    return true;
}

bool Editor::saveSelectedSequenceLayout()
{
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= (int)m_sequences.size())
        return false;

    const auto& seq = m_sequences[m_selectedSequenceIndex];
    nlohmann::json root;
    root["sequence"] = seq.id;
    root["nodes"] = nlohmann::json::object();
    for (const auto& node : seq.nodes)
    {
        const auto it = m_sequenceGraphLayout.find(node.id);
        if (it == m_sequenceGraphLayout.end())
            continue;
        root["nodes"][node.id] = {{"x", it->second.x}, {"y", it->second.y}};
    }

    try
    {
        const fs::path path = sequenceLayoutPathForId(seq.id);
        fs::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        if (!f)
        {
            m_lastIoStatus = "Cannot write sequence layout: " + path.string();
            return false;
        }
        f << root.dump(2) << "\n";
        m_sequenceLayoutDirty = false;
        return true;
    }
    catch (const std::exception& e)
    {
        m_lastIoStatus = std::string("Sequence layout save failed: ") + e.what();
        return false;
    }
}

std::string Editor::selectedSequenceSceneId() const
{
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= (int)m_sequences.size())
        return {};
    const auto& seq = m_sequences[m_selectedSequenceIndex];
    if (m_sequenceEditingSceneOwner == seq.id && !m_sequenceEditingSceneId.empty()) return m_sequenceEditingSceneId;
    for (const auto& node : seq.nodes)
    {
        for (const auto& action : node.actions)
        {
            if (action.type == "load_scene")
                return action.data.value("scene", action.data.value("id", std::string()));
        }
    }
    return {};
}

std::vector<std::string> Editor::selectedSequenceActorIds() const
{
    std::vector<std::string> ids;
    if (m_sequenceSceneDocument.is_object() && m_sequenceSceneDocument.contains("actors"))
        for (const auto& item : m_sequenceSceneDocument["actors"])
            if (item.is_object()) ids.push_back(item.value("id", std::string()));
    if (m_selectedSequenceIndex >= 0 && m_selectedSequenceIndex < (int)m_sequences.size())
    {
        for (const auto& node : m_sequences[m_selectedSequenceIndex].nodes)
        {
            for (const auto& action : node.actions)
            {
                const std::string actor = action.data.value("actor", std::string());
                if (!actor.empty() && !ContainsId(ids, actor))
                    ids.push_back(actor);
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> Editor::selectedSequencePropIds() const
{
    std::vector<std::string> ids;
    if (m_sequenceSceneDocument.is_object() && m_sequenceSceneDocument.contains("props"))
        for (const auto& item : m_sequenceSceneDocument["props"])
            if (item.is_object()) ids.push_back(item.value("id", std::string()));
    if (m_selectedSequenceIndex >= 0 && m_selectedSequenceIndex < (int)m_sequences.size())
    {
        for (const auto& node : m_sequences[m_selectedSequenceIndex].nodes)
        {
            for (const auto& action : node.actions)
            {
                const std::string prop = action.data.value("prop", std::string());
                if (!prop.empty() && !ContainsId(ids, prop))
                    ids.push_back(prop);
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> Editor::selectedSequenceMarkerIds() const
{
    std::vector<std::string> ids;
    for (const auto& marker : m_sequenceSceneMarkers)
        ids.push_back(marker.id);
    for (const auto& marker : m_sequenceMarkers)
    {
        if (!ContainsId(ids, marker.id))
            ids.push_back(marker.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool Editor::loadSequenceSceneMetadata(const std::string& sceneId)
{
    if (!m_sequenceEditingSceneId.empty() && m_sequenceSceneDocument.is_object())
        m_sequenceSceneDrafts[m_sequenceEditingSceneId] = sequencePreviewSceneDocument();
    m_sequenceSceneMarkers.clear();
    m_sequenceSceneDocument = nullptr;
    m_selectedSequenceMarkerIndex = -1;
    m_selectedSequenceEntityIndex = -1;
    m_sequenceDraggingMarker = -1;
    m_sequenceSceneActors.clear();
    m_sequenceSceneProps.clear();
    if (sceneId.empty())
        return false;

    nlohmann::json root;
    std::string err;
    const fs::path path = ResolveSequenceScenePath(sceneId);
    const auto draft = m_sequenceSceneDrafts.find(sceneId);
    if (draft != m_sequenceSceneDrafts.end()) root = draft->second;
    else if (!jsonutils::LoadJsonFileSafe(path.string(), root, err))
    {
        m_lastIoStatus = "Scene metadata load failed: " + err;
        return false;
    }

    // Legacy camera points are identified by their use, so an ordinary marker
    // with "camera" in its name keeps its original meaning.
    std::unordered_set<std::string> cameraMarkers;
    const auto camera = root.value("camera", nlohmann::json::object());
    cameraMarkers.insert(camera.value("marker", std::string()));
    if (m_selectedSequenceIndex >= 0 && m_selectedSequenceIndex < static_cast<int>(m_sequences.size()))
    {
        const auto& definition = m_sequences[m_selectedSequenceIndex];
        auto collect = [&](auto&& self, const std::vector<sequence::ActionDef>& actions) -> void
        {
            for (const auto& action : actions)
            {
                if (action.type == "set_camera") cameraMarkers.insert(action.data.value("marker", std::string()));
                if (action.type == "camera_move") cameraMarkers.insert(action.data.value("target_marker", std::string()));
                self(self, action.children);
            }
        };
        for (const auto& node : definition.nodes) collect(collect, node.actions);
        collect(collect, definition.onSkipActions);
    }

    if (root.contains("markers") && root["markers"].is_array())
    {
        for (const auto& jm : root["markers"])
        {
            if (!jm.is_object())
                continue;
            auto marker = sequence::MarkerFromJson(jm);
            if (!jm.contains("type") && cameraMarkers.contains(marker.id))
            {
                marker.type = "camera";
                marker.z = jm.value("z", camera.value("height", 0.56f));
                marker.angleDegrees = jm.value("angle_degrees", camera.value("angle_degrees", camera.value("angle", 0.0f)));
                marker.pitch = jm.value("pitch", camera.value("pitch", 0.0f));
            }
            if (!marker.id.empty())
                m_sequenceSceneMarkers.push_back(std::move(marker));
        }
    }

    auto collectIds = [&](const char* key, std::vector<std::string>& out)
    {
        if (!root.contains(key) || !root[key].is_array())
            return;
        for (const auto& item : root[key])
        {
            if (!item.is_object())
                continue;
            const std::string id = item.value("id", "");
            if (!id.empty())
                out.push_back(id);
        }
    };
    collectIds("actors", m_sequenceSceneActors);
    collectIds("props", m_sequenceSceneProps);
    m_sequenceSceneDocument = root;
    m_sequenceEditingSceneId = sceneId;
    m_sequenceEditingSceneOwner = m_selectedSequenceIndex >= 0 ? m_sequences[m_selectedSequenceIndex].id : std::string();
    m_sequenceDraggingImageLayer = -1;
    m_sequenceMarkerTool = 0;
    m_sequencePickMarkerMode = false;
    return true;
}

bool Editor::saveSelectedSequenceSceneMetadata()
{
    const auto drafts = sequenceSceneDrafts();
    for (const auto& [id, root] : drafts)
    {
        const auto path = ResolveSequenceScenePath(id);
        std::ofstream file(path, std::ios::binary);
        if (!file || !(file << root.dump(2) << "\n"))
        {
            m_lastIoStatus = "Cannot write scene metadata: " + path.string();
            m_sequenceEditorDirty = true;
            return false;
        }
    }
    m_sequenceSceneDrafts.clear();
    return true;
}

bool Editor::loadSequencePreviewSceneForSelection()
{
    stopSequencePreview();
    const std::string sceneId = selectedSequenceSceneId();
    if (sceneId.empty())
    {
        m_sequencePreviewSceneLoaded = false;
        loadSequenceSceneMetadata({});
        m_sequenceEditingSceneId.clear();
        m_sequenceEditingSceneOwner.clear();
        return false;
    }

    if (!loadSequenceSceneMetadata(sceneId)) return false;
    const auto document = sequencePreviewSceneDocument();
    m_sequencePreviewSceneLoaded = m_sequencePreviewScene.loadScene(sceneId, [&](const std::string& msg)
    {
        m_lastIoStatus = msg;
    }, &document);
    if (m_sequencePreviewSceneLoaded) syncSequenceMarkerScene();
    return m_sequencePreviewSceneLoaded;
}

void Editor::validateSelectedSequence()
{
    m_sequenceValidationIssues.clear();
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= (int)m_sequences.size())
        return;

    const auto& seq = m_sequences[m_selectedSequenceIndex];
    auto addIssue = [&](SequenceValidationIssue::Severity severity, std::string message, int nodeIndex, int actionIndex)
    {
        SequenceValidationIssue issue;
        issue.severity = severity;
        issue.message = std::move(message);
        issue.nodeIndex = nodeIndex;
        issue.actionIndex = actionIndex;
        m_sequenceValidationIssues.push_back(std::move(issue));
    };

    std::unordered_set<std::string> nodeIds;
    if (seq.id.empty())
        addIssue(SequenceValidationIssue::Severity::Error, "Missing sequence id", -1, -1);
    if (seq.start.empty() || findSequenceNodeIndexById(seq, seq.start) < 0)
        addIssue(SequenceValidationIssue::Severity::Error, "Missing start node: " + seq.start, -1, -1);

    const auto markerIds = selectedSequenceMarkerIds();
    for (int n = 0; n < (int)seq.nodes.size(); ++n)
    {
        const auto& node = seq.nodes[n];
        if (node.id.empty())
            addIssue(SequenceValidationIssue::Severity::Error, "Node id is empty", n, -1);
        else if (!nodeIds.insert(node.id).second)
            addIssue(SequenceValidationIssue::Severity::Error, "Duplicate node id: " + node.id, n, -1);
        if (!node.next.empty() && findSequenceNodeIndexById(seq, node.next) < 0)
            addIssue(SequenceValidationIssue::Severity::Error, node.id + " -> missing next node: " + node.next, n, -1);

        for (int a = 0; a < (int)node.actions.size(); ++a)
        {
            const auto& action = node.actions[a];
            if (!sequence::IsKnownActionType(action.type))
                addIssue(SequenceValidationIssue::Severity::Warning, node.id + " / action " + std::to_string(a + 1) + ": Unknown action type " + action.type, n, a);
            const std::string marker = action.data.value("target_marker", action.data.value("marker", std::string()));
            if (!marker.empty() && !ContainsId(markerIds, marker))
                addIssue(SequenceValidationIssue::Severity::Warning, node.id + " / action " + std::to_string(a + 1) + ": Unknown marker " + marker, n, a);
            if (action.type == "load_scene")
            {
                const std::string scene = action.data.value("scene", action.data.value("id", std::string()));
                if (!SceneExists(scene))
                    addIssue(SequenceValidationIssue::Severity::Warning, node.id + " / action " + std::to_string(a + 1) + ": Unknown scene " + scene, n, a);
            }
            if (action.type == "goto")
            {
                const std::string target = action.data.value("target", action.data.value("node", std::string()));
                if (!target.empty() && findSequenceNodeIndexById(seq, target) < 0)
                    addIssue(SequenceValidationIssue::Severity::Error, node.id + " / action " + std::to_string(a + 1) + ": Missing goto target " + target, n, a);
            }
        }
    }
}

void Editor::openSequenceEditor()
{
    if (m_sequenceEditorOpen)
        return;

    m_sequenceEditorOpen = true;
    m_activeTab = EditorTab::Sequences;
    m_npcPlacementMode = false;
    m_foragePlacementMode = false;
    if (m_selectedSequenceIndex < 0 && !m_sequences.empty())
    {
        const int introIndex = findSequenceIndexById("intro_houska");
        m_selectedSequenceIndex = introIndex >= 0 ? introIndex : 0;
    }
    if (m_selectedSequenceIndex >= 0 && m_selectedSequenceIndex < (int)m_sequences.size())
    {
        const auto& seq = m_sequences[m_selectedSequenceIndex];
        m_selectedSequenceNodeIndex = findSequenceNodeIndexById(seq, seq.start);
        if (m_selectedSequenceNodeIndex < 0 && !seq.nodes.empty())
            m_selectedSequenceNodeIndex = 0;
        m_selectedSequenceActionIndex = -1;
        m_sequenceSelectionKind = SequenceEditorSelectionKind::Sequence;
        loadSelectedSequenceLayout();
        loadSequencePreviewSceneForSelection();
        validateSelectedSequence();
    }
}

void Editor::deleteSelectedSequenceNode()
{
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= static_cast<int>(m_sequences.size())) return;
    auto& definition = m_sequences[m_selectedSequenceIndex];
    if (m_selectedSequenceNodeIndex < 0 || m_selectedSequenceNodeIndex >= static_cast<int>(definition.nodes.size())) return;
    stopSequencePreview();
    markSequenceDirty("delete sequence node");
    const auto id = definition.nodes[m_selectedSequenceNodeIndex].id;
    sequence::DeleteNode(definition, id);
    m_sequenceGraphLayout.erase(id);
    m_sequenceGraphLinkSource = -1;
    m_sequenceDraggingNodeIndex = -1;
    m_selectedSequenceNodeIndex = std::min(m_selectedSequenceNodeIndex, static_cast<int>(definition.nodes.size()) - 1);
    m_selectedSequenceActionIndex = -1;
    m_sequenceSelectionKind = definition.nodes.empty() ? SequenceEditorSelectionKind::Sequence : SequenceEditorSelectionKind::Node;
    m_sequenceTimelineSelection.clear();
    m_sequenceTimelineDragPath.clear();
    m_sequenceTimelineNode = -1;
    m_sequenceTimelineSequence = -1;
    m_sequenceTimelineCursor = 0;
    m_sequenceTimelineScrubbing = false;
    m_sequencePickMarkerMode = false;
    m_sequencePickActionPath.clear();
    m_sequenceLayoutDirty = true;
    validateSelectedSequence();
    m_lastIoStatus = "Smazán uzel: " + id + ". Ctrl+Z vrátí změnu.";
}

void Editor::handleSequenceEditorShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || m_sequenceAudioLibraryOpen || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        return;

    const bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        saveSelectedSequence();
        saveSelectedSequenceLayout();
        saveSelectedSequenceSceneMetadata();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z))
    {
        undoLastStep();
        m_sequenceEditorDirty = true;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y))
    {
        redoLastStep();
        m_sequenceEditorDirty = true;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D) && m_selectedSequenceIndex >= 0)
    {
        auto& seq = m_sequences[m_selectedSequenceIndex];
        if (m_sequenceSelectionKind == SequenceEditorSelectionKind::Action &&
            m_selectedSequenceNodeIndex >= 0 && m_selectedSequenceNodeIndex < (int)seq.nodes.size() &&
            m_selectedSequenceActionIndex >= 0 && m_selectedSequenceActionIndex < (int)seq.nodes[m_selectedSequenceNodeIndex].actions.size())
        {
            markSequenceDirty("duplicate sequence action");
            auto& node = seq.nodes[m_selectedSequenceNodeIndex];
            auto path = m_sequenceTimelineSelection;
            if (path.empty() || path.front() != m_selectedSequenceActionIndex ||
                m_sequenceTimelineNode != m_selectedSequenceNodeIndex || !sequence::FindTimelineAction(node, path))
                path = {m_selectedSequenceActionIndex};
            const int index = path.back();
            path.pop_back();
            auto* parent = sequence::FindTimelineAction(node, path);
            auto& actions = parent ? parent->children : node.actions;
            const auto copy = actions[index];
            actions.insert(actions.begin() + index + 1, copy);
            path.push_back(index + 1);
            m_sequenceTimelineSelection = path;
            m_selectedSequenceActionIndex = path.front();
        }
        else if (m_sequenceSelectionKind == SequenceEditorSelectionKind::Node &&
                 m_selectedSequenceNodeIndex >= 0 && m_selectedSequenceNodeIndex < (int)seq.nodes.size())
        {
            markSequenceDirty("duplicate sequence node");
            auto copy = seq.nodes[m_selectedSequenceNodeIndex];
            copy.id = UniqueNodeId(seq, copy.id + "_copy");
            seq.nodes.push_back(std::move(copy));
            m_selectedSequenceNodeIndex = (int)seq.nodes.size() - 1;
            m_sequenceGraphLayout[seq.nodes.back().id] = {80.0f, 80.0f + m_selectedSequenceNodeIndex * 112.0f};
            m_sequenceLayoutDirty = true;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space))
    {
        if (!m_sequencePreviewActive)
            playSelectedSequencePreview(false);
        else
            pauseSequencePreview(!m_sequencePreviewPaused);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        m_sequenceMarkerTool = 0;
        m_sequencePickMarkerMode = false;
        m_sequenceDraggingMarker = -1;
        stopSequencePreview();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F) &&
        m_selectedSequenceIndex >= 0 && m_selectedSequenceIndex < (int)m_sequences.size() &&
        m_selectedSequenceNodeIndex >= 0 && m_selectedSequenceNodeIndex < (int)m_sequences[m_selectedSequenceIndex].nodes.size())
    {
        auto& seq = m_sequences[m_selectedSequenceIndex];
        m_sequenceGraphLayout[seq.nodes[m_selectedSequenceNodeIndex].id] = {60.0f, 54.0f};
        m_sequenceLayoutDirty = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_selectedSequenceIndex >= 0)
    {
        auto& seq = m_sequences[m_selectedSequenceIndex];
        if (m_sequenceSelectionKind == SequenceEditorSelectionKind::Action &&
            m_selectedSequenceNodeIndex >= 0 && m_selectedSequenceNodeIndex < (int)seq.nodes.size() &&
            m_selectedSequenceActionIndex >= 0 && m_selectedSequenceActionIndex < (int)seq.nodes[m_selectedSequenceNodeIndex].actions.size())
        {
            markSequenceDirty("delete sequence action");
            auto& node = seq.nodes[m_selectedSequenceNodeIndex];
            auto path = m_sequenceTimelineSelection;
            if (path.empty() || path.front() != m_selectedSequenceActionIndex ||
                m_sequenceTimelineNode != m_selectedSequenceNodeIndex || !sequence::FindTimelineAction(node, path))
                path = {m_selectedSequenceActionIndex};
            const int index = path.back();
            path.pop_back();
            auto* parent = sequence::FindTimelineAction(node, path);
            auto& actions = parent ? parent->children : node.actions;
            actions.erase(actions.begin() + index);
            m_sequenceTimelineSelection.clear();
            m_selectedSequenceActionIndex = -1;
        }
        else if (m_sequenceSelectionKind == SequenceEditorSelectionKind::Node)
        {
            deleteSelectedSequenceNode();
        }
        else if (m_sequenceSelectionKind == SequenceEditorSelectionKind::Actor || m_sequenceSelectionKind == SequenceEditorSelectionKind::Prop)
        {
            deleteSequenceEntity(m_sequenceSelectionKind == SequenceEditorSelectionKind::Prop, m_selectedSequenceEntityIndex);
        }
        else if (m_sequenceSelectionKind == SequenceEditorSelectionKind::Marker &&
                 m_selectedSequenceMarkerIndex >= 0 && m_selectedSequenceMarkerIndex < (int)m_sequenceSceneMarkers.size())
        {
            deleteSelectedSequenceMarker();
        }
    }
}

bool Editor::consumeSceneEditorRequest(std::string& interiorId)
{
    if (m_sceneEditorRequestedInterior.empty())
        return false;
    interiorId = std::move(m_sceneEditorRequestedInterior);
    m_sceneEditorRequestedInterior.clear();
    return true;
}

void Editor::refreshSequenceScenePreview()
{
    // Keep unsaved sequence markers while reloading the edited environment.
    stopSequencePreview();
    const std::string sceneId = selectedSequenceSceneId();
    const auto document = sequencePreviewSceneDocument();
    m_sequencePreviewSceneLoaded = !sceneId.empty() && m_sequencePreviewScene.loadScene(sceneId, [&](const std::string& msg)
    {
        m_lastIoStatus = msg;
    }, document.is_object() ? &document : nullptr);
    if (m_sequencePreviewSceneLoaded) syncSequenceMarkerScene();
}

void Editor::renderSequenceEditorWorkspace()
{
    handleSequenceEditorShortcuts();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Sequence Editor Workspace", &m_sequenceEditorOpen, flags);

    renderSequenceEditorToolbar();
    ImGui::Separator();

    const float availableH = ImGui::GetContentRegionAvail().y;
    const float bottomH = std::clamp(availableH * 0.28f, 220.0f, 248.0f);
    const float mainH = std::max(260.0f, availableH - bottomH - 8.0f);

    ImGui::BeginChild("SequenceMain", ImVec2(0, mainH), false);
    const float fullW = ImGui::GetContentRegionAvail().x;
    const float leftW = std::max(220.0f, fullW * 0.18f);
    const float rightW = std::max(300.0f, fullW * 0.25f);
    const float centerW = std::max(360.0f, fullW - leftW - rightW - 16.0f);

    ImGui::BeginChild("SequenceLeft", ImVec2(leftW, 0), true);
    renderSequenceBrowser();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("SequenceCenter", ImVec2(centerW, 0), false);
    const float graphH = std::clamp(ImGui::GetContentRegionAvail().y * 0.28f, 90.0f, 176.0f);
    renderSequenceScenePreview();
    ImGui::BeginChild("NodeGraphRegion", ImVec2(0, graphH), true);
    renderSequenceNodeGraph();
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("SequenceRight", ImVec2(0, 0), true);
    renderSequenceInspector();
    ImGui::Separator();
    renderSequenceValidationPanel();
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::BeginChild("SequenceTimeline", ImVec2(0, 0), true);
    renderSequenceTimeline();
    ImGui::EndChild();
    renderSequenceAudioLibrary();

    ImGui::End();
    if (!m_sequenceEditorOpen)
    {
        stopSequencePreview();
        m_selectMapTab = true;
    }
}

void Editor::renderSequenceEditorToolbar()
{
    if (ImGui::Button("< Back"))
    {
        if (m_sequenceEditorDirty || m_sequenceLayoutDirty)
            ImGui::OpenPopup("Unsaved sequence changes");
        else
            m_sequenceEditorOpen = false;
    }
    ImGui::SameLine();

    const char* current = (m_selectedSequenceIndex >= 0 && m_selectedSequenceIndex < (int)m_sequences.size())
        ? m_sequences[m_selectedSequenceIndex].id.c_str()
        : "<none>";
    ImGui::SetNextItemWidth(std::clamp(ImGui::GetContentRegionAvail().x * 0.23f, 180.0f, 320.0f));
    if (ImGui::BeginCombo("Sequence", current))
    {
        for (int i = 0; i < (int)m_sequences.size(); ++i)
        {
            std::string label = m_sequences[i].id + ((i == m_selectedSequenceIndex && m_sequenceEditorDirty) ? " *" : "");
            if (ImGui::Selectable(label.c_str(), i == m_selectedSequenceIndex))
            {
                m_selectedSequenceIndex = i;
                m_selectedSequenceNodeIndex = m_sequences[i].nodes.empty() ? -1 : 0;
                m_selectedSequenceActionIndex = -1;
                m_selectedSequenceMarkerIndex = -1;
                m_sequenceSelectionKind = SequenceEditorSelectionKind::Sequence;
                loadSelectedSequenceLayout();
                loadSequencePreviewSceneForSelection();
                validateSelectedSequence();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("New"))
    {
        markSequenceDirty("new sequence");
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
        m_sequenceSelectionKind = SequenceEditorSelectionKind::Sequence;
        loadSelectedSequenceLayout();
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && m_selectedSequenceIndex >= 0)
    {
        markSequenceDirty("duplicate sequence");
        auto copy = m_sequences[m_selectedSequenceIndex];
        copy.id += "_copy";
        copy.name += " copy";
        m_sequences.push_back(std::move(copy));
        m_sequencePaths.push_back(sequencePathForId(m_sequences.back().id));
        m_selectedSequenceIndex = (int)m_sequences.size() - 1;
        loadSelectedSequenceLayout();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        saveSelectedSequence();
        saveSelectedSequenceLayout();
        saveSelectedSequenceSceneMetadata();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
    {
        if (m_sequenceEditorDirty || m_sequenceLayoutDirty)
            ImGui::OpenPopup("Reload unsaved sequence");
        else
        {
            loadSequences();
            loadSelectedSequenceLayout();
            loadSequencePreviewSceneForSelection();
            validateSelectedSequence();
        }
    }
    ImGui::SameLine();
    const std::string sceneId = selectedSequenceSceneId();
    ImGui::BeginDisabled(sceneId.empty());
    if (ImGui::Button("Edit scene"))
    {
        if (isSequenceImageScene())
            m_sequenceSelectionKind = SequenceEditorSelectionKind::Scene;
        else
        {
        nlohmann::json scene;
        std::string error;
        if (jsonutils::LoadJsonFileSafe(ResolveSequenceScenePath(sceneId).string(), scene, error))
        {
            m_sceneEditorRequestedInterior = scene.value("interior", scene.value("background", std::string("test_chamber")));
            stopSequencePreview();
        }
        else
            m_lastIoStatus = "Cannot open scene editor: " + error;
        }
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    if (ImGui::Button(m_sequencePreviewActive && !m_sequencePreviewPaused ? "Pause" : "Play"))
    {
        if (!m_sequencePreviewActive)
            playSelectedSequencePreview(false);
        else
            pauseSequencePreview(!m_sequencePreviewPaused);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
        stopSequencePreview();
    ImGui::SameLine();
    if (ImGui::Button("Restart"))
        restartSequencePreview(false);
    ImGui::SameLine();
    if (ImGui::Button("Play from node"))
        playSelectedSequencePreview(true);
    ImGui::SameLine();
    if (ImGui::Button("Step"))
        stepSequencePreview();
    ImGui::SameLine();
    if (ImGui::Button("Validate"))
        validateSelectedSequence();
    ImGui::SameLine();
    if (ImGui::Button("Audio / Import"))
    {
        m_sequenceAudioFiles = sequence::ListAudioFiles(pathutils::ProjectRoot());
        m_sequenceAudioLibraryOpen = true;
    }

    ImGui::SameLine();
    const bool hasErrors = std::any_of(m_sequenceValidationIssues.begin(), m_sequenceValidationIssues.end(), [](const SequenceValidationIssue& i)
    {
        return i.severity == SequenceValidationIssue::Severity::Error;
    });
    const bool hasWarnings = std::any_of(m_sequenceValidationIssues.begin(), m_sequenceValidationIssues.end(), [](const SequenceValidationIssue& i)
    {
        return i.severity == SequenceValidationIssue::Severity::Warning;
    });
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(SeverityColor(hasErrors, hasWarnings)),
        "%s%s", hasErrors ? "ERRORS" : (hasWarnings ? "WARNINGS" : "OK"), m_sequenceEditorDirty ? " *" : "");

    if (ImGui::BeginPopupModal("Unsaved sequence changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Save sequence changes before closing?");
        if (ImGui::Button("Save and close"))
        {
            saveSelectedSequence();
            saveSelectedSequenceLayout();
            saveSelectedSequenceSceneMetadata();
            m_sequenceEditorOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close without saving"))
        {
            m_sequenceEditorOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Reload unsaved sequence", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Discard unsaved sequence changes and reload JSON?");
        if (ImGui::Button("Reload"))
        {
            loadSequences();
            loadSelectedSequenceLayout();
            loadSequencePreviewSceneForSelection();
            validateSelectedSequence();
            m_sequenceEditorDirty = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::renderSequenceBrowser()
{
    ImGui::TextUnformatted("Sequences");
    ImGui::Separator();
    for (int i = 0; i < (int)m_sequences.size(); ++i)
    {
        std::string label = m_sequences[i].id;
        if (i == m_selectedSequenceIndex && m_sequenceEditorDirty)
            label += " *";
        if (ImGui::Selectable(label.c_str(), i == m_selectedSequenceIndex))
        {
            m_selectedSequenceIndex = i;
            m_selectedSequenceNodeIndex = m_sequences[i].nodes.empty() ? -1 : 0;
            m_selectedSequenceActionIndex = -1;
            m_sequenceSelectionKind = SequenceEditorSelectionKind::Sequence;
            loadSelectedSequenceLayout();
            loadSequencePreviewSceneForSelection();
            validateSelectedSequence();
        }
    }

    ImGui::Separator();
    renderSequenceMarkerBrowser();
    renderSequenceEntityBrowser();
    ImGui::Separator();
    ImGui::TextUnformatted("Nodes");
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= (int)m_sequences.size())
        return;
    auto& seq = m_sequences[m_selectedSequenceIndex];
    for (int i = 0; i < (int)seq.nodes.size(); ++i)
    {
        if (ImGui::Selectable(seq.nodes[i].id.c_str(), i == m_selectedSequenceNodeIndex))
        {
            m_selectedSequenceNodeIndex = i;
            m_selectedSequenceActionIndex = -1;
            m_sequenceSelectionKind = SequenceEditorSelectionKind::Node;
        }
    }
}

void Editor::renderSequenceScenePreview()
{
    const float graphH = std::clamp(ImGui::GetContentRegionAvail().y * 0.28f, 90.0f, 176.0f);
    ImGui::BeginChild("CinematicScenePreview", ImVec2(0, -graphH - ImGui::GetStyle().ItemSpacing.y), true);
    renderSequenceSceneSelector();
    if (isSequenceImageScene()) renderSequenceImageTools();
    else renderSequenceMarkerTools();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y = std::max(80.0f, size.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(18, 20, 22, 255));

    if (!m_sequencePreviewCampaign && !m_sequencePreviewSceneLoaded)
        loadSequencePreviewSceneForSelection();

    if ((m_sequencePreviewCampaign || m_sequencePreviewSceneLoaded) && m_renderer)
    {
        const int width = std::max(1, static_cast<int>(size.x));
        const int height = std::max(1, static_cast<int>(size.y));
        if (!m_sequencePreviewTexture || width != m_sequencePreviewTextureWidth || height != m_sequencePreviewTextureHeight)
        {
            if (m_sequencePreviewTexture)
                SDL_DestroyTexture(m_sequencePreviewTexture);
            m_sequencePreviewTexture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
            m_sequencePreviewTextureWidth = width;
            m_sequencePreviewTextureHeight = height;
        }

        SDL_Texture* oldTarget = SDL_GetRenderTarget(m_renderer);
        SDL_Rect oldViewport, oldClip;
        SDL_RenderGetViewport(m_renderer, &oldViewport);
        SDL_RenderGetClipRect(m_renderer, &oldClip);
        const bool hadClip = SDL_RenderIsClipEnabled(m_renderer);
        float oldScaleX, oldScaleY;
        SDL_RenderGetScale(m_renderer, &oldScaleX, &oldScaleY);
        SDL_BlendMode oldBlend;
        SDL_GetRenderDrawBlendMode(m_renderer, &oldBlend);
        Uint8 oldR, oldG, oldB, oldA;
        SDL_GetRenderDrawColor(m_renderer, &oldR, &oldG, &oldB, &oldA);

        if (m_sequencePreviewTexture && SDL_SetRenderTarget(m_renderer, m_sequencePreviewTexture) == 0)
        {
            SDL_RenderSetViewport(m_renderer, nullptr);
            SDL_RenderSetClipRect(m_renderer, nullptr);
            SDL_RenderSetScale(m_renderer, 1.0f, 1.0f);
            SDL_SetRenderDrawColor(m_renderer, 18, 20, 22, 255);
            SDL_RenderClear(m_renderer);
            if (m_sequencePreviewCampaign)
                m_sequencePreviewCampaign->render();
            else
                m_sequencePreviewScene.render();
            SDL_SetRenderTarget(m_renderer, oldTarget);
            draw->AddImage((ImTextureID)(intptr_t)m_sequencePreviewTexture, pos, ImVec2(pos.x + size.x, pos.y + size.y));
        }
        else
        {
            m_lastIoStatus = std::string("Preview render failed: ") + SDL_GetError();
        }
        SDL_RenderSetViewport(m_renderer, &oldViewport);
        SDL_RenderSetScale(m_renderer, oldScaleX, oldScaleY);
        SDL_RenderSetClipRect(m_renderer, hadClip ? &oldClip : nullptr);
        SDL_SetRenderDrawBlendMode(m_renderer, oldBlend);
        SDL_SetRenderDrawColor(m_renderer, oldR, oldG, oldB, oldA);
    }
    else
    {
        draw->AddText(ImVec2(pos.x + 14.0f, pos.y + 14.0f), IM_COL32(210, 210, 210, 255), "No cinematic scene loaded");
    }

    if (m_sequencePreviewCampaign)
    {
        const auto status = m_sequencePreviewCampaign->sequenceStatus();
        const std::string label = status.sequenceId + " / " + status.nodeId +
            (status.completed ? "  Finished" : (status.paused ? "  Paused" : "  Playing"));
        draw->AddText(ImVec2(pos.x + 10, pos.y + size.y - 22), IM_COL32(255, 220, 140, 255), label.c_str());
        const auto& text = m_sequencePreviewCampaign->sequenceText();
        if (!text.empty())
        {
            const float x = pos.x + std::max(4.0f, (size.x - ImGui::CalcTextSize(text.c_str()).x) * 0.5f);
            const float y = pos.y + size.y * 0.16f;
            draw->AddText(ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 255), text.c_str());
            draw->AddText(ImVec2(x, y), IM_COL32(245, 238, 220, 255), text.c_str());
        }
        ImGui::Dummy(size);
        ImGui::EndChild();
        return;
    }

    if (isSequenceImageScene()) renderSequenceImageViewport(pos, size);
    else renderSequenceMarkerViewport(pos, size);

    ImGui::EndChild();
}

void Editor::renderSequenceNodeGraph()
{
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= (int)m_sequences.size())
        return;

    auto& seq = m_sequences[m_selectedSequenceIndex];
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    draw->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(24, 26, 30, 255));

    if (ImGui::Button("Add Node"))
    {
        markSequenceDirty("add sequence node");
        sequence::NodeDef node;
        node.id = UniqueNodeId(seq, "node_" + std::to_string(seq.nodes.size() + 1));
        if (seq.start.empty()) seq.start = node.id;
        seq.nodes.push_back(std::move(node));
        m_selectedSequenceNodeIndex = (int)seq.nodes.size() - 1;
        m_selectedSequenceActionIndex = -1;
        m_sequenceSelectionKind = SequenceEditorSelectionKind::Node;
        m_sequenceGraphLayout[seq.nodes.back().id] = {60.0f, 50.0f + m_selectedSequenceNodeIndex * 112.0f};
        m_sequenceLayoutDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate Node") && m_selectedSequenceNodeIndex >= 0 && m_selectedSequenceNodeIndex < (int)seq.nodes.size())
    {
        markSequenceDirty("duplicate sequence node");
        auto copy = seq.nodes[m_selectedSequenceNodeIndex];
        copy.id = UniqueNodeId(seq, copy.id + "_copy");
        seq.nodes.push_back(std::move(copy));
        m_selectedSequenceNodeIndex = (int)seq.nodes.size() - 1;
        m_selectedSequenceActionIndex = -1;
        m_sequenceSelectionKind = SequenceEditorSelectionKind::Node;
        m_sequenceGraphLayout[seq.nodes.back().id] = {80.0f, 70.0f + m_selectedSequenceNodeIndex * 112.0f};
        m_sequenceLayoutDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Node")) deleteSelectedSequenceNode();
    ImGui::SameLine();
    ImGui::TextUnformatted("Scene Beat Graph");

    ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    // Reserve the canvas without taking mouse clicks away from its nodes.
    ImGui::Dummy(canvasSize);

    auto nodeRect = [&](int i)
    {
        auto& layout = m_sequenceGraphLayout[seq.nodes[i].id];
        if (layout.x == 0.0f && layout.y == 0.0f)
        {
            layout.x = 50.0f;
            layout.y = 50.0f + i * 112.0f;
        }
        ImVec2 min(canvasMin.x + layout.x, canvasMin.y + layout.y);
        return SequenceUiRect{min, ImVec2(min.x + 190.0f, min.y + 76.0f)};
    };

    for (int i = 0; i < (int)seq.nodes.size(); ++i)
    {
        const int next = findSequenceNodeIndexById(seq, seq.nodes[i].next);
        if (next < 0)
            continue;
        SequenceUiRect from = nodeRect(i);
        SequenceUiRect to = nodeRect(next);
        ImVec2 a(from.max.x, (from.min.y + from.max.y) * 0.5f);
        ImVec2 b(to.min.x, (to.min.y + to.max.y) * 0.5f);
        draw->AddBezierCubic(a, ImVec2(a.x + 55.0f, a.y), ImVec2(b.x - 55.0f, b.y), b, IM_COL32(120, 155, 210, 220), 2.0f);
    }

    for (int i = 0; i < (int)seq.nodes.size(); ++i)
    {
        SequenceUiRect rect = nodeRect(i);
        const bool selected = i == m_selectedSequenceNodeIndex;
        const bool current = m_sequencePreviewCampaign &&
            m_sequencePreviewCampaign->sequenceStatus().nodeId == seq.nodes[i].id;
        draw->AddRectFilled(rect.min, rect.max, selected ? IM_COL32(55, 72, 92, 255) : IM_COL32(38, 42, 48, 255), 6.0f);
        draw->AddRect(rect.min, rect.max, current ? IM_COL32(255, 215, 90, 255) : IM_COL32(92, 104, 118, 255), 6.0f, 0, current ? 3.0f : 1.0f);
        draw->AddText(ImVec2(rect.min.x + 12.0f, rect.min.y + 10.0f), IM_COL32(238, 238, 238, 255), TitleFromId(seq.nodes[i].id).c_str());
        const std::string meta = seq.nodes[i].id + " | " + std::to_string(seq.nodes[i].actions.size()) + " actions";
        draw->AddText(ImVec2(rect.min.x + 12.0f, rect.min.y + 34.0f), IM_COL32(180, 190, 200, 255), meta.c_str());
        draw->AddCircleFilled(ImVec2(rect.min.x, (rect.min.y + rect.max.y) * 0.5f), 5.0f, IM_COL32(125, 170, 220, 255));
        draw->AddCircleFilled(ImVec2(rect.max.x, (rect.min.y + rect.max.y) * 0.5f), 5.0f, IM_COL32(125, 220, 160, 255));

        // The edge strips belong to the connection handles rendered below.
        ImGui::SetCursorScreenPos(ImVec2(rect.min.x + 8.0f, rect.min.y));
        ImGui::PushID(i);
        ImGui::InvisibleButton("node", ImVec2(rect.size().x - 16.0f, rect.size().y), ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemClicked())
        {
            m_selectedSequenceNodeIndex = i;
            m_selectedSequenceActionIndex = -1;
            m_sequenceSelectionKind = SequenceEditorSelectionKind::Node;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            m_sequenceSelectionKind = SequenceEditorSelectionKind::Node;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            auto& layout = m_sequenceGraphLayout[seq.nodes[i].id];
            layout.x += delta.x;
            layout.y += delta.y;
            m_sequenceLayoutDirty = true;
        }
        ImGui::PopID();

        ImGui::PushID(10000 + i);
        ImGui::SetCursorScreenPos(ImVec2(rect.max.x - 8.0f, (rect.min.y + rect.max.y) * 0.5f - 8.0f));
        ImGui::InvisibleButton("out", ImVec2(16.0f, 16.0f), ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemClicked())
            m_sequenceGraphLinkSource = i;
        ImGui::SetCursorScreenPos(ImVec2(rect.min.x - 8.0f, (rect.min.y + rect.max.y) * 0.5f - 8.0f));
        ImGui::InvisibleButton("in", ImVec2(16.0f, 16.0f), ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemClicked() && m_sequenceGraphLinkSource >= 0 && m_sequenceGraphLinkSource < (int)seq.nodes.size() && m_sequenceGraphLinkSource != i)
        {
            markSequenceDirty("link sequence node");
            seq.nodes[m_sequenceGraphLinkSource].next = seq.nodes[i].id;
            m_sequenceGraphLinkSource = -1;
        }
        ImGui::PopID();
    }

    if (m_sequenceGraphLinkSource >= 0 && m_sequenceGraphLinkSource < (int)seq.nodes.size())
    {
        SequenceUiRect from = nodeRect(m_sequenceGraphLinkSource);
        ImVec2 a(from.max.x, (from.min.y + from.max.y) * 0.5f);
        ImVec2 b = ImGui::GetMousePos();
        draw->AddLine(a, b, IM_COL32(255, 210, 80, 240), 2.0f);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            m_sequenceGraphLinkSource = -1;
    }
}

void Editor::renderSequenceInspector()
{
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= (int)m_sequences.size())
        return;

    auto& seq = m_sequences[m_selectedSequenceIndex];
    if (ImGui::Button("Přidat marker do scény"))
    {
        stopSequencePreview();
        m_sequenceMarkerTool = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload preview")) refreshSequenceScenePreview();

    if (m_sequenceSelectionKind == SequenceEditorSelectionKind::Scene)
    {
        renderSequenceImageSceneInspector();
        return;
    }
    if (m_sequenceSelectionKind == SequenceEditorSelectionKind::Sequence)
    {
        EditorUndoState before = captureUndoState("edit sequence properties");
        std::string beforeJson = sequence::SequenceToJson(seq).dump();
        if (EditString("Id", seq.id, 96))
            m_sequencePaths[m_selectedSequenceIndex] = sequencePathForId(seq.id);
        EditString("Name", seq.name, 128);
        EditString("Start", seq.start, 96);
        if (beforeJson != sequence::SequenceToJson(seq).dump())
        {
            m_undoStack.push_back(std::move(before));
            if ((int)m_undoStack.size() > kMaxUndoStates)
                m_undoStack.erase(m_undoStack.begin());
            m_redoStack.clear();
            m_sequenceEditorDirty = true;
        }
        return;
    }

    if (m_sequenceSelectionKind == SequenceEditorSelectionKind::Marker &&
        m_selectedSequenceMarkerIndex >= 0 && m_selectedSequenceMarkerIndex < (int)m_sequenceSceneMarkers.size())
    {
        renderSequenceMarkerInspector();
        return;
    }

    if (m_sequenceSelectionKind == SequenceEditorSelectionKind::Actor ||
        m_sequenceSelectionKind == SequenceEditorSelectionKind::Prop)
    {
        renderSequenceEntityInspector(m_sequenceSelectionKind == SequenceEditorSelectionKind::Prop,
            m_selectedSequenceEntityIndex);
        return;
    }

    if (m_selectedSequenceNodeIndex < 0 || m_selectedSequenceNodeIndex >= (int)seq.nodes.size())
    {
        m_sequenceSelectionKind = SequenceEditorSelectionKind::Sequence;
        return;
    }

    auto& node = seq.nodes[m_selectedSequenceNodeIndex];
    if (m_sequenceSelectionKind == SequenceEditorSelectionKind::Node)
    {
        if (ImGui::Button("Smazat uzel (Delete)"))
        {
            deleteSelectedSequenceNode();
            return;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Odkazy se přepojí na následující uzel. Ctrl+Z vrátí smazání.");
        EditorUndoState before = captureUndoState("edit sequence node");
        std::string beforeId = node.id;
        std::string beforeNext = node.next;
        EditString("Node id", node.id, 96);
        std::vector<std::string> nodeIds;
        for (const auto& n : seq.nodes)
            nodeIds.push_back(n.id);
        DrawStringCombo("Next node", node.next, nodeIds);
        EditString("Next fallback", node.next, 96);
        if (beforeId != node.id || beforeNext != node.next)
        {
            m_undoStack.push_back(std::move(before));
            if ((int)m_undoStack.size() > kMaxUndoStates)
                m_undoStack.erase(m_undoStack.begin());
            m_redoStack.clear();
            m_sequenceEditorDirty = true;
        }
        renderSequenceActionList(node);
        return;
    }

    renderSequenceActionList(node);
    if (m_selectedSequenceActionIndex >= 0 && m_selectedSequenceActionIndex < (int)node.actions.size())
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Action");
        EditorUndoState before = captureUndoState("edit sequence action");
        std::string beforeJson = sequence::SequenceToJson(seq).dump();
        auto* editedAction = &node.actions[m_selectedSequenceActionIndex];
        if (m_sequenceTimelineSequence == m_selectedSequenceIndex && m_sequenceTimelineNode == m_selectedSequenceNodeIndex &&
            !m_sequenceTimelineSelection.empty() && m_sequenceTimelineSelection.front() == m_selectedSequenceActionIndex)
            if (auto* child = sequence::FindTimelineAction(node, m_sequenceTimelineSelection)) editedAction = child;
        renderSequenceActionEditor(*editedAction, m_selectedSequenceActionIndex);
        if (beforeJson != sequence::SequenceToJson(seq).dump())
        {
            m_undoStack.push_back(std::move(before));
            if ((int)m_undoStack.size() > kMaxUndoStates)
                m_undoStack.erase(m_undoStack.begin());
            m_redoStack.clear();
            m_sequenceEditorDirty = true;
        }
    }
}

void Editor::renderSequenceActionList(sequence::NodeDef& node)
{
    ImGui::Separator();
    ImGui::TextUnformatted("Actions");
    if (ImGui::BeginCombo("Add", "action type"))
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
                markSequenceDirty("add sequence action");
                sequence::ActionDef action;
                action.type = info.type;
                action.data = {{"type", info.type}, {"blocking", true}};
                if (ActionHasDuration(action))
                    action.data["duration"] = 1.0f;
                node.actions.push_back(std::move(action));
                m_selectedSequenceActionIndex = (int)node.actions.size() - 1;
                m_sequenceSelectionKind = SequenceEditorSelectionKind::Action;
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginListBox("##sequence_action_list", ImVec2(-1, 150)))
    {
        for (int i = 0; i < (int)node.actions.size(); ++i)
        {
            std::string label = ActionLabel(node.actions[i], i);
            if (m_sequencePreviewCampaign && m_sequencePreviewCampaign->sequenceStatus().nodeId == node.id &&
                m_sequencePreviewCampaign->sequenceStatus().actionIndex == i)
                label += "  <current>";
            if (ImGui::Selectable(label.c_str(), i == m_selectedSequenceActionIndex))
            {
                m_selectedSequenceActionIndex = i;
                m_sequenceSelectionKind = SequenceEditorSelectionKind::Action;
            }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("SEQ_ACTION", &i, sizeof(int));
                ImGui::TextUnformatted(label.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SEQ_ACTION"))
                {
                    int from = *static_cast<const int*>(payload->Data);
                    if (from >= 0 && from < (int)node.actions.size() && from != i)
                    {
                        markSequenceDirty("reorder sequence action");
                        auto moved = node.actions[from];
                        node.actions.erase(node.actions.begin() + from);
                        node.actions.insert(node.actions.begin() + i, std::move(moved));
                        m_selectedSequenceActionIndex = i;
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::EndListBox();
    }

    if (m_selectedSequenceActionIndex >= 0 && m_selectedSequenceActionIndex < (int)node.actions.size())
    {
        if (ImGui::Button("Delete"))
        {
            markSequenceDirty("delete sequence action");
            node.actions.erase(node.actions.begin() + m_selectedSequenceActionIndex);
            m_selectedSequenceActionIndex = std::min(m_selectedSequenceActionIndex, (int)node.actions.size() - 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate"))
        {
            markSequenceDirty("duplicate sequence action");
            node.actions.insert(node.actions.begin() + m_selectedSequenceActionIndex + 1, node.actions[m_selectedSequenceActionIndex]);
            ++m_selectedSequenceActionIndex;
        }
        ImGui::SameLine();
        if (ImGui::Button("Up") && m_selectedSequenceActionIndex > 0)
        {
            markSequenceDirty("move sequence action");
            std::swap(node.actions[m_selectedSequenceActionIndex], node.actions[m_selectedSequenceActionIndex - 1]);
            --m_selectedSequenceActionIndex;
        }
        ImGui::SameLine();
        if (ImGui::Button("Down") && m_selectedSequenceActionIndex + 1 < (int)node.actions.size())
        {
            markSequenceDirty("move sequence action");
            std::swap(node.actions[m_selectedSequenceActionIndex], node.actions[m_selectedSequenceActionIndex + 1]);
            ++m_selectedSequenceActionIndex;
        }
    }
}

void Editor::renderSequenceValidationPanel()
{
    validateSelectedSequence();
    int errors = 0;
    int warnings = 0;
    for (const auto& issue : m_sequenceValidationIssues)
    {
        if (issue.severity == SequenceValidationIssue::Severity::Error)
            ++errors;
        else
            ++warnings;
    }

    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(SeverityColor(errors > 0, warnings > 0)),
        "Validation: %s", errors > 0 ? "Errors" : (warnings > 0 ? "Warnings" : "OK"));
    ImGui::InputText("Filter", m_sequenceValidationFilter, sizeof(m_sequenceValidationFilter));
    const std::string filter = m_sequenceValidationFilter;
    for (const auto& issue : m_sequenceValidationIssues)
    {
        if (!filter.empty() && issue.message.find(filter) == std::string::npos)
            continue;
        ImVec4 color = issue.severity == SequenceValidationIssue::Severity::Error
            ? ImVec4(0.95f, 0.25f, 0.22f, 1.0f)
            : ImVec4(0.95f, 0.72f, 0.25f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        if (ImGui::Selectable(issue.message.c_str()))
        {
            m_selectedSequenceNodeIndex = issue.nodeIndex;
            m_selectedSequenceActionIndex = issue.actionIndex;
            m_sequenceSelectionKind = issue.actionIndex >= 0 ? SequenceEditorSelectionKind::Action : SequenceEditorSelectionKind::Node;
        }
        ImGui::PopStyleColor();
    }
}
