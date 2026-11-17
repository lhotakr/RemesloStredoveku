#include "SequenceTimeline.h"

#include <algorithm>
#include <cmath>

namespace sequence
{
    bool HasEditableDuration(const ActionDef& action)
    {
        return action.type == "wait" || action.type == "move_actor" || action.type == "camera_move" ||
            action.type == "fade_in" || action.type == "fade_out" || action.type == "screen_flash" ||
            action.type == "duck_music";
    }

    float ActionTimelineDuration(const ActionDef& action)
    {
        // Ducking has an editable hold time, but never delays subsequent actions.
        if (action.type == "duck_music") return 0.0f;
        if (action.type == "parallel")
        {
            float duration = 0.0f;
            for (const auto& child : action.children)
                if (child.blocking)
                    duration = std::max(duration, std::max(0.0f, child.startDelay) + ActionTimelineDuration(child));
            return duration;
        }
        // Text and audio start immediately; their duration does not block the next action.
        if (!HasEditableDuration(action))
            return 0.0f;
        const float duration = action.duration > 0.0f ? action.duration : action.data.value("duration", 0.0f);
        return std::isfinite(duration) ? std::max(0.0f, duration) : 0.0f;
    }

    namespace
    {
        void AddClip(const ActionDef& action, float base, std::vector<int> path, std::vector<TimelineClip>& clips)
        {
            const float delay = std::isfinite(action.startDelay) ? std::max(0.0f, action.startDelay) : 0.0f;
            const float start = base + delay;
            const float span = action.type == "duck_music"
                ? std::max(0.0f, action.duration > 0.0f ? action.duration : action.data.value("duration", 3.0f))
                : ActionTimelineDuration(action);
            clips.push_back({path, start, span, delay});
            if (action.type == "parallel")
                for (int i = 0; i < static_cast<int>(action.children.size()); ++i)
                {
                    auto childPath = path;
                    childPath.push_back(i);
                    AddClip(action.children[i], start, std::move(childPath), clips);
                }
        }
    }

    std::vector<TimelineClip> BuildTimeline(const NodeDef& node)
    {
        std::vector<TimelineClip> clips;
        float base = 0.0f;
        for (int i = 0; i < static_cast<int>(node.actions.size()); ++i)
        {
            AddClip(node.actions[i], base, {i}, clips);
            base += std::max(0.0f, node.actions[i].startDelay) + ActionTimelineDuration(node.actions[i]);
        }
        return clips;
    }

    ActionDef* FindTimelineAction(NodeDef& node, const std::vector<int>& path)
    {
        auto* actions = &node.actions;
        ActionDef* result = nullptr;
        for (int index : path)
        {
            if (index < 0 || index >= static_cast<int>(actions->size()))
                return nullptr;
            result = &(*actions)[index];
            actions = &result->children;
        }
        return result;
    }

    void SetActionDelay(ActionDef& action, float delay)
    {
        if (!std::isfinite(delay)) return;
        action.startDelay = std::max(0.0f, delay);
        action.data.erase("start_delay");
        action.data["startDelay"] = action.startDelay;
    }

    void SetActionDuration(ActionDef& action, float duration)
    {
        if (!std::isfinite(duration) || !HasEditableDuration(action)) return;
        action.duration = std::max(0.01f, duration);
        action.data["duration"] = action.duration;
    }

    std::vector<int> InsertTimedAction(NodeDef& node, ActionDef action, float time)
    {
        time = std::isfinite(time) ? std::max(0.0f, time) : 0.0f;
        const float duration = ActionTimelineDuration(action);
        const bool timed = duration > 0.0f;
        const bool motion = timed || action.type.rfind("camera_", 0) == 0 || action.type == "set_camera";
        if (timed)
        {
            action.blocking = true;
            action.data["blocking"] = true;
        }
        float base = 0.0f;
        for (int i = 0; i < static_cast<int>(node.actions.size()); ++i)
        {
            auto& existing = node.actions[i];
            const float start = base + std::max(0.0f, existing.startDelay);
            const float end = start + ActionTimelineDuration(existing);
            // Run scene loading/spawning and other instantaneous setup before motion at the same time.
            if (motion && time == start && start == end && existing.type != "end_sequence" && existing.type != "goto")
            {
                base = end;
                continue;
            }
            if (time <= start)
            {
                if (timed && time + duration > start && existing.type != "end_sequence" && existing.type != "goto")
                {
                    SetActionDelay(action, 0.0f);
                    if (existing.type == "parallel")
                    {
                        for (auto& child : existing.children)
                            SetActionDelay(child, child.startDelay + start - time);
                        SetActionDelay(existing, time - base);
                        existing.children.push_back(std::move(action));
                        return {i, static_cast<int>(existing.children.size()) - 1};
                    }
                    ActionDef group;
                    group.type = "parallel";
                    group.data["type"] = "parallel";
                    SetActionDelay(group, time - base);
                    SetActionDelay(existing, start - time);
                    group.children.push_back(std::move(existing));
                    group.children.push_back(std::move(action));
                    node.actions[i] = std::move(group);
                    return {i, 1};
                }
                SetActionDelay(action, time - base);
                SetActionDelay(existing, start - time - duration);
                node.actions.insert(node.actions.begin() + i, std::move(action));
                return {i};
            }
            if (time < end)
            {
                // Keep the ongoing action intact. Timed motion keeps the group alive until it finishes.
                SetActionDelay(action, time - start);
                action.blocking = timed;
                action.data["blocking"] = timed;
                if (existing.type == "parallel")
                {
                    existing.children.push_back(std::move(action));
                    return {i, static_cast<int>(existing.children.size()) - 1};
                }
                ActionDef group;
                group.type = "parallel";
                group.data["type"] = "parallel";
                SetActionDelay(group, existing.startDelay);
                SetActionDelay(existing, 0.0f);
                group.children.push_back(std::move(existing));
                group.children.push_back(std::move(action));
                node.actions[i] = std::move(group);
                return {i, 1};
            }
            base = end;
        }
        SetActionDelay(action, time - base);
        node.actions.push_back(std::move(action));
        return {static_cast<int>(node.actions.size()) - 1};
    }
}
