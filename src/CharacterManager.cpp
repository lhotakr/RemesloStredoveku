#include "CharacterManager.h"

#include <SDL_image.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <algorithm>

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

static SDL_Texture* loadTexture(SDL_Renderer* r, const char* path)
{
    SDL_Surface* surf = IMG_Load(path);
    if (!surf) return nullptr;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    return tex;
}

bool CharacterManager::init(SDL_Renderer* renderer, const std::string& folder)
{
    shutdown();

    m_renderer = renderer;
    m_folder = folder;
    m_lastError.clear();

    if (!m_renderer) {
        m_lastError = "CharacterManager: renderer is null";
        return false;
    }

    if (!fs::exists(m_folder)) {
        m_lastError = "CharacterManager: folder not found: " + m_folder;
        return false;
    }

    bool anyLoaded = false;

    for (const auto& entry : fs::directory_iterator(m_folder))
    {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;

        if (!loadJsonFile(entry.path().string()))
            return false;

        anyLoaded = true;
    }

    if (!anyLoaded) {
        m_lastError = "CharacterManager: no json files found in " + m_folder;
        return false;
    }

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
    SDL_Texture* tex = loadTexture(m_renderer, fullPath.string().c_str());
    if (!tex) {
        m_lastError = "CharacterManager: failed to load texture: " + fullPath.string();
        return nullptr;
    }

    m_texturesBySheet[sheetFile] = tex;
    return tex;
}

bool CharacterManager::loadJsonFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        m_lastError = "CharacterManager: failed to open json: " + path;
        return false;
    }

    json root;
    try {
        f >> root;
    }
    catch (const std::exception& ex) {
        m_lastError = std::string("CharacterManager: json parse error in ") + path + " : " + ex.what();
        return false;
    }

    if (!root.contains("characters") || !root["characters"].is_array()) {
        m_lastError = "CharacterManager: missing 'characters' array in " + path;
        return false;
    }

    for (const auto& jc : root["characters"])
    {
        CharacterDef cd;
        cd.id = jc.value("id", "");
        cd.name = jc.value("name", "");
        cd.sheet = jc.value("sheet", "");

        if (cd.id.empty() || cd.sheet.empty())
            continue;

        SDL_Texture* tex = loadTextureIfNeeded(cd.sheet);
        if (!tex)
            return false;

        int texW = 0;
        int texH = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);

        cd.sheetW = texW;
        cd.sheetH = texH;

        if (jc.contains("animations") && jc["animations"].is_object())
        {
            for (auto it = jc["animations"].begin(); it != jc["animations"].end(); ++it)
            {
                CharacterAnimation anim;
                const auto& ja = it.value();

                anim.loop = ja.value("loop", true);
                anim.fps = ja.value("fps", 6);

                if (ja.contains("frames") && ja["frames"].is_array())
                {
                    for (const auto& jf : ja["frames"])
                    {
                        AnimFrame fr;
                        fr.x = jf.value("x", 0);
                        fr.y = jf.value("y", 0);
                        fr.w = jf.value("w", 0);
                        fr.h = jf.value("h", 0);
                        anim.frames.push_back(fr);
                    }
                }

                cd.animations[it.key()] = anim;
            }
        }

        const std::string loadedId = cd.id;
        m_characters[loadedId] = std::move(cd);
    }

    return true;
}