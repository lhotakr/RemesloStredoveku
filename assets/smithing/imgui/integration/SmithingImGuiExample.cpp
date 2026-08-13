#include "SmithingImGuiSkin.h"

// The SDL texture can be passed as ImTextureID when the project uses the
// ImGui SDL_Renderer backend. Keep texture creation/destruction in Campaign.
void DrawSmithingSkinExample(ImTextureID smithingUiAtlas)
{
    using namespace SmithingUiAtlas;

    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin(
        "##smithing_skin_example",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings);

    const ImVec2 panelMin = ImGui::GetCursorScreenPos();
    const ImVec2 panelMax(panelMin.x + 460.0f, panelMin.y + 260.0f);

    SmithingImGui::DrawNineSlice(
        ImGui::GetWindowDrawList(),
        smithingUiAtlas,
        chrome_panel_parchment,
        panelMin,
        panelMax,
        32.0f,
        32.0f);

    ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0f, panelMin.y + 30.0f));
    ImGui::TextUnformatted(u8"CO PRÁVĚ POZORUJI");
    ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0f, panelMin.y + 78.0f));

    if (SmithingImGui::AtlasButton(
            "##continue_smithing",
            smithingUiAtlas,
            controls_button_normal,
            controls_button_hover,
            controls_button_pressed,
            controls_button_disabled,
            ImVec2(250.0f, 50.0f)))
    {
        // Continue the current station action.
    }

    ImGui::End();
}
