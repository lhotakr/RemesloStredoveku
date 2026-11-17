#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace inventorysprites
{
struct Choice
{
    std::string id;
    std::string name;
    std::string spriteId;
    std::string path;
};

// Uses the same spriteId paths as inventory icons. Only existing images are listed.
std::vector<Choice> Load(const std::filesystem::path& projectRoot);
}
