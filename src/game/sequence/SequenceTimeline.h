#pragma once

#include "SequenceDefinition.h"

namespace sequence
{
    struct TimelineClip
    {
        std::vector<int> path;
        float start = 0.0f;
        float duration = 0.0f;
        float delay = 0.0f;
    };

    bool HasEditableDuration(const ActionDef& action);
    float ActionTimelineDuration(const ActionDef& action);
    std::vector<TimelineClip> BuildTimeline(const NodeDef& node);
    ActionDef* FindTimelineAction(NodeDef& node, const std::vector<int>& path);
    void SetActionDelay(ActionDef& action, float delay);
    void SetActionDuration(ActionDef& action, float duration);
    std::vector<int> InsertTimedAction(NodeDef& node, ActionDef action, float time);
}
