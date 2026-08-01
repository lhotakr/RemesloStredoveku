#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace jsonutils
{
    bool ReadTextFileUtf8Safe(const std::string& path, std::string& outText, std::string& outError);

    bool LoadJsonFileSafe(const std::string& path, nlohmann::json& outJson, std::string& outError);
}