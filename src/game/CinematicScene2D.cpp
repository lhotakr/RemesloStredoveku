#include "CinematicScene.h"
#include "PathUtils.h"
#include "imgui.h"
#include <SDL_image.h>
#include <algorithm>
#include <cstddef>
#include <cfloat>
#include <cmath>

namespace
{
std::string ImagePath(const std::string& path)
{
    const std::filesystem::path file(path);
    return (file.is_absolute() ? file : pathutils::ProjectRoot() / file).string();
}
ImFont* SceneFont()
{
    if (!ImGui::GetCurrentContext()) return nullptr;
    auto* atlas = ImGui::GetIO().Fonts;
    if (!atlas->IsBuilt()) atlas->Build();
    return atlas->Fonts.empty() ? nullptr : atlas->Fonts[0];
}
}

CinematicScene::~CinematicScene() { clearImageScene(); }

void CinematicScene::clearImageScene()
{
    SDL_DestroyTexture(m_backgroundImage); m_backgroundImage = nullptr;
    SDL_DestroyTexture(m_fontAtlas); m_fontAtlas = nullptr;
    for (auto& [id, image] : m_imageTextures) SDL_DestroyTexture(image.texture);
    m_imageTextures.clear();
    m_imageScene = false;
}

bool CinematicScene::loadImageScene(const nlohmann::json& root, const std::function<void(const std::string&)>& log)
{
    const auto background = root.value("background_image", std::string());
    int width = 1672, height = 941;
    if (!background.empty())
    {
        m_backgroundImage = IMG_LoadTexture(m_renderer, ImagePath(background).c_str());
        if (!m_backgroundImage)
        {
            if (log) log("Cannot load 2D scene background: " + background + " | " + IMG_GetError());
            return false;
        }
        SDL_QueryTexture(m_backgroundImage, nullptr, nullptr, &width, &height);
        SDL_SetTextureScaleMode(m_backgroundImage, SDL_ScaleModeNearest);
    }
    const auto canvas = root.value("canvas", nlohmann::json::object());
    m_canvasWidth = std::clamp(canvas.value("width", float(width)), 1.0f, 32768.0f);
    m_canvasHeight = std::clamp(canvas.value("height", float(height)), 1.0f, 32768.0f);
    return true;
}

SDL_FRect CinematicScene::imageViewport(float width, float height) const
{
    const float scale = std::min(width / m_canvasWidth, height / m_canvasHeight);
    return {(width - m_canvasWidth * scale) * .5f, (height - m_canvasHeight * scale) * .5f,
            m_canvasWidth * scale, m_canvasHeight * scale};
}

bool CinematicScene::prepareImageEntity(Entity& entity, const std::function<void(const std::string&)>& log)
{
    if (auto found = m_imageTextures.find(entity.id); found != m_imageTextures.end())
    {
        SDL_DestroyTexture(found->second.texture);
        m_imageTextures.erase(found);
    }
    if (entity.textLayer) return true;
    ImageTexture image;
    std::vector<std::uint32_t> pixels;
    std::string animation;
    if (!entity.characterId.empty() && loadCharacterFramePixels(entity, pixels, image.width, image.height, animation, log))
    {
        entity.animation = animation;
        image.texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
        if (image.texture) SDL_UpdateTexture(image.texture, nullptr, pixels.data(), image.width * sizeof(std::uint32_t));
    }
    else if (!entity.texture.empty())
    {
        image.texture = IMG_LoadTexture(m_renderer, ImagePath(entity.texture).c_str());
        if (image.texture) SDL_QueryTexture(image.texture, nullptr, nullptr, &image.width, &image.height);
    }
    if (!image.texture)
    {
        if (log) log("Cannot load 2D scene layer '" + entity.id + "': " + entity.texture);
        return false;
    }
    SDL_SetTextureBlendMode(image.texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(image.texture, SDL_ScaleModeNearest);
    m_imageTextures[entity.id] = image;
    return true;
}

std::vector<std::string> CinematicScene::imageLayerOrder() const
{
    std::vector<std::string> ids;
    for (const auto& [id, entity] : m_entities) ids.push_back(id);
    std::sort(ids.begin(), ids.end(), [&](const auto& a, const auto& b) {
        const auto& x = m_entities.at(a); const auto& y = m_entities.at(b);
        return x.layer == y.layer ? a < b : x.layer < y.layer;
    });
    return ids;
}

bool CinematicScene::imageEntityBounds(const std::string& id, SDL_FRect& bounds) const
{
    const auto found = m_entities.find(id);
    if (found == m_entities.end()) return false;
    const auto& e = found->second;
    float width = e.width, height = e.height;
    if (e.textLayer)
    {
        if (auto* font = SceneFont())
        {
            const auto size = font->CalcTextSizeA(e.fontSize, FLT_MAX, width, e.text.c_str());
            if (width <= 0) width = size.x;
            if (height <= 0) height = size.y;
        }
    }
    else if (auto image = m_imageTextures.find(id); image != m_imageTextures.end())
    {
        if (width <= 0 && height <= 0) { width = image->second.width; height = image->second.height; }
        else if (width <= 0) width = height * image->second.width / image->second.height;
        else if (height <= 0) height = width * image->second.height / image->second.width;
    }
    bounds = {float(e.x), float(e.y), std::max(1.f, width * float(e.scale)), std::max(1.f, height * float(e.scale))};
    return true;
}

void CinematicScene::renderImageScene()
{
    if (!m_renderer) return;
    SDL_Rect viewport; SDL_RenderGetViewport(m_renderer, &viewport);
    const auto fitted = imageViewport(float(viewport.w), float(viewport.h));
    const float scale = fitted.w / m_canvasWidth;
    Uint8 r,g,b,a; SDL_GetRenderDrawColor(m_renderer, &r, &g, &b, &a);
    SDL_BlendMode blend; SDL_GetRenderDrawBlendMode(m_renderer, &blend);
    SDL_Rect oldClip; SDL_RenderGetClipRect(m_renderer, &oldClip);
    const bool hadClip = SDL_RenderIsClipEnabled(m_renderer);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(m_renderer, nullptr);
    if (m_backgroundImage) SDL_RenderCopyF(m_renderer, m_backgroundImage, nullptr, &fitted);
    SDL_Rect clip{int(std::ceil(fitted.x)), int(std::ceil(fitted.y)), int(fitted.w), int(fitted.h)};
    if (hadClip) SDL_IntersectRect(&clip, &oldClip, &clip);
    SDL_RenderSetClipRect(m_renderer, &clip);
    for (const auto& id : imageLayerOrder())
    {
        const auto& entity = m_entities.at(id);
        if (!entity.visible || entity.opacity <= 0) continue;
        SDL_FRect bounds; imageEntityBounds(id, bounds);
        const SDL_FRect dest{fitted.x + bounds.x * scale, fitted.y + bounds.y * scale, bounds.w * scale, bounds.h * scale};
        if (!entity.textLayer)
        {
            const auto image = m_imageTextures.find(id);
            if (image == m_imageTextures.end()) continue;
            SDL_SetTextureAlphaMod(image->second.texture, Uint8(entity.opacity * 255));
            SDL_RenderCopyExF(m_renderer, image->second.texture, nullptr, &dest, entity.yawDegrees, nullptr, SDL_FLIP_NONE);
            continue;
        }
        auto* font = SceneFont();
        if (!font || entity.text.empty()) continue;
        ImDrawList draw(ImGui::GetDrawListSharedData());
        draw._ResetForNewFrame();
        draw.PushClipRect({fitted.x, fitted.y}, {fitted.x + fitted.w, fitted.y + fitted.h});
        draw.PushTextureID(font->OwnerAtlas->TexRef);
        const auto color = entity.color;
        draw.AddText(font, entity.fontSize * float(entity.scale) * scale, {dest.x, dest.y},
            IM_COL32(color.r, color.g, color.b, Uint8(color.a * entity.opacity)), entity.text.c_str(), nullptr,
            entity.width > 0 ? dest.w : 0);
        for (const auto& cmd : draw.CmdBuffer)
        {
            if (cmd.ElemCount == 0) continue;
            // Text can add glyphs or grow ImGui's atlas during this draw. Upload
            // the texture referenced by each command after generating its geometry.
            const auto* atlas = cmd.TexRef._TexData;
            if (!atlas || !atlas->Pixels) continue;
            bool created = false;
            if (!m_fontAtlas || m_fontAtlasId != atlas->UniqueID || m_fontAtlasWidth != atlas->Width || m_fontAtlasHeight != atlas->Height)
            {
                SDL_DestroyTexture(m_fontAtlas);
                m_fontAtlas = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, atlas->Width, atlas->Height);
                m_fontAtlasId = atlas->UniqueID; m_fontAtlasWidth = atlas->Width; m_fontAtlasHeight = atlas->Height;
                if (!m_fontAtlas) continue;
                SDL_SetTextureBlendMode(m_fontAtlas, SDL_BLENDMODE_BLEND);
                created = true;
            }
            if (created || atlas->Status != ImTextureStatus_OK)
            {
                if (atlas->Format == ImTextureFormat_RGBA32)
                    SDL_UpdateTexture(m_fontAtlas, nullptr, atlas->Pixels, atlas->Width * 4);
                else
                {
                    std::vector<Uint32> rgba(atlas->Width * atlas->Height);
                    for (std::size_t i = 0; i < rgba.size(); ++i) rgba[i] = (Uint32(atlas->Pixels[i]) << 24) | 0x00ffffff;
                    SDL_UpdateTexture(m_fontAtlas, nullptr, rgba.data(), atlas->Width * 4);
                }
            }
            const auto* vertices = draw.VtxBuffer.Data + cmd.VtxOffset;
            SDL_RenderGeometryRaw(m_renderer, m_fontAtlas, &vertices->pos.x, sizeof(ImDrawVert),
                reinterpret_cast<const SDL_Color*>(&vertices->col), sizeof(ImDrawVert),
                &vertices->uv.x, sizeof(ImDrawVert), draw.VtxBuffer.Size - int(cmd.VtxOffset),
                draw.IdxBuffer.Data + cmd.IdxOffset, int(cmd.ElemCount), sizeof(ImDrawIdx));
        }
    }
    SDL_RenderSetClipRect(m_renderer, hadClip ? &oldClip : nullptr);
    SDL_SetRenderDrawBlendMode(m_renderer, blend);
    SDL_SetRenderDrawColor(m_renderer, r,g,b,a);
}
