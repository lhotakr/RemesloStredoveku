#include "Campaign.h"

#include <SDL_image.h>
#include <algorithm>
#include <cmath>

float Campaign::computeSkyDarkness() const
{
    const auto& now = m_gameTime.now();
    const auto sun = m_sunCycle.getDayInfo(now.day, now.month, now.hour, now.minute);
    const auto moon = m_moonCycle.getMoonInfo(now.day, now.month, now.year);

    const int nowMin = now.hour * 60 + now.minute;
    const int sunrise = sun.sunriseMinutes;
    const int sunset = sun.sunsetMinutes;

    const int twilight = 45;

    float daylight = 0.0f;

    if (nowMin >= sunrise && nowMin < sunset) {
        daylight = 1.0f;
    }

    if (nowMin >= sunrise - twilight && nowMin < sunrise) {
        float t = float(nowMin - (sunrise - twilight)) / float(twilight);
        daylight = std::max(daylight, t);
    }

    if (nowMin >= sunset && nowMin < sunset + twilight) {
        float t = 1.0f - float(nowMin - sunset) / float(twilight);
        daylight = std::max(0.0f, t);
    }

    daylight = std::clamp(daylight, 0.0f, 1.0f);

    float darkness = 1.0f - daylight;

    if (darkness > 0.01f) {
        darkness *= (1.0f - moon.brightness * 0.35f);
    }

    return std::clamp(darkness, 0.0f, 1.0f);
}

void Campaign::updateSkyOverlay(float dt)
{
    m_cloudTime += dt;

    const float driftX = std::sin(m_cloudTime * 0.11f) * 1.2f;
    const float driftY = std::cos(m_cloudTime * 0.07f) * 0.6f;

    m_skyOverlayScrollX += (m_cloudBaseSpeedX + driftX) * dt;
    m_skyOverlayScrollY += (m_cloudBaseSpeedY + driftY) * dt;
}

void Campaign::renderSkyOverlay()
{
    if (!m_skyOverlay)
        return;

    int screenW = 0, screenH = 0;
    SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);

    int texW = 0, texH = 0;
    SDL_QueryTexture(m_skyOverlay, nullptr, nullptr, &texW, &texH);
    if (texW <= 0 || texH <= 0)
        return;

    const float darkness = computeSkyDarkness();
    if (darkness <= 0.01f)
        return;

    const Uint8 alpha = (Uint8)std::clamp(int(darkness * 180.0f), 0, 220);

    SDL_SetTextureBlendMode(m_skyOverlay, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(m_skyOverlay, alpha);

    const int worldOffsetX = (int)std::floor(m_skyOverlayScrollX);
    const int worldOffsetY = (int)std::floor(m_skyOverlayScrollY);

    const int ox = ((m_camX + worldOffsetX) % texW + texW) % texW;
    const int oy = ((m_camY + worldOffsetY) % texH + texH) % texH;

    for (int y = -oy; y < screenH; y += texH)
    {
        for (int x = -ox; x < screenW; x += texW)
        {
            SDL_Rect dst{ x, y, texW, texH };
            SDL_RenderCopy(m_renderer, m_skyOverlay, nullptr, &dst);
        }
    }

    // globální ztmavení teï øeší hlavnì renderLightMask(),
    // takže sem už další èerný fill nedáváme
}

void Campaign::ensureLightMask(int screenW, int screenH)
{
    if (m_lightMask)
    {
        int w = 0, h = 0;
        SDL_QueryTexture(m_lightMask, nullptr, nullptr, &w, &h);
        if (w == screenW && h == screenH)
            return;

        SDL_DestroyTexture(m_lightMask);
        m_lightMask = nullptr;
    }

    m_lightMask = SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        screenW,
        screenH
    );

    if (m_lightMask) {
        SDL_SetTextureBlendMode(m_lightMask, SDL_BLENDMODE_BLEND);
    }
}

void Campaign::drawLightOnMask(int screenX, int screenY, float radiusPx, Uint8 intensity)
{
    if (!m_lightSoftTex)
        return;

    SDL_SetTextureBlendMode(m_lightSoftTex, SDL_BLENDMODE_ADD);
    SDL_SetTextureAlphaMod(m_lightSoftTex, intensity);
    SDL_SetTextureColorMod(m_lightSoftTex, 255, 255, 255);

    SDL_Rect dst;
    dst.w = (int)std::lround(radiusPx * 2.0f);
    dst.h = (int)std::lround(radiusPx * 2.0f);
    dst.x = screenX - dst.w / 2;
    dst.y = screenY - dst.h / 2;

    SDL_RenderCopy(m_renderer, m_lightSoftTex, nullptr, &dst);

    SDL_SetTextureAlphaMod(m_lightSoftTex, 255);
    SDL_SetTextureColorMod(m_lightSoftTex, 255, 255, 255);
}

void Campaign::renderLightMask(int screenW, int screenH, float darkness)
{
    if (!m_lightMask)
        return;

    SDL_SetRenderTarget(m_renderer, m_lightMask);

    const Uint8 ambient = (Uint8)std::clamp(
        (int)(255.0f - darkness * 235.0f),
        18,
        255
    );

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(m_renderer, ambient, ambient, ambient, 255);
    SDL_RenderClear(m_renderer);

    if (darkness > 0.01f)
    {
        const int playerScreenX = (int)std::lround(m_player.x) - m_camX;
        const int playerScreenY = (int)std::lround(m_player.y) - m_camY - m_tileSize / 2;

        const float radiusPx = 13.5f * (float)m_tileSize;

        const Uint8 intensity = (Uint8)std::clamp(
            (int)(darkness * 230.0f),
            0,
            255
        );

        drawLightOnMask(playerScreenX, playerScreenY, radiusPx, intensity);
    }

    SDL_SetRenderTarget(m_renderer, nullptr);

    SDL_SetTextureBlendMode(m_lightMask, SDL_BLENDMODE_MOD);
    SDL_RenderCopy(m_renderer, m_lightMask, nullptr, nullptr);
}