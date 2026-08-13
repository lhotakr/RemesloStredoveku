#pragma once

#include "imgui.h"
#include "SmithingUiAtlas.generated.h"

#include <algorithm>

namespace SmithingImGui
{
    using AtlasRect = SmithingUiAtlas::Rect;

    inline ImVec2 AtlasUv(float x, float y)
    {
        return ImVec2(
            x / static_cast<float>(SmithingUiAtlas::Width),
            y / static_cast<float>(SmithingUiAtlas::Height));
    }

    inline void DrawSprite(
        ImDrawList* drawList,
        ImTextureID atlas,
        const AtlasRect& src,
        const ImVec2& dstMin,
        const ImVec2& dstMax,
        ImU32 tint = IM_COL32_WHITE)
    {
        if (!drawList || !atlas)
            return;

        drawList->AddImage(
            atlas,
            dstMin,
            dstMax,
            AtlasUv(static_cast<float>(src.x), static_cast<float>(src.y)),
            AtlasUv(static_cast<float>(src.x + src.w), static_cast<float>(src.y + src.h)),
            tint);
    }

    inline void DrawNineSlice(
        ImDrawList* drawList,
        ImTextureID atlas,
        const AtlasRect& src,
        const ImVec2& dstMin,
        const ImVec2& dstMax,
        float sourceBorderPx = 32.0f,
        float destinationBorderPx = 32.0f,
        ImU32 tint = IM_COL32_WHITE)
    {
        if (!drawList || !atlas || src.w <= 0 || src.h <= 0)
            return;

        const float dstW = std::max(0.0f, dstMax.x - dstMin.x);
        const float dstH = std::max(0.0f, dstMax.y - dstMin.y);
        const float db = std::min(destinationBorderPx, std::min(dstW, dstH) * 0.5f);
        const float sb = std::min(sourceBorderPx, std::min(src.w, src.h) * 0.5f);

        const float sx[4] = {
            static_cast<float>(src.x),
            static_cast<float>(src.x) + sb,
            static_cast<float>(src.x + src.w) - sb,
            static_cast<float>(src.x + src.w)
        };
        const float sy[4] = {
            static_cast<float>(src.y),
            static_cast<float>(src.y) + sb,
            static_cast<float>(src.y + src.h) - sb,
            static_cast<float>(src.y + src.h)
        };
        const float dx[4] = { dstMin.x, dstMin.x + db, dstMax.x - db, dstMax.x };
        const float dy[4] = { dstMin.y, dstMin.y + db, dstMax.y - db, dstMax.y };

        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                drawList->AddImage(
                    atlas,
                    ImVec2(dx[col], dy[row]),
                    ImVec2(dx[col + 1], dy[row + 1]),
                    AtlasUv(sx[col], sy[row]),
                    AtlasUv(sx[col + 1], sy[row + 1]),
                    tint);
            }
        }
    }

    inline bool AtlasButton(
        const char* id,
        ImTextureID atlas,
        const AtlasRect& normal,
        const AtlasRect& hovered,
        const AtlasRect& pressed,
        const AtlasRect& disabled,
        const ImVec2& size,
        bool enabled = true)
    {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        if (!enabled)
            ImGui::BeginDisabled();
        ImGui::InvisibleButton(id, size);
        if (!enabled)
            ImGui::EndDisabled();

        const AtlasRect* sprite = &normal;
        if (!enabled)
            sprite = &disabled;
        else if (ImGui::IsItemActive())
            sprite = &pressed;
        else if (ImGui::IsItemHovered())
            sprite = &hovered;

        DrawSprite(
            ImGui::GetWindowDrawList(),
            atlas,
            *sprite,
            min,
            ImVec2(min.x + size.x, min.y + size.y));

        return enabled && ImGui::IsItemClicked();
    }
}
