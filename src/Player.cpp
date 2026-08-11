#include "Player.h"
#include "CharacterManager.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <terrain/TileMap.h>

static std::string facingToIdle(Player::Facing f)
{
    switch (f) {
    case Player::Facing::Down:  return "idle_down";
    case Player::Facing::Left:  return "idle_left";
    case Player::Facing::Right: return "idle_right";
    case Player::Facing::Up:    return "idle_up";
    }
    return "idle_down";
}

static std::string facingToWalk(Player::Facing f)
{
    switch (f) {
    case Player::Facing::Down:  return "walk_down";
    case Player::Facing::Left:  return "walk_left";
    case Player::Facing::Right: return "walk_right";
    case Player::Facing::Up:    return "walk_up";
    }
    return "walk_down";
}

Player::Player()
{
    applyBackground(campaignflow::GetSelectedBackground());
}

void Player::applyBackground(PlayerStats::Background background)
{
    stats.applyBackgroundPreset(background);
    noteProfileId = stats.backgroundId();
}

bool Player::selectCharacter(const std::string& id, const CharacterManager& manager)
{
    const CharacterDef* ch = manager.getCharacter(id);
    if (!ch) return false;

    characterId = id;
    currentFrame = 0;
    animTime = 0.0f;
    currentAnim = "idle_down";
    facing = Facing::Down;
    return true;
}

SDL_Rect Player::worldAABB() const
{
    SDL_Rect r{};
    r.w = 16;
    r.h = 16;
    r.x = (int)std::lround(x - 8.0f);
    r.y = (int)std::lround(y - 16.0f);
    return r;
}

float Player::currentMoveSpeed() const
{
    return stats.getLimitedMoveSpeed(isSprinting);
}

void Player::update(float dt, const CharacterManager& manager)
{
    const CharacterDef* ch = manager.getCharacter(characterId);
    if (!ch) return;

    const float moveLenSq = vx * vx + vy * vy;
    isMoving = (moveLenSq > 0.0001f);

    if (std::fabs(vx) > std::fabs(vy))
    {
        if (vx < 0.0f) facing = Facing::Left;
        else if (vx > 0.0f) facing = Facing::Right;
    }
    else
    {
        if (vy < 0.0f) facing = Facing::Up;
        else if (vy > 0.0f) facing = Facing::Down;
    }

    stats.updateVitals(dt, isMoving);

    currentAnim = isMoving ? facingToWalk(facing) : facingToIdle(facing);

    auto it = ch->animations.find(currentAnim);
    if (it == ch->animations.end() || it->second.frames.empty()) {
        currentFrame = 0;
        animTime = 0.0f;
        return;
    }

    const CharacterAnimation& anim = it->second;

    const float speedScale = std::clamp(currentMoveSpeed() / 90.0f, 0.55f, 1.65f);
    const float frameDuration = (anim.fps > 0)
        ? (1.0f / ((float)anim.fps * speedScale))
        : (0.15f / speedScale);

    if (!isMoving)
    {
        currentFrame = 0;
        animTime = 0.0f;
        return;
    }

    animTime += dt;
    while (animTime >= frameDuration)
    {
        animTime -= frameDuration;
        currentFrame++;

        if (currentFrame >= (int)anim.frames.size())
        {
            if (anim.loop) currentFrame = 0;
            else currentFrame = (int)anim.frames.size() - 1;
        }
    }
}

void Player::render(SDL_Renderer* r, int camX, int camY, const CharacterManager& manager) const
{
    const CharacterDef* ch = manager.getCharacter(characterId);
    SDL_Texture* tex = manager.getTextureForCharacter(characterId);

    SDL_Rect aabb = worldAABB();
    aabb.x -= camX;
    aabb.y -= camY;

    if (!ch || !tex)
    {
        SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
        SDL_RenderDrawRect(r, &aabb);
        return;
    }

    auto it = ch->animations.find(currentAnim);
    if (it == ch->animations.end() || it->second.frames.empty())
    {
        SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
        SDL_RenderDrawRect(r, &aabb);
        return;
    }

    const CharacterAnimation& anim = it->second;
    const int frameIndex = (currentFrame >= 0 && currentFrame < (int)anim.frames.size()) ? currentFrame : 0;
    const AnimFrame& fr = anim.frames[frameIndex];

    SDL_Rect src{ fr.x, fr.y, fr.w, fr.h };

    SDL_Rect dst;
    dst.w = fr.w;
    dst.h = fr.h;
    dst.x = (int)std::lround(x - fr.w * 0.5f) - camX;
    dst.y = (int)std::lround(y - fr.h) - camY;

    SDL_RenderCopy(r, tex, &src, &dst);
}
