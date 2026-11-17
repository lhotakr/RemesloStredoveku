#include "AudioManager.h"
#include <algorithm>
#include <cmath>

#include <SDL.h>
#include <SDL_mixer.h>

#include <string>

static std::string mixErr()
{
    const char* e = Mix_GetError();
    return e ? std::string(e) : std::string("Unknown SDL_mixer error");
}
static int g_audioOpenRefs = 0;

namespace
{
    float AudioGain(float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 1.0f; }
    float AudioSeconds(float value) { return std::isfinite(value) ? std::max(0.0f, value) : 0.0f; }
}

void AudioManager::GainRamp::set(float gain, float seconds)
{
    from = value;
    target = AudioGain(gain);
    elapsed = 0.0f;
    duration = AudioSeconds(seconds);
    if (duration == 0.0f) value = target;
}

void AudioManager::GainRamp::update(float dt)
{
    elapsed = std::min(duration, elapsed + dt);
    value = duration > 0.0f ? from + (target - from) * (elapsed / duration) : target;
}

void AudioManager::applyMusicVolume()
{
    if (m_initialized)
        Mix_VolumeMusic(static_cast<int>(std::lround(m_musicVol * m_musicGain.value * m_duckGain.value)));
}

void AudioManager::applySfxVolumes()
{
    if (!m_initialized) return;
    for (auto it = m_channelLevels.begin(); it != m_channelLevels.end();)
    {
        if (!Mix_Playing(it->first) || Mix_GetChunk(it->first) != it->second.chunk)
            it = m_channelLevels.erase(it);
        else
        {
            Mix_Volume(it->first, static_cast<int>(std::lround(m_sfxVol * m_sfxGain * it->second.gain)));
            ++it;
        }
    }
}

void AudioManager::setMusicGain(float gain, float fadeSeconds)
{
    m_musicGain.set(gain, fadeSeconds);
    applyMusicVolume();
}

void AudioManager::setSfxGain(float gain)
{
    m_sfxGain = AudioGain(gain);
    applySfxVolumes();
}

void AudioManager::duckMusic(float gain, float holdSeconds, float fadeOutSeconds, float fadeInSeconds)
{
    m_duckGain.set(gain, fadeOutSeconds);
    m_duckRemaining = m_duckGain.duration + AudioSeconds(holdSeconds);
    m_duckFadeIn = AudioSeconds(fadeInSeconds);
    applyMusicVolume();
}

void AudioManager::restoreMusic(float fadeSeconds)
{
    m_duckRemaining = -1.0f;
    m_duckGain.set(1.0f, fadeSeconds);
    applyMusicVolume();
}

void AudioManager::resetSequenceMix()
{
    setMusicGain(1.0f);
    setSfxGain(1.0f);
    restoreMusic(0.0f);
}

void AudioManager::update(float dt)
{
    if (m_paused || !std::isfinite(dt) || dt < 0.0f) return;
    m_musicGain.update(dt);
    if (m_duckRemaining >= 0.0f)
    {
        const float attackAndHold = std::min(dt, m_duckRemaining);
        m_duckGain.update(attackAndHold);
        m_duckRemaining -= attackAndHold;
        if (m_duckRemaining <= 0.0f)
        {
            restoreMusic(m_duckFadeIn);
            m_duckGain.update(dt - attackAndHold);
        }
    }
    else m_duckGain.update(dt);
    applyMusicVolume();
}

static bool envIsTruthy(const char* value)
{
    if (!value || !*value)
        return false;

    const std::string s(value);
    return s == "1" || s == "true" || s == "TRUE" || s == "yes" || s == "YES" || s == "on" || s == "ON";
}


bool AudioManager::init(int frequency, int channels, int chunkSize)
{
    if (m_initialized)
        return true;

    if (g_audioOpenRefs > 0)
    {
        ++g_audioOpenRefs;
        m_initialized = true;
        Mix_AllocateChannels(32);
        applyMusicVolume();
        return true;
    }

    const bool inFlatpak = SDL_getenv("container") != nullptr || SDL_getenv("FLATPAK_ID") != nullptr;
    const char* requestedDriver = SDL_getenv("SDL_AUDIODRIVER");
    const bool allowDummyAudio = envIsTruthy(SDL_getenv("REMESLO_ALLOW_DUMMY_AUDIO"));
    const char* flatpakDrivers[] = {requestedDriver, "pulseaudio", "pipewire", "dummy"};
    const char* nativeDrivers[] = {requestedDriver, nullptr, "pulseaudio", "pipewire", "alsa", "dummy"};
    const char** drivers = inFlatpak ? flatpakDrivers : nativeDrivers;
    const int driverCount = inFlatpak
        ? static_cast<int>(sizeof(flatpakDrivers) / sizeof(flatpakDrivers[0]))
        : static_cast<int>(sizeof(nativeDrivers) / sizeof(nativeDrivers[0]));

    bool success = false;
    std::string lastErr;

    for (int i = 0; i < driverCount; ++i) {
        const char* driver = drivers[i];
        if (driver && std::string(driver) == "dummy" && !allowDummyAudio) {
            fprintf(stderr, "Skipping dummy audio driver; set REMESLO_ALLOW_DUMMY_AUDIO=1 only for silent testing.\n");
            continue;
        }
        if (driver && i > 0) {
            bool alreadyTried = false;
            for (int j = 0; j < i; ++j) {
                if (drivers[j] && std::string(drivers[j]) == driver) {
                    alreadyTried = true;
                    break;
                }
            }
            if (alreadyTried) {
                continue;
            }
        }

        // IMPORTANT: Set driver BEFORE SDL audio init
        if (driver) {
            SDL_setenv("SDL_AUDIODRIVER", driver, 1);
            fprintf(stderr, "Trying audio driver: %s\n", driver);
        } else {
            fprintf(stderr, "Trying default audio driver\n");
        }

        // Quit any previous SDL audio subsystem
        SDL_QuitSubSystem(SDL_INIT_AUDIO);

        // Init SDL audio with the selected driver
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            lastErr = SDL_GetError();
            fprintf(stderr, "✗ SDL_InitSubSystem failed: %s\n", lastErr.c_str());
            continue;
        }

        // Mix_Init je pro OGG/MP3 podpory podle build�
        int flags = MIX_INIT_OGG | MIX_INIT_MP3;
        const int mixFlags = Mix_Init(flags);
        if ((mixFlags & MIX_INIT_OGG) != MIX_INIT_OGG) {
            lastErr = "SDL_mixer failed to initialize OGG support: " + mixErr();
            fprintf(stderr, "✗ %s\n", lastErr.c_str());
            Mix_Quit();
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            continue;
        }

        if (Mix_OpenAudio(frequency, MIX_DEFAULT_FORMAT, channels, chunkSize) == 0) {
            const char* currentDriver = SDL_GetCurrentAudioDriver();
            if (currentDriver && std::string(currentDriver) == "dummy" && !allowDummyAudio) {
                lastErr = "SDL opened dummy audio driver, so no real sound device is available. "
                    "Check PulseAudio/PipeWire access or set REMESLO_ALLOW_DUMMY_AUDIO=1 for silent testing.";
                fprintf(stderr, "✗ %s\n", lastErr.c_str());
                Mix_CloseAudio();
                Mix_Quit();
                SDL_QuitSubSystem(SDL_INIT_AUDIO);
                continue;
            }
            success = true;
            if (driver) {
                fprintf(stderr, "✓ Audio initialized with driver: %s\n", driver);
            } else {
                fprintf(stderr, "✓ Audio initialized with default driver\n");
            }
            if (currentDriver)
                fprintf(stderr, "SDL current audio driver: %s\n", currentDriver);
            break;
        } else {
            lastErr = mixErr();
            fprintf(stderr, "✗ Driver failed: %s\n", lastErr.c_str());
            Mix_Quit();
        }
    }

    if (!success) {
        if (inFlatpak) {
            if (!lastErr.empty())
                lastErr += "\n\n";
            lastErr += "Flatpak sandbox nema pristup ke zvukovemu socketu. "
                "Spust na hostu: flatpak override --user --socket=pulseaudio com.jetbrains.CLion "
                "a potom uplne restartuj CLion.";
        }
        setError(lastErr);
        return false;
    }

    Mix_AllocateChannels(32);
    ++g_audioOpenRefs;
    m_initialized = true;
    applyMusicVolume();
    return true;
}

void AudioManager::shutdown()
{
    if (m_initialized)
        stopMusic(0);

    for (auto& kv : m_sfx) {
        if (kv.second) Mix_FreeChunk(kv.second);
    }
    m_sfx.clear();
    m_channelLevels.clear();
    m_paused = false;
    resetSequenceMix();

    if (m_initialized)
    {
        m_initialized = false;
        if (g_audioOpenRefs > 0)
            --g_audioOpenRefs;

        if (g_audioOpenRefs == 0)
        {
            Mix_CloseAudio();
            Mix_Quit();
        }
    }
}

bool AudioManager::playMusic(const std::string& path, int loops, int fadeMs)
{
    if (path.empty()) return false;
    if (!m_initialized) {
        setError("Audio is not initialized");
        return false;
    }

    // Reusing the current track must not restart the soundtrack between nodes.
    if (m_music && m_musicPath == path && Mix_PlayingMusic()) {
        m_lastError.clear();
        return true;
    }

    // Validate the replacement before interrupting the current soundtrack.
    Mix_Music* mus = Mix_LoadMUS(path.c_str());
    if (!mus) {
        setError(mixErr());
        return false;
    }

    stopMusic(fadeMs);
    m_music = mus;
    m_musicPath = path;
    applyMusicVolume();

    const int result = fadeMs > 0
        ? Mix_FadeInMusic(m_music, loops, fadeMs)
        : Mix_PlayMusic(m_music, loops);
    if (result != 0) {
        setError(mixErr());
        Mix_FreeMusic(m_music);
        m_music = nullptr;
        m_musicPath.clear();
        return false;
    }

    m_lastError.clear();
    return true;
}

void AudioManager::stopMusic(int fadeMs)
{
    if (!m_initialized)
        return;

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
    applyMusicVolume();
}

bool AudioManager::loadSfx(const std::string& id, const std::string& path)
{
    if (id.empty() || path.empty()) return false;
    if (!m_initialized) {
        setError("Audio is not initialized");
        SDL_Log("SFX LOAD SKIPPED: audio is not initialized id=%s path=%s",
            id.c_str(), path.c_str());
        return false;
    }

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
    Mix_VolumeChunk(ch, MIX_MAX_VOLUME);

    SDL_Log("SFX LOADED: id=%s path=%s", id.c_str(), path.c_str());
    return true;
}

void AudioManager::playVoice(const std::string& id, int volume0_128)
{
    if (!m_initialized) return;
    Mix_HaltChannel(m_voiceChannel);
    playSfx(id, 0, m_voiceChannel, volume0_128);
}

void AudioManager::setSfxVolume(int volume0_128)
{
    m_sfxVol = std::clamp(volume0_128, 0, 128);
    applySfxVolumes();
}

void AudioManager::playSfx(const std::string& id, int loops, int channel, int volume0_128)
{
    if (!m_initialized)
    {
        SDL_Log("SFX PLAY SKIPPED: audio is not initialized id=%s", id.c_str());
        return;
    }

    auto it = m_sfx.find(id);
    if (it == m_sfx.end() || !it->second)
    {
        SDL_Log("SFX NOT FOUND IN MAP: %s", id.c_str());
        return;
    }

    const float gain = volume0_128 < 0 ? 1.0f : std::clamp(volume0_128, 0, 128) / 128.0f;
    // Keep clip gain on each channel: two instances of one chunk can have different levels.
    const int playedChannel = Mix_PlayChannel(channel, it->second, loops);
    if (playedChannel == -1)
        SDL_Log("SFX PLAY FAILED: id=%s err=%s", id.c_str(), Mix_GetError());
    else
    {
        m_channelLevels[playedChannel] = {it->second, gain};
        Mix_Volume(playedChannel, static_cast<int>(std::lround(m_sfxVol * m_sfxGain * gain)));
    }
}

void AudioManager::stopSfxChannel(int channel)
{
    if (m_initialized && channel >= 0)
        Mix_HaltChannel(channel);
}

void AudioManager::pausePlayback(bool paused)
{
    m_paused = paused;
    if (!m_initialized) return;
    if (m_music)
    {
        if (paused) Mix_PauseMusic();
        else Mix_ResumeMusic();
    }
    const int channels = Mix_AllocateChannels(-1);
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* chunk = Mix_GetChunk(channel);
        if (!chunk) continue;
        for (const auto& entry : m_sfx)
            if (entry.second == chunk)
            {
                if (paused) Mix_Pause(channel);
                else Mix_Resume(channel);
                break;
            }
    }
}

void AudioManager::seekMusic(double seconds, int loops)
{
    if (!m_initialized || !m_music || !std::isfinite(seconds)) return;
#if SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
    const double duration = Mix_MusicDuration(m_music);
    if (duration > 0.0)
    {
        if (loops >= 0 && seconds >= duration * (static_cast<double>(loops) + 1.0))
        {
            stopMusic(0);
            return;
        }
        seconds = std::fmod(seconds, duration);
    }
#endif
    Mix_RewindMusic();
    if (Mix_SetMusicPosition(seconds) < 0) setError(mixErr());
}
