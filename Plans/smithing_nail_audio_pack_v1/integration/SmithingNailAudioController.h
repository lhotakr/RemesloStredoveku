#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>

class AudioManager;

class SmithingNailAudioController
{
public:
    explicit SmithingNailAudioController(AudioManager& audio);

    bool load(const std::string& basePath = "assets/audio/smithing/nail");

    void onHammerImpact(float temperature01, float strikePower01, float scaleAmount01);
    void onGripClosed();
    void onRotationComplete();
    void onWaterContact(float temperature01);
    void onBellowsPush();
    void resetQuench();

private:
    template <std::size_t N>
    const char* choose(const std::array<const char*, N>& ids)
    {
        std::uniform_int_distribution<std::size_t> pick(0, N - 1);
        return ids[pick(m_rng)];
    }

    AudioManager& m_audio;
    std::mt19937 m_rng{0x1400u};
    bool m_quenchSoundPlayed = false;
    std::uint32_t m_lastImpactTicks = 0;
};
