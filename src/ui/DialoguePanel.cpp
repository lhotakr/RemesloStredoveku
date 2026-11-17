#include "MedievalUI.h"
#include <algorithm>

namespace medieval
{
void UI::portrait(const std::string& key, const std::string& name, ImVec2 size) const
{
    const auto pos = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    panel(draw, pos, size, Surface::Stone);
    const float inset = 9 * scale();
    const auto alias = m_portraitAliases.find(key);
    const auto found = m_textures.find("portrait:" + (alias == m_portraitAliases.end() ? key : alias->second));
    if (found != m_textures.end())
    {
        const auto& tex = found->second;
        const float factor = std::min(std::max(1.0f, size.x - inset * 2) / tex.size.x,
                                      std::max(1.0f, size.y - inset * 2) / tex.size.y);
        const ImVec2 fitted{tex.size.x * factor, tex.size.y * factor};
        const ImVec2 top{pos.x + (size.x - fitted.x) / 2, pos.y + (size.y - fitted.y) / 2};
        draw->AddImage((ImTextureID)(intptr_t)tex.handle.get(), top, {top.x + fitted.x, top.y + fitted.y});
    }
    else
    {
        Font fontScope(*this, FontRole::Heading);
        // A neutral initial is a placeholder, not a purported character illustration.
        std::string initial = "?";
        if (!name.empty())
        {
            size_t bytes = 1;
            const auto c = static_cast<unsigned char>(name.front());
            if (c >= 0xF0) bytes = 4; else if (c >= 0xE0) bytes = 3; else if (c >= 0xC0) bytes = 2;
            initial = name.substr(0, bytes);
        }
        const auto textSize = ImGui::CalcTextSize(initial.c_str());
        draw->AddText({pos.x + (size.x - textSize.x) / 2, pos.y + (size.y - textSize.y) / 2},
            ImGui::ColorConvertFloat4ToU32(Palette::Ivory), initial.c_str());
    }
    image(draw, "portrait_frame", pos, size);
    ImGui::Dummy(size);
}

DialogueAction UI::dialogue(const char* id, const DialogueView& view) const
{
    const auto* vp = ImGui::GetMainViewport();
    const auto rect = DialogueRect(vp->WorkPos, vp->WorkSize);
    ImGui::SetNextWindowPos(rect.pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(rect.size, ImGuiCond_Always);
    Window window(*this, id, Surface::Wood, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    return window ? dialogueContents(view) : DialogueAction{};
}

DialogueAction UI::dialogueContents(const DialogueView& view) const
{
    DialogueAction action;
    const ImGuiID stateKey = ImGui::GetID("##dialogue-node");
    const ImGuiID nodeKey = ImGui::GetID((view.nodeKey.empty() ? view.text : view.nodeKey).c_str());
    auto* storage = ImGui::GetStateStorage();
    const bool resetScroll = storage->GetInt(stateKey, -1) != static_cast<int>(nodeKey);
    storage->SetInt(stateKey, static_cast<int>(nodeKey));
    const float s = scale();
    const auto available = ImGui::GetContentRegionAvail();
    const float portraitWidth = std::min(152 * s, available.x * 0.22f);
    if (ImGui::BeginTable("##conversation", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("speaker", ImGuiTableColumnFlags_WidthFixed, portraitWidth);
        ImGui::TableSetupColumn("words", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        {
            // Separate scrolling protects long NPC names at narrow resolutions.
            Child speaker(*this, "##speaker", {0, available.y}, Surface::Wood);
            if (speaker)
            {
                const float w = std::max(12.0f, ImGui::GetContentRegionAvail().x);
                portrait(view.portraitKey, view.speaker, {w, std::min(w * 1.15f, available.y * 0.53f)});
                heading(view.speaker);
                if (view.mood >= 0)
                    ImGui::TextDisabled("%s", view.mood >= 70 ? "Přívětivý" : view.mood < 35 ? "Nedůvěřivý" : "Zdrženlivý");
            }
        }
        ImGui::TableNextColumn();
        {
            const float footer = 48 * s;
            const float gap = ImGui::GetStyle().ItemSpacing.y;
            const float speechHeight = std::max(38 * s, std::min(100 * s, available.y * 0.37f));
            {
                if (resetScroll) ImGui::SetNextWindowScroll({0, 0});
                Child speech(*this, "##speech", {0, speechHeight}, Surface::Parchment);
                if (speech) ImGui::TextWrapped("%s", view.text.c_str());
            }
            {
                // Long or numerous answers scroll; the exit control remains reachable.
                const float height = std::max(1.0f, available.y - speechHeight - footer - 2 * gap);
                if (resetScroll) ImGui::SetNextWindowScroll({0, 0});
                Child choices(*this, "##choices", {0, height}, Surface::Wood);
                if (choices)
                    for (const auto& choice : view.choices)
                    {
                        ImGui::PushID(choice.id);
                        if (button("##answer", choice.label)) action.choice = choice.id;
                        ImGui::PopID();
                    }
            }
            if (button("##leave", "Ukončit rozhovor")) action.close = true;
        }
        ImGui::EndTable();
    }
    return action;
}
}
