#include "SmithingNailAudioController.h"

#include "../audio/AudioManager.h"
#include <SDL.h>

#include <algorithm>

namespace
{
constexpr std::array<const char*, 4> HotLight{
    "nail_impact_hot_light_01", "nail_impact_hot_light_02",
    "nail_impact_hot_light_03", "nail_impact_hot_light_04"
};

constexpr std::array<const char*, 3> HotHeavy{
    "nail_impact_hot_heavy_01", "nail_impact_hot_heavy_02",
    "nail_impact_hot_heavy_03"
};

constexpr std::array<const char*, 2> Cooling{
    "nail_impact_cooling_01", "nail_impact_cooling_02"
};

constexpr std::array<const char*, 2> Grip{
    "tongs_grip_01", "tongs_grip_02"
};

constexpr std::array<const char*, 2> Rotate{
    "tongs_rotate_quarter_01", "tongs_rotate_quarter_02"
};
}

SmithingNailAudioController::SmithingNailAudioController(AudioManager& audio)
    : m_audio(audio)
{
}

bool SmithingNailAudioController::load(const std::string& basePath)
{
    bool ok = true;
    const auto load = [&](const char* id)
    {
        ok = m_audio.loadSfx(id, basePath + "/" + id + ".ogg") && ok;
    };

    for (const char* id : HotLight) load(id);
    for (const char* id : HotHeavy) load(id);
    for (const char* id : Cooling) load(id);
    for (const char* id : Grip) load(id);
    for (const char* id : Rotate) load(id);

    load("tongs_settle_01");
    load("scale_flakes_01");
    load("quench_short_01");
    load("quench_heavy_01");
    load("bellows_push_01");
    load("forge_fire_loop_01");
    load("smithy_room_loop_01");
    return ok;
}

void SmithingNailAudioController::onHammerImpact(
    float temperature01,
    float strikePower01,
    float scaleAmount01)
{
    const std::uint32_t now = SDL_GetTicks();
    if (now - m_lastImpactTicks < 65u)
        return;
    m_lastImpactTicks = now;

    temperature01 = std::clamp(temperature01, 0.0f, 1.0f);
    strikePower01 = std::clamp(strikePower01, 0.0f, 1.0f);

    if (temperature01 < 0.45f)
        m_audio.playSfx(choose(Cooling));
    else if (strikePower01 >= 0.68f)
        m_audio.playSfx(choose(HotHeavy));
    else
        m_audio.playSfx(choose(HotLight));

    if (scaleAmount01 >= 0.35f)
        m_audio.playSfx("scale_flakes_01");
}

void SmithingNailAudioController::onGripClosed()
{
    m_audio.playSfx(choose(Grip));
    m_audio.playSfx(choose(Rotate));
}

void SmithingNailAudioController::onRotationComplete()
{
    m_audio.playSfx("tongs_settle_01");
}

void SmithingNailAudioController::onWaterContact(float temperature01)
{
    if (m_quenchSoundPlayed || temperature01 < 0.35f)
        return;

    m_audio.playSfx(temperature01 >= 0.72f ? "quench_heavy_01" : "quench_short_01");
    m_quenchSoundPlayed = true;
}

void SmithingNailAudioController::onBellowsPush()
{
    m_audio.playSfx("bellows_push_01");
}

void SmithingNailAudioController::resetQuench()
{
    m_quenchSoundPlayed = false;
}
