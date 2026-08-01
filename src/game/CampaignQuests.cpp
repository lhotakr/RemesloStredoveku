#include "Campaign.h"
#include "../JsonUtils.h"
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

bool Campaign::loadQuestDefs(const std::string& path, std::string* outError)
{
    json root;
    std::string err;

    if (!jsonutils::LoadJsonFileSafe(path, root, err))
    {
        if (outError) *outError = "QuestDefs: " + err;
        return false;
    }

    if (!root.contains("quests") || !root["quests"].is_array())
    {
        if (outError) *outError = "QuestDefs: chybi pole 'quests' v " + path;
        return false;
    }

    m_questDefs.clear();

    for (const auto& jq : root["quests"])
    {
        QuestDef q;
        q.id = jq.value("id", "");
        q.title = jq.value("title", "");
        q.description = jq.value("description", "");
        q.startedFlag = jq.value("started_flag", "");
        q.readyFlag = jq.value("ready_flag", "");
        q.doneFlag = jq.value("done_flag", "");

        if (!q.id.empty())
            m_questDefs.push_back(std::move(q));
    }

    return true;
}

bool Campaign::saveQuestDefs(const std::string& path) const
{
    json root;
    root["quests"] = json::array();

    for (const auto& q : m_questDefs)
    {
        root["quests"].push_back({
            {"id", q.id},
            {"title", q.title},
            {"description", q.description},
            {"started_flag", q.startedFlag},
            {"ready_flag", q.readyFlag},
            {"done_flag", q.doneFlag}
        });
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;

    f << root.dump(2);
    return true;
}