#include "Campaign.h"

namespace
{
    static bool HasAllFlags(
        const std::unordered_set<std::string>& storyFlags,
        const std::vector<std::string>& requiredFlags,
        const std::string& legacyFlag)
    {
        if (!legacyFlag.empty() && storyFlags.find(legacyFlag) == storyFlags.end())
            return false;

        for (const auto& flag : requiredFlags)
        {
            if (!flag.empty() && storyFlags.find(flag) == storyFlags.end())
                return false;
        }

        return true;
    }

    static bool HasAnyFlag(
        const std::unordered_set<std::string>& storyFlags,
        const std::vector<std::string>& forbiddenFlags,
        const std::string& legacyFlag)
    {
        if (!legacyFlag.empty() && storyFlags.find(legacyFlag) != storyFlags.end())
            return true;

        for (const auto& flag : forbiddenFlags)
        {
            if (!flag.empty() && storyFlags.find(flag) != storyFlags.end())
                return true;
        }

        return false;
    }
}

bool Campaign::isDialogNodeAvailable(const DialogNode& node) const
{
    if (!HasAllFlags(m_storyFlags, node.requireFlags, node.requireFlag))
        return false;

    if (HasAnyFlag(m_storyFlags, node.forbidFlags, node.forbidFlag))
        return false;

    return true;
}

bool Campaign::isDialogChoiceAvailable(const DialogChoice& choice) const
{
    if (!HasAllFlags(m_storyFlags, choice.requireFlags, choice.requireFlag))
        return false;

    if (HasAnyFlag(m_storyFlags, choice.forbidFlags, choice.forbidFlag))
        return false;

    if (m_dialogNpcIndex >= 0 && m_dialogNpcIndex < (int)m_npcManager.npcs().size())
    {
        const auto& npc = m_npcManager.npcs()[m_dialogNpcIndex];
        if (npc.mood < choice.requireMoodMin)
            return false;
    }

    return true;
}

void Campaign::applyDialogChoiceEffects(const DialogChoice& choice)
{
    if (m_dialogNpcIndex >= 0 && m_dialogNpcIndex < (int)m_npcManager.npcs().size())
    {
        auto& npc = m_npcManager.npcs()[m_dialogNpcIndex];
        npc.mood = std::clamp(npc.mood + choice.npcMoodDelta, 0, 100);

        if (!choice.setNpcScript.empty())
            npc.scriptId = choice.setNpcScript;

        if (!choice.setNpcGreeting.empty())
            npc.greeting = choice.setNpcGreeting;
    }

    if (!choice.setFlag.empty())
        setStoryFlag(choice.setFlag);

    for (const auto& flag : choice.setFlags)
        setStoryFlag(flag);
}

bool Campaign::hasStoryFlag(const std::string& flag) const
{
    return !flag.empty() && m_storyFlags.contains(flag);
}

void Campaign::setStoryFlag(const std::string& flag)
{
    if (flag.empty())
        return;

    const bool inserted = m_storyFlags.insert(flag).second;
    if (!inserted)
        return;

    applyQuestRewardEffects(flag);
}

bool Campaign::isQuestActive(const QuestDef& q) const
{
    return hasStoryFlag(q.startedFlag) && !hasStoryFlag(q.doneFlag);
}

bool Campaign::isQuestReadyToTurnIn(const QuestDef& q) const
{
    return isQuestActive(q) && hasStoryFlag(q.readyFlag);
}

bool Campaign::isQuestDone(const QuestDef& q) const
{
    return hasStoryFlag(q.doneFlag);
}

std::string Campaign::formatChoiceLabel(const DialogChoice& choice) const
{
    if (choice.style.empty())
        return choice.text;

    if (choice.style == "polite")
        return "[Pokorne] " + choice.text;

    if (choice.style == "neutral")
        return "[Neutralne] " + choice.text;

    if (choice.style == "rude")
        return "[Arogantne] " + choice.text;

    return "[" + choice.style + "] " + choice.text;
}
