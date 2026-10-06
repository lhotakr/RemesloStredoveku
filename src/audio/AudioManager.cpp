#include "AudioManager.h"

#include <SDL.h>
#include <SDL_mixer.h>

#include <string>

static std::string mixErr()
{
    const char* e = Mix_GetError();
    return e ? std::string(e) : std::string("Unknown SDL_mixer error");
}
static int g_audioOpenRefs = 0;

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
        Mix_VolumeMusic(m_musicVol);
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
    Mix_VolumeMusic(m_musicVol);
    ++g_audioOpenRefs;
    m_initialized = true;
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

    // kdy� u� hraje stejn� track, nic ned�lej
    if (m_music && m_musicPath == path && Mix_PlayingMusic())
        return true;

    // vym�� track
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
    if (m_initialized)
        Mix_VolumeMusic(m_musicVol);
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
    Mix_VolumeChunk(ch, m_sfxVol);

    SDL_Log("SFX LOADED: id=%s path=%s", id.c_str(), path.c_str());
    return true;
}

void AudioManager::playVoice(const std::string& id)
{
    if (!m_initialized) {
        SDL_Log("VOICE PLAY SKIPPED: audio is not initialized id=%s", id.c_str());
        return;
    }

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
    // nastav� se p�i play/load
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

    int effectiveVolume = m_sfxVol;
    if (volume0_128 >= 0)
    {
        const int clamped = (volume0_128 < 0) ? 0 : (volume0_128 > 128 ? 128 : volume0_128);
        effectiveVolume = (m_sfxVol * clamped) / 128;
    }
    Mix_VolumeChunk(it->second, effectiveVolume);

    if (Mix_PlayChannel(channel, it->second, loops) == -1)
    {
        SDL_Log("SFX PLAY FAILED: id=%s err=%s", id.c_str(), Mix_GetError());
    }
}

void AudioManager::stopSfxChannel(int channel)
{
    if (m_initialized && channel >= 0)
        Mix_HaltChannel(channel);
}
