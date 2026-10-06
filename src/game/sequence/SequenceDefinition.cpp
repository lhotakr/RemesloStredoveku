#include "SequenceDefinition.h"

#include "JsonUtils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace sequence
{
    namespace
    {
        static std::string Context(const std::string& source, const std::string& nodeId, int actionIndex, const std::string& message)
        {
            std::ostringstream ss;
            ss << source;
            if (!nodeId.empty())
                ss << " | node '" << nodeId << "'";
            if (actionIndex >= 0)
                ss << " | action " << actionIndex;
            ss << ": " << message;
            return ss.str();
        }

        static std::vector<std::string> StringArray(const nlohmann::json& root, const char* key)
        {
            std::vector<std::string> out;
            if (!root.contains(key))
                return out;

            const auto& v = root.at(key);
            if (v.is_string())
            {
                const std::string s = v.get<std::string>();
                if (!s.empty())
                    out.push_back(s);
                return out;
            }

            if (!v.is_array())
                return out;

            for (const auto& item : v)
            {
                if (item.is_string())
                {
                    const std::string s = item.get<std::string>();
                    if (!s.empty())
                        out.push_back(s);
                }
            }
            return out;
        }

        static ActionDef ParseAction(const nlohmann::json& a)
        {
            ActionDef def;
            if (!a.is_object())
                return def;

            def.type = a.value("type", "");
            def.startDelay = a.value("startDelay", a.value("start_delay", 0.0f));
            def.duration = a.value("duration", 0.0f);
            def.blocking = a.value("blocking", true);
            def.data = a;

            if (a.contains("actions") && a["actions"].is_array())
            {
                for (const auto& child : a["actions"])
                    def.children.push_back(ParseAction(child));
            }

            return def;
        }

        static nlohmann::json ActionToJson(const ActionDef& action)
        {
            nlohmann::json out = action.data.is_object() ? action.data : nlohmann::json::object();
            out["type"] = action.type;
            if (action.startDelay > 0.0f)
                out["startDelay"] = action.startDelay;
            if (action.duration > 0.0f)
                out["duration"] = action.duration;
            if (!action.blocking)
                out["blocking"] = false;

            if (!action.children.empty())
            {
                out["actions"] = nlohmann::json::array();
                for (const auto& child : action.children)
                    out["actions"].push_back(ActionToJson(child));
            }
            return out;
        }
    }

    const NodeDef* SequenceDefinition::findNode(const std::string& nodeId) const
    {
        for (const auto& node : nodes)
        {
            if (node.id == nodeId)
                return &node;
        }
        return nullptr;
    }

    LoadResult SequenceFromJson(const nlohmann::json& root, const std::string& sourceName)
    {
        LoadResult result;
        if (!root.is_object())
        {
            result.errors.push_back(sourceName + ": root must be an object.");
            return result;
        }

        auto& seq = result.sequence;
        seq.id = root.value("id", "");
        seq.name = root.value("name", "");
        seq.start = root.value("start", root.value("initial_scene", ""));
        seq.flags = root.value("flags", nlohmann::json::object());
        seq.transitions = root.value("transitions", nlohmann::json::object());

        if (seq.id.empty())
            result.errors.push_back(sourceName + ": missing sequence id.");
        if (seq.start.empty())
            result.errors.push_back(sourceName + ": missing start node.");

        if (!root.contains("nodes") || !root["nodes"].is_array())
        {
            result.errors.push_back(sourceName + ": missing nodes array.");
            return result;
        }

        std::unordered_set<std::string> nodeIds;
        int nodeIndex = 0;
        for (const auto& n : root["nodes"])
        {
            NodeDef node;
            if (!n.is_object())
            {
                result.errors.push_back(Context(sourceName, "", nodeIndex, "node must be an object."));
                ++nodeIndex;
                continue;
            }

            node.id = n.value("id", "");
            node.requireFlags = StringArray(n, "require_flags");
            node.next = n.value("next", n.value("goto", ""));

            if (node.id.empty())
                result.errors.push_back(Context(sourceName, "", nodeIndex, "node id is empty."));
            else if (!nodeIds.insert(node.id).second)
                result.errors.push_back(Context(sourceName, node.id, -1, "duplicate node id."));

            if (n.contains("actions"))
            {
                if (!n["actions"].is_array())
                {
                    result.errors.push_back(Context(sourceName, node.id, -1, "actions must be an array."));
                }
                else
                {
                    int actionIndex = 0;
                    for (const auto& a : n["actions"])
                    {
                        ActionDef action = ParseAction(a);
                        if (action.type.empty())
                            result.errors.push_back(Context(sourceName, node.id, actionIndex, "missing action type."));
                        node.actions.push_back(std::move(action));
                        ++actionIndex;
                    }
                }
            }

            seq.nodes.push_back(std::move(node));
            ++nodeIndex;
        }

        if (!seq.start.empty() && !seq.findNode(seq.start))
            result.errors.push_back(sourceName + ": start node '" + seq.start + "' does not exist.");

        for (const auto& node : seq.nodes)
        {
            if (!node.next.empty() && !seq.findNode(node.next))
                result.errors.push_back(Context(sourceName, node.id, -1, "next node '" + node.next + "' does not exist."));

            for (int i = 0; i < (int)node.actions.size(); ++i)
            {
                const auto& action = node.actions[i];
                const std::string gotoTarget = action.data.value("target", action.data.value("node", ""));
                if (action.type == "goto" && !gotoTarget.empty() && !seq.findNode(gotoTarget))
                    result.errors.push_back(Context(sourceName, node.id, i, "goto target '" + gotoTarget + "' does not exist."));
            }
        }

        result.ok = result.errors.empty();
        return result;
    }

    LoadResult LoadSequenceFromFile(const std::string& path)
    {
        nlohmann::json root;
        std::string err;
        if (!jsonutils::LoadJsonFileSafe(path, root, err))
        {
            LoadResult result;
            result.errors.push_back(path + ": " + err);
            return result;
        }
        return SequenceFromJson(root, path);
    }

    nlohmann::json SequenceToJson(const SequenceDefinition& sequence)
    {
        nlohmann::json root;
        root["id"] = sequence.id;
        root["name"] = sequence.name;
        root["start"] = sequence.start;
        if (!sequence.flags.empty())
            root["flags"] = sequence.flags;
        if (!sequence.transitions.empty())
            root["transitions"] = sequence.transitions;

        root["nodes"] = nlohmann::json::array();
        for (const auto& node : sequence.nodes)
        {
            nlohmann::json n;
            n["id"] = node.id;
            if (!node.requireFlags.empty())
                n["require_flags"] = node.requireFlags;
            if (!node.next.empty())
                n["next"] = node.next;
            n["actions"] = nlohmann::json::array();
            for (const auto& action : node.actions)
                n["actions"].push_back(ActionToJson(action));
            root["nodes"].push_back(n);
        }
        return root;
    }

    bool SaveSequenceToFile(const SequenceDefinition& sequence, const std::string& path, std::string* outError)
    {
        try
        {
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            std::ofstream f(path, std::ios::binary);
            if (!f)
            {
                if (outError) *outError = "Cannot open for write: " + path;
                return false;
            }
            f << SequenceToJson(sequence).dump(4) << "\n";
            return true;
        }
        catch (const std::exception& e)
        {
            if (outError) *outError = e.what();
            return false;
        }
    }
}
