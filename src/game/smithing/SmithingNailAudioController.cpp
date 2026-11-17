#include "SmithingNailAudioController.h"

#include "PathUtils.h"
#include "audio/AudioManager.h"

#include <SDL.h>

#include <algorithm>
#include <filesystem>

namespace smithing
{
namespace fs = std::filesystem;
namespace
{
constexpr int kForgeFireChannel = 28;
constexpr int kRoomToneChannel = 29;

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

constexpr std::array<const char*, 6> OptionalTongsRecordings{
    "tongs_forge_place_01",
    "tongs_forge_rotate_01",
    "tongs_forge_settle_01",
    "tongs_quench_lower_01",
    "tongs_quench_lift_01",
    "tongs_quench_settle_01"
};

constexpr std::array<const char*, 12> OptionalMentorRecordings{
    "benes_hint_rotate_shank_01",
    "benes_hint_rotate_point_01",
    "benes_hint_rotate_head_01",
    "benes_hint_heat_start_01",
    "benes_hint_heat_more_01",
    "benes_hint_heat_ready_01",
    "benes_hint_anvil_drawing_01",
    "benes_hint_anvil_point_01",
    "benes_hint_anvil_head_01",
    "benes_hint_quench_01",
    "benes_hint_testing_01",
    "benes_hint_overheat_01"
};

std::string ResolveOptionalAudioPath(const std::string& relativePath)
{
    if (relativePath.empty())
        return relativePath;

    const fs::path rawPath(relativePath);
    if (rawPath.is_absolute() || fs::exists(rawPath))
        return relativePath;

    const fs::path projectPath = pathutils::ProjectRoot() / rawPath;
    if (fs::exists(projectPath))
        return projectPath.string();

    return std::string();
}
}

SmithingNailAudioController::SmithingNailAudioController(AudioManager& audio)
    : m_audio(audio)
{
}

bool SmithingNailAudioController::load(const std::string& basePath)
{
    if (m_loadAttempted)
        return m_loaded;
    m_loadAttempted = true;

    bool ok = true;
    const auto loadOne = [&](const char* id)
    {
        ok = m_audio.loadSfx(id, basePath + "/" + id + ".ogg") && ok;
    };
    const auto loadOptional = [&](const char* id)
    {
        const std::string path = ResolveOptionalAudioPath(basePath + "/" + id + ".ogg");
        if (!path.empty() && m_audio.loadSfx(id, path))
            m_loadedOptional.insert(id);
    };

    for (const char* id : HotLight) loadOne(id);
    for (const char* id : HotHeavy) loadOne(id);
    for (const char* id : Cooling) loadOne(id);
    for (const char* id : Grip) loadOne(id);
    for (const char* id : Rotate) loadOne(id);
    for (const char* id : OptionalTongsRecordings) loadOptional(id);
    for (const char* id : OptionalMentorRecordings) loadOptional(id);

    loadOne("tongs_settle_01");
    loadOne("scale_flakes_01");
    loadOne("quench_short_01");
    loadOne("quench_heavy_01");
    loadOne("bellows_push_01");
    loadOne("smithy_room_loop_01");

    m_loaded = ok;
    return ok;
}

void SmithingNailAudioController::updateAmbience(bool active, bool forgeFocused)
{
    (void)forgeFocused;

    if (!m_loaded || !active)
    {
        stopAmbience();
        return;
    }

    startAmbienceLoop("smithy_room_loop_01", kRoomToneChannel, 16, m_roomTonePlaying);
    if (m_forgeFirePlaying)
    {
        m_audio.stopSfxChannel(kForgeFireChannel);
        m_forgeFirePlaying = false;
    }
}

void SmithingNailAudioController::stopAmbience()
{
    if (m_roomTonePlaying)
    {
        m_audio.stopSfxChannel(kRoomToneChannel);
        m_roomTonePlaying = false;
    }

    if (m_forgeFirePlaying)
    {
        m_audio.stopSfxChannel(kForgeFireChannel);
        m_forgeFirePlaying = false;
    }
}

void SmithingNailAudioController::onHammerImpact(
    float temperature01,
    float strikePower01,
    float scaleAmount01)
{
    if (!m_loaded)
        return;

    const std::uint32_t now = SDL_GetTicks();
    if (now - m_lastImpactTicks < 65u)
        return;
    m_lastImpactTicks = now;

    temperature01 = std::clamp(temperature01, 0.0f, 1.0f);
    strikePower01 = std::clamp(strikePower01, 0.0f, 1.0f);
    scaleAmount01 = std::clamp(scaleAmount01, 0.0f, 1.0f);

    if (temperature01 < 0.45f)
        m_audio.playSfx(choose(Cooling), 0, -1, 104);
    else if (strikePower01 >= 0.68f)
        m_audio.playSfx(choose(HotHeavy), 0, -1, 118);
    else
        m_audio.playSfx(choose(HotLight), 0, -1, 108);

    if (scaleAmount01 >= 0.35f)
        m_audio.playSfx("scale_flakes_01", 0, -1, 72);
}

void SmithingNailAudioController::onGripClosed()
{
    if (!m_loaded)
        return;

    m_audio.playSfx(choose(Grip), 0, -1, 76);
    m_audio.playSfx(choose(Rotate), 0, -1, 66);
}

void SmithingNailAudioController::onRotationComplete()
{
    if (m_loaded)
        m_audio.playSfx("tongs_settle_01", 0, -1, 70);
}

void SmithingNailAudioController::onForgeTongsWork()
{
    if (!m_loaded)
        return;

    const bool placed = playOptional("tongs_forge_place_01", 76);
    const bool rotated = playOptional("tongs_forge_rotate_01", 66);
    if (!placed)
        m_audio.playSfx(choose(Grip), 0, -1, 70);
    if (!rotated)
        m_audio.playSfx(choose(Rotate), 0, -1, 58);
    if (!playOptional("tongs_forge_settle_01", 64))
        m_audio.playSfx("tongs_settle_01", 0, -1, 56);
}

void SmithingNailAudioController::onQuenchTongsWork(float temperature01)
{
    if (!m_loaded)
        return;

    const bool lowered = playOptional("tongs_quench_lower_01", 82);
    if (!lowered)
        m_audio.playSfx(choose(Grip), 0, -1, 74);
    playOptional(temperature01 >= 0.35f ? "tongs_quench_lift_01" : "tongs_quench_settle_01", 58);
}

void SmithingNailAudioController::onWaterContact(float temperature01)
{
    if (!m_loaded || m_quenchSoundPlayed || temperature01 < 0.35f)
        return;

    m_audio.playSfx(temperature01 >= 0.72f ? "quench_heavy_01" : "quench_short_01", 0, -1, 108);
    m_quenchSoundPlayed = true;
}

void SmithingNailAudioController::onBellowsPush()
{
    if (m_loaded)
        m_audio.playSfx("bellows_push_01", 0, -1, 82);
}

void SmithingNailAudioController::playMentorCue(const std::string& cueId)
{
    if (!m_loaded || cueId.empty())
        return;

    playOptional(cueId, 96);
}

void SmithingNailAudioController::resetQuench()
{
    m_quenchSoundPlayed = false;
}

void SmithingNailAudioController::startAmbienceLoop(
    const char* id,
    int channel,
    int volume0_128,
    bool& playing)
{
    if (playing)
        return;

    m_audio.playSfx(id, -1, channel, volume0_128);
    playing = true;
}

bool SmithingNailAudioController::playOptional(const std::string& id, int volume0_128)
{
    if (!m_loadedOptional.contains(id))
        return false;

    m_audio.playSfx(id, 0, -1, volume0_128);
    return true;
}
}
