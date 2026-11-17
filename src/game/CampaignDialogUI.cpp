#include "Campaign.h"
#include "ui/MedievalUI.h"

void Campaign::renderNpcDialog()
{
    if (!m_npcDialogOpen) return;
    const auto close = [this]
    {
        m_npcDialogOpen = false;
        m_dialogNpcIndex = -1;
        m_activeDialog = nullptr;
        m_activeDialogNodeId.clear();
    };
    if (m_dialogNpcIndex < 0 || m_dialogNpcIndex >= static_cast<int>(m_npcManager.npcs().size()))
    {
        close();
        return;
    }

    const auto& npc = m_npcManager.npcs()[m_dialogNpcIndex];
    medieval::DialogueView view;
    view.speaker = npc.displayName();
    view.portraitKey = npc.npcId.empty() ? npc.id : npc.npcId;
    view.mood = npc.mood;
    view.nodeKey = m_activeDialogNodeId;
    const DialogNode* node = nullptr;
    if (m_activeDialog && !m_activeDialogNodeId.empty())
    {
        node = m_dialogManager.findNode(*m_activeDialog, m_activeDialogNodeId);
        if (!node || !isDialogNodeAvailable(*node))
        {
            node = nullptr;
            view.text = "Tento rozhovor nyní není dostupný.";
        }
        else
        {
            if (!node->speaker.empty()) view.speaker = node->speaker;
            view.text = node->text;
            for (int i = 0; i < static_cast<int>(node->choices.size()); ++i)
                if (isDialogChoiceAvailable(node->choices[i]))
                    view.choices.push_back({i, formatChoiceLabel(node->choices[i])});
        }
    }
    else view.text = npc.greeting.empty() ? "Mlčky na tebe hledí." : npc.greeting;

    // Effects are applied after drawing, once, using the original choice index.
    const auto action = medieval::UI::Get().dialogue(("Rozhovor##npc_" + npc.id).c_str(), view);
    if (action.close) close();
    else if (node && action.choice >= 0 && action.choice < static_cast<int>(node->choices.size()))
    {
        const auto& choice = node->choices[action.choice];
        if (!isDialogChoiceAvailable(choice)) return;
        applyDialogChoiceEffects(choice);
        if (choice.closeDialog || choice.nextNodeId.empty()) close();
        else m_activeDialogNodeId = choice.nextNodeId;
    }
}
