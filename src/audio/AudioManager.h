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
    void playVoice(const std::string& id);

    // SFX (volitelné, pøipraveno do budoucna)
    bool loadSfx(const std::string& id, const std::string& path);
    void playSfx(const std::string& id, int loops = 0, int channel = -1);
    void setSfxVolume(int volume0_128);

    const std::string& lastError() const { return m_lastError; }

private:
    void setError(const std::string& s) { m_lastError = s; }
	int m_voiceChannel = 31; // rezervováno pro voice lines, aby se nepøerušovaly navzájem ani hudbou

    Mix_Music* m_music = nullptr;
    std::string m_musicPath;

    std::unordered_map<std::string, Mix_Chunk*> m_sfx;

    int m_musicVol = 96; // 0..128
    int m_sfxVol = 96;   // 0..128
    std::string m_lastError;
};