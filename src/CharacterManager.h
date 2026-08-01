#pragma once
#include <SDL.h>

#include <string>
#include <vector>
#include <unordered_map>

struct AnimFrame
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct CharacterAnimation
{
    bool loop = true;
    int fps = 6;
    std::vector<AnimFrame> frames;
};

struct CharacterDef
{
    std::string id;
    std::string name;
    std::string sheet;

    int sheetW = 0;
    int sheetH = 0;

    std::unordered_map<std::string, CharacterAnimation> animations;
};

class CharacterManager
{
public:
    bool init(SDL_Renderer* renderer, const std::string& folder);
    void shutdown();

    const CharacterDef* getCharacter(const std::string& id) const;
    SDL_Texture* getTextureForCharacter(const std::string& id) const;

    std::vector<std::string> characterIds() const;

    const std::string& lastError() const { return m_lastError; }

private:
    bool loadJsonFile(const std::string& path);
    SDL_Texture* loadTextureIfNeeded(const std::string& sheetFile);

private:
    SDL_Renderer* m_renderer = nullptr;
    std::string m_folder;
    std::string m_lastError;

    std::unordered_map<std::string, CharacterDef> m_characters;
    std::unordered_map<std::string, SDL_Texture*> m_texturesBySheet;
};