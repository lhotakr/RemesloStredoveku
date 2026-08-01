#pragma once
#include <filesystem>

namespace pathutils
{
    inline std::filesystem::path ProjectRoot()
    {
#ifdef REMESLO_PROJECT_ROOT
        return std::filesystem::weakly_canonical(std::filesystem::path(REMESLO_PROJECT_ROOT));
#else
        return std::filesystem::weakly_canonical(std::filesystem::current_path());
#endif
    }

    inline std::filesystem::path DataDir()
    {
        return ProjectRoot() / "data";
    }

    inline std::filesystem::path MapsDir()
    {
        return DataDir() / "maps";
    }

    inline std::filesystem::path NpcsDir()
    {
        return DataDir() / "npcs";
    }

    inline std::filesystem::path AssetsDir()
    {
        return ProjectRoot() / "assets";
    }
}