#include "CharacterManager.h"

#include <SDL_image.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::vector<std::string> CharacterManager::characterIds() const
{
    std::vector<std::string> ids;
    ids.reserve(m_characters.size());

    for (const auto& kv : m_characters)
        ids.push_back(kv.first);

    std::sort(ids.begin(), ids.end());
    return ids;
}

SDL_FPoint CharacterDef::frameAnchor(const AnimFrame& frame) const
{
    if (anchor)
        return {static_cast<float>(anchor->x), static_cast<float>(anchor->y)};
    return {frame.w * 0.5f, static_cast<float>(frame.h)};
}

SDL_Rect CharacterDef::frameDestination(const AnimFrame& frame, float x, float y,
                                        int camX, int camY, float scale) const
{
    const auto pivot = frameAnchor(frame);
    return {
        static_cast<int>(std::lround(x - pivot.x * scale)) - camX,
        static_cast<int>(std::lround(y - pivot.y * scale)) - camY,
        static_cast<int>(std::lround(frame.w * scale)),
        static_cast<int>(std::lround(frame.h * scale))
    };
}

bool CharacterManager::init(SDL_Renderer* renderer, const std::string& folder)
{
    shutdown();

    m_renderer = renderer;
    m_folder = folder;
    const auto fail = [this](std::string error)
    {
        shutdown();
        m_lastError = std::move(error);
        return false;
    };

    if (!m_renderer)
        return fail("CharacterManager: renderer is null");

    try
    {
        for (const auto& entry : fs::directory_iterator(m_folder))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;
            if (!loadJsonFile(entry.path().string()))
                return fail(m_lastError);
        }
    }
    catch (const fs::filesystem_error& ex)
    {
        return fail(std::string("CharacterManager: ") + ex.what());
    }

    if (m_characters.empty())
        return fail("CharacterManager: no character definitions found in " + m_folder);

    return true;
}

void CharacterManager::shutdown()
{
    for (auto& kv : m_texturesBySheet) {
        if (kv.second) SDL_DestroyTexture(kv.second);
    }

    m_texturesBySheet.clear();
    m_characters.clear();
    m_renderer = nullptr;
    m_folder.clear();
    m_lastError.clear();
}

const CharacterDef* CharacterManager::getCharacter(const std::string& id) const
{
    auto it = m_characters.find(id);
    if (it == m_characters.end()) return nullptr;
    return &it->second;
}

SDL_Texture* CharacterManager::getTextureForCharacter(const std::string& id) const
{
    auto itChar = m_characters.find(id);
    if (itChar == m_characters.end()) return nullptr;

    auto itTex = m_texturesBySheet.find(itChar->second.sheet);
    if (itTex == m_texturesBySheet.end()) return nullptr;

    return itTex->second;
}

SDL_Texture* CharacterManager::loadTextureIfNeeded(const std::string& sheetFile)
{
    auto it = m_texturesBySheet.find(sheetFile);
    if (it != m_texturesBySheet.end())
        return it->second;

    fs::path fullPath = fs::path(m_folder) / sheetFile;
    SDL_Texture* tex = IMG_LoadTexture(m_renderer, fullPath.string().c_str());
    if (!tex)
    {
        m_lastError = "cannot load atlas " + fullPath.string() + ": " + IMG_GetError();
        std::ifstream file(fullPath, std::ios::binary);
        constexpr std::string_view lfsHeader = "version https://git-lfs.github.com/spec/v1";
        char header[lfsHeader.size()]{};
        if (file.read(header, sizeof(header)) && std::string_view(header, sizeof(header)) == lfsHeader)
            m_lastError += " (soubor je pouze Git LFS ukazatel, spusťte git lfs pull)";
        return nullptr;
    }

    m_texturesBySheet[sheetFile] = tex;
    return tex;
}

static int readNonNegativeInt(const json& object, const char* key)
{
    const auto& value = object.at(key);
    if (!value.is_number_integer() || value.get<double>() < 0 ||
        value.get<double>() > std::numeric_limits<int>::max())
        throw std::runtime_error(std::string(key) + " must be a non-negative integer");
    return value.get<int>();
}

bool CharacterManager::loadJsonFile(const std::string& path)
{
    std::string context = path;
    try
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("cannot open JSON");
        const auto root = json::parse(file);
        const auto& characters = root.at("characters");
        if (!characters.is_array() || characters.empty())
            throw std::runtime_error("'characters' must be a non-empty array");

        for (const auto& entry : characters)
        {
            context = path;
            CharacterDef character;
            character.id = entry.at("id").get<std::string>();
            character.name = entry.value("name", "");
            character.sheet = entry.at("sheet").get<std::string>();
            if (character.id.empty() || character.sheet.empty())
                throw std::runtime_error("'id' and 'sheet' must not be empty");
            context = path + " / " + character.id;
            if (m_characters.contains(character.id))
                throw std::runtime_error("duplicate character ID");

            SDL_Texture* texture = loadTextureIfNeeded(character.sheet);
            if (!texture)
                throw std::runtime_error(m_lastError);
            if (SDL_QueryTexture(texture, nullptr, nullptr, &character.sheetW, &character.sheetH) != 0)
                throw std::runtime_error("cannot query atlas " + character.sheet + ": " + SDL_GetError());

            if (entry.contains("anchor"))
            {
                const auto& anchor = entry.at("anchor");
                character.anchor = SDL_Point{readNonNegativeInt(anchor, "x"), readNonNegativeInt(anchor, "y")};
            }

            const auto& animations = entry.at("animations");
            if (!animations.is_object() || animations.empty())
                throw std::runtime_error("'animations' must be a non-empty object");

            bool hasLocomotion = false;
            for (const auto& [name, clip] : animations.items())
            {
                const std::string clipContext = path + " / " + character.id + " / " + name;
                context = clipContext;
                if (name.empty())
                    throw std::runtime_error("empty animation name");
                CharacterAnimation animation;
                animation.loop = clip.value("loop", true);
                if (clip.contains("fps"))
                    animation.fps = readNonNegativeInt(clip, "fps");
                if (animation.fps < 1 || animation.fps > 240)
                    throw std::runtime_error("'fps' must be between 1 and 240");

                const auto& frames = clip.at("frames");
                if (!frames.is_array() || frames.empty())
                    throw std::runtime_error("'frames' must be a non-empty array");
                for (const auto& rect : frames)
                {
                    context = clipContext + " / frame " + std::to_string(animation.frames.size() + 1);
                    const AnimFrame frame{
                        readNonNegativeInt(rect, "x"), readNonNegativeInt(rect, "y"),
                        readNonNegativeInt(rect, "w"), readNonNegativeInt(rect, "h")
                    };
                    if (frame.w == 0 || frame.h == 0 ||
                        frame.x > character.sheetW - frame.w || frame.y > character.sheetH - frame.h)
                    {
                        throw std::runtime_error("frame rectangle exceeds atlas " + character.sheet + " or has zero area");
                    }
                    if (character.anchor && (character.anchor->x > frame.w || character.anchor->y > frame.h))
                        throw std::runtime_error("anchor is outside the frame");
                    animation.frames.push_back(frame);
                }
                hasLocomotion |= name.starts_with("walk_") || name.starts_with("run_");
                character.animations.emplace(name, std::move(animation));
            }

            context = path + " / " + character.id;
            if (hasLocomotion)
            {
                for (const std::string direction : {"down", "left", "right", "up"})
                {
                    for (const char* action : {"idle_", "walk_"})
                    {
                        const auto name = action + direction;
                        if (!character.animations.contains(name))
                            throw std::runtime_error("missing directional animation " + name);
                    }
                }
            }

            const auto id = character.id;
            m_characters.emplace(id, std::move(character));
        }
    }
    catch (const std::exception& ex)
    {
        m_lastError = "CharacterManager: " + context + ": " + ex.what();
        return false;
    }
    return true;
}
