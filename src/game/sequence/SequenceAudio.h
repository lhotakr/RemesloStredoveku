#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sequence
{
    bool IsAudioFile(const std::filesystem::path& path);
    std::filesystem::path ResolveAudioPath(const std::string& id, const std::filesystem::path& projectRoot);
    std::vector<std::string> ListAudioFiles(const std::filesystem::path& projectRoot);
    // Copies external files into assets/audio/sequences; never overwrites an existing asset.
    bool ImportAudioFile(const std::filesystem::path& source, const std::filesystem::path& projectRoot,
        std::string& assetPath, std::string& error);
}
