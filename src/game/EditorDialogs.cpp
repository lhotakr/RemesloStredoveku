#include "Editor.h"
#include "PathUtils.h"
#include <JsonUtils.h>

#include <algorithm>
#include <random>
#include <string>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include <SDL.h>
#include "imgui.h"


namespace fs = std::filesystem;
using json = nlohmann::json;

void Editor::renderDialogEditor()
{
    ImGui::SetNextWindowPos(ImVec2(1180, 580), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 560), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Dialog Editor"))
    {
        ImGui::End();
        return;
    }

    renderDialogEditorContents();
    ImGui::End();

    // Preview okno
    if (m_previewDialogIndex >= 0 && m_previewDialogIndex < (int)m_dialogDefs.size())
    {
        auto& dlg = m_dialogDefs[m_previewDialogIndex];

        ImGui::SetNextWindowSize(ImVec2(520, 320), ImGuiCond_FirstUseEver);
        ImGui::Begin("Dialog Preview");

        int nodeIndex = findDialogNodeIndexById(dlg, m_previewNodeId);

        if (nodeIndex < 0)
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Preview node nenalezen.");
            if (ImGui::Button("Close preview"))
            {
                m_previewDialogIndex = -1;
                m_previewNodeId.clear();
            }
            ImGui::End();
            return;
        }

        auto& node = dlg.nodes[nodeIndex];

        ImGui::Text("%s", node.speaker.empty() ? "(bez mluvciho)" : node.speaker.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s", node.text.c_str());
        ImGui::Spacing();

        for (int i = 0; i < (int)node.choices.size(); ++i)
        {
            std::string btnId = node.choices[i].text + "##preview_" + std::to_string(i);
            if (ImGui::Button(btnId.c_str(), ImVec2(-1, 0)))
            {
                if (node.choices[i].next.empty())
                {
                    m_previewDialogIndex = -1;
                    m_previewNodeId.clear();
                }
                else
                {
                    m_previewNodeId = node.choices[i].next;
                }
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Restart preview", ImVec2(-1, 0)))
            m_previewNodeId = dlg.startNode;

        if (ImGui::Button("Close preview", ImVec2(-1, 0)))
        {
            m_previewDialogIndex = -1;
            m_previewNodeId.clear();
        }

        ImGui::End();
    }
}

void Editor::renderDialogEditorContents()
{
    if (ImGui::Button("Reload dialogs"))
        loadDialogs();

    ImGui::SameLine();
    if (ImGui::Button("Save dialogs"))
        saveDialogs();

    ImGui::SameLine();
    if (ImGui::Button("New dialog"))
    {
        EditorDialogDef d;
        d.dialogId = "new_dialog";
        d.startNode = "start";
        m_dialogDefs.push_back(std::move(d));
        m_selectedDialogIndex = (int)m_dialogDefs.size() - 1;
        m_selectedDialogNodeIndex = -1;
        m_previewDialogIndex = -1;
        m_previewNodeId.clear();

        strncpy_s(m_dialogId, "new_dialog", _TRUNCATE);
        strncpy_s(m_dialogStartNode, "start", _TRUNCATE);
    }

    ImGui::SameLine();
    if (ImGui::Button("Delete dialog"))
    {
        if (m_selectedDialogIndex >= 0 && m_selectedDialogIndex < (int)m_dialogDefs.size())
        {
            m_dialogDefs.erase(m_dialogDefs.begin() + m_selectedDialogIndex);
            m_selectedDialogIndex = -1;
            m_selectedDialogNodeIndex = -1;
            m_previewDialogIndex = -1;
            m_previewNodeId.clear();

            m_dialogId[0] = '\0';
            m_dialogStartNode[0] = '\0';

            m_lastIoStatus = "Dialog deleted.";
        }
    }

    ImGui::Separator();

    if (ImGui::BeginListBox("Dialogs", ImVec2(180, 140)))
    {
        for (int i = 0; i < (int)m_dialogDefs.size(); ++i)
        {
            bool selected = (m_selectedDialogIndex == i);
            if (ImGui::Selectable(m_dialogDefs[i].dialogId.c_str(), selected))
            {
                m_selectedDialogIndex = i;
                m_selectedDialogNodeIndex = -1;
                m_previewDialogIndex = -1;
                m_previewNodeId.clear();

                strncpy_s(m_dialogId, m_dialogDefs[i].dialogId.c_str(), _TRUNCATE);
                strncpy_s(m_dialogStartNode, m_dialogDefs[i].startNode.c_str(), _TRUNCATE);
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    if (m_selectedDialogIndex >= 0 && m_selectedDialogIndex < (int)m_dialogDefs.size())
    {
        auto& dlg = m_dialogDefs[m_selectedDialogIndex];

        ImGui::Separator();
        ImGui::InputText("Dialog ID", m_dialogId, IM_ARRAYSIZE(m_dialogId));
        ImGui::InputText("Start node", m_dialogStartNode, IM_ARRAYSIZE(m_dialogStartNode));

        if (std::string(m_dialogId).empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Dialog ID je prazdne");
        else if (dialogIdDuplicate(m_dialogId, m_selectedDialogIndex))
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Dialog ID uz existuje");
        else
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Dialog ID OK");

        if (ImGui::Button("Apply dialog header"))
        {
            if (std::string(m_dialogId).empty())
            {
                m_lastIoStatus = "Dialog ID nesmi byt prazdne.";
            }
            else if (dialogIdDuplicate(m_dialogId, m_selectedDialogIndex))
            {
                m_lastIoStatus = "Dialog ID uz existuje.";
            }
            else
            {
                dlg.dialogId = m_dialogId;
                dlg.startNode = m_dialogStartNode;
                m_lastIoStatus = "Dialog header updated.";
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Preview dialog"))
        {
            m_previewDialogIndex = m_selectedDialogIndex;
            m_previewNodeId = dlg.startNode;
        }

        if (!dialogNodeExists(dlg, dlg.startNode))
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Start node nenalezen");

        ImGui::Separator();
        ImGui::Text("Nodes");

        if (ImGui::BeginListBox("Nodes", ImVec2(180, 140)))
        {
            for (int i = 0; i < (int)dlg.nodes.size(); ++i)
            {
                bool selected = (m_selectedDialogNodeIndex == i);
                if (ImGui::Selectable(dlg.nodes[i].id.c_str(), selected))
                {
                    m_selectedDialogNodeIndex = i;
                    strncpy_s(m_nodeId, dlg.nodes[i].id.c_str(), _TRUNCATE);
                    strncpy_s(m_nodeSpeaker, dlg.nodes[i].speaker.c_str(), _TRUNCATE);
                    strncpy_s(m_nodeText, dlg.nodes[i].text.c_str(), _TRUNCATE);
                    strncpy_s(m_nodeRequireFlag, dlg.nodes[i].requireFlag.c_str(), _TRUNCATE);
                    strncpy_s(m_nodeForbidFlag, dlg.nodes[i].forbidFlag.c_str(), _TRUNCATE);
                }

                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }

        if (ImGui::Button("New node"))
        {
            EditorDialogNode n;
            n.id = "node_" + std::to_string((int)dlg.nodes.size() + 1);
            dlg.nodes.push_back(std::move(n));
            m_selectedDialogNodeIndex = (int)dlg.nodes.size() - 1;
        }

        ImGui::SameLine();

        if (ImGui::Button("Delete node"))
        {
            if (m_selectedDialogNodeIndex >= 0 && m_selectedDialogNodeIndex < (int)dlg.nodes.size())
            {
                dlg.nodes.erase(dlg.nodes.begin() + m_selectedDialogNodeIndex);
                m_selectedDialogNodeIndex = -1;
                m_lastIoStatus = "Node deleted.";
            }
        }

        if (m_selectedDialogNodeIndex >= 0 && m_selectedDialogNodeIndex < (int)dlg.nodes.size())
        {
            auto& node = dlg.nodes[m_selectedDialogNodeIndex];

            ImGui::Separator();
            ImGui::InputText("Node ID", m_nodeId, IM_ARRAYSIZE(m_nodeId));
            ImGui::InputText("Speaker", m_nodeSpeaker, IM_ARRAYSIZE(m_nodeSpeaker));
            ImGui::InputTextMultiline("Text", m_nodeText, IM_ARRAYSIZE(m_nodeText), ImVec2(-1, 90));
            ImGui::InputText("Require flag", m_nodeRequireFlag, IM_ARRAYSIZE(m_nodeRequireFlag));
            ImGui::InputText("Forbid flag", m_nodeForbidFlag, IM_ARRAYSIZE(m_nodeForbidFlag));

            if (std::string(m_nodeId).empty())
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Node ID je prazdne");
            else if (nodeIdDuplicate(dlg, m_nodeId, m_selectedDialogNodeIndex))
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Node ID uz existuje");
            else
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Node ID OK");

            if (ImGui::Button("Apply node"))
            {
                if (std::string(m_nodeId).empty())
                {
                    m_lastIoStatus = "Node ID nesmi byt prazdne.";
                }
                else if (nodeIdDuplicate(dlg, m_nodeId, m_selectedDialogNodeIndex))
                {
                    m_lastIoStatus = "Node ID uz existuje.";
                }
                else
                {
                    node.id = m_nodeId;
                    node.speaker = m_nodeSpeaker;
                    node.text = m_nodeText;
                    node.requireFlag = m_nodeRequireFlag;
                    node.forbidFlag = m_nodeForbidFlag;
                    m_lastIoStatus = "Node updated.";
                }
            }

            ImGui::Separator();
            ImGui::Text("Choices");

            for (int i = 0; i < (int)node.choices.size(); ++i)
            {
                ImGui::PushID(i);

                char choiceTextBuf[256];
                char choiceSetFlagBuf[64];
                char choiceRequireFlagBuf[64];
                char choiceForbidFlagBuf[64];
                char choiceSetNpcScriptBuf[64];
                char choiceSetNpcGreetingBuf[256];

                strncpy_s(choiceTextBuf, node.choices[i].text.c_str(), _TRUNCATE);
                strncpy_s(choiceSetFlagBuf, node.choices[i].setFlag.c_str(), _TRUNCATE);
                strncpy_s(choiceRequireFlagBuf, node.choices[i].requireFlag.c_str(), _TRUNCATE);
                strncpy_s(choiceForbidFlagBuf, node.choices[i].forbidFlag.c_str(), _TRUNCATE);
                strncpy_s(choiceSetNpcScriptBuf, node.choices[i].setNpcScript.c_str(), _TRUNCATE);
                strncpy_s(choiceSetNpcGreetingBuf, node.choices[i].setNpcGreeting.c_str(), _TRUNCATE);

                ImGui::InputText("Choice text", choiceTextBuf, IM_ARRAYSIZE(choiceTextBuf));
                node.choices[i].text = choiceTextBuf;

                const char* styleLabels[] = { "(none)", "polite", "neutral", "rude" };
                int currentStyleIndex = 0;

                if (node.choices[i].style == "polite") currentStyleIndex = 1;
                else if (node.choices[i].style == "neutral") currentStyleIndex = 2;
                else if (node.choices[i].style == "rude") currentStyleIndex = 3;

                ImGui::Combo("Style", &currentStyleIndex, styleLabels, IM_ARRAYSIZE(styleLabels));

                if (currentStyleIndex == 0) node.choices[i].style.clear();
                else if (currentStyleIndex == 1) node.choices[i].style = "polite";
                else if (currentStyleIndex == 2) node.choices[i].style = "neutral";
                else if (currentStyleIndex == 3) node.choices[i].style = "rude";

                int currentNextIndex = 0;
                if (!node.choices[i].next.empty())
                {
                    int found = findDialogNodeIndexById(dlg, node.choices[i].next);
                    if (found >= 0)
                        currentNextIndex = found + 1;
                }

                std::vector<const char*> localNodeLabels;
                localNodeLabels.reserve(dlg.nodes.size() + 1);
                localNodeLabels.push_back("(end)");
                for (auto& n : dlg.nodes)
                    localNodeLabels.push_back(n.id.c_str());

                ImGui::Combo("Next", &currentNextIndex, localNodeLabels.data(), (int)localNodeLabels.size());

                if (currentNextIndex == 0)
                    node.choices[i].next.clear();
                else
                    node.choices[i].next = dlg.nodes[currentNextIndex - 1].id;

                if (!dialogNodeExists(dlg, node.choices[i].next))
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Neplatny next odkaz");

                ImGui::SliderInt("NPC mood delta", &node.choices[i].npcMoodDelta, -100, 100);

                ImGui::InputText("Set flag", choiceSetFlagBuf, IM_ARRAYSIZE(choiceSetFlagBuf));
                ImGui::InputText("Choice require flag", choiceRequireFlagBuf, IM_ARRAYSIZE(choiceRequireFlagBuf));
                ImGui::InputText("Choice forbid flag", choiceForbidFlagBuf, IM_ARRAYSIZE(choiceForbidFlagBuf));
                ImGui::SliderInt("Choice require mood", &node.choices[i].requireMoodMin, 0, 100);
                ImGui::Checkbox("Close dialog", &node.choices[i].closeDialog);
                ImGui::InputText("Set NPC script", choiceSetNpcScriptBuf, IM_ARRAYSIZE(choiceSetNpcScriptBuf));
                ImGui::InputText("Set NPC greeting", choiceSetNpcGreetingBuf, IM_ARRAYSIZE(choiceSetNpcGreetingBuf));

                node.choices[i].setFlag = choiceSetFlagBuf;
                node.choices[i].requireFlag = choiceRequireFlagBuf;
                node.choices[i].forbidFlag = choiceForbidFlagBuf;
                node.choices[i].setNpcScript = choiceSetNpcScriptBuf;
                node.choices[i].setNpcGreeting = choiceSetNpcGreetingBuf;

                if (ImGui::Button("Delete choice"))
                {
                    node.choices.erase(node.choices.begin() + i);
                    ImGui::PopID();
                    break;
                }

                ImGui::Separator();
                ImGui::PopID();
            }

            if (ImGui::Button("Add choice"))
            {
                EditorDialogChoice ch;
                ch.text = "Nova volba";
                ch.next = "";
                node.choices.push_back(std::move(ch));
            }
        }
    }
    else
    {
        ImGui::TextUnformatted("No dialog selected.");
    }
}

void Editor::renderDialogPreviewWindow()
{
    if (m_previewDialogIndex < 0 || m_previewDialogIndex >= (int)m_dialogDefs.size())
        return;

    auto& dlg = m_dialogDefs[m_previewDialogIndex];

    ImGui::SetNextWindowSize(ImVec2(520, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dialog Preview"))
    {
        ImGui::End();
        return;
    }

    int nodeIndex = findDialogNodeIndexById(dlg, m_previewNodeId);

    if (nodeIndex < 0)
    {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Preview node nenalezen.");
        if (ImGui::Button("Close preview"))
        {
            m_previewDialogIndex = -1;
            m_previewNodeId.clear();
        }
        ImGui::End();
        return;
    }

    auto& node = dlg.nodes[nodeIndex];

    ImGui::Text("%s", node.speaker.empty() ? "(bez mluvciho)" : node.speaker.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("%s", node.text.c_str());
    ImGui::Spacing();

    for (int i = 0; i < (int)node.choices.size(); ++i)
    {
        std::string btnId = node.choices[i].text + "##preview_" + std::to_string(i);
        if (ImGui::Button(btnId.c_str(), ImVec2(-1, 0)))
        {
            if (node.choices[i].next.empty())
            {
                m_previewDialogIndex = -1;
                m_previewNodeId.clear();
            }
            else
            {
                m_previewNodeId = node.choices[i].next;
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Restart preview", ImVec2(-1, 0)))
        m_previewNodeId = dlg.startNode;

    if (ImGui::Button("Close preview", ImVec2(-1, 0)))
    {
        m_previewDialogIndex = -1;
        m_previewNodeId.clear();
    }

    ImGui::End();
}

bool Editor::dialogNodeExists(const EditorDialogDef& dlg, const std::string& nodeId) const
{
    if (nodeId.empty())
        return true;

    for (const auto& node : dlg.nodes)
    {
        if (node.id == nodeId)
            return true;
    }

    return false;
}

int Editor::findDialogNodeIndexById(const EditorDialogDef& dlg, const std::string& nodeId) const
{
    for (int i = 0; i < (int)dlg.nodes.size(); ++i)
    {
        if (dlg.nodes[i].id == nodeId)
            return i;
    }

    return -1;
}

bool Editor::dialogIdDuplicate(const std::string& dialogId, int ignoreIndex) const
{
    if (dialogId.empty())
        return false;

    for (int i = 0; i < (int)m_dialogDefs.size(); ++i)
    {
        if (i == ignoreIndex)
            continue;

        if (m_dialogDefs[i].dialogId == dialogId)
            return true;
    }

    return false;
}

bool Editor::nodeIdDuplicate(const EditorDialogDef& dlg, const std::string& nodeId, int ignoreIndex) const
{
    if (nodeId.empty())
        return false;

    for (int i = 0; i < (int)dlg.nodes.size(); ++i)
    {
        if (i == ignoreIndex)
            continue;

        if (dlg.nodes[i].id == nodeId)
            return true;
    }

    return false;
}

bool Editor::loadDialogs()
{
    m_dialogDefs.clear();

    fs::path p = pathutils::DataDir() / "dialogs" / "dialogs.json";

    nlohmann::json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(p.string(), root, err)) {
        m_lastIoStatus = "Dialogs parse failed: " + err;
        return false;
    }

    if (!root.contains("dialogs") || !root["dialogs"].is_array()) {
        m_lastIoStatus = "Dialogs missing 'dialogs' array";
        return false;
    }

    for (const auto& jd : root["dialogs"])
    {
        EditorDialogDef def;
        def.dialogId = jd.value("dialog_id", "");
        def.startNode = jd.value("start_node", "");

        if (jd.contains("nodes") && jd["nodes"].is_array())
        {
            for (const auto& jn : jd["nodes"])
            {
                EditorDialogNode node;
                node.id = jn.value("id", "");
                node.speaker = jn.value("speaker", "");
                node.text = jn.value("text", "");
                node.requireFlag = jn.value("require_flag", "");
                node.forbidFlag = jn.value("forbid_flag", "");

                if (jn.contains("choices") && jn["choices"].is_array())
                {
                    for (const auto& jc : jn["choices"])
                    {
                        EditorDialogChoice ch;
                        ch.text = jc.value("text", "");
                        ch.next = jc.value("next", "");
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

                def.nodes.push_back(std::move(node));
            }
        }

        if (!def.dialogId.empty())
            m_dialogDefs.push_back(std::move(def));
    }

    if (!m_dialogDefs.empty())
        m_selectedDialogIndex = 0;
    else
        m_selectedDialogIndex = -1;

    m_selectedDialogNodeIndex = -1;
    m_lastIoStatus = "Dialogs loaded";
    return true;
}

bool Editor::saveDialogs()
{
    fs::path p = pathutils::DataDir() / "dialogs" / "dialogs.json";
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    json root;
    root["dialogs"] = json::array();

    for (const auto& def : m_dialogDefs)
    {
        json jd;
        jd["dialog_id"] = def.dialogId;
        jd["start_node"] = def.startNode;
        jd["nodes"] = json::array();

        for (const auto& node : def.nodes)
        {
            json jn;
            jn["id"] = node.id;
            jn["speaker"] = node.speaker;
            jn["text"] = node.text;
            jn["choices"] = json::array();

            if (!node.requireFlag.empty())
                jn["require_flag"] = node.requireFlag;

            if (!node.forbidFlag.empty())
                jn["forbid_flag"] = node.forbidFlag;

            for (const auto& ch : node.choices)
            {
                json jc = {
                    {"text", ch.text},
                    {"next", ch.next}
                };

                if (!ch.style.empty())
                    jc["style"] = ch.style;
                if (!ch.requireFlag.empty())
                    jc["require_flag"] = ch.requireFlag;
                if (!ch.forbidFlag.empty())
                    jc["forbid_flag"] = ch.forbidFlag;
                if (ch.requireMoodMin > 0)
                    jc["require_mood_min"] = ch.requireMoodMin;
                if (ch.closeDialog)
                    jc["close_dialog"] = true;
                if (!ch.setNpcScript.empty())
                    jc["set_npc_script"] = ch.setNpcScript;
                if (!ch.setNpcGreeting.empty())
                    jc["set_npc_greeting"] = ch.setNpcGreeting;
                if (ch.npcMoodDelta != 0)
                    jc["npc_mood_delta"] = ch.npcMoodDelta;
                if (!ch.setFlag.empty())
                    jc["set_flag"] = ch.setFlag;

                jn["choices"].push_back(std::move(jc));
            }

            jd["nodes"].push_back(std::move(jn));
        }

        root["dialogs"].push_back(std::move(jd));
    }

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        m_lastIoStatus = "Dialogs save failed: " + p.string();
        return false;
    }

    f << root.dump(2);
    m_lastIoStatus = "Dialogs saved";
    return true;
}

int Editor::findDialogIndexById(const std::string& dialogId) const
{
    for (int i = 0; i < (int)m_dialogDefs.size(); ++i)
    {
        if (m_dialogDefs[i].dialogId == dialogId)
            return i;
    }
    return -1;
}

bool Editor::dialogIdExists(const std::string& dialogId) const
{
    return findDialogIndexById(dialogId) >= 0;
}