#include "SequenceAudio.h"

#include <algorithm>
#include <cctype>
#include <SDL_mixer.h>

namespace fs = std::filesystem;

namespace sequence
{
    bool IsAudioFile(const fs::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac";
    }

    fs::path ResolveAudioPath(const std::string& id, const fs::path& projectRoot)
    {
        fs::path path(id);
        if (path.is_absolute()) return path;
        if (path.has_parent_path()) return projectRoot / path;
        if (!path.has_extension()) path.replace_extension(".ogg");
        return projectRoot / "assets/audio" / path;
    }

    std::vector<std::string> ListAudioFiles(const fs::path& projectRoot)
    {
        std::vector<std::string> files;
        std::error_code ec;
        const auto dir = projectRoot / "assets/audio";
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec))
            if (it->is_regular_file(ec) && IsAudioFile(it->path()))
                files.push_back(it->path().lexically_relative(projectRoot).generic_string());
        std::sort(files.begin(), files.end());
        return files;
    }

    bool ImportAudioFile(const fs::path& source, const fs::path& projectRoot, std::string& assetPath, std::string& error)
    {
        assetPath.clear();
        error.clear();
        try
        {
            if (!IsAudioFile(source) || !fs::is_regular_file(source))
            {
                error = "Vyberte existující soubor WAV, OGG, MP3 nebo FLAC.";
                return false;
            }
            // Validate with the same decoder used by music playback before copying anything.
            Mix_Music* decoded = Mix_LoadMUS(source.string().c_str());
            if (!decoded)
            {
                error = std::string("Zvuk nelze načíst: ") + Mix_GetError();
                return false;
            }
            Mix_FreeMusic(decoded);
            const auto canonical = fs::canonical(source);
            const auto audioDir = fs::weakly_canonical(projectRoot / "assets/audio");
            const auto relative = canonical.lexically_relative(audioDir);
            if (!relative.empty() && *relative.begin() != "..")
            {
                assetPath = (fs::path("assets/audio") / relative).generic_string();
                return true;
            }
            const auto dir = projectRoot / "assets/audio/sequences";
            fs::create_directories(dir);
            auto target = dir / source.filename();
            for (int suffix = 2; fs::exists(target); ++suffix)
                target = dir / (source.stem().string() + "_" + std::to_string(suffix) + source.extension().string());
            fs::copy_file(source, target);
            assetPath = target.lexically_relative(projectRoot).generic_string();
            return true;
        }
        catch (const std::exception& e)
        {
            error = std::string("Import audia selhal: ") + e.what();
            return false;
        }
    }
}
