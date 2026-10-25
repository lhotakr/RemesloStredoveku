#include "SequenceRuntime.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace sequence
{
    namespace
    {
        static float Clamp01(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        static float Smooth01(float t)
        {
            t = Clamp01(t);
            return t * t * (3.0f - 2.0f * t);
        }

        static std::string DataString(const nlohmann::json& data, const char* key, const std::string& fallback = {})
        {
            if (!data.contains(key) || !data.at(key).is_string())
                return fallback;
            return data.at(key).get<std::string>();
        }

        static int DataInt(const nlohmann::json& data, const char* key, int fallback)
        {
            if (!data.contains(key) || !data.at(key).is_number_integer())
                return fallback;
            return data.at(key).get<int>();
        }
    }

    CameraFollowSettings CameraFollowFromJson(const nlohmann::json& data)
    {
        CameraFollowSettings settings;
        settings.preserveOffset = data.value("preserve_offset", true);
        settings.lookAtActor = data.value("look_at_actor", true);
        settings.smoothing = std::clamp(data.value("smoothing", 0.0f), 0.0f, 100.0f);
        settings.distance = std::clamp(data.value("distance", 2.2f), 0.1f, 8192.0f);
        return settings;
    }

    bool SequenceRuntime::start(const SequenceDefinition* definition, ISequenceContext* context)
    {
        stop();
        if (!definition || !context)
        {
            m_lastError = "SequenceRuntime::start missing definition or context.";
            return false;
        }

        m_definition = definition;
        m_context = context;
        m_running = true;
        m_completed = false;
        m_elapsed = 0.0f;
        enterNode(definition->start);
        return m_node != nullptr;
    }

    bool SequenceRuntime::startAtNode(const SequenceDefinition* definition, ISequenceContext* context, const std::string& nodeId)
    {
        stop();
        if (!definition || !context)
        {
            m_lastError = "SequenceRuntime::startAtNode missing definition or context.";
            return false;
        }

        m_definition = definition;
        m_context = context;
        m_running = true;
        m_completed = false;
        m_elapsed = 0.0f;
        enterNode(nodeId.empty() ? definition->start : nodeId);
        return m_node != nullptr;
    }

    void SequenceRuntime::stop()
    {
        m_definition = nullptr;
        m_node = nullptr;
        m_context = nullptr;
        m_actions.clear();
        m_currentAction = 0;
        m_running = false;
        m_paused = false;
        m_completed = false;
        m_stepRequested = false;
        m_elapsed = 0.0f;
        m_lastError.clear();
    }

    void SequenceRuntime::enterNode(const std::string& nodeId)
    {
        m_node = m_definition ? m_definition->findNode(nodeId) : nullptr;
        m_actions.clear();
        m_currentAction = 0;

        if (!m_node)
        {
            m_lastError = "Sequence node not found: " + nodeId;
            if (m_context)
                m_context->sequenceLog(m_lastError);
            m_running = false;
            m_completed = true;
            return;
        }

        for (const auto& flag : m_node->requireFlags)
        {
            if (!flag.empty() && m_context && !m_context->hasStoryFlag(flag))
            {
                std::string msg = "Sequence node '" + m_node->id + "' requires missing flag '" + flag + "'.";
                m_context->sequenceLog(msg);
                finishNode();
                return;
            }
        }

        for (const auto& action : m_node->actions)
        {
            ActionRuntime runtime;
            runtime.def = &action;
            if (action.type == "parallel")
            {
                for (const auto& child : action.children)
                {
                    ActionRuntime childRuntime;
                    childRuntime.def = &child;
                    runtime.children.push_back(std::move(childRuntime));
                }
            }
            m_actions.push_back(std::move(runtime));
        }
    }

    void SequenceRuntime::update(float dt)
    {
        if (!m_running || m_paused || !m_node || !m_context)
            return;

        if (dt < 0.0f)
            dt = 0.0f;
        m_elapsed += dt;

        bool advanced = true;
        while (m_running && advanced)
        {
            advanced = false;

            if (m_currentAction >= (int)m_actions.size())
            {
                finishNode();
                return;
            }

            ActionRuntime& action = m_actions[m_currentAction];
            const ActionResult result = updateAction(action, dt, m_currentAction);
            dt = 0.0f;

            if (result == ActionResult::Running)
                return;
            if (result == ActionResult::Jumped || result == ActionResult::Ended)
                return;

            ++m_currentAction;
            advanced = true;

            if (m_stepRequested)
            {
                m_stepRequested = false;
                m_paused = true;
                return;
            }
        }
    }

    void SequenceRuntime::stepAction()
    {
        if (!m_running)
            return;
        m_paused = false;
        m_stepRequested = true;
        update(1000.0f);
    }

    bool SequenceRuntime::skip()
    {
        if (!m_definition || !m_context)
            return false;
        if (!m_definition->skippable)
        {
            m_context->sequenceLog("Sequence is not skippable: " + m_definition->id);
            return false;
        }

        runFinalizationActions(m_definition->onSkipActions);
        m_running = false;
        m_paused = false;
        m_completed = true;
        return true;
    }

    const Status SequenceRuntime::status() const
    {
        Status s;
        s.running = m_running;
        s.paused = m_paused;
        s.completed = m_completed;
        s.sequenceId = m_definition ? m_definition->id : std::string();
        s.nodeId = m_node ? m_node->id : std::string();
        s.nextNodeId = m_node ? m_node->next : std::string();
        s.actionIndex = m_currentAction;
        s.elapsed = m_elapsed;
        if (m_currentAction >= 0 && m_currentAction < (int)m_actions.size())
        {
            s.actionElapsed = m_actions[m_currentAction].elapsed;
            collectActiveActions(m_actions[m_currentAction], m_currentAction, s.activeActionIndices);
        }
        return s;
    }

    SequenceRuntime::ActionResult SequenceRuntime::updateAction(ActionRuntime& runtime, float dt, int actionIndex)
    {
        if (!runtime.def)
            return ActionResult::Complete;

        if (runtime.completed)
            return ActionResult::Complete;

        if (runtime.delayElapsed < runtime.def->startDelay)
        {
            runtime.delayElapsed += dt;
            if (runtime.delayElapsed < runtime.def->startDelay)
                return ActionResult::Running;
            dt = runtime.delayElapsed - runtime.def->startDelay;
        }

        if (!runtime.started)
        {
            if (runtime.def->type == "parallel" && runtime.children.empty())
                for (const auto& child : runtime.def->children)
                {
                    ActionRuntime childRuntime;
                    childRuntime.def = &child;
                    runtime.children.push_back(std::move(childRuntime));
                }
            const ActionResult begin = beginAction(runtime, actionIndex);
            runtime.started = true;
            if (begin != ActionResult::Complete)
                return begin;
        }

        const ActionResult tick = tickAction(runtime, dt, actionIndex);
        if (tick == ActionResult::Complete)
            runtime.completed = true;
        return tick;
    }

    SequenceRuntime::ActionResult SequenceRuntime::beginAction(ActionRuntime& runtime, int actionIndex)
    {
        const auto& action = *runtime.def;
        const auto& data = action.data;
        const std::string& type = action.type;

        if (type == "parallel")
            return ActionResult::Complete;

        if (type == "wait" || type == "fade_in" || type == "fade_out" || type == "screen_flash" ||
            type == "camera_move" || type == "camera_shake" || type == "move_actor")
        {
            if (type == "move_actor")
            {
                const std::string actor = DataString(data, "actor");
                if (!actor.empty())
                    runtime.hasFrom = m_context->getActorPosition(actor, runtime.from);
                runtime.to = readPosition(data, "target_marker", "target", "x", "y", runtime.from);
            }
            if (type == "camera_move")
            {
                m_context->setCameraFollow("");
                runtime.hasCameraPose = m_context->getCameraPose(runtime.cameraFrom);
                runtime.cameraTo = readCameraPose(data, "target_marker", runtime.cameraFrom);
                runtime.from = runtime.cameraFrom.position;
                runtime.to = runtime.cameraTo.position;
                runtime.hasFrom = runtime.hasCameraPose;
                Vec2 target;
                const auto actor = DataString(data, "target_actor");
                if (!actor.empty() && m_context->getActorPosition(actor, target))
                {
                    float dx = runtime.cameraFrom.position.x - target.x;
                    float dy = runtime.cameraFrom.position.y - target.y;
                    float length = std::hypot(dx, dy);
                    if (length < .0001f)
                    {
                        const float angle = runtime.cameraFrom.angleDegrees * 3.14159265358979323846f / 180;
                        dx = -std::cos(angle); dy = -std::sin(angle); length = 1;
                    }
                    const float distance = std::clamp(data.value("distance", 2.2f), .1f, 8192.f);
                    runtime.cameraTargetOffset = {dx / length * distance, dy / length * distance};
                    runtime.hasCameraTarget = true;
                }
                else if (!actor.empty()) m_context->sequenceLog("Unknown camera target actor: " + actor);
            }
            if (type == "fade_in")
                m_context->setFadeAlpha(1.0f);
            if (type == "fade_out")
                m_context->setFadeAlpha(0.0f);
            if (type == "screen_flash")
                m_context->setFlashAlpha(data.value("intensity", 1.0f));
            return ActionResult::Running;
        }

        if (type == "set_flag")
        {
            m_context->setStoryFlag(DataString(data, "flag", DataString(data, "id")));
            return ActionResult::Complete;
        }

        if (type == "require_flag")
        {
            const std::string flag = DataString(data, "flag", DataString(data, "id"));
            if (!flag.empty() && !m_context->hasStoryFlag(flag))
            {
                const std::string onFail = DataString(data, "on_fail", DataString(data, "goto"));
                if (!onFail.empty())
                {
                    enterNode(onFail);
                    return ActionResult::Jumped;
                }
                m_context->sequenceLog("Sequence require_flag missing: " + flag);
            }
            return ActionResult::Complete;
        }

        if (type == "goto")
        {
            const std::string node = DataString(data, "target", DataString(data, "node"));
            enterNode(node);
            return ActionResult::Jumped;
        }

        if (type == "end_sequence")
        {
            const std::string flag = DataString(data, "set_flag");
            if (!flag.empty())
                m_context->setStoryFlag(flag);
            m_running = false;
            m_completed = true;
            return ActionResult::Ended;
        }

        if (type == "spawn_actor")
        {
            ActorSpawnDef spawn;
            spawn.actor = DataString(data, "actor", DataString(data, "id"));
            spawn.kind = DataString(data, "kind", "cinematic");
            spawn.ref = DataString(data, "ref", DataString(data, "npc"));
            spawn.sprite = DataString(data, "sprite", DataString(data, "object"));
            spawn.characterId = DataString(data, "character", DataString(data, "character_id"));
            spawn.position = readPosition(data, "marker", "position", "x", "y", Vec2{});
            spawn.hasPosition = data.contains("marker") || data.contains("position") || data.contains("x") || data.contains("y");
            spawn.visible = data.value("visible", true);
            spawn.rotation = data.value("rotation", 0.0f);
            spawn.scale = data.value("scale", 1.0f);
            m_context->spawnActor(spawn);
            return ActionResult::Complete;
        }

        if (type == "show_prop" || type == "show_actor")
        {
            m_context->setActorVisible(DataString(data, "prop", DataString(data, "actor", DataString(data, "id"))), true);
            return ActionResult::Complete;
        }

        if (type == "hide_prop" || type == "hide_actor")
        {
            m_context->setActorVisible(DataString(data, "prop", DataString(data, "actor", DataString(data, "id"))), false);
            return ActionResult::Complete;
        }

        if (type == "remove_actor")
        {
            m_context->removeActor(DataString(data, "actor", DataString(data, "id")));
            return ActionResult::Complete;
        }

        if (type == "set_actor_animation")
        {
            m_context->setActorAnimation(DataString(data, "actor"), DataString(data, "animation", DataString(data, "id")));
            return ActionResult::Complete;
        }

        if (type == "face_actor")
        {
            m_context->faceActor(DataString(data, "actor"), DataString(data, "direction", DataString(data, "target")));
            return ActionResult::Complete;
        }

        if (type == "set_camera")
        {
            m_context->setCameraFollow("");
            CameraPose current;
            const bool hasPose = m_context->getCameraPose(current);
            CameraPose markerPose;
            if (hasPose || m_context->resolveCameraMarker(DataString(data, "marker"), markerPose))
                m_context->setCameraPose(readCameraPose(data, "marker", current));
            else
                m_context->setCameraPosition(readPosition(data, "marker", "position", "x", "y", Vec2{}));
            return ActionResult::Complete;
        }

        if (type == "camera_follow")
        {
            m_context->setCameraFollow(data.value("enabled", true) ? DataString(data, "actor") : std::string(), CameraFollowFromJson(data));
            return ActionResult::Complete;
        }

        if (type == "show_text")
        {
            m_context->showSequenceText(DataString(data, "text"), data.value("duration", 0.0f));
            return ActionResult::Complete;
        }

        if (type == "hide_text")
        {
            m_context->hideSequenceText();
            return ActionResult::Complete;
        }

        if (type == "play_music")
        {
            if (data.contains("volume"))
                m_context->setSequenceMusicVolume(data.value("volume", 1.0f), 0.0f);
            m_context->playMusic(DataString(data, "id", DataString(data, "path")), DataInt(data, "loops", -1), DataInt(data, "fade_ms", 500));
            return ActionResult::Complete;
        }

        if (type == "stop_music")
        {
            m_context->stopMusic(DataInt(data, "fade_ms", 300));
            return ActionResult::Complete;
        }

        if (type == "play_sfx")
        {
            m_context->playSfx(DataString(data, "id", DataString(data, "path")), data.value("volume", 1.0f));
            return ActionResult::Complete;
        }

        if (type == "play_voice")
        {
            m_context->playVoice(DataString(data, "id", DataString(data, "path")), data.value("volume", 1.0f));
            return ActionResult::Complete;
        }

        if (type == "set_music_volume")
        {
            m_context->setSequenceMusicVolume(data.value("volume", 1.0f), std::max(0, DataInt(data, "fade_ms", 500)) / 1000.0f);
            return ActionResult::Complete;
        }
        if (type == "set_sfx_volume")
        {
            m_context->setSequenceSfxVolume(data.value("volume", 1.0f));
            return ActionResult::Complete;
        }
        if (type == "duck_music")
        {
            m_context->duckSequenceMusic(data.value("volume", 0.3f), readDuration(action, 3.0f),
                std::max(0, DataInt(data, "fade_out_ms", 300)) / 1000.0f,
                std::max(0, DataInt(data, "fade_in_ms", 500)) / 1000.0f);
            return ActionResult::Complete;
        }
        if (type == "restore_music")
        {
            m_context->restoreSequenceMusic(std::max(0, DataInt(data, "fade_ms", 500)) / 1000.0f);
            return ActionResult::Complete;
        }
        if (type == "pickup_item")
        {
            if (!m_context->pickupSequenceItem(DataString(data, "item"), std::max(1, DataInt(data, "count", 1)),
                DataString(data, "prop"), DataString(data, "once_flag")))
                m_context->sequenceLog("Could not pick up item: " + DataString(data, "item"));
            return ActionResult::Complete;
        }

        if (type == "set_time")
        {
            m_context->setTimeOfDay(DataString(data, "value", DataString(data, "time")));
            return ActionResult::Complete;
        }

        if (type == "set_weather")
        {
            m_context->setWeather(data);
            return ActionResult::Complete;
        }

        if (type == "load_map")
        {
            m_context->loadMapForSequence(DataString(data, "map", DataString(data, "path")), DataString(data, "spawn", DataString(data, "spawn_id")));
            return ActionResult::Complete;
        }

        if (type == "load_scene")
        {
            m_context->loadScene(DataString(data, "scene", DataString(data, "id")));
            return ActionResult::Complete;
        }

        if (type == "teleport_player")
        {
            m_context->teleportPlayer(readPosition(data, "marker", "position", "x", "y", Vec2{}));
            return ActionResult::Complete;
        }

        if (type == "enable_player_control")
        {
            m_context->enablePlayerControl(true);
            return ActionResult::Complete;
        }

        if (type == "disable_player_control")
        {
            m_context->enablePlayerControl(false);
            return ActionResult::Complete;
        }

        if (type == "start_dialog")
        {
            m_context->startDialog(DataString(data, "npc", DataString(data, "dialog", DataString(data, "id"))));
            return ActionResult::Complete;
        }

        if (type == "change_renderer_mode")
        {
            m_context->changeRendererMode(DataString(data, "mode"));
            return ActionResult::Complete;
        }

        m_context->sequenceLog("Unknown sequence action type '" + type + "'.");
        return ActionResult::Complete;
    }

    SequenceRuntime::ActionResult SequenceRuntime::tickAction(ActionRuntime& runtime, float dt, int)
    {
        const auto& action = *runtime.def;
        const auto& data = action.data;
        const std::string& type = action.type;

        if (type == "parallel")
            return updateParallel(runtime, dt);

        const float duration = readDuration(action, 0.0f);
        runtime.elapsed += dt;
        const float t = duration <= 0.0f ? 1.0f : Clamp01(runtime.elapsed / duration);
        const auto easing = DataString(data, "easing", "smooth");
        const float eased = easing == "linear" ? t : (easing == "ease_in" ? t * t :
            (easing == "ease_out" ? 1 - (1 - t) * (1 - t) : Smooth01(t)));

        if (type == "wait")
            return t >= 1.0f ? ActionResult::Complete : ActionResult::Running;

        if (type == "fade_in")
        {
            m_context->setFadeAlpha(1.0f - eased);
            return t >= 1.0f ? ActionResult::Complete : ActionResult::Running;
        }

        if (type == "fade_out")
        {
            m_context->setFadeAlpha(eased);
            return t >= 1.0f ? ActionResult::Complete : ActionResult::Running;
        }

        if (type == "screen_flash")
        {
            const float intensity = data.value("intensity", 1.0f);
            m_context->setFlashAlpha(intensity * (1.0f - eased));
            return t >= 1.0f ? ActionResult::Complete : ActionResult::Running;
        }

        if (type == "move_actor")
        {
            const std::string actor = DataString(data, "actor");
            if (runtime.hasFrom && !actor.empty())
            {
                Vec2 p;
                p.x = runtime.from.x + (runtime.to.x - runtime.from.x) * eased;
                p.y = runtime.from.y + (runtime.to.y - runtime.from.y) * eased;
                m_context->setActorPosition(actor, p);
            }
            return t >= 1.0f ? ActionResult::Complete : ActionResult::Running;
        }

        if (type == "camera_move")
        {
            const auto targetActor = DataString(data, "target_actor");
            Vec2 target;
            if (runtime.hasCameraTarget && m_context->getActorPosition(targetActor, target))
                runtime.cameraTo.position = {target.x + runtime.cameraTargetOffset.x, target.y + runtime.cameraTargetOffset.y};
            CameraPose pose = runtime.cameraFrom;
            pose.position.x += (runtime.cameraTo.position.x - pose.position.x) * eased;
            pose.position.y += (runtime.cameraTo.position.y - pose.position.y) * eased;
            pose.height += (runtime.cameraTo.height - pose.height) * eased;
            pose.angleDegrees += std::remainder(runtime.cameraTo.angleDegrees - pose.angleDegrees, 360.0f) * eased;
            pose.pitch += (runtime.cameraTo.pitch - pose.pitch) * eased;
            auto lookAt = DataString(data, "look_at");
            if (lookAt.empty()) lookAt = targetActor;
            Vec2 lookPosition;
            if (!lookAt.empty() && m_context->getActorPosition(lookAt, lookPosition) &&
                std::hypot(lookPosition.x - pose.position.x, lookPosition.y - pose.position.y) > .0001f)
            {
                const float angle = std::atan2(lookPosition.y - pose.position.y, lookPosition.x - pose.position.x) * 180 / 3.14159265358979323846f;
                pose.angleDegrees = runtime.cameraFrom.angleDegrees + std::remainder(angle - runtime.cameraFrom.angleDegrees, 360.f) * eased;
            }
            if (runtime.hasCameraPose) m_context->setCameraPose(pose);
            else m_context->setCameraPosition(pose.position);
            if (t >= 1 && data.value("follow_after", false))
            {
                auto actor = DataString(data, "follow_actor");
                if (actor.empty()) actor = lookAt;
                if (!actor.empty())
                {
                    auto settings = CameraFollowFromJson(data);
                    settings.preserveOffset = true;
                    m_context->setCameraFollow(actor, settings);
                }
            }
            return t >= 1 ? ActionResult::Complete : ActionResult::Running;
        }

        if (type == "camera_shake")
        {
            m_context->setCameraShake(data.value("intensity", data.value("strength", 1.0f)), duration, data.value("frequency", 18.0f));
            return ActionResult::Complete;
        }

        return ActionResult::Complete;
    }

    SequenceRuntime::ActionResult SequenceRuntime::updateParallel(ActionRuntime& runtime, float dt)
    {
        bool allBlockingComplete = true;
        for (int phase = 0; phase < 2; ++phase)
        for (int i = 0; i < (int)runtime.children.size(); ++i)
        {
            auto& child = runtime.children[i];
            // Resolve actor movement before dependent camera targets in this frame.
            const bool camera = child.def && (child.def->type.rfind("camera_", 0) == 0 || child.def->type == "set_camera");
            if (camera != (phase == 1)) continue;
            if (!child.completed)
            {
                const ActionResult result = updateAction(child, dt, i);
                if (result == ActionResult::Jumped || result == ActionResult::Ended)
                    return result;
            }

            if (isActionBlocking(child) && !child.completed)
                allBlockingComplete = false;
        }

        if (allBlockingComplete)
        {
            runtime.completed = true;
            return ActionResult::Complete;
        }
        return ActionResult::Running;
    }

    bool SequenceRuntime::isActionBlocking(const ActionRuntime& runtime) const
    {
        return runtime.def ? runtime.def->blocking : false;
    }

    void SequenceRuntime::collectActiveActions(const ActionRuntime& runtime, int actionIndex, std::vector<int>& out) const
    {
        if (!runtime.def || runtime.completed)
            return;
        if (runtime.started || runtime.delayElapsed > 0.0f)
            out.push_back(actionIndex);
        if (runtime.def->type == "parallel")
        {
            for (int i = 0; i < (int)runtime.children.size(); ++i)
                collectActiveActions(runtime.children[i], i, out);
        }
    }

    void SequenceRuntime::runFinalizationActions(const std::vector<ActionDef>& actions)
    {
        std::vector<ActionRuntime> previous = std::move(m_actions);
        const NodeDef* previousNode = m_node;
        const int previousAction = m_currentAction;
        m_actions.clear();
        m_currentAction = 0;
        for (const auto& action : actions)
        {
            ActionRuntime runtime;
            runtime.def = &action;
            if (action.type == "parallel")
            {
                for (const auto& child : action.children)
                {
                    ActionRuntime childRuntime;
                    childRuntime.def = &child;
                    runtime.children.push_back(std::move(childRuntime));
                }
            }

            updateAction(runtime, 1000.0f, m_currentAction);
            ++m_currentAction;
        }
        m_actions = std::move(previous);
        m_node = previousNode;
        m_currentAction = previousAction;
    }

    void SequenceRuntime::finishNode()
    {
        if (!m_node)
            return;

        if (!m_node->next.empty())
        {
            enterNode(m_node->next);
            return;
        }

        m_running = false;
        m_completed = true;
    }

    Vec2 SequenceRuntime::readPosition(const nlohmann::json& data, const char* markerKey, const char* arrayKey, const char* xKey, const char* yKey, Vec2 fallback) const
    {
        const std::string marker = DataString(data, markerKey);
        if (!marker.empty() && m_context)
        {
            Vec2 resolved;
            if (m_context->resolveMarker(marker, resolved))
                return resolved;
            m_context->sequenceLog("Unknown sequence marker '" + marker + "'.");
        }

        if (data.contains(arrayKey) && data.at(arrayKey).is_array() && data.at(arrayKey).size() >= 2)
        {
            Vec2 p;
            p.x = data.at(arrayKey).at(0).get<float>();
            p.y = data.at(arrayKey).at(1).get<float>();
            return p;
        }

        if (data.contains(xKey) || data.contains(yKey))
        {
            fallback.x = data.value(xKey, fallback.x);
            fallback.y = data.value(yKey, fallback.y);
        }

        return fallback;
    }

    CameraPose SequenceRuntime::readCameraPose(const nlohmann::json& data, const char* markerKey, CameraPose fallback) const
    {
        CameraPose result = fallback;
        result.position = readPosition(data, markerKey, data.contains("target") ? "target" : "position", "x", "y", fallback.position);
        const std::string marker = DataString(data, markerKey);
        if (!marker.empty()) m_context->resolveCameraMarker(marker, result);
        result.height = data.value("height", data.value("z", result.height));
        result.angleDegrees = data.value("angle_degrees", result.angleDegrees);
        result.pitch = data.value("pitch", result.pitch);
        return result;
    }

    float SequenceRuntime::readDuration(const ActionDef& def, float fallback) const
    {
        if (def.duration > 0.0f)
            return def.duration;
        return def.data.value("duration", fallback);
    }
}
