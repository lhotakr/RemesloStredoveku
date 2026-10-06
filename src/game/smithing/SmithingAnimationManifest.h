#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace smithing
{
enum class AnimationPlayback
{
    Once,
    OnceHoldLast,
    StateDriven
};

struct AnimationFrameDef
{
    int atlasIndex = 0;
    std::string id;
    std::string phase;
    float referenceTemperature = 0.0f;
    int durationMs = 0;
};

struct AnimationEventDef
{
    int frame = 0;
    std::string event;
};

struct AnimationDef
{
    std::string id;
    std::string namespacedId;
    std::filesystem::path imagePath;

    int columns = 1;
    int rows = 1;
    int cellWidth = 512;
    int cellHeight = 512;

    AnimationPlayback playback = AnimationPlayback::Once;
    std::vector<AnimationFrameDef> frames;
    std::vector<AnimationEventDef> events;
};

struct SmithingAnimationManifest
{
    std::string namespaceId;
    int cellWidth = 512;
    int cellHeight = 512;
    int pivotX = 256;
    int pivotY = 256;
    std::vector<std::string> renderOrder;
    std::unordered_map<std::string, AnimationDef> animations;
    std::vector<AnimationFrameDef> workpieceFrames;

    const AnimationDef* Find(const std::string& id) const;
    const AnimationDef* FindNamespaced(const std::string& namespacedId) const;
};

std::optional<SmithingAnimationManifest> LoadSmithingAnimationManifest(
    const std::filesystem::path& manifestPath,
    const std::string& namespaceId);

const char* ToString(AnimationPlayback playback);
AnimationPlayback AnimationPlaybackFromString(const std::string& value);
}
