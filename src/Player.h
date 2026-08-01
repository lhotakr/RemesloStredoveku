#pragma once

#include "game/PlayerStats.h"
#include <SDL.h>
#include <string>
#include "PlayerInventory.h"

class CharacterManager;

namespace campaignflow
{
    inline PlayerStats::Background& SelectedBackgroundStorage()
    {
        static PlayerStats::Background value = PlayerStats::Background::ScholarAthlete;
        return value;
    }

    inline void SetSelectedBackground(PlayerStats::Background background)
    {
        SelectedBackgroundStorage() = background;
    }

    inline PlayerStats::Background GetSelectedBackground()
    {
        return SelectedBackgroundStorage();
    }
}

struct Player
{
    enum class Facing
    {
        Down,
        Left,
        Right,
        Up
    };

    Player();

    // world (feet pivot)
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;

    // collider
    int colW = 18;
    int colH = 28;

    // selected character
    std::string characterId;

    // stats / survival
    PlayerStats stats;
    PlayerInventory inventory;

    // animation state
    Facing facing = Facing::Down;
    std::string currentAnim = "idle_down";
    int currentFrame = 0;
    float animTime = 0.0f;

    // meta / progression
    std::string givenName = "Patrik";
    std::string familyName = "Nìmec";
    std::string noteProfileId = "scholar_athlete";
    bool isSprinting = false;
    bool isMoving = false;

    void setPosition(float px, float py) { x = px; y = py; }
    std::string fullName() const { return givenName + " " + familyName; }

    void applyBackground(PlayerStats::Background background);
    bool selectCharacter(const std::string& id, const CharacterManager& manager);

    void update(float dt, const CharacterManager& manager);
    SDL_Rect worldAABB() const;
    float currentMoveSpeed() const;
    void render(SDL_Renderer* r, int camX, int camY, const CharacterManager& manager) const;
};
