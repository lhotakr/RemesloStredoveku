#include "SmithingAnimationManifest.h"

#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace smithing
{
namespace
{
int ReadPositiveInt(const json& source, const char* key, int fallback)
{
    const int value = source.value(key, fallback);
    return value > 0 ? value : fallback;
}

std::filesystem::path ResolveManifestPath(
    const std::filesystem::path& manifestPath,
    const std::string& relative)
{
    std::filesystem::path path(relative);
    if (path.is_absolute())
        return path.lexically_normal();
    return (manifestPath.parent_path() / path).lexically_normal();
}

void ParseStringFrames(const json& frameRoot, const json& durations, AnimationDef& outAnimation)
{
    for (std::size_t i = 0; i < frameRoot.size(); ++i)
    {
        AnimationFrameDef frame;
        frame.atlasIndex = static_cast<int>(i);
        frame.id = frameRoot[i].get<std::string>();
        if (durations.is_array() && i < durations.size())
            frame.durationMs = durations[i].get<int>();
        outAnimation.frames.push_back(frame);
    }
}

void ParseObjectFrames(const json& frameRoot, AnimationDef& outAnimation)
{
    for (std::size_t i = 0; i < frameRoot.size(); ++i)
    {
        const json& source = frameRoot[i];
        AnimationFrameDef frame;
        frame.atlasIndex = static_cast<int>(i);
        frame.id = source.value("id", std::string());
        frame.phase = source.value("phase", std::string());
        frame.referenceTemperature = source.value("temperature", 0.0f);
        outAnimation.frames.push_back(frame);
    }
}

void ParseIndexedFrames(const json& indices, const json& durations, AnimationDef& outAnimation)
{
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        AnimationFrameDef frame;
        frame.atlasIndex = indices[i].get<int>();
        frame.id = std::to_string(frame.atlasIndex);
        if (durations.is_array() && i < durations.size())
            frame.durationMs = durations[i].get<int>();
        outAnimation.frames.push_back(frame);
    }
}
}

const AnimationDef* SmithingAnimationManifest::Find(const std::string& id) const
{
    const auto it = animations.find(id);
    return it != animations.end() ? &it->second : nullptr;
}

const AnimationDef* SmithingAnimationManifest::FindNamespaced(const std::string& namespacedId) const
{
    const std::string prefix = namespaceId + "::";
    if (namespacedId.rfind(prefix, 0) == 0)
        return Find(namespacedId.substr(prefix.size()));
    return nullptr;
}

const char* ToString(AnimationPlayback playback)
{
    switch (playback)
    {
    case AnimationPlayback::Once: return "once";
    case AnimationPlayback::OnceHoldLast: return "once_hold_last";
    case AnimationPlayback::StateDriven: return "state_driven";
    }
    return "once";
}

AnimationPlayback AnimationPlaybackFromString(const std::string& value)
{
    if (value == "once_hold_last")
        return AnimationPlayback::OnceHoldLast;
    if (value == "state_driven")
        return AnimationPlayback::StateDriven;
    return AnimationPlayback::Once;
}

std::optional<SmithingAnimationManifest> LoadSmithingAnimationManifest(
    const std::filesystem::path& manifestPath,
    const std::string& namespaceId)
{
    std::ifstream in(manifestPath);
    if (!in.good())
        return std::nullopt;

    json root;
    in >> root;
    if (!root.is_object())
        return std::nullopt;

    SmithingAnimationManifest manifest;
    manifest.namespaceId = namespaceId;

    if (root.contains("cell") && root["cell"].is_object())
    {
        const json& cell = root["cell"];
        manifest.cellWidth = ReadPositiveInt(cell, "width", manifest.cellWidth);
        manifest.cellHeight = ReadPositiveInt(cell, "height", manifest.cellHeight);
        if (cell.contains("pivot") && cell["pivot"].is_object())
        {
            manifest.pivotX = cell["pivot"].value("x", manifest.pivotX);
            manifest.pivotY = cell["pivot"].value("y", manifest.pivotY);
        }
    }

    if (root.contains("render_order") && root["render_order"].is_array())
    {
        for (const json& layer : root["render_order"])
        {
            if (layer.is_string())
                manifest.renderOrder.push_back(layer.get<std::string>());
        }
    }

    if (!root.contains("animations") || !root["animations"].is_array())
        return manifest;

    for (const json& sourceAnimation : root["animations"])
    {
        if (!sourceAnimation.is_object())
            continue;

        AnimationDef animation;
        animation.id = sourceAnimation.value("id", std::string());
        if (animation.id.empty())
            continue;

        animation.namespacedId = namespaceId + "::" + animation.id;
        animation.imagePath = ResolveManifestPath(
            manifestPath,
            sourceAnimation.value("image", std::string()));
        animation.playback = AnimationPlaybackFromString(sourceAnimation.value("playback", std::string("once")));
        animation.cellWidth = manifest.cellWidth;
        animation.cellHeight = manifest.cellHeight;

        if (sourceAnimation.contains("grid") && sourceAnimation["grid"].is_object())
        {
            animation.columns = ReadPositiveInt(sourceAnimation["grid"], "columns", animation.columns);
            animation.rows = ReadPositiveInt(sourceAnimation["grid"], "rows", animation.rows);
        }

        const json durations = sourceAnimation.value("frame_duration_ms", json::array());
        if (sourceAnimation.contains("frame_indices") && sourceAnimation["frame_indices"].is_array())
        {
            ParseIndexedFrames(sourceAnimation["frame_indices"], durations, animation);
        }
        else if (sourceAnimation.contains("frames") && sourceAnimation["frames"].is_array())
        {
            const json& frames = sourceAnimation["frames"];
            if (!frames.empty() && frames.front().is_object())
                ParseObjectFrames(frames, animation);
            else
                ParseStringFrames(frames, durations, animation);
        }

        if (sourceAnimation.contains("events") && sourceAnimation["events"].is_array())
        {
            for (const json& sourceEvent : sourceAnimation["events"])
            {
                if (!sourceEvent.is_object())
                    continue;
                animation.events.push_back(AnimationEventDef{
                    sourceEvent.value("frame", 0),
                    sourceEvent.value("event", std::string())
                });
            }
        }

        if (animation.id == "nail_workpiece_deformation")
            manifest.workpieceFrames = animation.frames;

        manifest.animations.emplace(animation.id, std::move(animation));
    }

    return manifest;
}
}
