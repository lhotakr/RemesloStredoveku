#include "Editor.h"
#include "PathUtils.h"
#include <JsonUtils.h>

#include <algorithm>
#include <cctype>
#include <random>
#include <string>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include <SDL.h>
#include "imgui.h"


namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
    constexpr float kDialogGraphNodeWidth = 260.0f;
    constexpr float kDialogGraphHeaderHeight = 30.0f;
    constexpr float kDialogGraphChoiceHeight = 42.0f;
    constexpr float kDialogGraphNodePadding = 12.0f;
    constexpr float kDialogGraphPinRadius = 5.0f;

    struct QuestFlagOption
    {
        std::string value;
        std::string source;
    };

    static float ClampFloat(float value, float minValue, float maxValue)
    {
        return std::max(minValue, std::min(value, maxValue));
    }

    static void CopyStringToBuffer(char* buffer, size_t bufferSize, const std::string& value)
    {
        if (!buffer || bufferSize == 0)
            return;

        std::strncpy(buffer, value.c_str(), bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
    }

    static void AppendQuestFlagOption(
        std::vector<QuestFlagOption>& options,
        const std::string& flag,
        const std::string& source)
    {
        if (flag.empty())
            return;

        const auto existing = std::find_if(
            options.begin(),
            options.end(),
            [&](const QuestFlagOption& option)
            {
                return option.value == flag;
            });

        if (existing == options.end())
            options.push_back(QuestFlagOption{ flag, source });
    }

    static std::vector<QuestFlagOption> BuildQuestFlagOptions(const std::vector<EditorQuestDef>& quests)
    {
        std::vector<QuestFlagOption> options;
        options.reserve(quests.size() * 3);

        for (const auto& quest : quests)
        {
            const std::string prefix = quest.id.empty() ? std::string("(quest)") : quest.id;
            AppendQuestFlagOption(options, quest.startedFlag, prefix + " started");
            AppendQuestFlagOption(options, quest.readyFlag, prefix + " ready");
            AppendQuestFlagOption(options, quest.doneFlag, prefix + " done");
        }

        return options;
    }

    static bool ContainsNoCaseLocal(const std::string& value, const char* filter)
    {
        if (!filter || filter[0] == '\0')
            return true;

        std::string haystack = value;
        std::string needle = filter;

        std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c)
        {
            return (char)std::tolower(c);
        });

        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c)
        {
            return (char)std::tolower(c);
        });

        return haystack.find(needle) != std::string::npos;
    }

    static void AddUniqueString(std::vector<std::string>& values, const std::string& value)
    {
        if (value.empty())
            return;

        if (std::find(values.begin(), values.end(), value) == values.end())
            values.push_back(value);
    }

    static bool ContainsString(const std::vector<std::string>& values, const std::string& value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    static void RemoveString(std::vector<std::string>& values, const std::string& value)
    {
        values.erase(std::remove(values.begin(), values.end(), value), values.end());
    }

    static std::string FirstFlagOrEmpty(const std::vector<std::string>& flags)
    {
        return flags.empty() ? std::string() : flags.front();
    }

    static void SyncLegacyFlag(std::string& legacyFlag, const std::vector<std::string>& flags)
    {
        legacyFlag = FirstFlagOrEmpty(flags);
    }

    static void NormalizeFlags(std::vector<std::string>& flags, const std::string& legacyFlag)
    {
        if (!legacyFlag.empty())
            AddUniqueString(flags, legacyFlag);
    }

    static std::vector<std::string> LoadDialogFlags(const json& object, const char* singularKey, const char* pluralKey)
    {
        std::vector<std::string> flags;
        AddUniqueString(flags, object.value(singularKey, ""));

        if (object.contains(pluralKey) && object[pluralKey].is_array())
        {
            for (const auto& flag : object[pluralKey])
            {
                if (flag.is_string())
                    AddUniqueString(flags, flag.get<std::string>());
            }
        }

        return flags;
    }

    static void WriteDialogFlags(json& object, const char* singularKey, const char* pluralKey, const std::vector<std::string>& flags)
    {
        if (flags.empty())
            return;

        if (flags.size() == 1)
        {
            object[singularKey] = flags.front();
            return;
        }

        object[pluralKey] = json::array();
        for (const auto& flag : flags)
        {
            if (!flag.empty())
                object[pluralKey].push_back(flag);
        }
    }

    static std::string JoinFlags(const std::vector<std::string>& flags, const char* separator = ", ")
    {
        std::string result;
        for (const auto& flag : flags)
        {
            if (flag.empty())
                continue;

            if (!result.empty())
                result += separator;

            result += flag;
        }

        return result;
    }

    static std::string FlagSummary(const char* prefix, const std::vector<std::string>& flags)
    {
        if (flags.empty())
            return {};

        return std::string(prefix) + ": " + JoinFlags(flags, ", ");
    }

    static std::string ChoiceFlagSummary(const EditorDialogChoice& choice)
    {
        std::vector<std::string> setFlagValues = choice.setFlags;
        std::vector<std::string> requireFlagValues = choice.requireFlags;
        std::vector<std::string> forbidFlagValues = choice.forbidFlags;
        NormalizeFlags(setFlagValues, choice.setFlag);
        NormalizeFlags(requireFlagValues, choice.requireFlag);
        NormalizeFlags(forbidFlagValues, choice.forbidFlag);

        std::vector<std::string> parts;
        const std::string setFlags = FlagSummary("set", setFlagValues);
        const std::string requireFlags = FlagSummary("req", requireFlagValues);
        const std::string forbidFlags = FlagSummary("block", forbidFlagValues);

        if (!setFlags.empty()) parts.push_back(setFlags);
        if (!requireFlags.empty()) parts.push_back(requireFlags);
        if (!forbidFlags.empty()) parts.push_back(forbidFlags);
        if (choice.requireMoodMin > 0) parts.push_back("mood >= " + std::to_string(choice.requireMoodMin));
        if (choice.closeDialog) parts.push_back("close");

        return JoinFlags(parts, " | ");
    }

    static std::string NodeFlagSummary(const EditorDialogNode& node)
    {
        std::vector<std::string> requireFlagValues = node.requireFlags;
        std::vector<std::string> forbidFlagValues = node.forbidFlags;
        NormalizeFlags(requireFlagValues, node.requireFlag);
        NormalizeFlags(forbidFlagValues, node.forbidFlag);

        std::vector<std::string> parts;
        const std::string requireFlags = FlagSummary("req", requireFlagValues);
        const std::string forbidFlags = FlagSummary("block", forbidFlagValues);

        if (!requireFlags.empty()) parts.push_back(requireFlags);
        if (!forbidFlags.empty()) parts.push_back(forbidFlags);

        return JoinFlags(parts, " | ");
    }

    static bool IsKnownQuestFlag(const std::vector<QuestFlagOption>& options, const std::string& value);

    static bool DrawQuestFlagMultiCombo(const char* label, std::vector<std::string>& values, const std::vector<EditorQuestDef>& quests)
    {
        const std::vector<QuestFlagOption> options = BuildQuestFlagOptions(quests);
        const std::string preview = values.empty() ? std::string("(none)") : JoinFlags(values, ", ");
        bool changed = false;

        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            static char filter[128] = "";
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("Filter##quest_flag_filter", filter, IM_ARRAYSIZE(filter));

            if (ImGui::SmallButton("Clear##quest_flags"))
            {
                values.clear();
                changed = true;
            }

            ImGui::SameLine();
            ImGui::TextDisabled("%d selected", (int)values.size());
            ImGui::Separator();

            for (int valueIndex = 0; valueIndex < (int)values.size(); ++valueIndex)
            {
                const std::string value = values[valueIndex];
                const bool known = IsKnownQuestFlag(options, value);
                if (known)
                    continue;

                if (!ContainsNoCaseLocal(value, filter))
                    continue;

                bool selected = true;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.32f, 1.0f));
                if (ImGui::Selectable(value.c_str(), &selected, ImGuiSelectableFlags_DontClosePopups))
                {
                    RemoveString(values, value);
                    changed = true;
                    ImGui::PopStyleColor();
                    break;
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("Flag neni v quests.json. Odskrtnutim ho odeberes.");
            }

            for (const auto& option : options)
            {
                if (!ContainsNoCaseLocal(option.value + " " + option.source, filter))
                    continue;

                bool selected = ContainsString(values, option.value);
                if (ImGui::Selectable(option.value.c_str(), &selected, ImGuiSelectableFlags_DontClosePopups))
                {
                    if (selected)
                        AddUniqueString(values, option.value);
                    else
                        RemoveString(values, option.value);

                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", option.source.c_str());
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    static bool IsKnownQuestFlag(const std::vector<QuestFlagOption>& options, const std::string& value)
    {
        if (value.empty())
            return true;

        return std::find_if(
            options.begin(),
            options.end(),
            [&](const QuestFlagOption& option)
            {
                return option.value == value;
            }) != options.end();
    }

    static bool DrawQuestFlagCombo(const char* label, std::string& value, const std::vector<EditorQuestDef>& quests)
    {
        const std::vector<QuestFlagOption> options = BuildQuestFlagOptions(quests);
        const bool known = IsKnownQuestFlag(options, value);
        const std::string preview = value.empty() ? std::string("(none)") : value;
        bool changed = false;

        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            const bool noneSelected = value.empty();
            if (ImGui::Selectable("(none)", noneSelected))
            {
                value.clear();
                changed = true;
            }
            if (noneSelected)
                ImGui::SetItemDefaultFocus();

            if (!value.empty() && !known)
            {
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.32f, 1.0f));
                ImGui::Selectable(value.c_str(), true, ImGuiSelectableFlags_Disabled);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("Flag neni v quests.json.");
                ImGui::Separator();
            }

            for (const auto& option : options)
            {
                const bool selected = (value == option.value);
                if (ImGui::Selectable(option.value.c_str(), selected))
                {
                    value = option.value;
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", option.source.c_str());
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        if (!value.empty() && !known)
            ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.32f, 1.0f), "Flag neni v quests.json");

        return changed;
    }

    static bool DrawQuestFlagComboToBuffer(
        const char* label,
        char* buffer,
        size_t bufferSize,
        const std::vector<EditorQuestDef>& quests)
    {
        std::string value = buffer ? buffer : "";
        if (!DrawQuestFlagCombo(label, value, quests))
            return false;

        CopyStringToBuffer(buffer, bufferSize, value);
        return true;
    }

    static std::string DialogGraphKey(const EditorDialogDef& dialog, const std::string& nodeId)
    {
        return dialog.dialogId + "\x1f" + nodeId;
    }

    static int FindDialogGraphNodeIndex(const EditorDialogDef& dialog, const std::string& nodeId)
    {
        if (nodeId.empty())
            return -1;

        for (int i = 0; i < (int)dialog.nodes.size(); ++i)
        {
            if (dialog.nodes[i].id == nodeId)
                return i;
        }

        return -1;
    }

    static std::vector<int> BuildDialogGraphDepths(const EditorDialogDef& dialog)
    {
        std::vector<int> depths(dialog.nodes.size(), -1);
        if (dialog.nodes.empty())
            return depths;

        int startIndex = FindDialogGraphNodeIndex(dialog, dialog.startNode);
        if (startIndex < 0)
            startIndex = 0;

        std::vector<int> queue;
        queue.push_back(startIndex);
        depths[startIndex] = 0;

        for (size_t cursor = 0; cursor < queue.size(); ++cursor)
        {
            const int nodeIndex = queue[cursor];
            const int nextDepth = depths[nodeIndex] + 1;

            for (const auto& choice : dialog.nodes[nodeIndex].choices)
            {
                const int targetIndex = FindDialogGraphNodeIndex(dialog, choice.next);
                if (targetIndex < 0)
                    continue;

                if (depths[targetIndex] < 0 || depths[targetIndex] > nextDepth)
                {
                    depths[targetIndex] = nextDepth;
                    queue.push_back(targetIndex);
                }
            }
        }

        return depths;
    }

    static ImVec2 Add(const ImVec2& a, const ImVec2& b)
    {
        return ImVec2(a.x + b.x, a.y + b.y);
    }

    static ImVec2 Sub(const ImVec2& a, const ImVec2& b)
    {
        return ImVec2(a.x - b.x, a.y - b.y);
    }

    static ImVec2 Mul(const ImVec2& value, float scale)
    {
        return ImVec2(value.x * scale, value.y * scale);
    }

    static ImVec2 DialogWorldToScreen(const ImVec2& canvasPos, float panX, float panY, float zoom, float x, float y)
    {
        return ImVec2(canvasPos.x + panX + x * zoom, canvasPos.y + panY + y * zoom);
    }

    static ImVec2 DialogScreenToWorld(const ImVec2& canvasPos, float panX, float panY, float zoom, const ImVec2& screen)
    {
        return ImVec2((screen.x - canvasPos.x - panX) / zoom, (screen.y - canvasPos.y - panY) / zoom);
    }

    static bool PointInRect(const ImVec2& point, const ImVec2& min, const ImVec2& max)
    {
        return point.x >= min.x && point.y >= min.y && point.x <= max.x && point.y <= max.y;
    }

    static bool ContainsIndex(const std::vector<int>& values, int value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    static void AddUniqueIndex(std::vector<int>& values, int value)
    {
        if (!ContainsIndex(values, value))
            values.push_back(value);
    }

    static void RemoveIndex(std::vector<int>& values, int value)
    {
        values.erase(std::remove(values.begin(), values.end(), value), values.end());
    }

    static void SanitizeNodeSelection(std::vector<int>& selection, int nodeCount)
    {
        selection.erase(
            std::remove_if(
                selection.begin(),
                selection.end(),
                [&](int index)
                {
                    return index < 0 || index >= nodeCount;
                }),
            selection.end());

        std::sort(selection.begin(), selection.end());
        selection.erase(std::unique(selection.begin(), selection.end()), selection.end());
    }

    static void ToggleNodeSelection(std::vector<int>& selection, int nodeIndex)
    {
        if (ContainsIndex(selection, nodeIndex))
            RemoveIndex(selection, nodeIndex);
        else
            AddUniqueIndex(selection, nodeIndex);
    }

    static ImU32 DialogChoiceColor(const EditorDialogChoice& choice)
    {
        if (choice.closeDialog)
            return IM_COL32(187, 134, 252, 255);
        if (!choice.requireFlags.empty() || !choice.forbidFlags.empty() || !choice.requireFlag.empty() || !choice.forbidFlag.empty())
            return IM_COL32(246, 196, 83, 255);
        if (!choice.setFlags.empty() || !choice.setFlag.empty())
            return IM_COL32(93, 201, 136, 255);
        if (choice.style == "rude")
            return IM_COL32(235, 112, 112, 255);
        if (choice.style == "polite")
            return IM_COL32(116, 173, 245, 255);
        return IM_COL32(190, 198, 210, 255);
    }

    static ImU32 DialogNodeFillColor(bool selected, bool isStartNode)
    {
        if (selected)
            return IM_COL32(43, 57, 76, 255);
        if (isStartNode)
            return IM_COL32(34, 52, 43, 255);
        return IM_COL32(35, 38, 44, 255);
    }

    static std::string FirstLineOrFallback(const std::string& value, const char* fallback)
    {
        if (value.empty())
            return fallback;

        const size_t newline = value.find_first_of("\r\n");
        if (newline == std::string::npos)
            return value;

        return value.substr(0, newline);
    }

    static void DrawGraphGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize, float panX, float panY, float zoom)
    {
        const float gridStep = 48.0f * zoom;
        if (gridStep < 8.0f)
            return;

        float startX = std::fmod(panX, gridStep);
        float startY = std::fmod(panY, gridStep);
        if (startX < 0.0f) startX += gridStep;
        if (startY < 0.0f) startY += gridStep;

        const ImU32 gridColor = IM_COL32(64, 69, 78, 90);
        for (float x = startX; x < canvasSize.x; x += gridStep)
            drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y), gridColor);

        for (float y = startY; y < canvasSize.y; y += gridStep)
            drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y), gridColor);
    }

    static void DrawBezierLink(ImDrawList* drawList, const ImVec2& start, const ImVec2& end, ImU32 color, float zoom)
    {
        const float handle = 80.0f * zoom;
        const ImVec2 cp1(start.x + handle, start.y);
        const ImVec2 cp2(end.x - handle, end.y);
        drawList->AddBezierCubic(start, cp1, cp2, end, color, std::max(1.5f, 2.5f * zoom), 24);

        const ImVec2 dir = Sub(end, cp2);
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len <= 0.001f)
            return;

        const ImVec2 unit(dir.x / len, dir.y / len);
        const ImVec2 perp(-unit.y, unit.x);
        const float arrowLength = 10.0f * zoom;
        const float arrowWidth = 5.0f * zoom;
        const ImVec2 base = Sub(end, Mul(unit, arrowLength));

        drawList->AddTriangleFilled(
            end,
            Add(base, Mul(perp, arrowWidth)),
            Sub(base, Mul(perp, arrowWidth)),
            color);
    }
}

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
        m_dialogGraphSelectedNodeIndices.clear();
        m_dialogGraphContextNodeIndex = -1;
        m_dialogGraphContextChoiceIndex = -1;
        m_dialogGraphAltPressedNodeIndex = -1;
        m_dialogGraphAltPressedWasSelected = false;
        m_dialogGraphAltDraggingSelection = false;

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
            m_dialogGraphSelectedNodeIndices.clear();
            m_dialogGraphContextNodeIndex = -1;
            m_dialogGraphContextChoiceIndex = -1;
            m_dialogGraphAltPressedNodeIndex = -1;
            m_dialogGraphAltPressedWasSelected = false;
            m_dialogGraphAltDraggingSelection = false;

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
                m_dialogGraphSelectedNodeIndices.clear();
                m_dialogGraphContextNodeIndex = -1;
                m_dialogGraphContextChoiceIndex = -1;
                m_dialogGraphAltPressedNodeIndex = -1;
                m_dialogGraphAltPressedWasSelected = false;
                m_dialogGraphAltDraggingSelection = false;

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

        renderDialogNodeEditor(dlg);

        ImGui::Separator();
        if (ImGui::BeginTable(
            "DialogNodeColumns",
            3,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Nodes", ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGui::TableSetupColumn("Node detail", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn("Choices", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            if (ImGui::BeginListBox("##DialogNodesList", ImVec2(-1.0f, 240.0f)))
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
                        strncpy_s(m_nodeRequireFlag, FirstFlagOrEmpty(dlg.nodes[i].requireFlags).c_str(), _TRUNCATE);
                        strncpy_s(m_nodeForbidFlag, FirstFlagOrEmpty(dlg.nodes[i].forbidFlags).c_str(), _TRUNCATE);
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
                strncpy_s(m_nodeId, dlg.nodes[m_selectedDialogNodeIndex].id.c_str(), _TRUNCATE);
                strncpy_s(m_nodeSpeaker, dlg.nodes[m_selectedDialogNodeIndex].speaker.c_str(), _TRUNCATE);
                strncpy_s(m_nodeText, dlg.nodes[m_selectedDialogNodeIndex].text.c_str(), _TRUNCATE);
                strncpy_s(m_nodeRequireFlag, FirstFlagOrEmpty(dlg.nodes[m_selectedDialogNodeIndex].requireFlags).c_str(), _TRUNCATE);
                strncpy_s(m_nodeForbidFlag, FirstFlagOrEmpty(dlg.nodes[m_selectedDialogNodeIndex].forbidFlags).c_str(), _TRUNCATE);
            }

            ImGui::SameLine();

            const bool deleteNodeDisabled = m_selectedDialogNodeIndex < 0 || m_selectedDialogNodeIndex >= (int)dlg.nodes.size();
            ImGui::BeginDisabled(deleteNodeDisabled);
            if (ImGui::Button("Delete node"))
            {
                if (m_selectedDialogNodeIndex >= 0 && m_selectedDialogNodeIndex < (int)dlg.nodes.size())
                {
                    const std::string deletedNodeId = dlg.nodes[m_selectedDialogNodeIndex].id;
                    dlg.nodes.erase(dlg.nodes.begin() + m_selectedDialogNodeIndex);
                    for (auto& otherNode : dlg.nodes)
                    {
                        for (auto& choice : otherNode.choices)
                        {
                            if (choice.next == deletedNodeId)
                                choice.next.clear();
                        }
                    }
                    m_selectedDialogNodeIndex = -1;
                    m_dialogGraphLinkSourceNodeIndex = -1;
                    m_dialogGraphLinkSourceChoiceIndex = -1;
                    m_dialogGraphSelectedNodeIndices.clear();
                    m_dialogGraphContextNodeIndex = -1;
                    m_dialogGraphContextChoiceIndex = -1;
                    m_dialogGraphAltPressedNodeIndex = -1;
                    m_dialogGraphAltPressedWasSelected = false;
                    m_dialogGraphAltDraggingSelection = false;
                    m_lastIoStatus = "Node deleted.";
                }
            }
            ImGui::EndDisabled();

            ImGui::TableSetColumnIndex(1);

            if (m_selectedDialogNodeIndex >= 0 && m_selectedDialogNodeIndex < (int)dlg.nodes.size())
            {
                auto& node = dlg.nodes[m_selectedDialogNodeIndex];
                ImGui::PushItemWidth(-150.0f);

                ImGui::InputText("Node ID", m_nodeId, IM_ARRAYSIZE(m_nodeId));
                ImGui::InputText("Speaker", m_nodeSpeaker, IM_ARRAYSIZE(m_nodeSpeaker));
                ImGui::InputTextMultiline("Text", m_nodeText, IM_ARRAYSIZE(m_nodeText), ImVec2(-1, 90));
                if (DrawQuestFlagMultiCombo("Require flags", node.requireFlags, m_questDefs))
                    SyncLegacyFlag(node.requireFlag, node.requireFlags);
                if (DrawQuestFlagMultiCombo("Forbid flags", node.forbidFlags, m_questDefs))
                    SyncLegacyFlag(node.forbidFlag, node.forbidFlags);

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
                        const std::string oldNodeId = node.id;
                        const std::string oldGraphKey = DialogGraphKey(dlg, oldNodeId);

                        node.id = m_nodeId;
                        node.speaker = m_nodeSpeaker;
                        node.text = m_nodeText;
                        SyncLegacyFlag(node.requireFlag, node.requireFlags);
                        SyncLegacyFlag(node.forbidFlag, node.forbidFlags);

                        if (oldNodeId != node.id)
                        {
                            if (dlg.startNode == oldNodeId)
                            {
                                dlg.startNode = node.id;
                                strncpy_s(m_dialogStartNode, dlg.startNode.c_str(), _TRUNCATE);
                            }

                            for (auto& otherNode : dlg.nodes)
                            {
                                for (auto& choice : otherNode.choices)
                                {
                                    if (choice.next == oldNodeId)
                                        choice.next = node.id;
                                }
                            }

                            auto graphIt = m_dialogGraphNodePositions.find(oldGraphKey);
                            if (graphIt != m_dialogGraphNodePositions.end())
                            {
                                m_dialogGraphNodePositions[DialogGraphKey(dlg, node.id)] = graphIt->second;
                                m_dialogGraphNodePositions.erase(graphIt);
                            }
                        }

                        m_lastIoStatus = "Node updated.";
                    }
                }

                ImGui::PopItemWidth();
                ImGui::TableSetColumnIndex(2);
                ImGui::PushItemWidth(-150.0f);

                ImGui::Separator();
                ImGui::Text("Choices");

                for (int i = 0; i < (int)node.choices.size(); ++i)
                {
                    ImGui::PushID(i);

                    char choiceTextBuf[256];
                    char choiceSetNpcScriptBuf[64];
                    char choiceSetNpcGreetingBuf[256];

                    strncpy_s(choiceTextBuf, node.choices[i].text.c_str(), _TRUNCATE);
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

                    if (DrawQuestFlagMultiCombo("Set flags", node.choices[i].setFlags, m_questDefs))
                        SyncLegacyFlag(node.choices[i].setFlag, node.choices[i].setFlags);
                    if (DrawQuestFlagMultiCombo("Choice require flags", node.choices[i].requireFlags, m_questDefs))
                        SyncLegacyFlag(node.choices[i].requireFlag, node.choices[i].requireFlags);
                    if (DrawQuestFlagMultiCombo("Choice forbid flags", node.choices[i].forbidFlags, m_questDefs))
                        SyncLegacyFlag(node.choices[i].forbidFlag, node.choices[i].forbidFlags);
                    ImGui::SliderInt("Choice require mood", &node.choices[i].requireMoodMin, 0, 100);
                    ImGui::Checkbox("Close dialog", &node.choices[i].closeDialog);
                    ImGui::InputText("Set NPC script", choiceSetNpcScriptBuf, IM_ARRAYSIZE(choiceSetNpcScriptBuf));
                    ImGui::InputText("Set NPC greeting", choiceSetNpcGreetingBuf, IM_ARRAYSIZE(choiceSetNpcGreetingBuf));

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

                ImGui::PopItemWidth();
            }
            else
            {
                ImGui::TextUnformatted("No node selected.");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted("No node selected.");
            }

            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Quests");
        if (m_questDefs.empty())
        {
            ImGui::TextDisabled("No quests loaded.");
        }
        else if (ImGui::BeginTable(
            "DialogQuestReference",
            5,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp,
            ImVec2(0.0f, 180.0f)))
        {
            ImGui::TableSetupColumn("Quest", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 1.3f);
            ImGui::TableSetupColumn("Started flag", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Ready flag", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Done flag", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)m_questDefs.size(); ++i)
            {
                const auto& q = m_questDefs[i];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(q.id.c_str(), m_selectedQuestIndex == i, ImGuiSelectableFlags_SpanAllColumns))
                    m_selectedQuestIndex = i;

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(q.title.empty() ? "(empty)" : q.title.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(q.startedFlag.empty() ? "(empty)" : q.startedFlag.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(q.readyFlag.empty() ? "(empty)" : q.readyFlag.c_str());

                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(q.doneFlag.empty() ? "(empty)" : q.doneFlag.c_str());
            }

            ImGui::EndTable();
        }
    }
    else
    {
        ImGui::TextUnformatted("No dialog selected.");
    }
}

void Editor::renderDialogNodeEditor(EditorDialogDef& dlg)
{
    ImGui::Separator();
    ImGui::TextUnformatted("Node graph");
    ImGui::SameLine();

    bool autoLayoutRequested = false;
    if (ImGui::SmallButton("Auto layout##dialog_graph"))
        autoLayoutRequested = true;
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Arrange dialog nodes by choice.next depth.");

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset view##dialog_graph"))
    {
        m_dialogGraphPanX = 32.0f;
        m_dialogGraphPanY = 48.0f;
        m_dialogGraphZoom = 1.0f;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Reset graph pan and zoom.");

    auto selectNode = [&](int nodeIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= (int)dlg.nodes.size())
            return;

        m_selectedDialogNodeIndex = nodeIndex;
        const auto& node = dlg.nodes[nodeIndex];
        strncpy_s(m_nodeId, node.id.c_str(), _TRUNCATE);
        strncpy_s(m_nodeSpeaker, node.speaker.c_str(), _TRUNCATE);
        strncpy_s(m_nodeText, node.text.c_str(), _TRUNCATE);
        strncpy_s(m_nodeRequireFlag, FirstFlagOrEmpty(node.requireFlags).c_str(), _TRUNCATE);
        strncpy_s(m_nodeForbidFlag, FirstFlagOrEmpty(node.forbidFlags).c_str(), _TRUNCATE);
    };

    auto addChoiceToNode = [&](int nodeIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= (int)dlg.nodes.size())
            return;

        EditorDialogChoice ch;
        ch.text = "Nova volba";
        ch.next.clear();

        dlg.nodes[nodeIndex].choices.push_back(std::move(ch));
        selectNode(nodeIndex);
        m_lastIoStatus = "Choice added.";
    };

    auto connectPendingChoiceToNode = [&](int targetNodeIndex)
    {
        if (m_dialogGraphLinkSourceNodeIndex < 0 ||
            m_dialogGraphLinkSourceNodeIndex >= (int)dlg.nodes.size() ||
            targetNodeIndex < 0 ||
            targetNodeIndex >= (int)dlg.nodes.size())
        {
            m_dialogGraphLinkSourceNodeIndex = -1;
            m_dialogGraphLinkSourceChoiceIndex = -1;
            return;
        }

        auto& sourceNode = dlg.nodes[m_dialogGraphLinkSourceNodeIndex];
        if (m_dialogGraphLinkSourceChoiceIndex < 0 ||
            m_dialogGraphLinkSourceChoiceIndex >= (int)sourceNode.choices.size())
        {
            m_dialogGraphLinkSourceNodeIndex = -1;
            m_dialogGraphLinkSourceChoiceIndex = -1;
            return;
        }

        sourceNode.choices[m_dialogGraphLinkSourceChoiceIndex].next = dlg.nodes[targetNodeIndex].id;
        m_dialogGraphLinkSourceNodeIndex = -1;
        m_dialogGraphLinkSourceChoiceIndex = -1;
        m_lastIoStatus = "Dialog link updated.";
    };

    auto assignLayout = [&](bool overwriteExisting)
    {
        const std::vector<int> depths = BuildDialogGraphDepths(dlg);

        int maxDepth = 0;
        int maxChoices = 1;
        for (int i = 0; i < (int)dlg.nodes.size(); ++i)
        {
            if (i < (int)depths.size() && depths[i] >= 0)
                maxDepth = std::max(maxDepth, depths[i]);
            maxChoices = std::max(maxChoices, (int)dlg.nodes[i].choices.size());
        }

        const float columnSpacing = kDialogGraphNodeWidth + 100.0f;
        const float rowSpacing = 122.0f + std::max(1, maxChoices) * kDialogGraphChoiceHeight;

        std::vector<int> rows((size_t)maxDepth + 2, 0);
        for (int i = 0; i < (int)dlg.nodes.size(); ++i)
        {
            int depth = (i < (int)depths.size()) ? depths[i] : -1;
            if (depth < 0)
                depth = maxDepth + 1;

            if (depth >= (int)rows.size())
                rows.resize((size_t)depth + 1, 0);

            const std::string key = DialogGraphKey(dlg, dlg.nodes[i].id);
            if (overwriteExisting || m_dialogGraphNodePositions.find(key) == m_dialogGraphNodePositions.end())
            {
                auto& pos = m_dialogGraphNodePositions[key];
                pos.x = 24.0f + depth * columnSpacing;
                pos.y = 20.0f + rows[depth] * rowSpacing;
            }

            rows[depth]++;
        }
    };

    if (m_selectedDialogNodeIndex >= (int)dlg.nodes.size())
        m_selectedDialogNodeIndex = -1;

    SanitizeNodeSelection(m_dialogGraphSelectedNodeIndices, (int)dlg.nodes.size());

    if (m_dialogGraphLinkSourceNodeIndex >= (int)dlg.nodes.size())
    {
        m_dialogGraphLinkSourceNodeIndex = -1;
        m_dialogGraphLinkSourceChoiceIndex = -1;
    }

    assignLayout(autoLayoutRequested);

    const float canvasHeight = 360.0f;
    if (!ImGui::BeginChild("DialogNodeGraphCanvas", ImVec2(0.0f, canvasHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::EndChild();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const bool graphMultiSelectMode = ImGui::IsKeyDown(ImGuiKey_LeftAlt);
    if (graphMultiSelectMode)
    {
        m_dialogGraphLinkSourceNodeIndex = -1;
        m_dialogGraphLinkSourceChoiceIndex = -1;
    }
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_dialogGraphAltPressedNodeIndex = -1;
        m_dialogGraphAltPressedWasSelected = false;
        m_dialogGraphAltDraggingSelection = false;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.x = std::max(canvasSize.x, 64.0f);
    canvasSize.y = std::max(canvasSize.y, 64.0f);
    const ImVec2 canvasMax = Add(canvasPos, canvasSize);
    const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        PointInRect(io.MousePos, canvasPos, canvasMax);

    if (canvasHovered && io.MouseWheel != 0.0f)
    {
        const float oldZoom = m_dialogGraphZoom;
        const ImVec2 mouseWorld = DialogScreenToWorld(canvasPos, m_dialogGraphPanX, m_dialogGraphPanY, oldZoom, io.MousePos);
        m_dialogGraphZoom = ClampFloat(m_dialogGraphZoom + io.MouseWheel * 0.08f, 0.55f, 1.65f);
        m_dialogGraphPanX = io.MousePos.x - canvasPos.x - mouseWorld.x * m_dialogGraphZoom;
        m_dialogGraphPanY = io.MousePos.y - canvasPos.y - mouseWorld.y * m_dialogGraphZoom;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        m_dialogGraphLinkSourceNodeIndex = -1;
        m_dialogGraphLinkSourceChoiceIndex = -1;
    }

    drawList->AddRectFilled(canvasPos, canvasMax, IM_COL32(26, 28, 33, 255));
    DrawGraphGrid(drawList, canvasPos, canvasSize, m_dialogGraphPanX, m_dialogGraphPanY, m_dialogGraphZoom);
    drawList->PushClipRect(canvasPos, canvasMax, true);

    struct NodeVisual
    {
        ImVec2 min;
        ImVec2 max;
        ImVec2 inputPin;
        std::vector<ImVec2> outputPins;
    };

    std::vector<NodeVisual> visuals(dlg.nodes.size());
    for (int i = 0; i < (int)dlg.nodes.size(); ++i)
    {
        const auto& node = dlg.nodes[i];
        const auto& pos = m_dialogGraphNodePositions[DialogGraphKey(dlg, node.id)];
        const int choiceRows = std::max(1, (int)node.choices.size());
        const float nodeHeight = 118.0f + choiceRows * kDialogGraphChoiceHeight;
        const ImVec2 min = DialogWorldToScreen(canvasPos, m_dialogGraphPanX, m_dialogGraphPanY, m_dialogGraphZoom, pos.x, pos.y);
        const ImVec2 size(kDialogGraphNodeWidth * m_dialogGraphZoom, nodeHeight * m_dialogGraphZoom);
        const ImVec2 max = Add(min, size);

        visuals[i].min = min;
        visuals[i].max = max;
        visuals[i].inputPin = ImVec2(min.x, min.y + kDialogGraphHeaderHeight * 0.5f * m_dialogGraphZoom);
        visuals[i].outputPins.reserve(node.choices.size());

        for (int choiceIndex = 0; choiceIndex < (int)node.choices.size(); ++choiceIndex)
        {
            const float pinY = min.y + (116.0f + choiceIndex * kDialogGraphChoiceHeight + kDialogGraphChoiceHeight * 0.5f) * m_dialogGraphZoom;
            visuals[i].outputPins.push_back(ImVec2(max.x, pinY));
        }
    }

    for (int i = 0; i < (int)dlg.nodes.size(); ++i)
    {
        const auto& node = dlg.nodes[i];
        for (int choiceIndex = 0; choiceIndex < (int)node.choices.size(); ++choiceIndex)
        {
            const auto& choice = node.choices[choiceIndex];
            const int targetIndex = FindDialogGraphNodeIndex(dlg, choice.next);
            const ImVec2 start = visuals[i].outputPins[choiceIndex];
            const ImU32 color = (targetIndex >= 0)
                ? DialogChoiceColor(choice)
                : IM_COL32(235, 98, 98, 230);

            if (targetIndex >= 0)
            {
                DrawBezierLink(drawList, start, visuals[targetIndex].inputPin, color, m_dialogGraphZoom);
            }
            else if (!choice.next.empty())
            {
                const ImVec2 end(start.x + 58.0f * m_dialogGraphZoom, start.y);
                drawList->AddLine(start, end, color, std::max(1.5f, 2.0f * m_dialogGraphZoom));
                drawList->AddCircleFilled(end, 4.0f * m_dialogGraphZoom, color);
                drawList->AddText(
                    ImGui::GetFont(),
                    ImGui::GetFontSize() * ClampFloat(m_dialogGraphZoom, 0.75f, 1.25f),
                    ImVec2(end.x + 6.0f * m_dialogGraphZoom, end.y - 8.0f),
                    color,
                    choice.next.c_str());
            }
        }
    }

    if (m_dialogGraphLinkSourceNodeIndex >= 0 &&
        m_dialogGraphLinkSourceNodeIndex < (int)visuals.size() &&
        m_dialogGraphLinkSourceChoiceIndex >= 0 &&
        m_dialogGraphLinkSourceChoiceIndex < (int)visuals[m_dialogGraphLinkSourceNodeIndex].outputPins.size())
    {
        const ImVec2 start = visuals[m_dialogGraphLinkSourceNodeIndex].outputPins[m_dialogGraphLinkSourceChoiceIndex];
        DrawBezierLink(drawList, start, io.MousePos, IM_COL32(255, 255, 255, 210), m_dialogGraphZoom);
    }

    const float textScale = ClampFloat(m_dialogGraphZoom, 0.75f, 1.25f);
    const float fontSize = ImGui::GetFontSize() * textScale;
    const ImU32 textColor = IM_COL32(235, 238, 245, 255);
    const ImU32 mutedTextColor = IM_COL32(170, 178, 190, 255);
    const ImU32 headerTextColor = IM_COL32(255, 255, 255, 255);

    for (int i = 0; i < (int)dlg.nodes.size(); ++i)
    {
        const auto& node = dlg.nodes[i];
        const NodeVisual& visual = visuals[i];
        const bool selected = (m_selectedDialogNodeIndex == i);
        const bool multiSelected = ContainsIndex(m_dialogGraphSelectedNodeIndices, i);
        const bool startNode = (!dlg.startNode.empty() && node.id == dlg.startNode);
        const float rounding = std::max(3.0f, 7.0f * m_dialogGraphZoom);
        const ImVec2 headerMax(visual.max.x, visual.min.y + kDialogGraphHeaderHeight * m_dialogGraphZoom);
        const ImU32 borderColor = selected
            ? IM_COL32(112, 175, 255, 255)
            : (multiSelected ? IM_COL32(255, 188, 82, 255) : IM_COL32(88, 96, 110, 255));
        const float borderThickness = (selected || multiSelected) ? 2.5f : 1.0f;

        drawList->AddRectFilled(visual.min, visual.max, DialogNodeFillColor(selected, startNode), rounding);
        drawList->AddRectFilled(visual.min, headerMax, startNode ? IM_COL32(48, 116, 74, 255) : IM_COL32(50, 57, 68, 255), rounding);
        drawList->AddRect(visual.min, visual.max, borderColor, rounding, 0, borderThickness);

        if (multiSelected)
        {
            drawList->AddCircleFilled(
                ImVec2(visual.max.x - 12.0f * m_dialogGraphZoom, visual.min.y + 15.0f * m_dialogGraphZoom),
                std::max(3.0f, 5.0f * m_dialogGraphZoom),
                IM_COL32(255, 188, 82, 255));
        }

        const ImVec4 clipRect(
            visual.min.x + kDialogGraphNodePadding * m_dialogGraphZoom,
            visual.min.y,
            visual.max.x - kDialogGraphNodePadding * m_dialogGraphZoom,
            visual.max.y);

        const std::string title = node.id.empty() ? "(empty node id)" : node.id;
        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            ImVec2(visual.min.x + kDialogGraphNodePadding * m_dialogGraphZoom, visual.min.y + 7.0f * m_dialogGraphZoom),
            node.id.empty() ? IM_COL32(255, 130, 130, 255) : headerTextColor,
            title.c_str(),
            nullptr,
            0.0f,
            &clipRect);

        const std::string speaker = FirstLineOrFallback(node.speaker, "(bez mluvciho)");
        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            ImVec2(visual.min.x + kDialogGraphNodePadding * m_dialogGraphZoom, visual.min.y + 42.0f * m_dialogGraphZoom),
            mutedTextColor,
            speaker.c_str(),
            nullptr,
            0.0f,
            &clipRect);

        const std::string text = FirstLineOrFallback(node.text, "(bez textu)");
        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            ImVec2(visual.min.x + kDialogGraphNodePadding * m_dialogGraphZoom, visual.min.y + 62.0f * m_dialogGraphZoom),
            textColor,
            text.c_str(),
            nullptr,
            0.0f,
            &clipRect);

        const std::string nodeFlags = NodeFlagSummary(node);
        if (!nodeFlags.empty())
        {
            drawList->AddText(
                ImGui::GetFont(),
                fontSize,
                ImVec2(visual.min.x + kDialogGraphNodePadding * m_dialogGraphZoom, visual.min.y + 80.0f * m_dialogGraphZoom),
                IM_COL32(246, 196, 83, 255),
                nodeFlags.c_str(),
                nullptr,
                0.0f,
                &clipRect);
        }

        if (node.choices.empty())
        {
            drawList->AddText(
                ImGui::GetFont(),
                fontSize,
                ImVec2(visual.min.x + kDialogGraphNodePadding * m_dialogGraphZoom, visual.min.y + 116.0f * m_dialogGraphZoom),
                mutedTextColor,
                "(end node)");
        }
        else
        {
            for (int choiceIndex = 0; choiceIndex < (int)node.choices.size(); ++choiceIndex)
            {
                const auto& choice = node.choices[choiceIndex];
                const float y = visual.min.y + (116.0f + choiceIndex * kDialogGraphChoiceHeight) * m_dialogGraphZoom;
                const ImU32 choiceColor = DialogChoiceColor(choice);
                const std::string choiceText = FirstLineOrFallback(choice.text, "(empty choice)");
                const std::string flagsText = ChoiceFlagSummary(choice);

                drawList->AddText(
                    ImGui::GetFont(),
                    fontSize,
                    ImVec2(visual.min.x + kDialogGraphNodePadding * m_dialogGraphZoom, y),
                    choiceColor,
                    choiceText.c_str(),
                    nullptr,
                    0.0f,
                    &clipRect);

                if (!flagsText.empty())
                {
                    drawList->AddText(
                        ImGui::GetFont(),
                        fontSize * 0.88f,
                        ImVec2(visual.min.x + kDialogGraphNodePadding * m_dialogGraphZoom, y + 18.0f * m_dialogGraphZoom),
                        IM_COL32(246, 196, 83, 255),
                        flagsText.c_str(),
                        nullptr,
                        0.0f,
                        &clipRect);
                }
            }
        }

        const float pinRadius = kDialogGraphPinRadius * m_dialogGraphZoom;
        drawList->AddCircleFilled(visual.inputPin, pinRadius, IM_COL32(116, 173, 245, 255));
        drawList->AddCircle(visual.inputPin, pinRadius + 1.5f, IM_COL32(20, 22, 26, 255), 12, 1.5f);

        for (int choiceIndex = 0; choiceIndex < (int)visual.outputPins.size(); ++choiceIndex)
        {
            const ImVec2 pin = visual.outputPins[choiceIndex];
            const auto& choice = node.choices[choiceIndex];
            const bool missingTarget = !choice.next.empty() && FindDialogGraphNodeIndex(dlg, choice.next) < 0;
            const ImU32 pinColor = missingTarget ? IM_COL32(235, 98, 98, 255) : DialogChoiceColor(choice);
            drawList->AddCircleFilled(pin, pinRadius, pinColor);
            drawList->AddCircle(pin, pinRadius + 1.5f, IM_COL32(20, 22, 26, 255), 12, 1.5f);
        }
    }

    bool openContextPopup = false;
    int popupNodeIndex = -1;
    int popupChoiceIndex = -1;

    for (int i = 0; i < (int)dlg.nodes.size(); ++i)
    {
        auto& node = dlg.nodes[i];
        const NodeVisual& visual = visuals[i];
        const float pinHitSize = std::max(18.0f, 18.0f * m_dialogGraphZoom);
        const float bodyInset = graphMultiSelectMode ? 0.0f : pinHitSize * 0.5f;
        const ImVec2 bodyMin(visual.min.x + bodyInset, visual.min.y);
        const ImVec2 bodySize(std::max(1.0f, visual.max.x - visual.min.x - bodyInset * 2.0f), std::max(1.0f, visual.max.y - visual.min.y));

        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(bodyMin);
        ImGui::InvisibleButton("node_body", bodySize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            if (graphMultiSelectMode)
            {
                m_dialogGraphAltPressedNodeIndex = i;
                m_dialogGraphAltPressedWasSelected = ContainsIndex(m_dialogGraphSelectedNodeIndices, i);
                m_dialogGraphAltDraggingSelection = false;
                if (!m_dialogGraphAltPressedWasSelected)
                    AddUniqueIndex(m_dialogGraphSelectedNodeIndices, i);
            }
            else
            {
                if (m_dialogGraphLinkSourceNodeIndex >= 0)
                    connectPendingChoiceToNode(i);
                selectNode(i);
            }
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !graphMultiSelectMode)
        {
            popupNodeIndex = i;
            popupChoiceIndex = -1;
            openContextPopup = true;
            selectNode(i);
        }

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            if (graphMultiSelectMode)
            {
                if (!ContainsIndex(m_dialogGraphSelectedNodeIndices, i))
                    AddUniqueIndex(m_dialogGraphSelectedNodeIndices, i);

                m_dialogGraphAltDraggingSelection = true;
                const std::vector<int> movingNodes = m_dialogGraphSelectedNodeIndices;
                for (int selectedNodeIndex : movingNodes)
                {
                    if (selectedNodeIndex < 0 || selectedNodeIndex >= (int)dlg.nodes.size())
                        continue;

                    auto& pos = m_dialogGraphNodePositions[DialogGraphKey(dlg, dlg.nodes[selectedNodeIndex].id)];
                    pos.x += io.MouseDelta.x / m_dialogGraphZoom;
                    pos.y += io.MouseDelta.y / m_dialogGraphZoom;
                }
            }
            else if (m_dialogGraphLinkSourceNodeIndex < 0)
            {
                auto& pos = m_dialogGraphNodePositions[DialogGraphKey(dlg, node.id)];
                pos.x += io.MouseDelta.x / m_dialogGraphZoom;
                pos.y += io.MouseDelta.y / m_dialogGraphZoom;
            }
        }

        if (graphMultiSelectMode &&
            ImGui::IsItemDeactivated() &&
            m_dialogGraphAltPressedNodeIndex == i)
        {
            if (!m_dialogGraphAltDraggingSelection && m_dialogGraphAltPressedWasSelected)
                RemoveIndex(m_dialogGraphSelectedNodeIndices, i);

            m_dialogGraphAltPressedNodeIndex = -1;
            m_dialogGraphAltPressedWasSelected = false;
            m_dialogGraphAltDraggingSelection = false;
        }

        if (ImGui::IsItemHovered())
        {
            if (graphMultiSelectMode)
                ImGui::SetItemTooltip("Alt mode: click toggles group selection, drag moves selected nodes.");
            else
                ImGui::SetItemTooltip("Drag to move. Click to select. Right click opens menu.");
        }

        if (!graphMultiSelectMode)
        {
            ImGui::SetCursorScreenPos(ImVec2(visual.inputPin.x - pinHitSize * 0.5f, visual.inputPin.y - pinHitSize * 0.5f));
            ImGui::InvisibleButton("input_pin", ImVec2(pinHitSize, pinHitSize), ImGuiButtonFlags_MouseButtonLeft);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                if (m_dialogGraphLinkSourceNodeIndex >= 0)
                    connectPendingChoiceToNode(i);
                selectNode(i);
            }

            if (ImGui::IsItemHovered())
                ImGui::SetItemTooltip("Input: target node.");
        }

        for (int choiceIndex = 0; choiceIndex < (int)node.choices.size(); ++choiceIndex)
        {
            if (graphMultiSelectMode)
                continue;

            ImGui::PushID(choiceIndex);
            const ImVec2 pin = visual.outputPins[choiceIndex];
            ImGui::SetCursorScreenPos(ImVec2(pin.x - pinHitSize * 0.5f, pin.y - pinHitSize * 0.5f));
            ImGui::InvisibleButton(
                "output_pin",
                ImVec2(pinHitSize, pinHitSize),
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

            if (!graphMultiSelectMode && ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                selectNode(i);
                m_dialogGraphLinkSourceNodeIndex = i;
                m_dialogGraphLinkSourceChoiceIndex = choiceIndex;
                m_lastIoStatus = "Vyber cilovy node pro choice.next.";
            }

            if (!graphMultiSelectMode && ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                popupNodeIndex = i;
                popupChoiceIndex = choiceIndex;
                openContextPopup = true;
                selectNode(i);
            }

            if (ImGui::IsItemHovered())
            {
                const auto& choice = node.choices[choiceIndex];
                if (graphMultiSelectMode)
                {
                    ImGui::SetItemTooltip("Alt mode: graph editing is disabled.");
                }
                else
                {
                    ImGui::SetItemTooltip(
                        "Output: %s\nnext: %s\nLeft click starts link, right click opens menu.",
                        choice.text.empty() ? "(empty choice)" : choice.text.c_str(),
                        choice.next.empty() ? "(end)" : choice.next.c_str());
                }
            }

            ImGui::PopID();
        }

        ImGui::PopID();
    }

    if (!openContextPopup &&
        !graphMultiSelectMode &&
        canvasHovered &&
        !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] <= 36.0f &&
        m_selectedDialogNodeIndex >= 0 &&
        m_selectedDialogNodeIndex < (int)dlg.nodes.size())
    {
        popupNodeIndex = m_selectedDialogNodeIndex;
        popupChoiceIndex = -1;
        openContextPopup = true;
    }

    if (openContextPopup)
    {
        m_dialogGraphContextNodeIndex = popupNodeIndex;
        m_dialogGraphContextChoiceIndex = popupChoiceIndex;
        m_dialogGraphLinkSourceNodeIndex = -1;
        m_dialogGraphLinkSourceChoiceIndex = -1;
        ImGui::OpenPopup("DialogGraphContext");
    }

    if (ImGui::BeginPopup("DialogGraphContext"))
    {
        const bool hasContextNode =
            m_dialogGraphContextNodeIndex >= 0 &&
            m_dialogGraphContextNodeIndex < (int)dlg.nodes.size();

        ImGui::BeginDisabled(!hasContextNode);
        if (ImGui::MenuItem("Add choice"))
        {
            addChoiceToNode(m_dialogGraphContextNodeIndex);
            m_dialogGraphContextChoiceIndex = -1;
        }
        ImGui::EndDisabled();

        if (hasContextNode &&
            m_dialogGraphContextChoiceIndex >= 0 &&
            m_dialogGraphContextChoiceIndex < (int)dlg.nodes[m_dialogGraphContextNodeIndex].choices.size())
        {
            ImGui::Separator();
            if (ImGui::MenuItem("Clear choice link"))
            {
                dlg.nodes[m_dialogGraphContextNodeIndex].choices[m_dialogGraphContextChoiceIndex].next.clear();
                m_lastIoStatus = "Dialog link cleared.";
            }

            if (ImGui::MenuItem("Delete choice"))
            {
                auto& choices = dlg.nodes[m_dialogGraphContextNodeIndex].choices;
                choices.erase(choices.begin() + m_dialogGraphContextChoiceIndex);
                m_dialogGraphContextChoiceIndex = -1;
                m_lastIoStatus = "Choice deleted.";
            }
        }

        ImGui::EndPopup();
    }

    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
    {
        m_dialogGraphPanX += io.MouseDelta.x;
        m_dialogGraphPanY += io.MouseDelta.y;
    }

    if (canvasHovered &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) &&
        !ImGui::IsAnyItemActive())
    {
        m_dialogGraphPanX += io.MouseDelta.x;
        m_dialogGraphPanY += io.MouseDelta.y;
    }

    if (dlg.nodes.empty())
    {
        drawList->AddText(
            ImVec2(canvasPos.x + 20.0f, canvasPos.y + 20.0f),
            IM_COL32(190, 198, 210, 255),
            "Dialog has no nodes.");
    }

    drawList->PopClipRect();
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::Dummy(canvasSize);
    ImGui::EndChild();
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
                node.requireFlags = LoadDialogFlags(jn, "require_flag", "require_flags");
                node.forbidFlags = LoadDialogFlags(jn, "forbid_flag", "forbid_flags");
                node.requireFlag = FirstFlagOrEmpty(node.requireFlags);
                node.forbidFlag = FirstFlagOrEmpty(node.forbidFlags);

                if (jn.contains("choices") && jn["choices"].is_array())
                {
                    for (const auto& jc : jn["choices"])
                    {
                        EditorDialogChoice ch;
                        ch.text = jc.value("text", "");
                        ch.next = jc.value("next", "");
                        ch.style = jc.value("style", "");
                        ch.npcMoodDelta = jc.value("npc_mood_delta", 0);
                        ch.setFlags = LoadDialogFlags(jc, "set_flag", "set_flags");
                        ch.requireFlags = LoadDialogFlags(jc, "require_flag", "require_flags");
                        ch.forbidFlags = LoadDialogFlags(jc, "forbid_flag", "forbid_flags");
                        ch.setFlag = FirstFlagOrEmpty(ch.setFlags);
                        ch.requireFlag = FirstFlagOrEmpty(ch.requireFlags);
                        ch.forbidFlag = FirstFlagOrEmpty(ch.forbidFlags);
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
    m_dialogGraphSelectedNodeIndices.clear();
    m_dialogGraphContextNodeIndex = -1;
    m_dialogGraphContextChoiceIndex = -1;
    m_dialogGraphAltPressedNodeIndex = -1;
    m_dialogGraphAltPressedWasSelected = false;
    m_dialogGraphAltDraggingSelection = false;
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

            std::vector<std::string> nodeRequireFlags = node.requireFlags;
            std::vector<std::string> nodeForbidFlags = node.forbidFlags;
            NormalizeFlags(nodeRequireFlags, node.requireFlag);
            NormalizeFlags(nodeForbidFlags, node.forbidFlag);
            WriteDialogFlags(jn, "require_flag", "require_flags", nodeRequireFlags);
            WriteDialogFlags(jn, "forbid_flag", "forbid_flags", nodeForbidFlags);

            for (const auto& ch : node.choices)
            {
                json jc = {
                    {"text", ch.text},
                    {"next", ch.next}
                };

                if (!ch.style.empty())
                    jc["style"] = ch.style;
                std::vector<std::string> choiceRequireFlags = ch.requireFlags;
                std::vector<std::string> choiceForbidFlags = ch.forbidFlags;
                std::vector<std::string> choiceSetFlags = ch.setFlags;
                NormalizeFlags(choiceRequireFlags, ch.requireFlag);
                NormalizeFlags(choiceForbidFlags, ch.forbidFlag);
                NormalizeFlags(choiceSetFlags, ch.setFlag);

                WriteDialogFlags(jc, "require_flag", "require_flags", choiceRequireFlags);
                WriteDialogFlags(jc, "forbid_flag", "forbid_flags", choiceForbidFlags);
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
                WriteDialogFlags(jc, "set_flag", "set_flags", choiceSetFlags);

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
