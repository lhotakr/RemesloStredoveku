#include "DialogManager.h"
#include "../JsonUtils.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

void DialogManager::clear()
{
    m_dialogs.clear();
}

bool DialogManager::loadFromFile(const std::string& path, std::string* outError)
{
    json root;
    std::string err;

    if (!jsonutils::LoadJsonFileSafe(path, root, err))
    {
        if (outError) *outError = "DialogManager: " + err;
        return false;
    }

    if (!root.contains("dialogs") || !root["dialogs"].is_array())
    {
        if (outError) *outError = "DialogManager: chybi pole 'dialogs' v " + path;
        return false;
    }

    m_dialogs.clear();

    for (const auto& jd : root["dialogs"])
    {
        DialogDefinition def;
        def.dialogId = jd.value("dialog_id", "");
        def.startNodeId = jd.value("start_node", "");

        if (def.dialogId.empty())
            continue;

        if (!jd.contains("nodes") || !jd["nodes"].is_array())
            continue;

        for (const auto& jn : jd["nodes"])
        {
            DialogNode node;
            node.id = jn.value("id", "");
            node.speaker = jn.value("speaker", "");
            node.text = jn.value("text", "");
            node.requireFlag = jn.value("require_flag", "");
            node.forbidFlag = jn.value("forbid_flag", "");

            if (node.id.empty())
                continue;

            if (jn.contains("choices") && jn["choices"].is_array())
            {
                for (const auto& jc : jn["choices"])
                {
                    DialogChoice ch;
                    ch.text = jc.value("text", "");
                    ch.nextNodeId = jc.value("next", "");
                    ch.style = jc.value("style", "");
                    ch.npcMoodDelta = jc.value("npc_mood_delta", 0);
                    ch.setFlag = jc.value("set_flag", "");
                    ch.requireFlag = jc.value("require_flag", "");
                    ch.forbidFlag = jc.value("forbid_flag", "");
                    ch.requireMoodMin = jc.value("require_mood_min", 0);
                    ch.closeDialog = jc.value("close_dialog", false);
                    ch.setNpcScript = jc.value("set_npc_script", "");
                    ch.setNpcGreeting = jc.value("set_npc_greeting", "");
                    node.choices.push_back(std::move(ch));
                }
            }

            def.nodes[node.id] = std::move(node);
        }

        if (!def.startNodeId.empty() && !def.nodes.empty())
            m_dialogs[def.dialogId] = std::move(def);
    }

    if (m_dialogs.empty())
    {
        if (outError) *outError = "DialogManager: nenacten zadny dialog z " + path;
        return false;
    }

    return true;
}

const DialogDefinition* DialogManager::findDialog(const std::string& dialogId) const
{
    auto it = m_dialogs.find(dialogId);
    if (it == m_dialogs.end())
        return nullptr;

    return &it->second;
}

const DialogNode* DialogManager::findNode(const DialogDefinition& dialog, const std::string& nodeId) const
{
    auto it = dialog.nodes.find(nodeId);
    if (it == dialog.nodes.end())
        return nullptr;

    return &it->second;
}