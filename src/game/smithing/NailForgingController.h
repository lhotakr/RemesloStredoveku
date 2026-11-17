#pragma once

#include "NailForgingState.h"
#include "SmithingAnimationManifest.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace smithing
{
struct FiredAnimationEvent
{
    std::string animationId;
    std::string eventId;
};

class SmithingAnimationPlayer
{
public:
    void Play(const AnimationDef* animation);
    void Update(float dtSeconds, std::vector<FiredAnimationEvent>& outEvents);
    void Stop();

    bool IsPlaying() const { return m_animation != nullptr && m_playing; }
    int FrameIndex() const { return m_frameIndex; }
    int AtlasIndex() const;
    const AnimationDef* Animation() const { return m_animation; }

private:
    void DispatchEventsCrossed(int previousFrame, int nextFrame, std::vector<FiredAnimationEvent>& outEvents);

    const AnimationDef* m_animation = nullptr;
    int m_frameIndex = 0;
    float m_elapsedMs = 0.0f;
    bool m_playing = false;
    std::vector<bool> m_eventFired;
};

struct ActiveEffect
{
    std::string animationId;
    SmithingAnimationPlayer player;
};

class NailForgingController
{
public:
    bool LoadManifest(const std::filesystem::path& manifestPath);
    bool IsManifestLoaded() const { return m_manifest.has_value(); }
    const SmithingAnimationManifest* Manifest() const { return m_manifest ? &(*m_manifest) : nullptr; }

    void Reset();
    void SyncVisualState(NailAnimationPhase phase, int workpieceFrameIndex, float temperature01);
    void SetStation(SmithingStation station);
    void UpdateTemperature(float dtSeconds);
    void Update(float dtSeconds);
    bool RequestStrike(const StrikeInput& input);
    bool RequestQuarterTurn();
    bool RequestTongsWork();
    bool RequestQuench();
    void SetWaterContact(bool waterContact);

    const NailForgingWorkpieceState& State() const { return m_state; }
    const SmithingAnimationPlayer& HammerPlayer() const { return m_hammerPlayer; }
    const SmithingAnimationPlayer& TongsPlayer() const { return m_tongsPlayer; }
    const std::vector<ActiveEffect>& Effects() const { return m_effects; }
    const std::vector<FiredAnimationEvent>& RecentEvents() const { return m_recentEvents; }
    void ClearRecentEvents() { m_recentEvents.clear(); }
    std::optional<StrikeResult> LastCommittedStrike() const { return m_lastCommittedStrike; }
    void ClearLastCommittedStrike() { m_lastCommittedStrike.reset(); }

private:
    const AnimationDef* FindAnimation(const std::string& id) const;
    StrikeResult EvaluateStrike(const StrikeInput& input) const;
    void CommitPendingStrike();
    void HandleEvent(const FiredAnimationEvent& event);
    void AdvanceWorkpieceState();
    void UpdateCoolingFrame();
    void UpdateRotationRequirement();
    void PlayEffect(const std::string& id);
    void AddDefect(NailForgingDefect defect);

    std::optional<SmithingAnimationManifest> m_manifest;
    NailForgingWorkpieceState m_state;
    std::optional<StrikeResult> m_pendingStrike;
    std::optional<StrikeResult> m_lastCommittedStrike;
    bool m_tongsQuarterTurnPending = false;
    std::vector<FiredAnimationEvent> m_recentEvents;
    SmithingAnimationPlayer m_hammerPlayer;
    SmithingAnimationPlayer m_tongsPlayer;
    std::vector<ActiveEffect> m_effects;
};
}
