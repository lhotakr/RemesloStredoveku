#include "Editor.h"
#include "PathUtils.h"
#include <JsonUtils.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "imgui.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

void Editor::renderQuestEditor()
{
    if (!ImGui::Begin("Quest Editor"))
    {
        ImGui::End();
        return;
    }

    renderQuestEditorContents();
    ImGui::End();
}

void Editor::renderQuestEditorContents()
{
    if (ImGui::Button("Load quests"))
        loadQuests();

    ImGui::SameLine();

    if (ImGui::Button("Save quests"))
        saveQuests();

    ImGui::Separator();

    if (ImGui::Button("New quest"))
    {
        EditorQuestDef q;
        q.id = "new_quest";
        q.title = "New quest";
        m_questDefs.push_back(std::move(q));
        m_selectedQuestIndex = (int)m_questDefs.size() - 1;
    }

    ImGui::SameLine();

    if (m_selectedQuestIndex < 0 || m_selectedQuestIndex >= (int)m_questDefs.size())
        ImGui::BeginDisabled();

    if (ImGui::Button("Delete quest"))
    {
        if (m_selectedQuestIndex >= 0 &&
            m_selectedQuestIndex < (int)m_questDefs.size())
        {
            m_questDefs.erase(m_questDefs.begin() + m_selectedQuestIndex);

            if (m_questDefs.empty())
                m_selectedQuestIndex = -1;
            else if (m_selectedQuestIndex >= (int)m_questDefs.size())
                m_selectedQuestIndex = (int)m_questDefs.size() - 1;

            m_lastIoStatus = "Quest deleted.";
        }
    }

    if (m_selectedQuestIndex < 0 || m_selectedQuestIndex >= (int)m_questDefs.size())
        ImGui::EndDisabled();

    for (int i = 0; i < (int)m_questDefs.size(); ++i)
    {
        const bool sel = (i == m_selectedQuestIndex);
        if (ImGui::Selectable(m_questDefs[i].id.c_str(), sel))
            m_selectedQuestIndex = i;
    }

    ImGui::Separator();

    if (m_selectedQuestIndex >= 0 &&
        m_selectedQuestIndex < (int)m_questDefs.size())
    {
        auto& q = m_questDefs[m_selectedQuestIndex];

        char questIdBuf[128]{};
        char titleBuf[128]{};
        char descBuf[1024]{};
        char startedBuf[128]{};
        char readyBuf[128]{};
        char doneBuf[128]{};

        strncpy_s(questIdBuf, q.id.c_str(), _TRUNCATE);
        strncpy_s(titleBuf, q.title.c_str(), _TRUNCATE);
        strncpy_s(descBuf, q.description.c_str(), _TRUNCATE);
        strncpy_s(startedBuf, q.startedFlag.c_str(), _TRUNCATE);
        strncpy_s(readyBuf, q.readyFlag.c_str(), _TRUNCATE);
        strncpy_s(doneBuf, q.doneFlag.c_str(), _TRUNCATE);

        if (ImGui::InputText("Quest ID", questIdBuf, IM_ARRAYSIZE(questIdBuf)))
            q.id = questIdBuf;

        if (ImGui::InputText("Title", titleBuf, IM_ARRAYSIZE(titleBuf)))
            q.title = titleBuf;

        if (ImGui::InputTextMultiline("Description", descBuf, IM_ARRAYSIZE(descBuf)))
            q.description = descBuf;

        if (ImGui::InputText("Started flag", startedBuf, IM_ARRAYSIZE(startedBuf)))
            q.startedFlag = startedBuf;

        if (ImGui::InputText("Ready flag", readyBuf, IM_ARRAYSIZE(readyBuf)))
            q.readyFlag = readyBuf;

        if (ImGui::InputText("Done flag", doneBuf, IM_ARRAYSIZE(doneBuf)))
            q.doneFlag = doneBuf;
    }
    else
    {
        ImGui::TextUnformatted("No quest selected.");
    }
}

bool Editor::loadQuests()
{
    m_questDefs.clear();

    fs::path p = pathutils::DataDir() / "quests" / "quests.json";

    nlohmann::json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(p.string(), root, err))
    {
        m_lastIoStatus = "Quests parse failed: " + err;
        return false;
    }

    if (!root.contains("quests") || !root["quests"].is_array())
    {
        m_lastIoStatus = "Quests missing 'quests' array";
        return false;
    }

    for (const auto& jq : root["quests"])
    {
        EditorQuestDef q;
        q.id = jq.value("id", "");
        q.title = jq.value("title", "");
        q.description = jq.value("description", "");
        q.startedFlag = jq.value("started_flag", "");
        q.readyFlag = jq.value("ready_flag", "");
        q.doneFlag = jq.value("done_flag", "");

        if (!q.id.empty())
            m_questDefs.push_back(std::move(q));
    }

    m_selectedQuestIndex = m_questDefs.empty() ? -1 : 0;
    m_lastIoStatus = "Quests loaded";
    return true;
}

bool Editor::saveQuests()
{
    fs::path p = pathutils::DataDir() / "quests" / "quests.json";
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

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

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        m_lastIoStatus = "Quests save failed: " + p.string();
        return false;
    }

    f << root.dump(2);
    m_lastIoStatus = "Quests saved";
    return true;
}

int Editor::findQuestIndexById(const std::string& questId) const
{
    for (int i = 0; i < (int)m_questDefs.size(); ++i)
    {
        if (m_questDefs[i].id == questId)
            return i;
    }
    return -1;
}

bool Editor::questIdDuplicate(const std::string& questId, int ignoreIndex) const
{
    if (questId.empty())
        return false;

    for (int i = 0; i < (int)m_questDefs.size(); ++i)
    {
        if (i == ignoreIndex)
            continue;

        if (m_questDefs[i].id == questId)
            return true;
    }

    return false;
}
