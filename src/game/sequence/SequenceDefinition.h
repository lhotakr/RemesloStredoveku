#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace sequence
{
    struct ActionDef
    {
        std::string type;
        float startDelay = 0.0f;
        float duration = 0.0f;
        bool blocking = true;
        nlohmann::json data = nlohmann::json::object();
        std::vector<ActionDef> children;
    };

    struct NodeDef
    {
        std::string id;
        std::vector<std::string> requireFlags;
        std::vector<ActionDef> actions;
        std::string next;
    };

    struct SequenceDefinition
    {
        std::string id;
        std::string name;
        std::string start;
        bool skippable = false;
        nlohmann::json flags = nlohmann::json::object();
        nlohmann::json transitions = nlohmann::json::object();
        std::vector<ActionDef> onSkipActions;
        std::vector<NodeDef> nodes;

        const NodeDef* findNode(const std::string& nodeId) const;
    };

    struct LoadResult
    {
        bool ok = false;
        SequenceDefinition sequence;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    LoadResult LoadSequenceFromFile(const std::string& path);
    bool SaveSequenceToFile(const SequenceDefinition& sequence, const std::string& path, std::string* outError = nullptr);
    nlohmann::json SequenceToJson(const SequenceDefinition& sequence);
    LoadResult SequenceFromJson(const nlohmann::json& root, const std::string& sourceName);
}
