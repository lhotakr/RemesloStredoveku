#include "Campaign.h"

#include "../PathUtils.h"
#include "../Utf8.h"

#include <SDL_image.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <string>

#include "imgui.h"

namespace fs = std::filesystem;

namespace
{
SDL_Texture* LoadTexture(SDL_Renderer* renderer, const char* relativePath)
{
    if (!renderer || !relativePath)
        return nullptr;

    const fs::path path = pathutils::ProjectRoot() / relativePath;
    SDL_Texture* texture = IMG_LoadTexture(renderer, path.string().c_str());
    if (texture)
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

SDL_Texture* LoadTextureFromPath(SDL_Renderer* renderer, const fs::path& path)
{
    if (!renderer || path.empty())
        return nullptr;

    SDL_Texture* texture = IMG_LoadTexture(renderer, path.string().c_str());
    if (texture)
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

void DestroyTexture(SDL_Texture*& texture)
{
    if (texture)
    {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
}

int WorkpieceFrameIndex(smithing::NailForgePhase phase)
{
    switch (phase)
    {
    case smithing::NailForgePhase::RawMaterial: return 0;
    case smithing::NailForgePhase::Heating: return 1;
    case smithing::NailForgePhase::DrawingShank: return 2;
    case smithing::NailForgePhase::ForgingPoint: return 3;
    case smithing::NailForgePhase::CuttingBlank: return 4;
    case smithing::NailForgePhase::FormingHead: return 5;
    case smithing::NailForgePhase::Inspecting: return 6;
    case smithing::NailForgePhase::Finished: return 7;
    }
    return 0;
}

int WorkpieceAnimationFrameIndex(const smithing::NailWorkpieceState& nail)
{
    switch (nail.phase)
    {
    case smithing::NailForgePhase::RawMaterial:
        return 0;
    case smithing::NailForgePhase::Heating:
        return 0;
    case smithing::NailForgePhase::DrawingShank:
        if (nail.drawingWork < 0.9f)
            return 1;
        if (nail.drawingWork < 1.8f)
            return 2;
        return 3;
    case smithing::NailForgePhase::ForgingPoint:
        if (nail.pointSharpness < 0.30f)
            return 4;
        if (nail.pointSharpness < 0.62f)
            return 5;
        return 6;
    case smithing::NailForgePhase::CuttingBlank:
        return nail.cutFromStock ? 8 : 7;
    case smithing::NailForgePhase::FormingHead:
        if (nail.headSize < 0.30f)
            return 9;
        if (nail.headSize < 0.58f)
            return 10;
        if (nail.headSize < 0.78f)
            return 11;
        return 12;
    case smithing::NailForgePhase::Inspecting:
        if (nail.heat <= 0.08f)
            return 15;
        if (nail.heat <= 0.30f)
            return 14;
        return 13;
    case smithing::NailForgePhase::Finished:
        return nail.heat > 0.08f ? 14 : 15;
    }
    return 0;
}

smithing::NailAnimationPhase NailAnimationPhaseFor(const smithing::NailForgePhase phase)
{
    switch (phase)
    {
    case smithing::NailForgePhase::RawMaterial:
        return smithing::NailAnimationPhase::Material;
    case smithing::NailForgePhase::Heating:
        return smithing::NailAnimationPhase::Heating;
    case smithing::NailForgePhase::DrawingShank:
        return smithing::NailAnimationPhase::Drawing;
    case smithing::NailForgePhase::ForgingPoint:
        return smithing::NailAnimationPhase::Pointing;
    case smithing::NailForgePhase::CuttingBlank:
        return smithing::NailAnimationPhase::Cutting;
    case smithing::NailForgePhase::FormingHead:
        return smithing::NailAnimationPhase::Heading;
    case smithing::NailForgePhase::Inspecting:
        return smithing::NailAnimationPhase::Cooling;
    case smithing::NailForgePhase::Finished:
        return smithing::NailAnimationPhase::Finished;
    }
    return smithing::NailAnimationPhase::Heating;
}

int DefectFrameIndex(smithing::NailDefectId defect)
{
    switch (defect)
    {
    case smithing::NailDefectId::CrookedShank: return 0;
    case smithing::NailDefectId::BluntPoint: return 1;
    case smithing::NailDefectId::PointOffAxis: return 2;
    case smithing::NailDefectId::UndersizedHead: return 3;
    case smithing::NailDefectId::HeadOffCenter: return 4;
    case smithing::NailDefectId::NeckCrack: return 5;
    case smithing::NailDefectId::BurnedIron: return 6;
    case smithing::NailDefectId::OverdrawnShank: return 7;
    }
    return 0;
}

const smithing::NailDefect* DominantDefect(const smithing::NailWorkpieceState& nail)
{
    const smithing::NailDefect* dominant = nullptr;
    for (const smithing::NailDefect& defect : nail.defects)
    {
        if (!dominant ||
            (defect.critical && !dominant->critical) ||
            (defect.critical == dominant->critical && defect.severity > dominant->severity))
        {
            dominant = &defect;
        }
    }
    return dominant;
}

ImU32 HeatTint(float heat)
{
    if (heat > 0.90f)
        return IM_COL32(255, 210, 118, 255);
    if (heat > 0.70f)
        return IM_COL32(255, 150, 74, 255);
    if (heat > 0.42f)
        return IM_COL32(255, 104, 58, 255);
    if (heat > 0.18f)
        return IM_COL32(190, 82, 60, 255);
    return IM_COL32(210, 210, 205, 255);
}

ImU32 WithAlpha(ImU32 color, float alpha);

ImVec2 RotateAround(const ImVec2& point, const ImVec2& origin, float radians)
{
    if (std::abs(radians) < 0.0001f)
        return point;

    const float s = std::sin(radians);
    const float c = std::cos(radians);
    const ImVec2 local(point.x - origin.x, point.y - origin.y);
    return ImVec2(
        origin.x + local.x * c - local.y * s,
        origin.y + local.x * s + local.y * c);
}

float SegmentThicknessAt(const smithing::NailWorkpieceState& nail, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const float segmentPos = t * static_cast<float>(nail.shankSegments.size() - 1);
    const int left = std::clamp(static_cast<int>(std::floor(segmentPos)), 0, static_cast<int>(nail.shankSegments.size()) - 1);
    const int right = std::clamp(left + 1, 0, static_cast<int>(nail.shankSegments.size()) - 1);
    const float blend = segmentPos - static_cast<float>(left);
    return std::lerp(nail.shankSegments[left].thickness, nail.shankSegments[right].thickness, blend);
}

float SegmentOffsetAt(const smithing::NailWorkpieceState& nail, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const float segmentPos = t * static_cast<float>(nail.shankSegments.size() - 1);
    const int left = std::clamp(static_cast<int>(std::floor(segmentPos)), 0, static_cast<int>(nail.shankSegments.size()) - 1);
    const int right = std::clamp(left + 1, 0, static_cast<int>(nail.shankSegments.size()) - 1);
    const float blend = segmentPos - static_cast<float>(left);
    return std::lerp(nail.shankSegments[left].lateralOffset, nail.shankSegments[right].lateralOffset, blend);
}

float NailHalfHeightAt(const smithing::NailWorkpieceState& nail, float t)
{
    float thickness = 0.58f + SegmentThicknessAt(nail, t) * 0.42f;
    if (t > 0.68f)
    {
        const float tipT = std::clamp((t - 0.68f) / 0.32f, 0.0f, 1.0f);
        const float pointTaper = std::lerp(0.80f, 0.08f, nail.pointSharpness);
        thickness *= std::lerp(1.0f, pointTaper, tipT);
    }
    if (t < 0.17f && nail.phase == smithing::NailForgePhase::FormingHead)
    {
        const float headT = 1.0f - std::clamp(t / 0.17f, 0.0f, 1.0f);
        thickness += headT * (0.42f + nail.headSize * 0.95f);
    }
    return thickness;
}

float Hash01(int value)
{
    std::uint32_t x = static_cast<std::uint32_t>(value) * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

void DrawProceduralNailModel(ImDrawList* drawList,
                             const smithing::NailWorkpieceState& nail,
                             const ImVec2& center,
                             float size,
                             float rotation,
                             float alpha)
{
    if (!drawList || size <= 1.0f || alpha <= 0.0f)
        return;

    const float length = size * 0.78f;
    const float baseHalfHeight = size * 0.030f;
    const float heat = std::clamp(nail.heat, 0.0f, 1.0f);
    const float hot = std::clamp((heat - 0.14f) / 0.72f, 0.0f, 1.0f);
    const float pulse = 0.82f + 0.18f * std::sin(static_cast<float>(ImGui::GetTime()) * 7.0f + heat * 5.0f);
    const ImU32 edgeColor = WithAlpha(IM_COL32(34, 29, 25, 255), alpha * 0.88f);
    const ImU32 scaleColor = WithAlpha(IM_COL32(22, 20, 18, 255), alpha * 0.50f);
    const int samples = 24;

    auto toScreen = [&](float t, float y)
    {
        const float x = center.x - length * 0.5f + t * length;
        const float bend = SegmentOffsetAt(nail, t) * size * 0.045f;
        return RotateAround(ImVec2(x, center.y + bend + y), center, rotation);
    };

    if (hot > 0.0f)
    {
        const ImU32 outerGlow = WithAlpha(IM_COL32(255, 92, 28, 255), alpha * hot * pulse * 0.22f);
        const ImU32 innerGlow = WithAlpha(IM_COL32(255, 178, 74, 255), alpha * hot * pulse * 0.30f);
        drawList->AddLine(toScreen(0.08f, 0.0f), toScreen(0.95f, 0.0f), outerGlow, baseHalfHeight * (8.5f + hot * 4.0f));
        drawList->AddLine(toScreen(0.13f, 0.0f), toScreen(0.88f, 0.0f), innerGlow, baseHalfHeight * (3.4f + hot * 2.0f));
    }

    for (int i = 0; i < samples - 1; ++i)
    {
        const float t0 = static_cast<float>(i) / static_cast<float>(samples - 1);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(samples - 1);
        const float h0 = baseHalfHeight * NailHalfHeightAt(nail, t0);
        const float h1 = baseHalfHeight * NailHalfHeightAt(nail, t1);
        const float mid = (t0 + t1) * 0.5f;
        const float ember = hot * pulse * (0.45f + 0.55f * std::sin(mid * 14.0f + static_cast<float>(ImGui::GetTime()) * 3.5f));
        const float soot = 0.82f + Hash01(i * 31 + 9) * 0.22f;
        const int r = static_cast<int>((62.0f * soot) + ember * 135.0f);
        const int g = static_cast<int>((58.0f * soot) + ember * 56.0f);
        const int b = static_cast<int>((52.0f * soot) + ember * 18.0f);
        const ImU32 bodyColor = IM_COL32(
            std::clamp(r, 38, 220),
            std::clamp(g, 34, 145),
            std::clamp(b, 30, 92),
            static_cast<int>(alpha * 235.0f));
        ImVec2 quad[4] = {
            toScreen(t0, -h0),
            toScreen(t1, -h1),
            toScreen(t1, h1),
            toScreen(t0, h0)
        };
        drawList->AddConvexPolyFilled(quad, 4, bodyColor);
    }

    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(samples - 1);
        const float h = baseHalfHeight * NailHalfHeightAt(nail, t);
        const ImVec2 top = toScreen(t, -h);
        const ImVec2 bottom = toScreen(t, h);
        if (i % 3 == 0)
            drawList->AddLine(top, bottom, scaleColor, 0.9f);
    }

    for (int i = 0; i < 34; ++i)
    {
        const float t = 0.03f + Hash01(i * 53 + 7) * 0.94f;
        const float h = baseHalfHeight * NailHalfHeightAt(nail, t);
        const float y = (Hash01(i * 97 + 3) * 2.0f - 1.0f) * h * 0.72f;
        const float markLength = size * (0.008f + Hash01(i * 41 + 13) * 0.018f);
        const ImU32 markColor = Hash01(i * 17 + 5) > 0.28f
            ? WithAlpha(IM_COL32(18, 17, 16, 255), alpha * 0.46f)
            : WithAlpha(IM_COL32(150, 138, 118, 255), alpha * 0.24f);
        drawList->AddLine(
            toScreen(t - markLength / length, y),
            toScreen(t + markLength / length, y + (Hash01(i * 23 + 11) - 0.5f) * h * 0.35f),
            markColor,
            1.0f);
    }

    const ImVec2 start = toScreen(0.0f, 0.0f);
    const ImVec2 end = toScreen(1.0f, 0.0f);
    drawList->AddLine(start, end, WithAlpha(IM_COL32(255, 196, 98, 255), alpha * hot * pulse * 0.55f), 1.6f);

    if (nail.phase == smithing::NailForgePhase::FormingHead ||
        nail.phase == smithing::NailForgePhase::Inspecting ||
        nail.phase == smithing::NailForgePhase::Finished)
    {
        const float headRadius = baseHalfHeight * (2.1f + nail.headSize * 3.6f);
        const ImU32 bodyColor = IM_COL32(
            static_cast<int>(68.0f + hot * pulse * 120.0f),
            static_cast<int>(62.0f + hot * pulse * 48.0f),
            static_cast<int>(54.0f + hot * pulse * 18.0f),
            static_cast<int>(alpha * 235.0f));
        drawList->AddCircleFilled(toScreen(0.03f, 0.0f), headRadius, bodyColor, 24);
        drawList->AddCircle(toScreen(0.03f, 0.0f), headRadius, edgeColor, 24, 1.4f);
    }

    drawList->AddLine(toScreen(0.0f, -baseHalfHeight * NailHalfHeightAt(nail, 0.0f)),
                      toScreen(1.0f, -baseHalfHeight * NailHalfHeightAt(nail, 1.0f)),
                      edgeColor, 1.2f);
    drawList->AddLine(toScreen(0.0f, baseHalfHeight * NailHalfHeightAt(nail, 0.0f)),
                      toScreen(1.0f, baseHalfHeight * NailHalfHeightAt(nail, 1.0f)),
                      edgeColor, 1.2f);
}

void DrawForgeHeatAnimation(ImDrawList* drawList,
                            const ImVec2& playMin,
                            const ImVec2& playMax,
                            float workpieceHeat,
                            float alpha)
{
    if (!drawList || alpha <= 0.0f)
        return;

    const float w = playMax.x - playMin.x;
    const float h = playMax.y - playMin.y;
    const float time = static_cast<float>(ImGui::GetTime());
    const float heat = std::clamp(0.52f + workpieceHeat * 0.48f, 0.0f, 1.0f);
    const float pulse = 0.78f + 0.22f * std::sin(time * 5.2f);
    const ImVec2 bedCenter(playMin.x + w * 0.43f, playMin.y + h * 0.62f);

    for (int i = 0; i < 5; ++i)
    {
        const float drift = std::sin(time * (1.2f + i * 0.17f) + i * 1.9f);
        const float rx = w * (0.11f + Hash01(i * 31 + 4) * 0.08f);
        const float ry = h * (0.040f + Hash01(i * 47 + 8) * 0.035f);
        const ImVec2 center(
            bedCenter.x + (Hash01(i * 17 + 2) - 0.5f) * w * 0.22f + drift * w * 0.010f,
            bedCenter.y + (Hash01(i * 29 + 6) - 0.5f) * h * 0.10f);
        const int a = static_cast<int>(alpha * heat * pulse * (42.0f + Hash01(i * 11 + 5) * 55.0f));
        drawList->AddEllipseFilled(
            center,
            ImVec2(rx, ry),
            IM_COL32(255, 78, 22, std::clamp(a, 0, 135)),
            0.0f,
            36);
    }

    for (int i = 0; i < 26; ++i)
    {
        const float seed = Hash01(i * 71 + 13);
        const float cycle = std::fmod(time * (0.28f + Hash01(i * 19 + 7) * 0.42f) + seed, 1.0f);
        const float x = bedCenter.x + (Hash01(i * 23 + 3) - 0.5f) * w * 0.43f;
        const float y = bedCenter.y + (Hash01(i * 37 + 9) - 0.5f) * h * 0.13f - cycle * h * 0.12f;
        const float r = 1.0f + Hash01(i * 41 + 1) * 2.4f;
        const int a = static_cast<int>((1.0f - cycle) * heat * alpha * 180.0f);
        drawList->AddCircleFilled(
            ImVec2(x, y),
            r,
            IM_COL32(255, 132, 34, std::clamp(a, 0, 190)),
            10);
    }

    for (int i = 0; i < 8; ++i)
    {
        const float x = bedCenter.x + (static_cast<float>(i) - 3.5f) * w * 0.035f;
        const float wav = std::sin(time * (2.0f + i * 0.18f) + i * 0.7f);
        const float y0 = bedCenter.y - h * (0.03f + Hash01(i * 17 + 12) * 0.03f);
        const float y1 = y0 - h * (0.13f + Hash01(i * 13 + 2) * 0.07f);
        const int a = static_cast<int>(alpha * heat * pulse * 80.0f);
        drawList->AddBezierCubic(
            ImVec2(x, y0),
            ImVec2(x - w * 0.015f, (y0 + y1) * 0.5f),
            ImVec2(x + w * (0.012f + wav * 0.006f), (y0 + y1) * 0.5f),
            ImVec2(x + wav * w * 0.018f, y1),
            IM_COL32(255, 170, 62, std::clamp(a, 0, 120)),
            2.2f,
            16);
    }
}

float ScaleAmountForAudio(const smithing::NailWorkpieceState& nail)
{
    const float currentHeatScale = nail.heat > 0.86f ? (nail.heat - 0.86f) * 2.5f : 0.0f;
    const float overheatedScale = nail.overheatExposure * 1.7f;
    const float damagedSurface = (1.0f - nail.materialIntegrity) * 0.5f;
    return std::clamp(std::max({currentHeatScale, overheatedScale, damagedSurface}), 0.0f, 1.0f);
}

int ColorAlpha(ImU32 color)
{
    return static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFF);
}

ImU32 WithAlpha(ImU32 color, float alpha)
{
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const int a = static_cast<int>(static_cast<float>(ColorAlpha(color)) * alpha);
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(a, 0, 255)) << IM_COL32_A_SHIFT);
}

void AddWrappedDrawText(ImDrawList* drawList,
                        const ImVec2& pos,
                        float wrapWidth,
                        const std::string& text,
                        ImU32 color)
{
    if (!drawList || text.empty() || wrapWidth <= 8.0f)
        return;

    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const float lineHeight = fontSize + 3.0f;
    const char* lineStart = text.c_str();
    const char* textEnd = lineStart + text.size();
    float y = pos.y;
    int lineCount = 0;

    while (lineStart < textEnd && lineCount < 3)
    {
        const char* lineEnd = font->CalcWordWrapPositionA(1.0f, lineStart, textEnd, wrapWidth);
        if (lineEnd == lineStart)
            ++lineEnd;

        drawList->AddText(font, fontSize, ImVec2(pos.x, y), color, lineStart, lineEnd);
        y += lineHeight;
        ++lineCount;

        lineStart = lineEnd;
        while (lineStart < textEnd && (*lineStart == ' ' || *lineStart == '\n' || *lineStart == '\r' || *lineStart == '\t'))
            ++lineStart;
    }
}

float SmoothStep01(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void AddAtlasCellGrid(ImDrawList* drawList, SDL_Texture* texture, int frameIndex,
                      int columns, int rows, const ImVec2& center, float size, ImU32 tint);

void AddAtlasCell(ImDrawList* drawList, SDL_Texture* texture, int frameIndex,
                  const ImVec2& center, float size, ImU32 tint)
{
    AddAtlasCellGrid(drawList, texture, frameIndex, 4, 2, center, size, tint);
}

void AddAtlasCellGrid(ImDrawList* drawList, SDL_Texture* texture, int frameIndex,
                      int columns, int rows, const ImVec2& center, float size, ImU32 tint)
{
    if (!drawList || !texture)
        return;

    columns = std::max(1, columns);
    rows = std::max(1, rows);
    frameIndex = std::clamp(frameIndex, 0, columns * rows - 1);
    const int col = frameIndex % columns;
    const int row = frameIndex / columns;
    const ImVec2 uv0(static_cast<float>(col) / static_cast<float>(columns),
                     static_cast<float>(row) / static_cast<float>(rows));
    const ImVec2 uv1(static_cast<float>(col + 1) / static_cast<float>(columns),
                     static_cast<float>(row + 1) / static_cast<float>(rows));
    const ImVec2 p0(center.x - size * 0.5f, center.y - size * 0.5f);
    const ImVec2 p1(center.x + size * 0.5f, center.y + size * 0.5f);
    drawList->AddImage(reinterpret_cast<ImTextureID>(texture), p0, p1, uv0, uv1, tint);
}

void AddAtlasCellGridRotated(ImDrawList* drawList, SDL_Texture* texture, int frameIndex,
                             int columns, int rows, const ImVec2& center, float size,
                             float rotationRadians, ImU32 tint)
{
    if (!drawList || !texture)
        return;

    columns = std::max(1, columns);
    rows = std::max(1, rows);
    frameIndex = std::clamp(frameIndex, 0, columns * rows - 1);
    const int col = frameIndex % columns;
    const int row = frameIndex / columns;
    const ImVec2 uv0(static_cast<float>(col) / static_cast<float>(columns),
                     static_cast<float>(row) / static_cast<float>(rows));
    const ImVec2 uv1(static_cast<float>(col + 1) / static_cast<float>(columns),
                     static_cast<float>(row + 1) / static_cast<float>(rows));

    const float half = size * 0.5f;
    const float c = std::cos(rotationRadians);
    const float s = std::sin(rotationRadians);
    auto rotatePoint = [&](float x, float y)
    {
        return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
    };

    const ImVec2 p0 = rotatePoint(-half, -half);
    const ImVec2 p1 = rotatePoint( half, -half);
    const ImVec2 p2 = rotatePoint( half,  half);
    const ImVec2 p3 = rotatePoint(-half,  half);

    drawList->AddImageQuad(
        reinterpret_cast<ImTextureID>(texture),
        p0,
        p1,
        p2,
        p3,
        uv0,
        ImVec2(uv1.x, uv0.y),
        uv1,
        ImVec2(uv0.x, uv1.y),
        tint);
}

void AddAtlasCellOffset(ImDrawList* drawList, SDL_Texture* texture, int frameIndex,
                        const ImVec2& center, const ImVec2& offset, float size, ImU32 tint)
{
    AddAtlasCell(drawList, texture, frameIndex, ImVec2(center.x + offset.x, center.y + offset.y), size, tint);
}

const char* PhaseText(smithing::NailForgePhase phase)
{
    switch (phase)
    {
    case smithing::NailForgePhase::RawMaterial: return U8("studená hranatá tyč");
    case smithing::NailForgePhase::Heating: return U8("zahřívání pracovního konce");
    case smithing::NailForgePhase::DrawingShank: return U8("vytahování dříku");
    case smithing::NailForgePhase::ForgingPoint: return U8("kování čtyřhranné špičky");
    case smithing::NailForgePhase::CuttingBlank: return U8("oddělení polotovaru");
    case smithing::NailForgePhase::FormingHead: return U8("rozkování hlavy");
    case smithing::NailForgePhase::Inspecting: return U8("kontrola žhavého výrobku");
    case smithing::NailForgePhase::Finished: return U8("vychladlý hotový hřebík");
    }
    return "";
}

const char* GradeText(smithing::NailQualityGrade grade)
{
    switch (grade)
    {
    case smithing::NailQualityGrade::Rejected: return U8("vyřazený");
    case smithing::NailQualityGrade::Provisional: return U8("nouzově použitelný");
    case smithing::NailQualityGrade::Common: return U8("obyčejný");
    case smithing::NailQualityGrade::Honest: return U8("poctivý");
    case smithing::NailQualityGrade::Excellent: return U8("výborný");
    }
    return "";
}

const char* HeatText(float heat)
{
    if (heat <= 0.12f) return U8("železo je tmavé a studené");
    if (heat < 0.42f) return U8("jen temně rudý stín, klade odpor");
    if (heat < 0.72f) return U8("třešňový pracovní žár");
    if (heat < 0.86f) return U8("sytý oranžový žár, dobře se hýbe");
    if (heat < 0.96f) return U8("příliš světlý žár a rychlé okuje");
    return U8("nebezpečně jasné železo");
}

const char* StraightnessText(float value)
{
    if (value > 0.88f) return U8("dřík drží osu");
    if (value > 0.72f) return U8("dřík je trochu živý v ruce");
    if (value > 0.55f) return U8("dřík se viditelně stáčí");
    return U8("dřík je křivý");
}

const char* PointText(float sharpness, float alignment)
{
    if (sharpness > 0.78f && alignment > 0.82f) return U8("špička je ostrá a sedí v ose");
    if (sharpness < 0.55f) return U8("špička zůstává tupá");
    if (alignment < 0.72f) return U8("špička uhýbá mimo osu");
    return U8("špička potřebuje přesnější vedení");
}

const char* HeadText(float size, float alignment)
{
    if (size > 0.76f && alignment > 0.82f) return U8("hlava sedí dostatečně a rovně");
    if (size < 0.55f) return U8("hlava je malá");
    if (alignment < 0.72f) return U8("hlava utíká stranou");
    return U8("hlava ještě chce srovnat");
}

const char* IntegrityText(const smithing::NailWorkpieceState& nail)
{
    if (smithing::HasDefect(nail, smithing::NailDefectId::BurnedIron))
        return U8("povrch je spálený a drobivý");
    if (smithing::HasDefect(nail, smithing::NailDefectId::NeckCrack))
        return U8("u krčku je patrná trhlina");
    if (nail.materialIntegrity < 0.75f)
        return U8("železo nese stopy hrubé práce");
    return U8("povrch železa drží pohromadě");
}

const char* MessageText(const std::string& key)
{
    if (key == "begin_heating") return U8("Tyč je založená do výhně.");
    if (key == "heated") return U8("Konec se ohřívá. Sleduj barvu, ne čas.");
    if (key == "heated_with_bellows") return U8("Měchy přidaly žár. Nepřežeň to.");
    if (key == "too_cold_for_anvil") return U8("Na kovadlinu je to ještě chladné.");
    if (key == "forge_ready") return U8("Tyč je u výhně. Zahřej ji až ve chvíli, kdy ji založíš do uhlí.");
    if (key == "anvil_ready") return U8("Materiál je na kovadlině. Teď můžeš začít kovat.");
    if (key == "begin_drawing") return U8("Materiál je na kovadlině, vytahuj dřík.");
    if (key == "begin_point") return U8("Teď stáhni konec do čtyřhranné špičky.");
    if (key == "cut_blank") return U8("Polotovar je oddělený od tyče.");
    if (key == "begin_head") return U8("Postav polotovar a rozkovávej hlavu.");
    if (key == "begin_inspection") return U8("Hřebík je vykovaný. V kleštích ho přenes ke kádi.");
    if (key == "hammer_strike") return U8("Úder změnil tvar v místě dopadu.");
    if (key == "quench_nail") return U8("Hřebík syčí v kádi a rychle ztrácí žár.");
    if (key == "ready_for_quench") return U8("Hřebík je vykovaný. V kleštích ho přenes ke kádi.");
    if (key == "ready_for_testing") return U8("Hřebík je studený. Odnes ho na kontrolní lavici.");
    if (key == "not_on_anvil") return U8("Údery patří na kovadlinu a ve správné fázi.");
    if (key == "finished_nail") return U8("Beneš hřebík převzal k posouzení.");
    if (key == "smithing_practice_point") return U8("Kovářství +1 za dokončený hřebík.");
    if (key == "lesson_started") return U8("Beneš vydal železo na deset hřebíků.");
    if (key == "lesson_complete") return U8("Lekce je uzavřená.");
    if (key == "material_taken") return U8("Ze stojanu sis vzal kus hranaté tyče na jeden hřebík.");
    if (key == "already_has_material") return U8("Jeden pracovní kus už máš. Nejdřív ho dokonči nebo zkaz.");
    if (key == "need_material") return U8("Nejdřív si ze stojanu vezmi kus hranaté tyče.");
    if (key == "need_tongs") return U8("Na horké železo potřebuješ kovářské kleště z držáku nářadí.");
    if (key == "need_hammer") return U8("Na kovadlině potřebuješ mít u sebe kovářské kladivo.");
    if (key == "need_heat_first") return U8("Se studenou tyčí musíš nejdřív k výhni.");
    if (key == "need_forged_nail") return U8("Ke kádi a kontrole patří až vykovaný polotovar hřebíku.");
    if (key == "need_quench_first") return U8("Hřebík je ještě horký. Nejdřív ho zchlaď v kádi.");
    if (key == "cannot_heat_finished") return U8("Hotový hřebík už do výhně nepatří.");
    if (key == "tongs_taken") return U8("Vzal sis kovářské kleště.");
    if (key == "hammer_taken") return U8("Vzal sis kovářské kladivo.");
    if (key == "tools_ready") return U8("Kleště i kladivo už máš u sebe.");
    if (key == "inventory_full") return U8("Nemáš kam uložit další věc.");
    if (key == "missing_item_definition") return U8("Chybí definice předmětu v katalogu itemů.");
    return "";
}

const char* BenesFeedbackText(const smithing::NailEvaluationResult& result)
{
    const std::string& key = result.feedbackKey;
    if (key == "crooked_shank") return U8("Beneš: Střídej strany a otáčej železo. Dřík nesmí utíkat za kladivem.");
    if (key == "blunt_point") return U8("Beneš: Špičku jsi nechal moc krátkou. Konec vytahuj déle a přesněji.");
    if (key == "point_off_axis") return U8("Beneš: Špička je mimo osu. U hřebíku se chyba ukáže hned při zatloukání.");
    if (key == "undersized_head") return U8("Beneš: Hlava je malá. Takový hřebík by prošel dřevem.");
    if (key == "head_off_center") return U8("Beneš: Hlava sedí stranou. Údery veď na střed, ne přes hranu.");
    if (key == "neck_crack") return U8("Beneš: Tohle bylo moc studené na takovou ránu. Krček praskl.");
    if (key == "burned_iron") return U8("Beneš: Přepálené železo se neomlouvá. Ten kus vyřaď.");
    if (key == "overdrawn_shank") return U8("Beneš: Dřík jsi přetáhl. Méně úderů a dřív přejdi na špičku.");
    if (key == "honest") return U8("Beneš: Poctivá práce. Takhle má běžný hřebík držet.");
    if (key == "excellent") return U8("Beneš: Výborný kus. Na první lekci vzácné.");
    if (key == "common") return U8("Beneš: Použitelný hřebík. Není ozdobný, ale poslouží.");
    return U8("Beneš: Nouzově by se použít dal, ale vidíš, kde je slabina.");
}

float SuggestedStrikePosition(const smithing::NailWorkpieceState& nail)
{
    switch (nail.phase)
    {
    case smithing::NailForgePhase::ForgingPoint: return 0.86f;
    case smithing::NailForgePhase::FormingHead: return 0.12f;
    case smithing::NailForgePhase::DrawingShank:
    {
        int best = 0;
        float thickest = -1.0f;
        for (int i = 0; i < static_cast<int>(nail.shankSegments.size()); ++i)
        {
            if (nail.shankSegments[i].thickness > thickest)
            {
                thickest = nail.shankSegments[i].thickness;
                best = i;
            }
        }
        return (static_cast<float>(best) + 0.5f) / static_cast<float>(nail.shankSegments.size());
    }
    default:
        return 0.5f;
    }
}

SDL_Texture* BackgroundForStation(SDL_Texture* forge, SDL_Texture* anvil, SDL_Texture* quench, SDL_Texture* testing, const std::string& station)
{
    if (station == "anvil")
        return anvil;
    if (station == "quench")
        return quench;
    if (station == "testing")
        return testing;
    return forge;
}

void DrawStationBackground(ImDrawList* drawList,
                           SDL_Texture* forge,
                           SDL_Texture* anvil,
                           SDL_Texture* quench,
                           SDL_Texture* testing,
                           const std::string& station,
                           const ImVec2& playMin,
                           const ImVec2& playMax,
                           float alpha)
{
    SDL_Texture* background = BackgroundForStation(
        forge,
        anvil,
        quench,
        testing,
        station);
    if (!background || alpha <= 0.0f)
        return;

    drawList->AddImage(
        reinterpret_cast<ImTextureID>(background),
        playMin,
        playMax,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        IM_COL32(255, 255, 255, static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)));
}

bool IsNailStrikeKeyDown()
{
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    return keys && keys[SDL_SCANCODE_E] != 0;
}

bool ShouldDrawDefectOverlay(const smithing::NailWorkpieceState& nail)
{
    return nail.phase == smithing::NailForgePhase::Inspecting ||
        nail.phase == smithing::NailForgePhase::Finished;
}

bool ShouldUseAnimatedWorkpieceAtlas(const smithing::NailWorkpieceState& nail, bool animationAtlasAvailable)
{
    if (!animationAtlasAvailable)
        return false;
    if (nail.phase == smithing::NailForgePhase::RawMaterial)
        return false;
    if (nail.phase == smithing::NailForgePhase::Heating && nail.heat < 0.18f)
        return false;
    return true;
}

int StaticWorkpieceFrameIndex(const smithing::NailWorkpieceState& nail)
{
    if (nail.phase == smithing::NailForgePhase::Heating && nail.heat < 0.18f)
        return 0;
    return WorkpieceFrameIndex(nail.phase);
}

bool IsHammerWorkPhase(smithing::NailForgePhase phase)
{
    return phase == smithing::NailForgePhase::DrawingShank ||
        phase == smithing::NailForgePhase::ForgingPoint ||
        phase == smithing::NailForgePhase::FormingHead;
}

std::string BenesGuidanceText(const smithing::NailTrainingBatchState& batch,
                              const smithing::NailWorkpieceState& nail,
                              const std::string& station,
                              int selectedRotation)
{
    const smithing::NailHintLevel hint = smithing::HintLevelForActiveNail(batch);
    const bool sameSideQueued =
        IsHammerWorkPhase(nail.phase) &&
        nail.hammerStrikeCount > 0 &&
        nail.lastStrikeRotation == selectedRotation;

    if (sameSideQueued)
    {
        if (nail.phase == smithing::NailForgePhase::FormingHead)
            return U8("Beneš: Teď hřebík otoč v kleštích. Hlava se jinak roztáhne stranou od otvoru v kovadlině.");
        if (nail.phase == smithing::NailForgePhase::ForgingPoint)
            return U8("Beneš: Teď otoč. Špička musí dostávat krátké rány ze střídavých stran, jinak uteče z osy.");
        return U8("Beneš: Teď otoč železo. Další rána ze stejné strany by tahala dřík do křiva.");
    }

    if (nail.heat >= 0.96f)
        return U8("Beneš: Vytáhni to z nejprudšího žáru. Tak jasné železo se rychle přepálí.");

    if (station == "forge")
    {
        if (nail.phase == smithing::NailForgePhase::RawMaterial)
            return U8("Beneš: Založ do uhlí jen pracovní konec. Dokud nechytí rudý žár, na kovadlinu nespěchej.");
        if (nail.heat < 0.38f)
            return U8("Beneš: Ještě zahřívej a občas otoč v žáru, ať není horká jen jedna hrana.");
        if (nail.heatUniformity < 0.72f)
            return U8("Beneš: Otoč kus v žáru. Nerovnoměrně ohřáté železo se pod kladivem kroutí.");
        if (nail.heat > 0.86f)
            return U8("Beneš: Už stačí. Odnes to na kovadlinu, měchy teď nech být.");
        return U8("Beneš: Máš pracovní žár. Vezmi tyč na kovadlinu a začni přesnými ranami.");
    }

    if (station == "anvil")
    {
        if (nail.heat < 0.42f &&
            (nail.phase == smithing::NailForgePhase::DrawingShank ||
             nail.phase == smithing::NailForgePhase::ForgingPoint ||
             nail.phase == smithing::NailForgePhase::FormingHead))
        {
            return U8("Beneš: Zpátky do výhně. Studené železo pod silnou ranou praská.");
        }

        switch (nail.phase)
        {
        case smithing::NailForgePhase::Heating:
            return U8("Beneš: Žár už drží. Začni vytahovat dřík a po každé ráně kus pootoč.");
        case smithing::NailForgePhase::DrawingShank:
            if (hint == smithing::NailHintLevel::ActiveGuidance)
                return U8("Beneš: Udeř na zvýrazněné místo dříku, pak otoč. Dřík má být stejnoměrný, ne placatý z jedné strany.");
            return U8("Beneš: Vytahuj dřík po úsecích a střídej strany po každé ráně.");
        case smithing::NailForgePhase::ForgingPoint:
            return U8("Beneš: Teď pracuj u konce. Lehčí rány stáhnou čtyřhrannou špičku, po každé ráně otoč.");
        case smithing::NailForgePhase::CuttingBlank:
            return U8("Beneš: Odděl polotovar od tyče. Pak ho postav do otvoru kovadliny na hlavu.");
        case smithing::NailForgePhase::FormingHead:
            return U8("Beneš: Dřík patří dolů do otvoru kovadliny. Krátké rány veď na střed hlavy a po dvou ranách otoč.");
        case smithing::NailForgePhase::Inspecting:
            return U8("Beneš: Už nekovej. Vezmi hotový kus v kleštích ke kádi a zchlaď ho.");
        default:
            break;
        }
    }

    if (station == "quench")
    {
        if (nail.phase == smithing::NailForgePhase::Inspecting && nail.heat > 0.12f)
            return U8("Beneš: Ponoř ho krátce do vody. Na kontrolní lavici patří až studený hřebík.");
        return U8("Beneš: Voda udělala své. Odnes hřebík na kontrolu.");
    }

    if (station == "testing")
    {
        if (nail.heat > 0.12f)
            return U8("Beneš: Ještě je horký. Nejdřív ke kádi, pak mi ho nos na posouzení.");
        return U8("Beneš: Teď ho prohlédneme: osa dříku, špička, hlava a povrch.");
    }

    return U8("Beneš: Vezmi správný kus, kleště a kladivo. Jeden hřebík dokonči dřív, než začneš další.");
}

std::string BenesGuidanceVoiceId(const smithing::NailTrainingBatchState& batch,
                                 const smithing::NailWorkpieceState& nail,
                                 const std::string& station,
                                 int selectedRotation)
{
    (void)batch;

    const bool sameSideQueued =
        IsHammerWorkPhase(nail.phase) &&
        nail.hammerStrikeCount > 0 &&
        nail.lastStrikeRotation == selectedRotation;

    if (sameSideQueued)
    {
        if (nail.phase == smithing::NailForgePhase::FormingHead)
            return "benes_hint_rotate_head_01";
        if (nail.phase == smithing::NailForgePhase::ForgingPoint)
            return "benes_hint_rotate_point_01";
        return "benes_hint_rotate_shank_01";
    }

    if (nail.heat >= 0.96f)
        return "benes_hint_overheat_01";

    if (station == "forge")
    {
        if (nail.phase == smithing::NailForgePhase::RawMaterial)
            return "benes_hint_heat_start_01";
        if (nail.heat < 0.38f || nail.heatUniformity < 0.72f)
            return "benes_hint_heat_more_01";
        return "benes_hint_heat_ready_01";
    }

    if (station == "anvil")
    {
        switch (nail.phase)
        {
        case smithing::NailForgePhase::Heating:
        case smithing::NailForgePhase::DrawingShank:
            return "benes_hint_anvil_drawing_01";
        case smithing::NailForgePhase::ForgingPoint:
            return "benes_hint_anvil_point_01";
        case smithing::NailForgePhase::FormingHead:
            return "benes_hint_anvil_head_01";
        default:
            break;
        }
    }

    if (station == "quench")
        return "benes_hint_quench_01";
    if (station == "testing")
        return "benes_hint_testing_01";

    return std::string();
}

constexpr const char* kNailRodItemId = "smithing_iron_rod_piece";
constexpr const char* kNailWorkpieceItemId = "smithing_nail_workpiece";
constexpr const char* kFinishedNailItemId = "smithing_nail_common";
constexpr const char* kSmithingTongsItemId = "smithing_tongs";
constexpr const char* kSmithingHammerItemId = "smithing_hammer";

bool IsNailWorkpieceInventoryItem(const std::string& itemId)
{
    return itemId == kNailRodItemId || itemId == kNailWorkpieceItemId;
}

const char* NailWorkpieceItemForPhase(smithing::NailForgePhase phase)
{
    switch (phase)
    {
    case smithing::NailForgePhase::RawMaterial:
    case smithing::NailForgePhase::Heating:
    case smithing::NailForgePhase::DrawingShank:
    case smithing::NailForgePhase::ForgingPoint:
        return kNailRodItemId;
    case smithing::NailForgePhase::CuttingBlank:
    case smithing::NailForgePhase::FormingHead:
    case smithing::NailForgePhase::Inspecting:
        return kNailWorkpieceItemId;
    case smithing::NailForgePhase::Finished:
        return "";
    }
    return "";
}

const char* StationText(const std::string& station)
{
    if (station == "stock") return U8("stojan se železem");
    if (station == "tool_rack") return U8("držák nářadí");
    if (station == "forge") return U8("výheň");
    if (station == "anvil") return U8("kovadlina");
    if (station == "quench") return U8("káď");
    if (station == "testing") return U8("kontrola");
    return U8("pracoviště");
}
}

int Campaign::currentGameMinuteSerial() const
{
    const auto& now = m_gameTime.now();
    return (((now.year * 12 + now.month) * 31 + now.day) * 24 + now.hour) * 60 + now.minute;
}

int Campaign::countInventoryItem(const std::string& itemId) const
{
    if (itemId.empty())
        return 0;

    int total = 0;
    auto addStack = [&](const ItemStack& stack)
    {
        if (!stack.empty() && stack.itemId == itemId)
            total += stack.count;
    };

    const PlayerInventory& inv = m_player.inventory;
    addStack(inv.equipped.head);
    addStack(inv.equipped.torsoInner);
    addStack(inv.equipped.torsoOuter);
    addStack(inv.equipped.legs);
    addStack(inv.equipped.feet);
    addStack(inv.equipped.cloak);
    addStack(inv.equipped.belt);
    addStack(inv.equipped.back);
    addStack(inv.equipped.mainHand);
    addStack(inv.equipped.offHand);
    addStack(inv.beltSlots.knife);
    addStack(inv.beltSlots.pouch);
    addStack(inv.beltSlots.utility1);
    addStack(inv.beltSlots.utility2);

    for (const ItemStack& stack : inv.pockets.items)
        addStack(stack);
    for (const ItemStack& stack : inv.backpack.items)
        addStack(stack);

    if (m_dragItem.active)
        addStack(m_dragItem.stack);

    return total;
}

bool Campaign::hasInventoryItem(const std::string& itemId) const
{
    return countInventoryItem(itemId) > 0;
}

bool Campaign::consumeInventoryItem(const std::string& itemId, int count)
{
    if (itemId.empty() || count <= 0)
        return false;

    if (countInventoryItem(itemId) < count)
        return false;

    int remaining = count;
    auto consumeFromStack = [&](ItemStack& stack)
    {
        if (remaining <= 0 || stack.empty() || stack.itemId != itemId)
            return;

        const int take = std::min(stack.count, remaining);
        stack.count -= take;
        remaining -= take;
        if (stack.count <= 0)
            stack.clear();
    };

    PlayerInventory& inv = m_player.inventory;
    consumeFromStack(inv.beltSlots.utility1);
    consumeFromStack(inv.beltSlots.utility2);
    consumeFromStack(inv.equipped.offHand);
    consumeFromStack(inv.equipped.mainHand);
    consumeFromStack(inv.beltSlots.knife);
    consumeFromStack(inv.beltSlots.pouch);
    consumeFromStack(inv.equipped.head);
    consumeFromStack(inv.equipped.torsoInner);
    consumeFromStack(inv.equipped.torsoOuter);
    consumeFromStack(inv.equipped.legs);
    consumeFromStack(inv.equipped.feet);
    consumeFromStack(inv.equipped.cloak);
    consumeFromStack(inv.equipped.belt);
    consumeFromStack(inv.equipped.back);

    for (ItemStack& stack : inv.pockets.items)
        consumeFromStack(stack);
    for (ItemStack& stack : inv.backpack.items)
        consumeFromStack(stack);

    if (m_dragItem.active)
    {
        consumeFromStack(m_dragItem.stack);
        if (m_dragItem.stack.empty())
        {
            m_dragItem.active = false;
            m_dragItem.source = nullptr;
        }
    }

    inv.pockets.compact();
    inv.backpack.compact();
    refreshInventoryDerivedStats();
    return remaining <= 0;
}

bool Campaign::addInventoryItem(const std::string& itemId, int count, bool preferQuickWorkSlot)
{
    if (itemId.empty() || count <= 0)
        return false;

    const auto defIt = m_itemDefs.find(itemId);
    if (defIt == m_itemDefs.end())
    {
        m_nailLastMessageKey = "missing_item_definition";
        return false;
    }

    const ItemDef& def = defIt->second;
    PlayerInventory& inv = m_player.inventory;
    int remaining = count;

    auto placeIntoSlot = [&](ItemStack& slot, int moveCount) -> bool
    {
        if (moveCount <= 0)
            return false;

        if (slot.empty())
        {
            slot.itemId = itemId;
            slot.count = moveCount;
            slot.durability = 100.0f;
            slot.wetness = 0.0f;
            return true;
        }

        if (slot.itemId == itemId && def.stackable && slot.count < def.maxStack)
        {
            const int moved = std::min(moveCount, def.maxStack - slot.count);
            slot.count += moved;
            remaining -= moved;
            return true;
        }

        return false;
    };

    if (preferQuickWorkSlot && remaining > 0)
    {
        if (inv.beltSlots.utility1.empty())
        {
            placeIntoSlot(inv.beltSlots.utility1, 1);
            --remaining;
        }
        else if (inv.beltSlots.utility2.empty())
        {
            placeIntoSlot(inv.beltSlots.utility2, 1);
            --remaining;
        }
    }

    const bool added = remaining <= 0 || inv.addItem(def, remaining, m_itemDefs);
    if (!added)
    {
        m_nailLastMessageKey = "inventory_full";
        return false;
    }

    refreshInventoryDerivedStats();
    return true;
}

bool Campaign::issueSmithingTool(const std::string& itemId)
{
    const auto defIt = m_itemDefs.find(itemId);
    if (defIt == m_itemDefs.end())
    {
        m_nailLastMessageKey = "missing_item_definition";
        return false;
    }

    ItemStack* preferredSlot = nullptr;
    if (itemId == kSmithingHammerItemId)
        preferredSlot = &m_player.inventory.equipped.mainHand;
    else if (itemId == kSmithingTongsItemId)
        preferredSlot = &m_player.inventory.equipped.offHand;

    if (preferredSlot && !preferredSlot->empty() && preferredSlot->itemId == itemId)
        return true;

    if (preferredSlot && preferredSlot->empty())
    {
        if (hasInventoryItem(itemId))
            consumeInventoryItem(itemId, 1);

        preferredSlot->itemId = itemId;
        preferredSlot->count = 1;
        preferredSlot->durability = 100.0f;
        preferredSlot->wetness = 0.0f;
        refreshInventoryDerivedStats();
        return true;
    }

    if (hasInventoryItem(itemId))
        return true;

    return addInventoryItem(itemId, 1, false);
}

bool Campaign::takeNailRodMaterial()
{
    if (m_nailTrainingBatch.completed)
    {
        m_nailLastMessageKey = "lesson_complete";
        return false;
    }

    beginBenesNailTraining();

    smithing::NailWorkpieceState* nail = smithing::ActiveNail(m_nailTrainingBatch);
    if (!nail)
    {
        m_nailLastMessageKey = "lesson_complete";
        return false;
    }

    if (nail->phase != smithing::NailForgePhase::RawMaterial || hasActiveNailWorkpieceItem())
    {
        m_nailLastMessageKey = "already_has_material";
        syncActiveNailWorkpieceItem();
        return false;
    }

    if (!addInventoryItem(kNailRodItemId, 1, true))
        return false;

    syncActiveNailWorkpieceItem();
    m_nailLastMessageKey = "material_taken";
    consoleLog(U8("Ze stojanu beres kus hranate tyce na hrebik."));
    return true;
}

bool Campaign::hasActiveNailWorkpieceItem() const
{
    return hasInventoryItem(kNailRodItemId) || hasInventoryItem(kNailWorkpieceItemId);
}

void Campaign::syncActiveNailWorkpieceItem()
{
    const smithing::NailWorkpieceState* nail = smithing::ActiveNail(m_nailTrainingBatch);
    if (!nail || nail->phase == smithing::NailForgePhase::Finished)
        return;

    const std::string targetItemId = NailWorkpieceItemForPhase(nail->phase);
    if (targetItemId.empty())
        return;

    const char* workpieceItemIds[] = {kNailRodItemId, kNailWorkpieceItemId};
    for (const char* itemId : workpieceItemIds)
    {
        if (targetItemId != itemId && hasInventoryItem(itemId))
        {
            consumeInventoryItem(itemId, 1);
            break;
        }
    }

    if (!hasInventoryItem(targetItemId))
    {
        addInventoryItem(targetItemId, 1, true);
        return;
    }

    PlayerInventory& inv = m_player.inventory;
    if (inv.beltSlots.utility1.itemId == targetItemId || inv.beltSlots.utility2.itemId == targetItemId)
        return;

    ItemStack* freeQuickSlot = nullptr;
    if (inv.beltSlots.utility1.empty())
        freeQuickSlot = &inv.beltSlots.utility1;
    else if (inv.beltSlots.utility2.empty())
        freeQuickSlot = &inv.beltSlots.utility2;

    if (!freeQuickSlot)
    {
        auto tryFreeQuickSlot = [&](ItemStack& slot) -> bool
        {
            if (slot.empty() || IsNailWorkpieceInventoryItem(slot.itemId))
                return false;

            const auto defIt = m_itemDefs.find(slot.itemId);
            if (defIt == m_itemDefs.end())
                return false;

            ItemStack displaced = slot;
            if (inv.backpack.canAccept(defIt->second, displaced.count, m_itemDefs))
            {
                inv.backpack.items.push_back(displaced);
                slot.clear();
                return true;
            }
            if (inv.pockets.canAccept(defIt->second, displaced.count, m_itemDefs))
            {
                inv.pockets.items.push_back(displaced);
                slot.clear();
                return true;
            }
            return false;
        };

        if (tryFreeQuickSlot(inv.beltSlots.utility1))
            freeQuickSlot = &inv.beltSlots.utility1;
        else if (tryFreeQuickSlot(inv.beltSlots.utility2))
            freeQuickSlot = &inv.beltSlots.utility2;
    }

    if (freeQuickSlot && consumeInventoryItem(targetItemId, 1))
    {
        freeQuickSlot->itemId = targetItemId;
        freeQuickSlot->count = 1;
        freeQuickSlot->durability = 100.0f;
        freeQuickSlot->wetness = 0.0f;
        refreshInventoryDerivedStats();
    }
}

void Campaign::clearActiveNailWorkpieceItems()
{
    const char* workpieceItemIds[] = {kNailWorkpieceItemId, kNailRodItemId};
    for (const char* itemId : workpieceItemIds)
    {
        if (hasInventoryItem(itemId))
            consumeInventoryItem(itemId, 1);
    }
}

bool Campaign::canUseCurrentSmithingStation(std::string* outMessageKey) const
{
    auto fail = [&](const char* messageKey)
    {
        if (outMessageKey)
            *outMessageKey = messageKey;
        return false;
    };

    const smithing::NailWorkpieceState* nail = smithing::ActiveNail(m_nailTrainingBatch);
    if (!nail || !hasActiveNailWorkpieceItem())
        return fail("need_material");

    if (m_nailTrainingStation == "forge")
    {
        if (!hasInventoryItem(kSmithingTongsItemId))
            return fail("need_tongs");
        if (nail->phase == smithing::NailForgePhase::Inspecting || nail->phase == smithing::NailForgePhase::Finished)
            return fail("cannot_heat_finished");
        return true;
    }

    if (m_nailTrainingStation == "anvil")
    {
        if (!hasInventoryItem(kSmithingTongsItemId))
            return fail("need_tongs");
        if (!hasInventoryItem(kSmithingHammerItemId))
            return fail("need_hammer");
        if (nail->phase == smithing::NailForgePhase::RawMaterial)
            return fail("need_heat_first");
        if (nail->phase == smithing::NailForgePhase::Heating && nail->heat < 0.38f)
            return fail("too_cold_for_anvil");
        if (nail->phase == smithing::NailForgePhase::Inspecting || nail->phase == smithing::NailForgePhase::Finished)
            return fail("need_quench_first");
        return true;
    }

    if (m_nailTrainingStation == "quench")
    {
        if (!hasInventoryItem(kSmithingTongsItemId))
            return fail("need_tongs");
        if (nail->phase != smithing::NailForgePhase::Inspecting)
            return fail("need_forged_nail");
        return true;
    }

    if (m_nailTrainingStation == "testing")
    {
        if (nail->phase != smithing::NailForgePhase::Inspecting)
            return fail("need_forged_nail");
        if (nail->heat > 0.12f)
            return fail("need_quench_first");
        return true;
    }

    return true;
}

void Campaign::quenchActiveNailWorkpiece()
{
    smithing::NailWorkpieceState* nail = smithing::ActiveNail(m_nailTrainingBatch);
    if (!nail)
        return;

    m_nailForgingController.SyncVisualState(
        NailAnimationPhaseFor(nail->phase),
        WorkpieceAnimationFrameIndex(*nail),
        nail->heat);
    m_nailForgingController.SetStation(smithing::SmithingStation::Quench);
    const bool tongsAnimated = m_nailForgingController.RequestTongsWork();
    m_nailForgingController.RequestQuench();
    if (!tongsAnimated)
        m_smithingNailAudio.onQuenchTongsWork(nail->heat);
    m_smithingNailAudio.onWaterContact(nail->heat);
    m_nailQuenchActive = true;
    nail->heat = std::max(0.0f, nail->heat - 0.05f);
    nail->heatUniformity = std::min(1.0f, nail->heatUniformity + 0.12f);
    nail->lastUpdatedGameMinute = currentGameMinuteSerial();
    smithing::RecomputeNailDerivedState(*nail, true);
    syncActiveNailWorkpieceItem();
    m_nailLastMessageKey = "quench_nail";
}

bool Campaign::ensureSmithingMinigameAssets()
{
    if (m_smithingAssets.loadAttempted)
    {
        m_smithingNailAudio.load();
        return m_smithingAssets.forgeBackground &&
            m_smithingAssets.anvilBackground &&
            m_smithingAssets.quenchBackground &&
            m_smithingAssets.testingBackground &&
            m_smithingAssets.nailWorkpieceAtlas &&
            m_smithingAssets.nailDefectAtlas;
    }

    m_smithingAssets.loadAttempted = true;
    m_smithingNailAudio.load();
    m_smithingAssets.forgeBackground = LoadTexture(m_renderer, "assets/smithing/minigames/assets/backgrounds/forge_playfield_1536x768.jpg");
    m_smithingAssets.anvilBackground = LoadTexture(m_renderer, "assets/smithing/minigames/assets/backgrounds/anvil_playfield_1536x768.jpg");
    m_smithingAssets.quenchBackground = LoadTexture(m_renderer, "assets/smithing/minigames/assets/backgrounds/quench_playfield_1536x768.jpg");
    m_smithingAssets.testingBackground = LoadTexture(m_renderer, "assets/smithing/minigames/assets/backgrounds/testing_playfield_1536x768.jpg");
    m_smithingAssets.nailWorkpieceAtlas = LoadTexture(m_renderer, "assets/smithing/minigames/assets/overlays/workpiece_nail/nail_workpiece_stages_atlas_2048x1024_v1.png");
    m_smithingAssets.nailDefectAtlas = LoadTexture(m_renderer, "assets/smithing/minigames/assets/overlays/defects_nail/nail_defects_atlas_2048x1024_v1.png");
    m_smithingAssets.handsToolsAtlas = LoadTexture(m_renderer, "assets/smithing/minigames/assets/overlays/smithing_hands_tools_atlas_2048x1024.png");
    m_smithingAssets.effectsAtlas = LoadTexture(m_renderer, "assets/smithing/minigames/assets/effects/smithing_vfx_atlas_2048x1024.png");
    const fs::path nailManifestPath = pathutils::ProjectRoot() / "assets/smithing/minigames/nail/nail_forging_animations.json";
    if (m_nailForgingController.LoadManifest(nailManifestPath))
    {
        if (const smithing::SmithingAnimationManifest* manifest = m_nailForgingController.Manifest())
        {
            if (const smithing::AnimationDef* workpiece = manifest->Find("nail_workpiece_deformation"))
                m_smithingAssets.nailAnimationWorkpieceAtlas = LoadTextureFromPath(m_renderer, workpiece->imagePath);
            if (const smithing::AnimationDef* hammer = manifest->Find("hammer_strike_heavy"))
                m_smithingAssets.hammerStrikeAtlas = LoadTextureFromPath(m_renderer, hammer->imagePath);
            if (const smithing::AnimationDef* tongs = manifest->Find("tongs_rotate_quarter"))
                m_smithingAssets.tongsRotateAtlas = LoadTextureFromPath(m_renderer, tongs->imagePath);
            if (const smithing::AnimationDef* effects = manifest->Find("sparks_small"))
                m_smithingAssets.nailAnimationEffectsAtlas = LoadTextureFromPath(m_renderer, effects->imagePath);
        }
    }

    return m_smithingAssets.forgeBackground &&
        m_smithingAssets.anvilBackground &&
        m_smithingAssets.quenchBackground &&
        m_smithingAssets.testingBackground &&
        m_smithingAssets.nailWorkpieceAtlas &&
        m_smithingAssets.nailDefectAtlas;
}

void Campaign::destroySmithingMinigameAssets()
{
    DestroyTexture(m_smithingAssets.forgeBackground);
    DestroyTexture(m_smithingAssets.anvilBackground);
    DestroyTexture(m_smithingAssets.quenchBackground);
    DestroyTexture(m_smithingAssets.testingBackground);
    DestroyTexture(m_smithingAssets.nailWorkpieceAtlas);
    DestroyTexture(m_smithingAssets.nailDefectAtlas);
    DestroyTexture(m_smithingAssets.handsToolsAtlas);
    DestroyTexture(m_smithingAssets.effectsAtlas);
    DestroyTexture(m_smithingAssets.nailAnimationWorkpieceAtlas);
    DestroyTexture(m_smithingAssets.hammerStrikeAtlas);
    DestroyTexture(m_smithingAssets.tongsRotateAtlas);
    DestroyTexture(m_smithingAssets.nailAnimationEffectsAtlas);
    m_smithingAssets.loadAttempted = false;
}

void Campaign::beginBenesNailTraining()
{
    const bool wasStarted = m_nailTrainingBatch.lessonStarted;
    smithing::StartNailTraining(m_nailTrainingBatch, currentGameMinuteSerial());
    if (!wasStarted)
    {
        m_nailForgingController.Reset();
        m_pendingNailHammerStrike.reset();
        m_nailStrikePowerSelecting = false;
        m_nailStrikePowerPhase = 0.0f;
        m_smithingNailAudio.resetQuench();
        m_nailLastMessageKey = "lesson_started";
        consoleLog(U8("Beneš ti vydal železo a chce deset pokusů o ručně kovaný hřebík."));
    }
}

void Campaign::openNailTrainingStation(const std::string& stationId)
{
    const std::string previousStation = m_nailTrainingStation;

    if (stationId == "stock" ||
        stationId == "tool_rack" ||
        stationId == "anvil" ||
        stationId == "testing" ||
        stationId == "forge" ||
        stationId == "quench")
    {
        m_nailTrainingStation = stationId;
    }
    else
    {
        m_nailTrainingStation = "stock";
    }

    if (m_nailTrainingStation == "anvil")
        m_nailStrikeKeyWasDown = IsNailStrikeKeyDown();
    else
        m_nailStrikePowerSelecting = false;
    if (m_nailTrainingStation != "forge")
    {
        m_nailForgeInserted = false;
        m_nailForgeDragging = false;
        m_nailForgeHeatAccumulator = 0.0f;
    }

    if (!hasActiveNailWorkpieceItem())
    {
        m_nailForgingController.SyncVisualState(smithing::NailAnimationPhase::Material, 0, 0.0f);
        m_nailForgingController.SetStation(smithing::SmithingStation::None);
    }
    else if (m_nailTrainingStation == "stock" || m_nailTrainingStation == "tool_rack")
    {
        m_nailForgingController.SetStation(smithing::SmithingStation::None);
    }
    else if (m_nailTrainingStation == "forge")
        m_nailForgingController.SetStation(smithing::SmithingStation::Forge);
    else if (m_nailTrainingStation == "anvil")
        m_nailForgingController.SetStation(smithing::SmithingStation::Anvil);
    else if (m_nailTrainingStation == "quench")
    {
        m_nailForgingController.SetStation(smithing::SmithingStation::Quench);
        m_nailForgingController.SetWaterContact(m_nailQuenchActive);
    }
    else if (m_nailTrainingStation == "testing")
        m_nailForgingController.SetStation(smithing::SmithingStation::Inspection);

    if (previousStation == "quench" && m_nailTrainingStation != "quench")
    {
        m_smithingNailAudio.resetQuench();
        m_nailQuenchActive = false;
        m_nailForgingController.SetWaterContact(false);
    }
    if (m_nailTrainingStation != "quench")
        m_nailQuenchActive = false;

    m_nailTrainingUiOpen = true;
}

bool Campaign::handleInteriorInteraction(const InteriorInteractionEvent& event)
{
    if (event.workshopId != "smithy_benes" && event.interiorId != "smithy_benes")
        return false;

    if (event.actionId != "start_nail_training" &&
        event.actionId != "take_smithing_tools" &&
        event.actionId != "heat_nail" &&
        event.actionId != "heat_nail_bellows" &&
        event.actionId != "forge_nail" &&
        event.actionId != "quench_nail" &&
        event.actionId != "inspect_nail")
    {
        return false;
    }

    if (!hasStoryFlag("quest_benes_nails_started"))
        setStoryFlag("quest_benes_nails_started");

    if (event.actionId == "start_nail_training")
    {
        openNailTrainingStation("stock");
        return true;
    }

    if (event.actionId == "take_smithing_tools")
    {
        openNailTrainingStation("tool_rack");
        return true;
    }

    if (event.actionId == "heat_nail")
    {
        openNailTrainingStation("forge");
        std::string blockMessage;
        if (!canUseCurrentSmithingStation(&blockMessage))
        {
            m_nailLastMessageKey = blockMessage;
            return true;
        }

        syncActiveNailWorkpieceItem();
        m_nailLastMessageKey = "forge_ready";
    }
    else if (event.actionId == "heat_nail_bellows")
    {
        openNailTrainingStation("forge");
        std::string blockMessage;
        if (!canUseCurrentSmithingStation(&blockMessage))
        {
            m_nailLastMessageKey = blockMessage;
            return true;
        }

        syncActiveNailWorkpieceItem();
        m_nailLastMessageKey = "forge_ready";
    }
    else if (event.actionId == "forge_nail")
    {
        openNailTrainingStation("anvil");
        std::string blockMessage;
        if (!canUseCurrentSmithingStation(&blockMessage))
        {
            m_nailLastMessageKey = blockMessage;
            return true;
        }

        syncActiveNailWorkpieceItem();
        m_nailLastMessageKey = "anvil_ready";
    }
    else if (event.actionId == "quench_nail")
    {
        openNailTrainingStation("quench");
        std::string blockMessage;
        if (!canUseCurrentSmithingStation(&blockMessage))
        {
            m_nailLastMessageKey = blockMessage;
            return true;
        }

        syncActiveNailWorkpieceItem();
    }
    else if (event.actionId == "inspect_nail")
    {
        openNailTrainingStation("testing");
        std::string blockMessage;
        if (!canUseCurrentSmithingStation(&blockMessage))
        {
            m_nailLastMessageKey = blockMessage;
            return true;
        }
        syncActiveNailWorkpieceItem();
    }
    else
    {
        openNailTrainingStation(event.stationId.empty() ? "stock" : event.stationId);
    }

    return true;
}

void Campaign::updateSmithingAudioForAnimationEvents()
{
    const std::vector<smithing::FiredAnimationEvent>& events = m_nailForgingController.RecentEvents();
    if (events.empty())
        return;

    const smithing::NailWorkpieceState* nail = smithing::ActiveNail(m_nailTrainingBatch);

    for (const smithing::FiredAnimationEvent& event : events)
    {
        if (event.animationId == "hammer_strike_heavy" &&
            event.eventId == "impact" &&
            nail &&
            m_pendingNailHammerStrike)
        {
            m_smithingNailAudio.onHammerImpact(
                nail->heat,
                m_pendingNailHammerStrike->force,
                ScaleAmountForAudio(*nail));
        }
        else if (event.animationId == "tongs_rotate_quarter" && event.eventId == "grip_closed")
        {
            if (m_nailTrainingStation == "forge")
                m_smithingNailAudio.onForgeTongsWork();
            else if (m_nailTrainingStation == "quench" && nail)
                m_smithingNailAudio.onQuenchTongsWork(nail->heat);
            else
                m_smithingNailAudio.onGripClosed();
        }
        else if (event.animationId == "tongs_rotate_quarter" && event.eventId == "rotation_complete")
        {
            if (m_nailTrainingStation == "anvil")
                m_smithingNailAudio.onRotationComplete();
        }
    }

    m_nailForgingController.ClearRecentEvents();
}

void Campaign::updateSmithingTrainingRuntime(int elapsedGameMinutes, float dt)
{
    if (m_nailTrainingBatch.lessonStarted)
        smithing::AdvanceNailTrainingTime(m_nailTrainingBatch, currentGameMinuteSerial());

    if (!hasActiveNailWorkpieceItem())
    {
        m_nailForgingController.SyncVisualState(smithing::NailAnimationPhase::Material, 0, 0.0f);
        m_nailForgingController.SetStation(smithing::SmithingStation::None);
        m_nailQuenchActive = false;
        m_nailForgeInserted = false;
        m_nailForgeDragging = false;
        m_nailForgeHeatAccumulator = 0.0f;
    }

    if (smithing::NailWorkpieceState* nail = smithing::ActiveNail(m_nailTrainingBatch))
    {
        const bool canKeepQuenching =
            m_nailQuenchActive &&
            m_nailTrainingUiOpen &&
            m_nailTrainingStation == "quench" &&
            nail->phase == smithing::NailForgePhase::Inspecting &&
            hasActiveNailWorkpieceItem();

        if (canKeepQuenching)
        {
            nail->heat = std::max(0.0f, nail->heat - dt * 0.16f);
            nail->heatUniformity = std::min(1.0f, nail->heatUniformity + dt * 0.32f);
            nail->lastUpdatedGameMinute = currentGameMinuteSerial();
            smithing::RecomputeNailDerivedState(*nail, true);

            m_nailForgingController.SyncVisualState(
                NailAnimationPhaseFor(nail->phase),
                WorkpieceAnimationFrameIndex(*nail),
                nail->heat);
            m_nailForgingController.SetStation(smithing::SmithingStation::Quench);
            m_nailForgingController.SetWaterContact(true);

            if (nail->heat <= 0.12f)
            {
                nail->heat = 0.0f;
                m_nailQuenchActive = false;
                m_nailForgingController.SetWaterContact(false);
                m_nailLastMessageKey = "ready_for_testing";
            }
        }
        else if (m_nailTrainingStation == "quench")
        {
            m_nailQuenchActive = false;
            m_nailForgingController.SetWaterContact(false);
        }

        const bool canHeatInForge =
            m_nailTrainingUiOpen &&
            m_nailTrainingStation == "forge" &&
            m_nailForgeInserted &&
            hasActiveNailWorkpieceItem() &&
            nail->phase != smithing::NailForgePhase::Inspecting &&
            nail->phase != smithing::NailForgePhase::Finished;
        if (canHeatInForge)
        {
            const float dx = std::abs(m_nailForgeZoneX - 0.50f) * 2.0f;
            const float dy = std::abs(m_nailForgeZoneY - 0.58f) * 2.0f;
            const float hotZone = std::clamp(1.0f - dx * 0.45f - dy * 0.75f, 0.20f, 1.0f);
            m_nailForgeHeatAccumulator += dt * (0.38f + hotZone * 0.72f);
            if (m_nailForgeHeatAccumulator >= 1.0f)
            {
                m_nailForgeHeatAccumulator = 0.0f;
                const smithing::NailActionResult action =
                    smithing::HeatNail(m_nailTrainingBatch, currentGameMinuteSerial(), false, false);
                if (action.changed)
                {
                    syncActiveNailWorkpieceItem();
                    m_nailForgingController.SyncVisualState(
                        NailAnimationPhaseFor(nail->phase),
                        WorkpieceAnimationFrameIndex(*nail),
                        nail->heat);
                    m_nailForgingController.SetStation(smithing::SmithingStation::Forge);
                }
            }
        }
        else
        {
            m_nailForgeHeatAccumulator = 0.0f;
        }
    }

    m_nailForgingController.Update(dt);
    updateSmithingAudioForAnimationEvents();
    if (m_pendingNailHammerStrike && m_nailForgingController.LastCommittedStrike())
    {
        const smithing::NailActionResult action =
            smithing::ApplyHammerStrike(m_nailTrainingBatch, *m_pendingNailHammerStrike, currentGameMinuteSerial());
        m_pendingNailHammerStrike.reset();
        m_nailForgingController.ClearLastCommittedStrike();
        m_nailLastMessageKey = action.messageKey;
        if (action.changed)
        {
            syncActiveNailWorkpieceItem();
            m_nailStrikeFlash = 1.0f;
            m_nailToolImpactPosition = m_nailStrikePosition;
        }
    }

    if (elapsedGameMinutes > 0)
        m_nailTrainingBatch.lastGameMinute = currentGameMinuteSerial();

    if (m_nailStrikeFlash > 0.0f)
        m_nailStrikeFlash = std::max(0.0f, m_nailStrikeFlash - dt * 3.0f);

    if (m_benesGuidanceVoiceCooldown > 0.0f)
        m_benesGuidanceVoiceCooldown = std::max(0.0f, m_benesGuidanceVoiceCooldown - dt);

    m_smithingNailAudio.updateAmbience(
        m_nailTrainingUiOpen && m_nailTrainingBatch.lessonStarted,
        m_nailTrainingStation == "forge");
}

void Campaign::grantSkillEvidence(const std::string& evidenceId)
{
    if (evidenceId.empty())
        return;

    const bool inserted = m_skillEvidence.insert(evidenceId).second;
    if (inserted)
    {
        if (!hasStoryFlag(evidenceId))
            setStoryFlag(evidenceId);
        consoleLog(U8("Důkaz dovednosti zapsán: ") + evidenceId);
    }
}

void Campaign::applyNailEvaluationResult(const smithing::NailEvaluationResult& result)
{
    m_lastNailEvaluation = result;
    m_hasLastNailEvaluation = true;

    const std::string feedbackFlag = "benes_nail_feedback_" + result.feedbackKey;
    if (!hasStoryFlag(feedbackFlag))
        setStoryFlag(feedbackFlag);

    clearActiveNailWorkpieceItems();
    const float before = m_player.stats.craft.smithing;
    m_player.stats.craft.smithing = std::clamp(before + 1.0f, 0.0f, 100.0f);
    if (m_player.stats.craft.smithing > before)
        consoleLog(MessageText("smithing_practice_point"));

    if (result.accepted)
    {
        addInventoryItem(kFinishedNailItemId, 1, false);
    }

    if (m_nailTrainingBatch.completed)
    {
        setStoryFlag("quest_benes_nails_done");
        grantSkillEvidence("smithing.nail.basic");
        m_nailLastMessageKey = "lesson_complete";
    }
}

bool Campaign::applyCurrentNailHammerStrike()
{
    std::string blockMessage;
    if (!canUseCurrentSmithingStation(&blockMessage))
    {
        m_nailLastMessageKey = blockMessage;
        return false;
    }

    smithing::NailHammerStrike strike;
    strike.position = m_nailStrikePosition;
    strike.force = m_nailStrikeForce;
    strike.rotationQuarterTurns = m_nailStrikeRotation;

    const smithing::NailWorkpieceState* audioNail = smithing::ActiveNail(m_nailTrainingBatch);
    const float impactHeat = audioNail ? audioNail->heat : 0.0f;
    const float scaleAmount = audioNail ? ScaleAmountForAudio(*audioNail) : 0.0f;

    if (const smithing::NailWorkpieceState* nail = smithing::ActiveNail(m_nailTrainingBatch))
    {
        m_nailForgingController.SyncVisualState(
            NailAnimationPhaseFor(nail->phase),
            WorkpieceAnimationFrameIndex(*nail),
            nail->heat);
    }

    smithing::StrikeInput visualStrike;
    visualStrike.targetX01 = strike.position;
    visualStrike.power01 = strike.force;
    visualStrike.direction01 = static_cast<float>(strike.rotationQuarterTurns) / 3.0f;

    if (m_nailForgingController.IsManifestLoaded() &&
        m_nailForgingController.RequestStrike(visualStrike))
    {
        m_pendingNailHammerStrike = strike;
        return true;
    }

    const smithing::NailActionResult action =
        smithing::ApplyHammerStrike(m_nailTrainingBatch, strike, currentGameMinuteSerial());
    m_nailLastMessageKey = action.messageKey;
    if (action.changed)
    {
        if (!m_nailForgingController.IsManifestLoaded())
            m_smithingNailAudio.onHammerImpact(impactHeat, strike.force, scaleAmount);

        m_nailStrikeFlash = 1.0f;
        m_nailToolImpactPosition = m_nailStrikePosition;
        return true;
    }
    return false;
}

void Campaign::renderNailTrainingDebugPanel()
{
#ifndef NDEBUG
    if (!m_nailTrainingUiOpen || !m_showDebugHud)
        return;

    smithing::NailWorkpieceState* nail = smithing::ActiveNail(m_nailTrainingBatch);
    if (!nail)
        return;

    ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Smithing nail debug", nullptr))
    {
        ImGui::End();
        return;
    }

    ImGui::SliderFloat("heat", &nail->heat, 0.0f, 1.0f);
    ImGui::SliderFloat("straightness", &nail->straightness, 0.0f, 1.0f);
    ImGui::SliderFloat("point sharpness", &nail->pointSharpness, 0.0f, 1.0f);
    ImGui::SliderFloat("head size", &nail->headSize, 0.0f, 1.0f);
    ImGui::SliderFloat("integrity", &nail->materialIntegrity, 0.0f, 1.0f);

    bool closedAfterDebugFinish = false;
    if (ImGui::Button("next phase"))
    {
        smithing::NailActionResult action = smithing::AdvancePhase(m_nailTrainingBatch, currentGameMinuteSerial());
        m_nailLastMessageKey = action.messageKey;
        if (action.evaluation)
        {
            applyNailEvaluationResult(*action.evaluation);
            closedAfterDebugFinish = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("finish nail"))
    {
        smithing::NailActionResult action = smithing::FinishActiveNail(m_nailTrainingBatch, currentGameMinuteSerial());
        m_nailLastMessageKey = action.messageKey;
        if (action.evaluation)
            applyNailEvaluationResult(*action.evaluation);
        closedAfterDebugFinish = true;
    }

    if (ImGui::Button("next batch item"))
    {
        nail->phase = smithing::NailForgePhase::Inspecting;
        smithing::NailActionResult action = smithing::FinishActiveNail(m_nailTrainingBatch, currentGameMinuteSerial());
        m_nailLastMessageKey = action.messageKey;
        if (action.evaluation)
            applyNailEvaluationResult(*action.evaluation);
        closedAfterDebugFinish = true;
    }

    if (closedAfterDebugFinish)
    {
        ImGui::End();
        return;
    }

    const char* defects[] = {
        "crooked_shank", "blunt_point", "point_off_axis", "undersized_head",
        "head_off_center", "neck_crack", "burned_iron", "overdrawn_shank"
    };
    static int selectedDefect = 0;
    ImGui::Combo("force defect", &selectedDefect, defects, IM_ARRAYSIZE(defects));
    if (ImGui::Button("apply defect"))
    {
        smithing::ForceDefect(*nail, smithing::NailDefectIdFromString(defects[selectedDefect]));
        smithing::RecomputeNailDerivedState(*nail, false);
    }

    ImGui::Separator();
    ImGui::Text("phase: %s", smithing::ToString(nail->phase));
    ImGui::Text("strikes: %d", nail->hammerStrikeCount);
    ImGui::Text("drawing %.3f point %.3f head %.3f", nail->drawingWork, nail->pointWork, nail->headWork);
    ImGui::Text("cold stress %.3f overheat %.3f", nail->coldHammerStress, nail->overheatExposure);
    for (const smithing::NailDefect& defect : nail->defects)
        ImGui::BulletText("%s severity %.2f critical %s", smithing::ToString(defect.id), defect.severity, defect.critical ? "yes" : "no");

    ImGui::End();
#endif
}

void Campaign::renderNailTrainingUi()
{
    if (!m_nailTrainingUiOpen)
        return;

    ensureSmithingMinigameAssets();

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    const ImVec2 windowSize(std::max(980.0f, display.x * 0.92f), std::max(620.0f, display.y * 0.86f));
    ImGui::SetNextWindowPos(ImVec2((display.x - windowSize.x) * 0.5f, (display.y - windowSize.y) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin(U8("Benešova lekce hřebíků"), &m_nailTrainingUiOpen, flags))
    {
        ImGui::End();
        return;
    }

    smithing::NailWorkpieceState* nail = smithing::ActiveNail(m_nailTrainingBatch);

    auto renderLastMessage = [&]()
    {
        if (!m_nailLastMessageKey.empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", MessageText(m_nailLastMessageKey));
        }
    };

    auto renderStationLine = [&]()
    {
        ImGui::Text(U8("Pracoviště: %s"), StationText(m_nailTrainingStation));
        if (ImGui::GetContentRegionAvail().x > 250.0f)
            ImGui::SameLine();
        if (ImGui::Button(U8("Odejít od pracoviště")))
            m_nailTrainingUiOpen = false;
    };

    auto renderStockControls = [&]()
    {
        renderStationLine();
        ImGui::Separator();
        ImGui::TextUnformatted(U8("Materiál pro hřebík"));

        const bool canTakeRod =
            !m_nailTrainingBatch.completed &&
            (!nail || nail->phase == smithing::NailForgePhase::RawMaterial) &&
            !hasActiveNailWorkpieceItem();

        ImGui::BeginDisabled(!canTakeRod);
        if (ImGui::Button(U8("Vzít kus hranaté tyče")))
        {
            takeNailRodMaterial();
            nail = smithing::ActiveNail(m_nailTrainingBatch);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::Button(U8("Plochý pásek"));
        ImGui::SameLine();
        ImGui::Button(U8("Silná tyč"));
        ImGui::SameLine();
        ImGui::Button(U8("Plát železa"));
        ImGui::EndDisabled();

        ImGui::TextDisabled(U8("Pro Benešovu lekci je povolený jen krátký kus tyče na hřebík."));
        renderLastMessage();
    };

    auto renderToolRackControls = [&]()
    {
        renderStationLine();
        ImGui::Separator();
        ImGui::TextUnformatted(U8("Nářadí"));

        const bool hasTongs = hasInventoryItem(kSmithingTongsItemId);
        ImGui::BeginDisabled(hasTongs);
        if (ImGui::Button(U8("Vzít kleště")))
        {
            if (issueSmithingTool(kSmithingTongsItemId))
                m_nailLastMessageKey = "tongs_taken";
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        const bool hasHammer = hasInventoryItem(kSmithingHammerItemId);
        ImGui::BeginDisabled(hasHammer);
        if (ImGui::Button(U8("Vzít kovářské kladivo")))
        {
            if (issueSmithingTool(kSmithingHammerItemId))
                m_nailLastMessageKey = "hammer_taken";
        }
        ImGui::EndDisabled();

        if (hasInventoryItem(kSmithingTongsItemId) && hasInventoryItem(kSmithingHammerItemId))
            m_nailLastMessageKey = "tools_ready";

        renderLastMessage();
    };

    if (!nail)
    {
        if (m_nailTrainingBatch.completed)
        {
            ImGui::TextWrapped(U8("Lekce je uzavřená. Přijato: %d, vyřazeno: %d. Důkaz: smithing.nail.basic"),
                m_nailTrainingBatch.acceptedCount,
                m_nailTrainingBatch.rejectedCount);
        }
        else if (m_nailTrainingStation == "tool_rack")
        {
            renderToolRackControls();
        }
        else if (m_nailTrainingStation == "stock")
        {
            renderStockControls();
        }
        else
        {
            renderStationLine();
            ImGui::Separator();
            m_nailLastMessageKey = "need_material";
            renderLastMessage();
        }

        if (ImGui::Button(U8("Zavřít")))
            m_nailTrainingUiOpen = false;
        ImGui::End();
        renderNailTrainingDebugPanel();
        return;
    }

    if (m_nailTrainingStation == "stock")
    {
        renderStockControls();
        ImGui::End();
        renderNailTrainingDebugPanel();
        return;
    }

    if (m_nailTrainingStation == "tool_rack")
    {
        renderToolRackControls();
        ImGui::End();
        renderNailTrainingDebugPanel();
        return;
    }

    if (!hasActiveNailWorkpieceItem())
    {
        renderStationLine();
        ImGui::Separator();
        m_nailLastMessageKey = "need_material";
        renderLastMessage();
        ImGui::End();
        renderNailTrainingDebugPanel();
        return;
    }

    const float dt = std::clamp(io.DeltaTime, 0.0f, 0.05f);
    if (m_nailVisualStation.empty())
    {
        m_nailVisualStation = m_nailTrainingStation;
        m_nailPreviousVisualStation = m_nailTrainingStation;
        m_nailStationTransition = 1.0f;
    }
    else if (m_nailVisualStation != m_nailTrainingStation)
    {
        m_nailPreviousVisualStation = m_nailVisualStation;
        m_nailVisualStation = m_nailTrainingStation;
        m_nailStationTransition = 0.0f;
    }
    m_nailStationTransition = std::min(1.0f, m_nailStationTransition + dt / 0.24f);

    const bool animationAtlasAvailable = m_smithingAssets.nailAnimationWorkpieceAtlas != nullptr;
    const bool targetUsesAnimationAtlas = ShouldUseAnimatedWorkpieceAtlas(*nail, animationAtlasAvailable);
    const int targetWorkpieceFrame = targetUsesAnimationAtlas
        ? WorkpieceAnimationFrameIndex(*nail)
        : StaticWorkpieceFrameIndex(*nail);
    if (m_nailVisualWorkpieceFrame < 0)
    {
        m_nailVisualWorkpieceFrame = targetWorkpieceFrame;
        m_nailPreviousWorkpieceFrame = targetWorkpieceFrame;
        m_nailVisualWorkpieceUsesAnimationAtlas = targetUsesAnimationAtlas;
        m_nailPreviousWorkpieceUsesAnimationAtlas = targetUsesAnimationAtlas;
        m_nailWorkpieceTransition = 1.0f;
    }
    else if (m_nailVisualWorkpieceFrame != targetWorkpieceFrame ||
             m_nailVisualWorkpieceUsesAnimationAtlas != targetUsesAnimationAtlas)
    {
        m_nailPreviousWorkpieceFrame = m_nailVisualWorkpieceFrame;
        m_nailPreviousWorkpieceUsesAnimationAtlas = m_nailVisualWorkpieceUsesAnimationAtlas;
        m_nailVisualWorkpieceFrame = targetWorkpieceFrame;
        m_nailVisualWorkpieceUsesAnimationAtlas = targetUsesAnimationAtlas;
        m_nailWorkpieceTransition = 0.0f;
    }
    m_nailWorkpieceTransition = std::min(1.0f, m_nailWorkpieceTransition + dt / 0.20f);

    const std::string benesGuidance =
        BenesGuidanceText(m_nailTrainingBatch, *nail, m_nailTrainingStation, m_nailStrikeRotation);
    const std::string benesGuidanceVoice =
        BenesGuidanceVoiceId(m_nailTrainingBatch, *nail, m_nailTrainingStation, m_nailStrikeRotation);
    if (!benesGuidanceVoice.empty() &&
        m_benesGuidanceVoiceCooldown <= 0.0f &&
        m_lastBenesGuidanceVoiceId != benesGuidanceVoice)
    {
        m_smithingNailAudio.playMentorCue(benesGuidanceVoice);
        m_lastBenesGuidanceVoiceId = benesGuidanceVoice;
        m_benesGuidanceVoiceCooldown = 5.5f;
    }

    auto renderStationActions = [&]()
    {
        const ImVec2 largeButton(190.0f, 46.0f);

        if (m_nailTrainingStation == "forge")
        {
            std::string blockMessage;
            const bool stationReady = canUseCurrentSmithingStation(&blockMessage);

            ImGui::BeginDisabled(!stationReady);
            if (ImGui::Button(m_nailForgeInserted ? U8("Vyndat") : U8("Vložit"), largeButton))
            {
                m_nailForgingController.SyncVisualState(
                    NailAnimationPhaseFor(nail->phase),
                    WorkpieceAnimationFrameIndex(*nail),
                    nail->heat);
                m_nailForgingController.SetStation(smithing::SmithingStation::Forge);
                m_smithingNailAudio.onForgeTongsWork();
                m_nailForgeInserted = !m_nailForgeInserted;
                m_nailForgeDragging = false;
                if (m_nailForgeInserted)
                {
                    const smithing::NailActionResult action = smithing::HeatNail(m_nailTrainingBatch, currentGameMinuteSerial(), false, false);
                    m_nailLastMessageKey = action.messageKey;
                }
                else
                {
                    m_nailLastMessageKey = "forge_ready";
                }
                syncActiveNailWorkpieceItem();
                m_nailForgingController.SetStation(smithing::SmithingStation::Forge);
            }
            ImGui::BeginDisabled(!m_nailForgeInserted);
            if (ImGui::Button(U8("Otočit v žáru"), largeButton))
            {
                m_nailForgingController.SyncVisualState(
                    NailAnimationPhaseFor(nail->phase),
                    WorkpieceAnimationFrameIndex(*nail),
                    nail->heat);
                m_nailForgingController.SetStation(smithing::SmithingStation::Forge);
                const smithing::NailActionResult action = smithing::HeatNail(m_nailTrainingBatch, currentGameMinuteSerial(), false, true);
                m_nailLastMessageKey = action.messageKey;
                syncActiveNailWorkpieceItem();
                m_nailForgingController.SetStation(smithing::SmithingStation::Forge);
                if (action.changed)
                    m_smithingNailAudio.onForgeTongsWork();
            }
            ImGui::EndDisabled();
            if (ImGui::Button(U8("Měchy"), largeButton))
            {
                m_nailForgingController.SyncVisualState(
                    NailAnimationPhaseFor(nail->phase),
                    WorkpieceAnimationFrameIndex(*nail),
                    nail->heat);
                m_nailForgingController.SetStation(smithing::SmithingStation::Forge);
                if (m_nailForgeInserted)
                {
                    const smithing::NailActionResult action = smithing::HeatNail(m_nailTrainingBatch, currentGameMinuteSerial(), true, false);
                    m_nailLastMessageKey = action.messageKey;
                    syncActiveNailWorkpieceItem();
                }
                else
                {
                    m_nailLastMessageKey = "forge_ready";
                }
                m_nailForgingController.SetStation(smithing::SmithingStation::Forge);
                m_smithingNailAudio.onBellowsPush();
            }
            ImGui::EndDisabled();

            if (!stationReady)
                ImGui::TextWrapped("%s", MessageText(blockMessage));
            else
                ImGui::TextWrapped("%s", m_nailForgeInserted
                    ? U8("LMB přetáhni kus v žhavé zóně. RMB otočí.")
                    : U8("Kus je mimo žár. Vlož ho do uhlí."));
            return;
        }

        if (m_nailTrainingStation == "anvil")
        {
            std::string blockMessage;
            const bool stationReady = canUseCurrentSmithingStation(&blockMessage);

            ImGui::BeginDisabled(!stationReady);
            ImGui::ProgressBar(std::clamp(m_nailStrikeForce, 0.0f, 1.0f), ImVec2(190.0f, 22.0f), U8("Síla"));
            ImGui::TextDisabled(U8("LMB držet/pustit"));
            if (ImGui::Button(U8("Otočit"), largeButton))
            {
                m_nailStrikeRotation = (m_nailStrikeRotation + 1) % 4;
                if (!m_nailForgingController.RequestQuarterTurn() &&
                    !m_nailForgingController.IsManifestLoaded())
                {
                    m_smithingNailAudio.onGripClosed();
                    m_smithingNailAudio.onRotationComplete();
                }
            }
            ImGui::Text(U8("Strana %d"), m_nailStrikeRotation + 1);
            ImGui::TextDisabled(U8("RMB v poli otočí"));

            const bool strikeKeyDown = IsNailStrikeKeyDown();
            const bool strikeByKeyboard = strikeKeyDown && !m_nailStrikeKeyWasDown && !io.WantTextInput;
            m_nailStrikeKeyWasDown = strikeKeyDown;
            if (stationReady && strikeByKeyboard)
                applyCurrentNailHammerStrike();

            if (ImGui::Button(U8("Úder"), largeButton))
                applyCurrentNailHammerStrike();

            const char* phaseButton = U8("Další krok");
            if (nail->phase == smithing::NailForgePhase::Heating) phaseButton = U8("Vytahovat dřík");
            else if (nail->phase == smithing::NailForgePhase::DrawingShank) phaseButton = U8("Začít špičku");
            else if (nail->phase == smithing::NailForgePhase::ForgingPoint) phaseButton = U8("Oddělit kus");
            else if (nail->phase == smithing::NailForgePhase::CuttingBlank) phaseButton = U8("Do otvoru");
            else if (nail->phase == smithing::NailForgePhase::FormingHead) phaseButton = U8("Dokončit hlavu");
            if (ImGui::Button(phaseButton, largeButton))
            {
                smithing::NailActionResult action = smithing::AdvancePhase(m_nailTrainingBatch, currentGameMinuteSerial());
                m_nailLastMessageKey = action.messageKey;
                if (action.evaluation)
                    applyNailEvaluationResult(*action.evaluation);
                if (action.changed)
                    syncActiveNailWorkpieceItem();
            }
            ImGui::EndDisabled();

            if (!stationReady)
                ImGui::TextWrapped("%s", MessageText(blockMessage));
            return;
        }

        if (m_nailTrainingStation == "quench")
        {
            std::string blockMessage;
            const bool stationReady = canUseCurrentSmithingStation(&blockMessage);
            const bool needsQuench = stationReady && nail->heat > 0.12f && !m_nailQuenchActive;

            ImGui::BeginDisabled(!needsQuench);
            if (ImGui::Button(U8("Ponořit"), largeButton))
                quenchActiveNailWorkpiece();
            ImGui::EndDisabled();

            if (!stationReady)
                ImGui::TextWrapped("%s", MessageText(blockMessage));
            else if (m_nailQuenchActive)
                ImGui::TextWrapped(U8("Hřebík chladne v kleštích ve vodě."));
            else if (!needsQuench)
                ImGui::TextWrapped("%s", MessageText("ready_for_testing"));
            return;
        }

        if (nail->phase == smithing::NailForgePhase::FormingHead)
        {
            if (ImGui::Button(U8("Prohlédnout"), largeButton))
            {
                smithing::NailActionResult action = smithing::AdvancePhase(m_nailTrainingBatch, currentGameMinuteSerial());
                m_nailLastMessageKey = action.messageKey;
            }
        }
        else if (nail->phase == smithing::NailForgePhase::Inspecting)
        {
            std::string blockMessage;
            const bool stationReady = canUseCurrentSmithingStation(&blockMessage);

            ImGui::BeginDisabled(!stationReady);
            if (ImGui::Button(U8("Předat"), largeButton))
            {
                smithing::NailActionResult action = smithing::FinishActiveNail(m_nailTrainingBatch, currentGameMinuteSerial());
                m_nailLastMessageKey = action.messageKey;
                if (action.evaluation)
                    applyNailEvaluationResult(*action.evaluation);
            }
            ImGui::EndDisabled();

            if (!stationReady)
                ImGui::TextWrapped("%s", MessageText(blockMessage));
        }
        else
        {
            ImGui::TextWrapped(U8("Ke kontrole se dostaneš až po vykování hlavy."));
        }
    };

    const float leftWidth = 230.0f;
    const float rightWidth = 360.0f;
    const float playWidth = std::max(520.0f, ImGui::GetContentRegionAvail().x - leftWidth - rightWidth - 28.0f);
    const float playHeight = std::max(430.0f, ImGui::GetContentRegionAvail().y - 8.0f);

    ImGui::BeginGroup();
    ImGui::BeginChild("##nail_steps_panel", ImVec2(leftWidth, playHeight), false);
    renderStationLine();
    ImGui::Separator();
    renderStationActions();
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::Text(U8("Hřebík %d z %d"), m_nailTrainingBatch.activeIndex + 1, m_nailTrainingBatch.targetCount);
    ImGui::SameLine();
    ImGui::TextDisabled(U8("přijato %d, vyřazeno %d"), m_nailTrainingBatch.acceptedCount, m_nailTrainingBatch.rejectedCount);
    ImGui::Text(U8("Fáze: %s"), PhaseText(nail->phase));

    ImGui::InvisibleButton("##nail_playfield", ImVec2(playWidth, playHeight));
    const bool playfieldHovered = ImGui::IsItemHovered();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 playMin = ImGui::GetItemRectMin();
    const ImVec2 playMax = ImGui::GetItemRectMax();
    drawList->AddRectFilled(playMin, playMax, IM_COL32(38, 30, 24, 255));

    const float stationT = SmoothStep01(m_nailStationTransition);
    if (m_nailStationTransition < 1.0f && !m_nailPreviousVisualStation.empty())
    {
        DrawStationBackground(
            drawList,
            m_smithingAssets.forgeBackground,
            m_smithingAssets.anvilBackground,
            m_smithingAssets.quenchBackground,
            m_smithingAssets.testingBackground,
            m_nailPreviousVisualStation,
            playMin,
            playMax,
            1.0f - stationT);
    }
    DrawStationBackground(
        drawList,
        m_smithingAssets.forgeBackground,
        m_smithingAssets.anvilBackground,
        m_smithingAssets.quenchBackground,
        m_smithingAssets.testingBackground,
        m_nailVisualStation,
        playMin,
        playMax,
        stationT);
    if (m_nailVisualStation == "forge")
        DrawForgeHeatAnimation(drawList, playMin, playMax, nail->heat, stationT);

    const ImVec2 center((playMin.x + playMax.x) * 0.5f, (playMin.y + playMax.y) * 0.52f);
    const float pieceSize = std::min(playWidth, playHeight) * 0.72f;
    const ImVec2 forgeZoneMin(playMin.x + playWidth * 0.25f, playMin.y + playHeight * 0.48f);
    const ImVec2 forgeZoneMax(playMin.x + playWidth * 0.73f, playMin.y + playHeight * 0.67f);
    if (m_nailTrainingStation == "forge")
    {
        drawList->AddRectFilled(forgeZoneMin, forgeZoneMax, IM_COL32(255, 96, 26, 28), 8.0f);
        drawList->AddRect(forgeZoneMin, forgeZoneMax, IM_COL32(255, 150, 48, 130), 8.0f, 0, 1.5f);
    }
    const bool headingOverAnvilHole =
        m_nailTrainingStation == "anvil" &&
        nail->phase == smithing::NailForgePhase::FormingHead;
    if (headingOverAnvilHole)
        m_nailStrikePosition = SuggestedStrikePosition(*nail);

    ImVec2 workpieceCenter = center;
    if (headingOverAnvilHole)
    {
        workpieceCenter = ImVec2(playMin.x + playWidth * 0.50f, playMin.y + playHeight * 0.64f);
    }
    else if (m_nailTrainingStation == "forge")
    {
        if (m_nailForgeInserted)
        {
            workpieceCenter = ImVec2(
                std::lerp(forgeZoneMin.x, forgeZoneMax.x, std::clamp(m_nailForgeZoneX, 0.0f, 1.0f)),
                std::lerp(forgeZoneMin.y, forgeZoneMax.y, std::clamp(m_nailForgeZoneY, 0.0f, 1.0f)));
        }
        else
        {
            workpieceCenter = ImVec2(playMin.x + playWidth * 0.38f, playMin.y + playHeight * 0.44f);
        }
    }
    const float workpieceSize = headingOverAnvilHole ? pieceSize * 0.62f : pieceSize;
    const float workpieceRotation = headingOverAnvilHole ? 1.57079632679f : 0.0f;
    const ImVec2 headingHoleCenter(
        workpieceCenter.x,
        workpieceCenter.y - workpieceSize * 0.35f);

    const float pieceMinX = center.x - pieceSize * 0.5f;
    const float pieceMaxX = center.x + pieceSize * 0.5f;

    if (m_nailTrainingStation == "anvil" && !headingOverAnvilHole && playfieldHovered && pieceMaxX > pieceMinX)
    {
        m_nailStrikePosition = std::clamp(
            (io.MousePos.x - pieceMinX) / (pieceMaxX - pieceMinX),
            0.0f,
            1.0f);
    }

    std::string playfieldBlockMessage;
    const bool playfieldAnvilReady =
        m_nailTrainingStation == "anvil" &&
        canUseCurrentSmithingStation(&playfieldBlockMessage) &&
        !m_pendingNailHammerStrike.has_value() &&
        !m_nailForgingController.State().inputLocked;
    const bool playfieldForgeReady =
        m_nailTrainingStation == "forge" &&
        canUseCurrentSmithingStation(&playfieldBlockMessage);

    if (playfieldForgeReady && m_nailForgeInserted && playfieldHovered)
    {
        const bool inForgeZone =
            io.MousePos.x >= forgeZoneMin.x && io.MousePos.x <= forgeZoneMax.x &&
            io.MousePos.y >= forgeZoneMin.y && io.MousePos.y <= forgeZoneMax.y;
        if (inForgeZone && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_nailForgeDragging = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_nailForgeDragging = false;
        if (m_nailForgeDragging)
        {
            m_nailForgeZoneX = std::clamp((io.MousePos.x - forgeZoneMin.x) / (forgeZoneMax.x - forgeZoneMin.x), 0.0f, 1.0f);
            m_nailForgeZoneY = std::clamp((io.MousePos.y - forgeZoneMin.y) / (forgeZoneMax.y - forgeZoneMin.y), 0.0f, 1.0f);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            m_nailForgingController.SyncVisualState(
                NailAnimationPhaseFor(nail->phase),
                WorkpieceAnimationFrameIndex(*nail),
                nail->heat);
            m_nailForgingController.SetStation(smithing::SmithingStation::Forge);
            const smithing::NailActionResult action = smithing::HeatNail(m_nailTrainingBatch, currentGameMinuteSerial(), false, true);
            m_nailLastMessageKey = action.messageKey;
            if (action.changed)
            {
                syncActiveNailWorkpieceItem();
                m_smithingNailAudio.onForgeTongsWork();
            }
        }
    }

    if (playfieldAnvilReady && playfieldHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        m_nailStrikeRotation = (m_nailStrikeRotation + 1) % 4;
        if (!m_nailForgingController.RequestQuarterTurn() &&
            !m_nailForgingController.IsManifestLoaded())
        {
            m_smithingNailAudio.onGripClosed();
            m_smithingNailAudio.onRotationComplete();
        }
    }

    if (playfieldAnvilReady && playfieldHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        m_nailStrikePowerSelecting = true;
        m_nailStrikePowerPhase = 0.0f;
    }

    if (m_nailStrikePowerSelecting)
    {
        m_nailStrikePowerPhase += dt * 1.65f;
        while (m_nailStrikePowerPhase > 2.0f)
            m_nailStrikePowerPhase -= 2.0f;
        const float wave = 1.0f - std::abs(m_nailStrikePowerPhase - 1.0f);
        m_nailStrikeForce = 0.10f + wave * 0.90f;

        if (!playfieldAnvilReady || ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            const bool releaseStrike = playfieldAnvilReady && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            m_nailStrikePowerSelecting = false;
            if (releaseStrike)
                applyCurrentNailHammerStrike();
        }
    }

    const float toolFollow = 1.0f - std::exp(-dt * 16.0f);
    m_nailToolSmoothedPosition += (m_nailStrikePosition - m_nailToolSmoothedPosition) * toolFollow;

    if (m_nailTrainingStation == "anvil" && !headingOverAnvilHole)
    {
        for (int i = 1; i < 5; ++i)
        {
            const float x = pieceMinX + (pieceMaxX - pieceMinX) * static_cast<float>(i) / 5.0f;
            drawList->AddLine(ImVec2(x, center.y - pieceSize * 0.22f),
                              ImVec2(x, center.y + pieceSize * 0.22f),
                              IM_COL32(120, 150, 170, 85), 1.0f);
        }
    }

    if (headingOverAnvilHole)
    {
        const float holeW = std::max(18.0f, workpieceSize * 0.13f);
        const float holeH = std::max(10.0f, workpieceSize * 0.055f);
        const ImVec2 holeMin(headingHoleCenter.x - holeW * 0.5f, headingHoleCenter.y - holeH * 0.5f);
        const ImVec2 holeMax(headingHoleCenter.x + holeW * 0.5f, headingHoleCenter.y + holeH * 0.5f);
        drawList->AddRectFilled(holeMin, holeMax, IM_COL32(22, 18, 16, 210), 3.0f);
        drawList->AddRect(holeMin, holeMax, IM_COL32(210, 165, 90, 150), 3.0f, 0, 2.0f);
    }

    auto drawWorkpieceFrame = [&](int frameIndex, bool usesAnimationAtlas, float alpha)
    {
        SDL_Texture* atlas = usesAnimationAtlas
            ? m_smithingAssets.nailAnimationWorkpieceAtlas
            : m_smithingAssets.nailWorkpieceAtlas;
        if (!atlas)
            return;

        const int rows = usesAnimationAtlas ? 4 : 2;
        if (headingOverAnvilHole)
        {
            AddAtlasCellGridRotated(
                drawList,
                atlas,
                frameIndex,
                4,
                rows,
                workpieceCenter,
                workpieceSize,
                workpieceRotation,
                WithAlpha(HeatTint(nail->heat), alpha));
        }
        else
        {
            AddAtlasCellGrid(
                drawList,
                atlas,
                frameIndex,
                4,
                rows,
                workpieceCenter,
                workpieceSize,
                WithAlpha(HeatTint(nail->heat), alpha));
        }
    };

    const float workpieceT = SmoothStep01(m_nailWorkpieceTransition);
    const bool drawProceduralWorkpiece = true;
    if (drawProceduralWorkpiece)
    {
        DrawProceduralNailModel(
            drawList,
            *nail,
            workpieceCenter,
            workpieceSize,
            workpieceRotation,
            0.96f);
    }
    else
    {
        if (m_nailWorkpieceTransition < 1.0f && m_nailPreviousWorkpieceFrame >= 0)
            drawWorkpieceFrame(m_nailPreviousWorkpieceFrame, m_nailPreviousWorkpieceUsesAnimationAtlas, 1.0f - workpieceT);
        drawWorkpieceFrame(m_nailVisualWorkpieceFrame, m_nailVisualWorkpieceUsesAnimationAtlas, workpieceT);
    }

    const bool drawAnimatedHammer =
        m_smithingAssets.hammerStrikeAtlas && m_nailForgingController.HammerPlayer().IsPlaying();
    const bool drawAnimatedTongs =
        m_nailTrainingStation != "forge" &&
        m_smithingAssets.tongsRotateAtlas &&
        m_nailForgingController.TongsPlayer().IsPlaying();
    const ImVec2 toolImpactCenter = headingOverAnvilHole
        ? headingHoleCenter
        : ImVec2(center.x + (m_nailToolSmoothedPosition - 0.50f) * pieceSize * 0.62f, center.y);
    const ImVec2 effectImpactCenter = headingOverAnvilHole
        ? headingHoleCenter
        : ImVec2(center.x + (m_nailToolImpactPosition - 0.50f) * pieceSize * 0.62f, center.y);
    if (drawAnimatedTongs)
    {
        AddAtlasCellGrid(
            drawList,
            m_smithingAssets.tongsRotateAtlas,
            m_nailForgingController.TongsPlayer().FrameIndex(),
            4,
            2,
            workpieceCenter,
            headingOverAnvilHole ? pieceSize * 0.78f : pieceSize,
            IM_COL32(255, 255, 255, 230));
    }
    if (drawAnimatedHammer)
    {
        const ImVec2 hammerCenter = headingOverAnvilHole
            ? ImVec2(toolImpactCenter.x + workpieceSize * 0.06f, toolImpactCenter.y - workpieceSize * 0.14f)
            : toolImpactCenter;
        AddAtlasCellGrid(
            drawList,
            m_smithingAssets.hammerStrikeAtlas,
            m_nailForgingController.HammerPlayer().FrameIndex(),
            4,
            2,
            hammerCenter,
            headingOverAnvilHole ? pieceSize * 0.86f : pieceSize,
            IM_COL32(255, 255, 255, 240));
    }
    if (m_nailTrainingStation == "anvil" &&
        m_smithingAssets.handsToolsAtlas &&
        !drawAnimatedHammer &&
        !drawAnimatedTongs)
    {
        const int handFrame = m_nailTrainingStation == "anvil"
            ? (m_nailStrikeFlash > 0.45f ? 3 : 2)
            : 0;
        float handOffsetX = 0.0f;
        float handOffsetY = pieceSize * 0.08f;
        float handAlpha = 210.0f;
        if (m_nailTrainingStation == "anvil")
        {
            if (headingOverAnvilHole)
            {
                handOffsetX = workpieceSize * 0.08f;
                handOffsetY = -workpieceSize * 0.18f + (m_nailStrikeFlash > 0.45f ? workpieceSize * 0.05f : 0.0f);
            }
            else
            {
                handOffsetX = (m_nailToolSmoothedPosition - 0.50f) * pieceSize * 0.62f;
                handOffsetY += (m_nailStrikeFlash > 0.45f ? pieceSize * 0.07f : -pieceSize * 0.03f);
            }
            handAlpha = 230.0f;
        }
        AddAtlasCellOffset(
            drawList,
            m_smithingAssets.handsToolsAtlas,
            handFrame,
            headingOverAnvilHole ? headingHoleCenter : center,
            ImVec2(handOffsetX, handOffsetY),
            headingOverAnvilHole ? pieceSize * 0.78f : pieceSize,
            IM_COL32(255, 255, 255, static_cast<int>(handAlpha)));
    }
    if (m_smithingAssets.effectsAtlas)
    {
        if (m_nailTrainingStation == "forge")
            AddAtlasCell(drawList, m_smithingAssets.effectsAtlas, 4, center, pieceSize * 0.92f, IM_COL32(255, 185, 82, 105));
        if (m_smithingAssets.nailAnimationEffectsAtlas && !m_nailForgingController.Effects().empty())
        {
            const float effectOffsetX = (m_nailToolImpactPosition - 0.50f) * pieceSize * 0.62f;
            for (const smithing::ActiveEffect& effect : m_nailForgingController.Effects())
            {
                AddAtlasCellGrid(
                    drawList,
                    m_smithingAssets.nailAnimationEffectsAtlas,
                    effect.player.AtlasIndex(),
                    4,
                    4,
                    headingOverAnvilHole ? headingHoleCenter : ImVec2(center.x + effectOffsetX, center.y),
                    headingOverAnvilHole ? pieceSize * 0.50f : pieceSize * 0.78f,
                    IM_COL32(255, 255, 255, 220));
            }
        }
        else if (m_nailStrikeFlash > 0.0f)
        {
            AddAtlasCellOffset(
                drawList,
                m_smithingAssets.effectsAtlas,
                0,
                effectImpactCenter,
                ImVec2(0.0f, 0.0f),
                headingOverAnvilHole ? pieceSize * 0.50f : pieceSize * 0.78f,
                IM_COL32(255, 255, 255, static_cast<int>(m_nailStrikeFlash * 220.0f)));
        }
    }
    if (m_nailTrainingStation == "quench" && (m_nailQuenchActive || nail->heat > 0.12f))
    {
        const float heat = std::clamp(nail->heat, 0.0f, 1.0f);
        const float time = static_cast<float>(ImGui::GetTime());
        const ImVec2 waterCenter(workpieceCenter.x, workpieceCenter.y + workpieceSize * 0.10f);
        for (int i = 0; i < 4; ++i)
        {
            const float phase = std::fmod(time * 1.9f + static_cast<float>(i) * 0.23f, 1.0f);
            const float radius = workpieceSize * (0.12f + phase * 0.20f);
            const int alpha = static_cast<int>((1.0f - phase) * (90.0f + heat * 90.0f));
            drawList->AddEllipse(
                waterCenter,
                ImVec2(radius, radius * 0.28f),
                IM_COL32(165, 225, 235, std::clamp(alpha, 0, 185)),
                0.0f,
                32,
                2.0f);
        }
        for (int i = 0; i < 7; ++i)
        {
            const float x = waterCenter.x + (static_cast<float>(i) - 3.0f) * workpieceSize * 0.045f;
            const float lift = std::fmod(time * (0.55f + i * 0.07f) + i * 0.17f, 1.0f);
            const float y0 = waterCenter.y - lift * workpieceSize * 0.18f;
            const float y1 = y0 - workpieceSize * (0.10f + heat * 0.12f);
            const int alpha = static_cast<int>((1.0f - lift) * heat * 175.0f);
            drawList->AddBezierCubic(
                ImVec2(x, y0),
                ImVec2(x - workpieceSize * 0.03f, (y0 + y1) * 0.5f),
                ImVec2(x + workpieceSize * 0.03f, (y0 + y1) * 0.5f),
                ImVec2(x, y1),
                IM_COL32(225, 235, 230, std::clamp(alpha, 0, 180)),
                1.8f,
                12);
        }
    }

    if (m_nailTrainingStation == "anvil" && smithing::HintLevelForActiveNail(m_nailTrainingBatch) == smithing::NailHintLevel::ActiveGuidance)
    {
        if (headingOverAnvilHole)
        {
            drawList->AddCircle(headingHoleCenter, 17.0f, IM_COL32(255, 215, 110, 190), 24, 3.0f);
            drawList->AddCircleFilled(headingHoleCenter, 7.0f, IM_COL32(255, 215, 110, 210));
        }
        else
        {
            const float suggested = SuggestedStrikePosition(*nail);
            const float x = pieceMinX + suggested * (pieceMaxX - pieceMinX);
            drawList->AddLine(ImVec2(x, playMin.y + 28.0f), ImVec2(x, playMax.y - 28.0f), IM_COL32(255, 215, 110, 170), 3.0f);
            drawList->AddCircleFilled(ImVec2(x, center.y), 9.0f, IM_COL32(255, 215, 110, 210));
        }
    }

    if (m_nailTrainingStation == "anvil")
    {
        if (headingOverAnvilHole)
        {
            drawList->AddCircle(headingHoleCenter, 23.0f, IM_COL32(110, 205, 255, 230), 24, 2.0f);
            drawList->AddTriangleFilled(ImVec2(headingHoleCenter.x, headingHoleCenter.y - 34.0f),
                                        ImVec2(headingHoleCenter.x - 8.0f, headingHoleCenter.y - 20.0f),
                                        ImVec2(headingHoleCenter.x + 8.0f, headingHoleCenter.y - 20.0f),
                                        IM_COL32(110, 205, 255, 230));
        }
        else
        {
            const float strikeX = pieceMinX + m_nailStrikePosition * (pieceMaxX - pieceMinX);
            drawList->AddLine(ImVec2(strikeX, playMin.y + 24.0f),
                              ImVec2(strikeX, playMax.y - 24.0f),
                              IM_COL32(110, 205, 255, 230), 2.0f);
            drawList->AddTriangleFilled(ImVec2(strikeX, playMin.y + 24.0f),
                                        ImVec2(strikeX - 7.0f, playMin.y + 36.0f),
                                        ImVec2(strikeX + 7.0f, playMin.y + 36.0f),
                                        IM_COL32(110, 205, 255, 230));
        }

        const float meterWidth = std::min(300.0f, playWidth * 0.42f);
        const float meterHeight = 16.0f;
        const ImVec2 meterMin(playMax.x - meterWidth - 22.0f, playMax.y - 38.0f);
        const ImVec2 meterMax(meterMin.x + meterWidth, meterMin.y + meterHeight);
        drawList->AddRectFilled(meterMin, meterMax, IM_COL32(24, 20, 18, 210), 4.0f);
        const float fillX = meterMin.x + (meterMax.x - meterMin.x) * std::clamp(m_nailStrikeForce, 0.0f, 1.0f);
        drawList->AddRectFilled(meterMin, ImVec2(fillX, meterMax.y), IM_COL32(235, 154, 64, 235), 4.0f);
        drawList->AddRect(meterMin, meterMax, IM_COL32(255, 230, 180, m_nailStrikePowerSelecting ? 245 : 150), 4.0f, 0, 1.5f);
        drawList->AddLine(ImVec2(fillX, meterMin.y - 4.0f), ImVec2(fillX, meterMax.y + 4.0f), IM_COL32(255, 245, 215, 245), 2.0f);
        drawList->AddText(ImVec2(meterMin.x, meterMin.y - 22.0f),
                          IM_COL32(245, 232, 205, 220),
                          U8("LMB držet: síla, pustit: úder"));
    }
    drawList->AddRect(playMin, playMax, IM_COL32(170, 132, 76, 220), 0.0f, 0, 2.0f);

    if (!benesGuidance.empty())
    {
        const float hintWidth = std::min(playWidth - 24.0f, 700.0f);
        const float hintHeight = 76.0f;
        const ImVec2 hintMin(playMin.x + 12.0f, playMin.y + 12.0f);
        const ImVec2 hintMax(hintMin.x + hintWidth, hintMin.y + hintHeight);
        drawList->AddRectFilled(hintMin, hintMax, IM_COL32(23, 18, 12, 226), 5.0f);
        drawList->AddRect(hintMin, hintMax, IM_COL32(238, 185, 82, 230), 5.0f, 0, 2.0f);
        drawList->AddRectFilled(
            ImVec2(hintMin.x, hintMin.y),
            ImVec2(hintMin.x + 5.0f, hintMax.y),
            IM_COL32(238, 185, 82, 255),
            5.0f);
        AddWrappedDrawText(
            drawList,
            ImVec2(hintMin.x + 16.0f, hintMin.y + 12.0f),
            hintWidth - 28.0f,
            benesGuidance,
            IM_COL32(255, 244, 214, 255));
    }

    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextUnformatted(U8("Pozorování"));
    ImGui::Separator();
    ImGui::BulletText("%s", HeatText(nail->heat));
    ImGui::BulletText("%s", StraightnessText(nail->straightness));
    ImGui::BulletText("%s", PointText(nail->pointSharpness, nail->pointAlignment));
    ImGui::BulletText("%s", HeadText(nail->headSize, nail->headAlignment));
    ImGui::BulletText("%s", IntegrityText(*nail));

    if (!nail->defects.empty())
    {
        ImGui::Spacing();
        ImGui::TextUnformatted(U8("Viditelné vady"));
        for (const smithing::NailDefect& defect : nail->defects)
        {
            if (const smithing::NailDefectInfo* info = smithing::FindNailDefectInfo(defect.id))
                ImGui::BulletText("%s (%s)", info->nameCs, info->technicalId);
            else
                ImGui::BulletText("%s", smithing::ToString(defect.id));
        }
    }

    ImGui::Spacing();
    if (!benesGuidance.empty())
        ImGui::TextWrapped("%s", benesGuidance.c_str());

    if (m_hasLastNailEvaluation)
    {
        ImGui::Spacing();
        ImGui::Text(U8("Poslední kus: %s"), GradeText(m_lastNailEvaluation.grade));
        ImGui::TextWrapped("%s", BenesFeedbackText(m_lastNailEvaluation));
    }

    if (!m_nailLastMessageKey.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", MessageText(m_nailLastMessageKey));
    }
    ImGui::EndGroup();

    ImGui::End();
    renderNailTrainingDebugPanel();
}
