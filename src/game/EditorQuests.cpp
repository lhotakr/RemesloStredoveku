#include "Editor.h"
#include "PathUtils.h"
#include <JsonUtils.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "imgui.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
    constexpr float kQuestGraphQuestWidth = 260.0f;
    constexpr float kQuestGraphFlagWidth = 230.0f;
    constexpr float kQuestGraphNodeHeight = 86.0f;
    constexpr float kQuestGraphRowHeight = 132.0f;

    static float QuestClampFloat(float value, float minValue, float maxValue)
    {
        return std::max(minValue, std::min(value, maxValue));
    }

    static ImVec2 QuestAdd(const ImVec2& a, const ImVec2& b)
    {
        return ImVec2(a.x + b.x, a.y + b.y);
    }

    static ImVec2 QuestWorldToScreen(const ImVec2& canvasPos, float panX, float panY, float zoom, float x, float y)
    {
        return ImVec2(canvasPos.x + panX + x * zoom, canvasPos.y + panY + y * zoom);
    }

    static ImVec2 QuestScreenToWorld(const ImVec2& canvasPos, float panX, float panY, float zoom, const ImVec2& screen)
    {
        return ImVec2((screen.x - canvasPos.x - panX) / zoom, (screen.y - canvasPos.y - panY) / zoom);
    }

    static bool QuestPointInRect(const ImVec2& point, const ImVec2& min, const ImVec2& max)
    {
        return point.x >= min.x && point.y >= min.y && point.x <= max.x && point.y <= max.y;
    }

    static void DrawQuestGraphGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize, float panX, float panY, float zoom)
    {
        const float gridStep = 48.0f * zoom;
        if (gridStep < 8.0f)
            return;

        float startX = std::fmod(panX, gridStep);
        float startY = std::fmod(panY, gridStep);
        if (startX < 0.0f) startX += gridStep;
        if (startY < 0.0f) startY += gridStep;

        const ImU32 gridColor = IM_COL32(64, 69, 78, 85);
        for (float x = startX; x < canvasSize.x; x += gridStep)
            drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y), gridColor);

        for (float y = startY; y < canvasSize.y; y += gridStep)
            drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y), gridColor);
    }

    static void DrawQuestLink(ImDrawList* drawList, const ImVec2& start, const ImVec2& end, ImU32 color, float zoom)
    {
        const float handle = 62.0f * zoom;
        drawList->AddBezierCubic(
            start,
            ImVec2(start.x + handle, start.y),
            ImVec2(end.x - handle, end.y),
            end,
            color,
            std::max(1.5f, 2.2f * zoom),
            20);
    }
}

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

    renderQuestNodeEditor();

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

void Editor::renderQuestNodeEditor()
{
    ImGui::Separator();
    ImGui::TextUnformatted("Quest graph");
    ImGui::SameLine();

    if (ImGui::SmallButton("Reset view##quest_graph"))
    {
        m_questGraphPanX = 32.0f;
        m_questGraphPanY = 42.0f;
        m_questGraphZoom = 1.0f;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Reset graph pan and zoom.");

    const float canvasHeight = 280.0f;
    if (!ImGui::BeginChild("QuestNodeGraphCanvas", ImVec2(0.0f, canvasHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::EndChild();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.x = std::max(canvasSize.x, 64.0f);
    canvasSize.y = std::max(canvasSize.y, 64.0f);
    const ImVec2 canvasMax = QuestAdd(canvasPos, canvasSize);
    const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        QuestPointInRect(io.MousePos, canvasPos, canvasMax);

    if (canvasHovered && io.MouseWheel != 0.0f)
    {
        const float oldZoom = m_questGraphZoom;
        const ImVec2 mouseWorld = QuestScreenToWorld(canvasPos, m_questGraphPanX, m_questGraphPanY, oldZoom, io.MousePos);
        m_questGraphZoom = QuestClampFloat(m_questGraphZoom + io.MouseWheel * 0.08f, 0.55f, 1.65f);
        m_questGraphPanX = io.MousePos.x - canvasPos.x - mouseWorld.x * m_questGraphZoom;
        m_questGraphPanY = io.MousePos.y - canvasPos.y - mouseWorld.y * m_questGraphZoom;
    }

    drawList->AddRectFilled(canvasPos, canvasMax, IM_COL32(26, 28, 33, 255));
    DrawQuestGraphGrid(drawList, canvasPos, canvasSize, m_questGraphPanX, m_questGraphPanY, m_questGraphZoom);
    drawList->PushClipRect(canvasPos, canvasMax, true);

    struct QuestHitRect
    {
        int questIndex = -1;
        ImVec2 min;
        ImVec2 max;
    };

    std::vector<QuestHitRect> hitRects;
    hitRects.reserve(m_questDefs.size() * 4);

    const float zoom = m_questGraphZoom;
    const float fontSize = ImGui::GetFontSize() * QuestClampFloat(zoom, 0.75f, 1.25f);
    const ImU32 textColor = IM_COL32(235, 238, 245, 255);
    const ImU32 mutedTextColor = IM_COL32(170, 178, 190, 255);
    const ImU32 linkColor = IM_COL32(116, 173, 245, 210);

    auto drawQuestNode = [&](int questIndex, float x, float y, float width, const char* label, const std::string& value, ImU32 fillColor, ImU32 accentColor)
    {
        const ImVec2 min = QuestWorldToScreen(canvasPos, m_questGraphPanX, m_questGraphPanY, zoom, x, y);
        const ImVec2 max = QuestAdd(min, ImVec2(width * zoom, kQuestGraphNodeHeight * zoom));
        const float rounding = std::max(3.0f, 7.0f * zoom);
        const bool selected = (questIndex == m_selectedQuestIndex);

        drawList->AddRectFilled(min, max, fillColor, rounding);
        drawList->AddRectFilled(min, ImVec2(max.x, min.y + 24.0f * zoom), accentColor, rounding);
        drawList->AddRect(min, max, selected ? IM_COL32(112, 175, 255, 255) : IM_COL32(88, 96, 110, 255), rounding, 0, selected ? 2.5f : 1.0f);

        const ImVec4 clipRect(min.x + 10.0f * zoom, min.y, max.x - 10.0f * zoom, max.y);
        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            ImVec2(min.x + 10.0f * zoom, min.y + 5.0f * zoom),
            IM_COL32(255, 255, 255, 255),
            label,
            nullptr,
            0.0f,
            &clipRect);

        const std::string displayValue = value.empty() ? std::string("(empty)") : value;
        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            ImVec2(min.x + 10.0f * zoom, min.y + 36.0f * zoom),
            value.empty() ? IM_COL32(255, 135, 135, 255) : textColor,
            displayValue.c_str(),
            nullptr,
            0.0f,
            &clipRect);

        hitRects.push_back(QuestHitRect{ questIndex, min, max });
        return std::pair<ImVec2, ImVec2>(min, max);
    };

    for (int i = 0; i < (int)m_questDefs.size(); ++i)
    {
        const auto& q = m_questDefs[i];
        const float y = 22.0f + i * kQuestGraphRowHeight;
        const float questX = 24.0f;
        const float startedX = 342.0f;
        const float readyX = 622.0f;
        const float doneX = 902.0f;

        const auto questRect = std::pair<ImVec2, ImVec2>(
            QuestWorldToScreen(canvasPos, m_questGraphPanX, m_questGraphPanY, zoom, questX, y),
            QuestWorldToScreen(canvasPos, m_questGraphPanX, m_questGraphPanY, zoom, questX + kQuestGraphQuestWidth, y + kQuestGraphNodeHeight));
        const auto startedRect = std::pair<ImVec2, ImVec2>(
            QuestWorldToScreen(canvasPos, m_questGraphPanX, m_questGraphPanY, zoom, startedX, y),
            QuestWorldToScreen(canvasPos, m_questGraphPanX, m_questGraphPanY, zoom, startedX + kQuestGraphFlagWidth, y + kQuestGraphNodeHeight));
        const auto readyRect = std::pair<ImVec2, ImVec2>(
            QuestWorldToScreen(canvasPos, m_questGraphPanX, m_questGraphPanY, zoom, readyX, y),
            QuestWorldToScreen(canvasPos, m_questGraphPanX, m_questGraphPanY, zoom, readyX + kQuestGraphFlagWidth, y + kQuestGraphNodeHeight));
        const auto doneRect = std::pair<ImVec2, ImVec2>(
            QuestWorldToScreen(canvasPos, m_questGraphPanX, m_questGraphPanY, zoom, doneX, y),
            QuestWorldToScreen(canvasPos, m_questGraphPanX, m_questGraphPanY, zoom, doneX + kQuestGraphFlagWidth, y + kQuestGraphNodeHeight));

        DrawQuestLink(drawList, ImVec2(questRect.second.x, (questRect.first.y + questRect.second.y) * 0.5f), ImVec2(startedRect.first.x, (startedRect.first.y + startedRect.second.y) * 0.5f), linkColor, zoom);
        DrawQuestLink(drawList, ImVec2(startedRect.second.x, (startedRect.first.y + startedRect.second.y) * 0.5f), ImVec2(readyRect.first.x, (readyRect.first.y + readyRect.second.y) * 0.5f), linkColor, zoom);
        DrawQuestLink(drawList, ImVec2(readyRect.second.x, (readyRect.first.y + readyRect.second.y) * 0.5f), ImVec2(doneRect.first.x, (doneRect.first.y + doneRect.second.y) * 0.5f), linkColor, zoom);

        const std::string questValue = q.title.empty() ? q.id : q.title + " [" + q.id + "]";
        drawQuestNode(i, questX, y, kQuestGraphQuestWidth, "Quest", questValue, IM_COL32(35, 38, 44, 255), IM_COL32(50, 57, 68, 255));
        drawQuestNode(i, startedX, y, kQuestGraphFlagWidth, "Started flag", q.startedFlag, q.startedFlag.empty() ? IM_COL32(62, 40, 42, 255) : IM_COL32(34, 52, 43, 255), IM_COL32(48, 116, 74, 255));
        drawQuestNode(i, readyX, y, kQuestGraphFlagWidth, "Ready flag", q.readyFlag, q.readyFlag.empty() ? IM_COL32(62, 40, 42, 255) : IM_COL32(42, 47, 64, 255), IM_COL32(94, 117, 196, 255));
        drawQuestNode(i, doneX, y, kQuestGraphFlagWidth, "Done flag", q.doneFlag, q.doneFlag.empty() ? IM_COL32(62, 40, 42, 255) : IM_COL32(51, 47, 35, 255), IM_COL32(183, 137, 58, 255));
    }

    if (m_questDefs.empty())
    {
        drawList->AddText(
            ImVec2(canvasPos.x + 20.0f, canvasPos.y + 20.0f),
            mutedTextColor,
            "No quests loaded.");
    }

    drawList->PopClipRect();

    for (int i = 0; i < (int)hitRects.size(); ++i)
    {
        const auto& hit = hitRects[i];
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(hit.min);
        ImGui::InvisibleButton("quest_graph_node", ImVec2(std::max(1.0f, hit.max.x - hit.min.x), std::max(1.0f, hit.max.y - hit.min.y)), ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            m_selectedQuestIndex = hit.questIndex;
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip("Click to select quest.");
        ImGui::PopID();
    }

    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
    {
        m_questGraphPanX += io.MouseDelta.x;
        m_questGraphPanY += io.MouseDelta.y;
    }

    if (canvasHovered &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) &&
        !ImGui::IsAnyItemActive())
    {
        m_questGraphPanX += io.MouseDelta.x;
        m_questGraphPanY += io.MouseDelta.y;
    }

    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::Dummy(canvasSize);
    ImGui::EndChild();
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
