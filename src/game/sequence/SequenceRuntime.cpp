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
        enterNode(definition->start);
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

    const Status SequenceRuntime::status() const
    {
        Status s;
        s.running = m_running;
        s.paused = m_paused;
        s.completed = m_completed;
        s.sequenceId = m_definition ? m_definition->id : std::string();
        s.nodeId = m_node ? m_node->id : std::string();
        s.actionIndex = m_currentAction;
        if (m_currentAction >= 0 && m_currentAction < (int)m_actions.size())
            s.actionElapsed = m_actions[m_currentAction].elapsed;
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
                runtime.to = readPosition(data, "target_marker", "target", "x", "y", Vec2{});
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
            m_context->setCameraPosition(readPosition(data, "marker", "position", "x", "y", Vec2{}));
            return ActionResult::Complete;
        }

        if (type == "camera_follow")
        {
            m_context->setCameraFollow(DataString(data, "actor"));
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
            m_context->playSfx(DataString(data, "id"));
            return ActionResult::Complete;
        }

        if (type == "play_voice")
        {
            m_context->playVoice(DataString(data, "id"));
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
        const float eased = Smooth01(t);

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
            m_context->setCameraPosition(runtime.to);
            return t >= 1.0f ? ActionResult::Complete : ActionResult::Running;
        }

        if (type == "camera_shake")
        {
            m_context->setCameraShake(data.value("intensity", 1.0f), duration);
            return ActionResult::Complete;
        }

        return ActionResult::Complete;
    }

    SequenceRuntime::ActionResult SequenceRuntime::updateParallel(ActionRuntime& runtime, float dt)
    {
        bool allBlockingComplete = true;
        for (int i = 0; i < (int)runtime.children.size(); ++i)
        {
            auto& child = runtime.children[i];
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

    float SequenceRuntime::readDuration(const ActionDef& def, float fallback) const
    {
        if (def.duration > 0.0f)
            return def.duration;
        return def.data.value("duration", fallback);
    }
}
