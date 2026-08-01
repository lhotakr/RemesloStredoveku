#include "AudioManager.h"

#include <SDL.h>
#include <SDL_mixer.h>

static std::string mixErr()
{
    const char* e = Mix_GetError();
    return e ? std::string(e) : std::string("Unknown SDL_mixer error");
}

bool AudioManager::init(int frequency, int channels, int chunkSize)
{
    // Mix_Init je pro OGG/MP3 podpory podle buildù; ne všude je potøeba,
    // ale bezpeèné je požádat aspoò o OGG + MP3.
    int flags = MIX_INIT_OGG | MIX_INIT_MP3;
    if ((Mix_Init(flags) & flags) == 0) {
        // není fatální, mùže to mít jen èást kodekù — ale radìji to hlásíme
        // a pokraèujeme; Mix_OpenAudio mùže poøád fungovat.
    }

    if (Mix_OpenAudio(frequency, MIX_DEFAULT_FORMAT, channels, chunkSize) != 0) {
        setError(mixErr());
        return false;
    }

    Mix_AllocateChannels(32);
    Mix_VolumeMusic(m_musicVol);
    return true;
}

void AudioManager::shutdown()
{
    stopMusic(0);

    for (auto& kv : m_sfx) {
        if (kv.second) Mix_FreeChunk(kv.second);
    }
    m_sfx.clear();

    Mix_CloseAudio();
    Mix_Quit();
}

bool AudioManager::playMusic(const std::string& path, int loops, int fadeMs)
{
    if (path.empty()) return false;

    // když už hraje stejný track, nic nedìlej
    if (m_music && m_musicPath == path && Mix_PlayingMusic())
        return true;

    // vymìò track
    stopMusic(fadeMs);

    Mix_Music* mus = Mix_LoadMUS(path.c_str());
    if (!mus) {
        setError(mixErr());
        return false;
    }

    m_music = mus;
    m_musicPath = path;

    Mix_VolumeMusic(m_musicVol);

    if (fadeMs > 0) {
        if (Mix_FadeInMusic(m_music, loops, fadeMs) != 0) {
            setError(mixErr());
            return false;
        }
    }
    else {
        if (Mix_PlayMusic(m_music, loops) != 0) {
            setError(mixErr());
            return false;
        }
    }

    return true;
}

void AudioManager::stopMusic(int fadeMs)
{
    if (Mix_PlayingMusic()) {
        if (fadeMs > 0) Mix_FadeOutMusic(fadeMs);
        else Mix_HaltMusic();
    }

    if (m_music) {
        Mix_FreeMusic(m_music);
        m_music = nullptr;
    }
    m_musicPath.clear();
}

void AudioManager::setMusicVolume(int volume0_128)
{
    m_musicVol = (volume0_128 < 0) ? 0 : (volume0_128 > 128 ? 128 : volume0_128);
    Mix_VolumeMusic(m_musicVol);
}

bool AudioManager::loadSfx(const std::string& id, const std::string& path)
{
    if (id.empty() || path.empty()) return false;

    auto it = m_sfx.find(id);
    if (it != m_sfx.end() && it->second) {
        Mix_FreeChunk(it->second);
        it->second = nullptr;
    }

    Mix_Chunk* ch = Mix_LoadWAV(path.c_str());
    if (!ch) {
        setError(mixErr());
        SDL_Log("SFX LOAD FAILED: id=%s path=%s err=%s",
            id.c_str(), path.c_str(), m_lastError.c_str());
        return false;
    }

    m_sfx[id] = ch;
    Mix_VolumeChunk(ch, m_sfxVol);

    SDL_Log("SFX LOADED: id=%s path=%s", id.c_str(), path.c_str());
    return true;
}

void AudioManager::playVoice(const std::string& id)
{
    auto it = m_sfx.find(id);
    if (it == m_sfx.end()) {
        SDL_Log("VOICE NOT FOUND IN MAP: %s", id.c_str());
        return;
    }

    if (!it->second) {
        SDL_Log("VOICE CHUNK NULL: %s", id.c_str());
        return;
    }

    Mix_VolumeChunk(it->second, m_sfxVol);

    SDL_Log("VOICE PLAY: id=%s channel=%d volume=%d",
        id.c_str(), m_voiceChannel, m_sfxVol);

    Mix_HaltChannel(m_voiceChannel);

    if (Mix_PlayChannel(m_voiceChannel, it->second, 0) == -1) {
        SDL_Log("VOICE PLAY FAILED: id=%s err=%s",
            id.c_str(), Mix_GetError());
    }
}
void AudioManager::setSfxVolume(int volume0_128)
{
    m_sfxVol = (volume0_128 < 0) ? 0 : (volume0_128 > 128 ? 128 : volume0_128);
    // nastaví se pøi play/load
}

void AudioManager::playSfx(const std::string& id, int loops, int channel)
{
    auto it = m_sfx.find(id);
    if (it == m_sfx.end() || !it->second)
    {
        SDL_Log("SFX NOT FOUND IN MAP: %s", id.c_str());
        return;
    }

    Mix_PlayChannel(channel, it->second, loops);
}