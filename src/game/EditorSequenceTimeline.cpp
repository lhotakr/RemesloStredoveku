#include "Editor.h"

#include "PathUtils.h"
#include "game/sequence/SequenceTimeline.h"

#include <algorithm>
#include <cmath>

namespace
{
    int TimelineRow(const std::string& type)
    {
        if (type.find("actor") != std::string::npos || type == "show_prop" || type == "hide_prop") return 0;
        if (type.find("camera") != std::string::npos) return 1;
        if (type.find("music") != std::string::npos || type.find("sfx") != std::string::npos || type.find("voice") != std::string::npos) return 2;
        if (type.find("fade") != std::string::npos || type == "screen_flash" || type == "show_text" || type == "hide_text") return 3;
        if (type == "set_time" || type == "set_weather" || type == "load_map" || type == "load_scene") return 4;
        return 5;
    }
}

void Editor::seekSequencePreview(float time)
{
    if (!std::isfinite(time) || m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= static_cast<int>(m_sequences.size())) return;
    const auto& seq = m_sequences[m_selectedSequenceIndex];
    if (m_selectedSequenceNodeIndex < 0 || m_selectedSequenceNodeIndex >= static_cast<int>(seq.nodes.size())) return;
    stopSequencePreview();
    m_sequenceAudioPlayer.stopSfxChannel(30);
    m_sequencePreviewCampaign = std::make_unique<Campaign>();
    if (!m_sequencePreviewCampaign->init(m_window, m_renderer) ||
        !m_sequencePreviewCampaign->startSequencePreview(seq, seq.nodes[m_selectedSequenceNodeIndex].id,
            selectedSequenceSceneId(), std::max(0.0f, time), sequencePreviewSceneDocument(), sequenceSceneDrafts()))
    {
        stopSequencePreview();
        m_lastIoStatus = "Náhled v zadaném čase nelze načíst.";
        return;
    }
    const auto status = m_sequencePreviewCampaign->sequenceStatus();
    m_sequencePreviewActive = status.running;
    m_sequencePreviewPaused = true;
    m_sequencePreviewTimer = status.elapsed;
    m_sequencePreviewNodeTimer = time;
    m_sequencePreviewNodeId = status.nodeId;
    m_sequenceTimelineCursor = time;
}

void Editor::renderSequenceTimeline()
{
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= static_cast<int>(m_sequences.size()) ||
        m_selectedSequenceNodeIndex < 0 || m_selectedSequenceNodeIndex >= static_cast<int>(m_sequences[m_selectedSequenceIndex].nodes.size()))
    {
        ImGui::TextUnformatted("Vyberte uzel pro úpravu časové osy.");
        return;
    }
    if (m_sequenceTimelineNode != m_selectedSequenceNodeIndex || m_sequenceTimelineSequence != m_selectedSequenceIndex)
    {
        m_sequenceTimelineSelection.clear();
        m_sequenceTimelineDragPath.clear();
        m_sequenceTimelineCursor = 0.0f;
        m_sequenceTimelineScrubbing = false;
        m_sequenceTimelineNode = m_selectedSequenceNodeIndex;
        m_sequenceTimelineSequence = m_selectedSequenceIndex;
    }
    auto& node = m_sequences[m_selectedSequenceIndex].nodes[m_selectedSequenceNodeIndex];
    renderSequenceMotionTools();
    ImGui::SameLine();
    auto clips = sequence::BuildTimeline(node);
    float total = 0.0f;
    for (const auto& clip : clips)
        if (clip.path.size() == 1) total = std::max(total, clip.start + clip.duration);
    total = std::max(1.0f, total);
    if (m_sequencePreviewActive && !m_sequencePreviewPaused && m_sequencePreviewNodeId == node.id)
        m_sequenceTimelineCursor = m_sequencePreviewNodeTimer;

    ImGui::Text("Časová osa: %s", node.id.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputFloat("Čas (s)", &m_sequenceTimelineCursor, 0, 0, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) seekSequencePreview(std::clamp(m_sequenceTimelineCursor, 0.0f, total));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("Zoom", &m_sequenceTimelineZoom, 0.5f, 12.0f, "%.1fx");
    ImGui::SameLine();
    if (ImGui::Button("Celá osa")) m_sequenceTimelineZoom = 1.0f;
    ImGui::SameLine();
    ImGui::Checkbox("Přichytávat 0,1 s", &m_sequenceTimelineSnap);
    ImGui::TextDisabled("Kliknutí: vlastnosti | tažení: zpoždění | pravý okraj: délka | pravé tlačítko: nabídka | pravítko: náhled");

    ImGui::BeginChild("TimelineCanvas", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    const float labelW = 78.0f;
    const float rowH = 24.0f;
    const float rulerH = 22.0f;
    const float height = rulerH + 6 * rowH;
    const float visibleW = ImGui::GetContentRegionAvail().x;
    float scale = std::max(8.0f, (visibleW - labelW - 20.0f) / total) * m_sequenceTimelineZoom;
    if (!m_sequenceTimelineDragPath.empty()) scale = m_sequenceTimelineDragScale;
    const float width = std::max(visibleW, labelW + (total + 0.5f) * scale);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("TimelineInput", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    std::string droppedAudio;
    if (ImGui::BeginDragDropTarget())
    {
        if (const auto* payload = ImGui::AcceptDragDropPayload("SEQ_AUDIO"))
            droppedAudio = static_cast<const char*>(payload->Data);
        ImGui::EndDragDropTarget();
    }
    const auto mouse = ImGui::GetMousePos();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(23, 26, 31, 255));
    const char* rows[] = {"Actor", "Camera", "Audio", "Screen", "World", "Flow"};
    const ImU32 colors[] = {IM_COL32(91, 165, 215, 255), IM_COL32(143, 125, 215, 255), IM_COL32(80, 183, 131, 255),
        IM_COL32(211, 145, 83, 255), IM_COL32(120, 169, 102, 255), IM_COL32(144, 154, 169, 255)};
    const float tick = std::pow(10.0f, std::ceil(std::log10(65.0f / scale)));
    const float first = std::max(0.0f, std::floor(ImGui::GetScrollX() / scale / tick) * tick);
    const float last = std::min(total + tick, (ImGui::GetScrollX() + visibleW) / scale);
    for (float t = first; t <= last; t += tick)
    {
        const float x = origin.x + labelW + t * scale;
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), IM_COL32(50, 56, 65, 255));
        char label[32];
        SDL_snprintf(label, sizeof(label), "%.1fs", t);
        draw->AddText(ImVec2(x + 3, origin.y + 2), IM_COL32(185, 192, 203, 255), label);
    }
    for (int row = 0; row < 6; ++row)
        draw->AddLine(ImVec2(origin.x, origin.y + rulerH + (row + 1) * rowH),
            ImVec2(origin.x + width, origin.y + rulerH + (row + 1) * rowH), IM_COL32(48, 53, 61, 255));

    // Stack simultaneous events within each lane so even instantaneous actions remain selectable.
    std::vector<ImVec4> rectangles;
    std::vector<int> laneLevels(clips.size());
    int hit = -1;
    for (int i = 0; i < static_cast<int>(clips.size()); ++i)
    {
        const auto& clip = clips[i];
        const auto* action = sequence::FindTimelineAction(node, clip.path);
        const int row = TimelineRow(action->type);
        const float x = origin.x + labelW + clip.start * scale;
        const float w = std::max(10.0f, clip.duration * scale);
        int level = 0;
        for (int j = 0; j < i; ++j)
            if (TimelineRow(sequence::FindTimelineAction(node, clips[j].path)->type) == row &&
                rectangles[j].z > x && rectangles[j].x < x + w)
                level = std::max(level, laneLevels[j] + 1);
        laneLevels[i] = level;
        const float y = origin.y + rulerH + row * rowH + 2 + (level % 3) * 4;
        rectangles.emplace_back(x, y, x + w, y + rowH - 5 - (level % 3) * 4);
        if (hovered && ImGui::IsMouseHoveringRect(ImVec2(x, y), ImVec2(x + w, rectangles.back().w))) hit = i;
        const bool selected = clip.path == m_sequenceTimelineSelection;
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, rectangles.back().w), selected ? IM_COL32(238, 187, 78, 255) : colors[row], 3);
        draw->PushClipRect(ImVec2(x, y), ImVec2(x + w, rectangles.back().w), true);
        const std::string label = action->type + (action->data.contains("id") && action->data["id"].is_string() ? " " + action->data["id"].get<std::string>() : "");
        if (w > 28) draw->AddText(ImVec2(x + 4, y), IM_COL32(16, 23, 30, 255), label.c_str());
        draw->PopClipRect();
        if (sequence::HasEditableDuration(*action) && w > 12)
            draw->AddLine(ImVec2(x + w - 4, y + 3), ImVec2(x + w - 4, rectangles.back().w - 3), IM_COL32(255, 255, 255, 160));
    }
    const float playX = origin.x + labelW + std::max(0.0f, m_sequenceTimelineCursor) * scale;
    draw->AddLine(ImVec2(playX, origin.y), ImVec2(playX, origin.y + height), IM_COL32(255, 91, 83, 255), 2);

    // Keep labels fixed while horizontally scrolling the time axis.
    const float fixedX = origin.x + ImGui::GetScrollX();
    draw->AddRectFilled(ImVec2(fixedX, origin.y), ImVec2(fixedX + labelW, origin.y + height), IM_COL32(30, 34, 41, 255));
    for (int row = 0; row < 6; ++row)
        draw->AddText(ImVec2(fixedX + 5, origin.y + rulerH + row * rowH + 3), colors[row], rows[row]);
    if (mouse.x < fixedX + labelW) hit = -1;

    if (hovered && hit >= 0 && m_sequenceTimelineDragPath.empty())
    {
        const auto& clip = clips[hit];
        const auto* action = sequence::FindTimelineAction(node, clip.path);
        const bool edge = sequence::HasEditableDuration(*action) && mouse.x >= rectangles[hit].z - 6;
        ImGui::SetMouseCursor(edge ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_Hand);
        ImGui::BeginTooltip();
        ImGui::Text("%s  |  %.2f s  |  délka %.2f s", action->type.c_str(), clip.start, clip.duration);
        ImGui::Text("Zpoždění: %.2f s", clip.delay);
        ImGui::EndTooltip();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            m_sequenceTimelineSelection = clip.path;
            m_selectedSequenceActionIndex = clip.path.front();
            m_sequenceSelectionKind = SequenceEditorSelectionKind::Action;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("TimelineActionMenu");
            else
            {
                pauseSequencePreview(true);
                m_sequenceTimelineDragPath = clip.path;
                m_sequenceTimelineResize = edge;
                m_sequenceTimelineDragValue = edge ? clip.duration : clip.delay;
                m_sequenceTimelineDragScale = scale;
                m_sequenceTimelineDragChanged = false;
            }
        }
    }
    if (!m_sequenceTimelineDragPath.empty())
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            if (auto* action = sequence::FindTimelineAction(node, m_sequenceTimelineDragPath))
            {
                float value = std::max(0.0f, m_sequenceTimelineDragValue + ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x / scale);
                if (m_sequenceTimelineSnap && !ImGui::GetIO().KeyCtrl) value = std::round(value * 10.0f) / 10.0f;
                if (!m_sequenceTimelineDragChanged)
                {
                    markSequenceDirty(m_sequenceTimelineResize ? "resize timeline clip" : "move timeline clip");
                    stopSequencePreview();
                    m_sequenceTimelineDragChanged = true;
                }
                if (m_sequenceTimelineResize) sequence::SetActionDuration(*action, value);
                else sequence::SetActionDelay(*action, value);
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_sequenceTimelineDragPath.clear();
    }
    else if (hovered && hit < 0 && mouse.x >= fixedX + labelW && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        pauseSequencePreview(true);
        m_sequenceTimelineScrubbing = true;
    }
    if (m_sequenceTimelineScrubbing)
    {
        m_sequenceTimelineCursor = std::clamp((mouse.x - origin.x - labelW) / scale, 0.0f, total);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_sequenceTimelineScrubbing = false;
            seekSequencePreview(m_sequenceTimelineCursor);
        }
    }
    if ((hovered || active) && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0)
        m_sequenceTimelineZoom = std::clamp(m_sequenceTimelineZoom * std::pow(1.2f, ImGui::GetIO().MouseWheel), 0.5f, 12.0f);
    if (!droppedAudio.empty())
    {
        m_sequenceTimelineCursor = std::max(0.0f, (mouse.x - origin.x - labelW) / scale);
        const char* types[] = {"play_music", "play_sfx", "play_voice"};
        insertSequenceAudio(droppedAudio, types[m_sequenceAudioKind]);
    }
    if (ImGui::BeginPopup("TimelineActionMenu"))
    {
        auto* action = sequence::FindTimelineAction(node, m_sequenceTimelineSelection);
        if (action)
        {
            if (ImGui::MenuItem("Zrušit zpoždění"))
            {
                markSequenceDirty("reset timeline delay");
                sequence::SetActionDelay(*action, 0);
            }
            if (m_sequenceTimelineSelection.size() == 1)
            {
                int index = m_sequenceTimelineSelection.front();
                if (ImGui::MenuItem("Posunout v pořadí doleva", nullptr, false, index > 0))
                {
                    markSequenceDirty("reorder timeline clip");
                    std::swap(node.actions[index], node.actions[index - 1]);
                    m_sequenceTimelineSelection = {index - 1};
                    m_selectedSequenceActionIndex = index - 1;
                }
                if (ImGui::MenuItem("Posunout v pořadí doprava", nullptr, false, index + 1 < static_cast<int>(node.actions.size())))
                {
                    markSequenceDirty("reorder timeline clip");
                    std::swap(node.actions[index], node.actions[index + 1]);
                    m_sequenceTimelineSelection = {index + 1};
                    m_selectedSequenceActionIndex = index + 1;
                }
            }
            if (ImGui::MenuItem("Smazat akci"))
            {
                markSequenceDirty("delete timeline clip");
                auto parentPath = m_sequenceTimelineSelection;
                const int index = parentPath.back();
                parentPath.pop_back();
                auto* parent = sequence::FindTimelineAction(node, parentPath);
                auto& actions = parent ? parent->children : node.actions;
                actions.erase(actions.begin() + index);
                m_sequenceTimelineSelection.clear();
                m_selectedSequenceActionIndex = -1;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();
}
