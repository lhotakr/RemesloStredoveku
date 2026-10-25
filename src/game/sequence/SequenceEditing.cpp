#include "SequenceEditing.h"

#include <algorithm>

namespace sequence
{
    bool DeleteNode(SequenceDefinition& definition, const std::string& id)
    {
        const auto found = std::find_if(definition.nodes.begin(), definition.nodes.end(),
            [&](const NodeDef& node) { return node.id == id; });
        if (found == definition.nodes.end()) return false;

        const std::string removedId = found->id;
        const std::string successor = found->next != removedId && definition.findNode(found->next) ? found->next : std::string();
        definition.nodes.erase(found);
        if (definition.start == removedId)
            definition.start = !successor.empty() ? successor :
                (definition.nodes.empty() ? std::string() : definition.nodes.front().id);

        auto updateActions = [&](auto&& self, std::vector<ActionDef>& actions) -> void
        {
            for (auto& action : actions)
            {
                if (action.type == "goto" &&
                    action.data.value("target", action.data.value("node", std::string())) == removedId)
                {
                    action.data.erase("target");
                    action.data.erase("node");
                    if (successor.empty())
                    {
                        action.type = "end_sequence";
                        action.data["type"] = action.type;
                    }
                    else action.data["target"] = successor;
                }
                if (action.type == "require_flag")
                {
                    for (const char* key : {"on_fail", "goto"})
                    {
                        if (action.data.value(key, std::string()) != removedId) continue;
                        if (successor.empty()) action.data.erase(key);
                        else action.data[key] = successor;
                    }
                }
                self(self, action.children);
            }
        };
        for (auto& node : definition.nodes)
        {
            if (node.next == removedId)
                node.next = successor == node.id ? std::string() : successor;
            updateActions(updateActions, node.actions);
        }
        updateActions(updateActions, definition.onSkipActions);
        return true;
    }
}
