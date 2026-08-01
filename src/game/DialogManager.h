#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct DialogChoice
{
    std::string text;
    std::string nextNodeId;
    std::string style;
    int npcMoodDelta = 0;
    std::string setFlag;

    std::string requireFlag;
    std::string forbidFlag;
    int requireMoodMin = 0;
    bool closeDialog = false;
    std::string setNpcScript;
    std::string setNpcGreeting;
};

struct DialogNode
{
    std::string id;
    std::string speaker;
    std::string text;
    std::string requireFlag;
    std::string forbidFlag;
    std::vector<DialogChoice> choices;
};

struct DialogDefinition
{
    std::string dialogId;
    std::string startNodeId;
    std::unordered_map<std::string, DialogNode> nodes;
};

class DialogManager
{
public:
    bool loadFromFile(const std::string& path, std::string* outError = nullptr);
    void clear();

    const DialogDefinition* findDialog(const std::string& dialogId) const;
    const DialogNode* findNode(const DialogDefinition& dialog, const std::string& nodeId) const;

private:
    std::unordered_map<std::string, DialogDefinition> m_dialogs;
};