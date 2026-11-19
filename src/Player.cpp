#include "Player.h"
#include "CharacterManager.h"

#include <algorithm>
#include <cmath>
#include <string>

static bool isLocomotion(const std::string& animation)
{
    return animation.starts_with("walk_") || animation.starts_with("run_");
}

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
    animFrameDuration = 0.0f;
    currentAnim = "idle_down";
    facing = Facing::Down;
    stopAnimation();
    return true;
}

void Player::syncCharacterVariant(const CharacterManager& manager)
{
    if (characterId != "patrik" && characterId != "patrik_s_batohem") return;
    const std::string variant = inventory.equipped.back.empty() ? "patrik" : "patrik_s_batohem";
    if (manager.getCharacter(variant)) characterId = variant;
}

bool Player::playAnimation(const std::string& animation, const CharacterManager& manager)
{
    if (animation.empty() || animation == "auto")
    {
        stopAnimation();
        return true;
    }
    syncCharacterVariant(manager);
    const auto* character = manager.getCharacter(characterId);
    if (!character) return false;
    std::string name = animation;
    if (!character->animations.contains(name))
        name += facingToIdle(facing).substr(4);
    const auto found = character->animations.find(name);
    if (found == character->animations.end() || found->second.frames.empty()) return false;

    animationOverride = name;
    animationHoldLastFrame = name.starts_with("lie_down_") || name.starts_with("death_");
    currentAnim = name;
    currentFrame = 0;
    animTime = 0.0f;
    animFrameDuration = 0.0f;
    return true;
}

void Player::stopAnimation()
{
    animationOverride.clear();
    animationHoldLastFrame = false;
}

bool Player::movementLocked(const CharacterManager& manager) const
{
    if (stats.condition.health <= 0.0f) return true;
    const auto* character = manager.getCharacter(characterId);
    if (!character || animationOverride.empty()) return false;
    const auto found = character->animations.find(animationOverride);
    return found != character->animations.end() && !found->second.loop;
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
    syncCharacterVariant(manager);
    const CharacterDef* ch = manager.getCharacter(characterId);
    if (!ch) return;

    if (stats.condition.health <= 0.0f && !animationOverride.starts_with("death_"))
        playAnimation("death", manager);
    if (movementLocked(manager)) vx = vy = 0.0f;

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

    std::string nextAnim = isMoving ? facingToWalk(facing) : facingToIdle(facing);
    if (isMoving && isSprinting)
    {
        const auto run = "run" + facingToIdle(facing).substr(4);
        const auto found = ch->animations.find(run);
        if (found != ch->animations.end() && !found->second.frames.empty()) nextAnim = run;
    }
    if (!animationOverride.empty()) nextAnim = animationOverride;
    const auto next = ch->animations.find(nextAnim);
    if (next == ch->animations.end())
    {
        currentAnim = nextAnim;
        currentFrame = 0;
        animTime = animFrameDuration = 0.0f;
        return;
    }

    const CharacterAnimation& anim = next->second;
    const float referenceSpeed = nextAnim.starts_with("run_") ? 135.0f : 90.0f;
    const float speedScale = isMoving && animationOverride.empty() && isLocomotion(nextAnim)
        ? std::sqrt(moveLenSq) / referenceSpeed : 1.0f;
    const float frameDuration = 1.0f / (static_cast<float>(anim.fps) * speedScale);
    const float previousDuration = animFrameDuration > 0.0f ? animFrameDuration : frameDuration;
    const int frameCount = static_cast<int>(anim.frames.size());
    // Keep the fraction of the previous frame when the playback speed changes.
    const double frameProgress = static_cast<double>(animTime) / previousDuration;
    double playhead = 0.0;
    if (currentAnim == nextAnim)
    {
        playhead = std::clamp(currentFrame, 0, frameCount - 1) + frameProgress;
    }
    else if (anim.loop && isLocomotion(nextAnim) && isLocomotion(currentAnim))
    {
        const auto previous = ch->animations.find(currentAnim);
        if (previous != ch->animations.end() && previous->second.loop)
        {
            const int previousCount = static_cast<int>(previous->second.frames.size());
            const double previousPlayhead = std::clamp(currentFrame, 0, previousCount - 1) + frameProgress;
            playhead = std::fmod(previousPlayhead, previousCount) * frameCount / previousCount;
        }
    }

    currentAnim = nextAnim;
    animFrameDuration = frameDuration;
    playhead += static_cast<double>(std::max(0.0f, dt)) / frameDuration;
    if (anim.loop)
        playhead = std::fmod(playhead, frameCount);
    else if (playhead >= frameCount)
    {
        playhead = frameCount - 1;
        if (!animationOverride.empty() && !animationHoldLastFrame)
        {
            stopAnimation();
            currentAnim = facingToIdle(facing);
            playhead = 0.0;
        }
    }
    currentFrame = static_cast<int>(playhead);
    animTime = static_cast<float>(playhead - currentFrame) * frameDuration;
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

    const SDL_Rect dst = ch->frameDestination(fr, x, y, camX, camY);

    SDL_RenderCopy(r, tex, &src, &dst);
}
