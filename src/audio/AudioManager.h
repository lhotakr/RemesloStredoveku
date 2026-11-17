#pragma once
#include <string>
#include <unordered_map>

struct Mix_Music;
struct Mix_Chunk;

class AudioManager
{
public:
    bool init(int frequency = 44100, int channels = 2, int chunkSize = 1024);
    void shutdown();

    // Music
    bool playMusic(const std::string& path, int loops = -1, int fadeMs = 500);
    void stopMusic(int fadeMs = 300);
    void setMusicVolume(int volume0_128);
    void playVoice(const std::string& id, int volume0_128 = 128);

    // Sequence levels multiply the player's volume settings; fades use simulation time.
    void setMusicGain(float gain, float fadeSeconds = 0.0f);
    void setSfxGain(float gain);
    void duckMusic(float gain, float holdSeconds, float fadeOutSeconds = 0.3f, float fadeInSeconds = 0.5f);
    void restoreMusic(float fadeSeconds = 0.5f);
    void resetSequenceMix();
    void update(float dt);

    // SFX (volitelné, pøipraveno do budoucna)
    bool loadSfx(const std::string& id, const std::string& path);
    bool hasSfx(const std::string& id) const { return m_sfx.find(id) != m_sfx.end(); }
    void playSfx(const std::string& id, int loops = 0, int channel = -1, int volume0_128 = -1);
    void stopSfxChannel(int channel);
    void pausePlayback(bool paused);
    void seekMusic(double seconds, int loops);
    void setSfxVolume(int volume0_128);

    const std::string& lastError() const { return m_lastError; }

private:
    void setError(const std::string& s) { m_lastError = s; }
	int m_voiceChannel = 31; // rezervováno pro voice lines, aby se nepøerušovaly navzájem ani hudbou
    bool m_initialized = false;

    struct GainRamp
    {
        float value = 1.0f, from = 1.0f, target = 1.0f;
        float elapsed = 0.0f, duration = 0.0f;
        void set(float gain, float seconds);
        void update(float dt);
    };
    struct ChannelLevel { Mix_Chunk* chunk; float gain; };
    void applyMusicVolume();
    void applySfxVolumes();
    GainRamp m_musicGain, m_duckGain;
    float m_sfxGain = 1.0f;
    float m_duckRemaining = -1.0f, m_duckFadeIn = 0.5f;
    bool m_paused = false;
    std::unordered_map<int, ChannelLevel> m_channelLevels;

    Mix_Music* m_music = nullptr;
    std::string m_musicPath;

    std::unordered_map<std::string, Mix_Chunk*> m_sfx;

    int m_musicVol = 96; // 0..128
    int m_sfxVol = 96;   // 0..128
    std::string m_lastError;
};