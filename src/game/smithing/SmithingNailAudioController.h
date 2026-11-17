#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_set>

class AudioManager;

namespace smithing
{
class SmithingNailAudioController
{
public:
    explicit SmithingNailAudioController(AudioManager& audio);

    bool load(const std::string& basePath = "assets/audio/smithing/nail");
    bool isLoaded() const { return m_loaded; }

    void updateAmbience(bool active, bool forgeFocused);
    void stopAmbience();

    void onHammerImpact(float temperature01, float strikePower01, float scaleAmount01);
    void onGripClosed();
    void onRotationComplete();
    void onForgeTongsWork();
    void onQuenchTongsWork(float temperature01);
    void onWaterContact(float temperature01);
    void onBellowsPush();
    void playMentorCue(const std::string& cueId);
    void resetQuench();

private:
    template <std::size_t N>
    const char* choose(const std::array<const char*, N>& ids)
    {
        std::uniform_int_distribution<std::size_t> pick(0, N - 1);
        return ids[pick(m_rng)];
    }

    void startAmbienceLoop(const char* id, int channel, int volume0_128, bool& playing);
    bool playOptional(const std::string& id, int volume0_128);

    AudioManager& m_audio;
    std::mt19937 m_rng{0x1400u};
    std::unordered_set<std::string> m_loadedOptional;
    bool m_loadAttempted = false;
    bool m_loaded = false;
    bool m_quenchSoundPlayed = false;
    bool m_forgeFirePlaying = false;
    bool m_roomTonePlaying = false;
    std::uint32_t m_lastImpactTicks = 0;
};
}
