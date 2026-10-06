#include "NailForgingController.h"

#include <algorithm>
#include <cmath>

namespace smithing
{
namespace
{
float Clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

int DurationForFrame(const AnimationDef& animation, int frameIndex)
{
    if (frameIndex >= 0 && frameIndex < static_cast<int>(animation.frames.size()))
        return std::max(1, animation.frames[frameIndex].durationMs);
    return 1;
}

bool IsHammerPhase(NailAnimationPhase phase)
{
    return phase == NailAnimationPhase::Drawing ||
        phase == NailAnimationPhase::Pointing ||
        phase == NailAnimationPhase::Cutting ||
        phase == NailAnimationPhase::Heading;
}

bool IsCritical(NailForgingDefect defect)
{
    return defect == NailForgingDefect::NeckCrack ||
        defect == NailForgingDefect::BurnedIron ||
        defect == NailForgingDefect::OverdrawnShank;
}

NailAnimationPhase PhaseFromFramePhase(const std::string& phase)
{
    if (phase == "drawing")
        return NailAnimationPhase::Drawing;
    if (phase == "point")
        return NailAnimationPhase::Pointing;
    if (phase == "cutting")
        return NailAnimationPhase::Cutting;
    if (phase == "heading")
        return NailAnimationPhase::Heading;
    if (phase == "cooling")
        return NailAnimationPhase::Cooling;
    if (phase == "finished")
        return NailAnimationPhase::Finished;
    return NailAnimationPhase::Heating;
}

int PhaseHitLimit(NailAnimationPhase phase)
{
    switch (phase)
    {
    case NailAnimationPhase::Drawing: return 3;
    case NailAnimationPhase::Pointing: return 3;
    case NailAnimationPhase::Cutting: return 2;
    case NailAnimationPhase::Heading: return 5;
    default: return 0;
    }
}
}

const char* ToString(NailAnimationPhase phase)
{
    switch (phase)
    {
    case NailAnimationPhase::Material: return "material";
    case NailAnimationPhase::Heating: return "heating";
    case NailAnimationPhase::Drawing: return "drawing";
    case NailAnimationPhase::Pointing: return "pointing";
    case NailAnimationPhase::Cutting: return "cutting";
    case NailAnimationPhase::ReheatingForHeading: return "reheating_for_heading";
    case NailAnimationPhase::Heading: return "heading";
    case NailAnimationPhase::Cooling: return "cooling";
    case NailAnimationPhase::Inspecting: return "inspecting";
    case NailAnimationPhase::Finished: return "finished";
    case NailAnimationPhase::Ruined: return "ruined";
    }
    return "heating";
}

const char* ToString(NailForgingDefect defect)
{
    switch (defect)
    {
    case NailForgingDefect::None: return "none";
    case NailForgingDefect::CrookedShank: return "crooked_shank";
    case NailForgingDefect::BluntPoint: return "blunt_point";
    case NailForgingDefect::PointOffAxis: return "point_off_axis";
    case NailForgingDefect::UndersizedHead: return "undersized_head";
    case NailForgingDefect::HeadOffCenter: return "head_off_center";
    case NailForgingDefect::NeckCrack: return "neck_crack";
    case NailForgingDefect::BurnedIron: return "burned_iron";
    case NailForgingDefect::OverdrawnShank: return "overdrawn_shank";
    }
    return "none";
}

void SmithingAnimationPlayer::Play(const AnimationDef* animation)
{
    m_animation = animation;
    m_frameIndex = 0;
    m_elapsedMs = 0.0f;
    m_playing = animation != nullptr && animation->playback != AnimationPlayback::StateDriven;
    m_eventFired.assign(animation ? animation->events.size() : 0, false);
}

void SmithingAnimationPlayer::Stop()
{
    m_animation = nullptr;
    m_frameIndex = 0;
    m_elapsedMs = 0.0f;
    m_playing = false;
    m_eventFired.clear();
}

int SmithingAnimationPlayer::AtlasIndex() const
{
    if (!m_animation || m_animation->frames.empty())
        return 0;
    const int index = std::clamp(m_frameIndex, 0, static_cast<int>(m_animation->frames.size()) - 1);
    return m_animation->frames[index].atlasIndex;
}

void SmithingAnimationPlayer::Update(float dtSeconds, std::vector<FiredAnimationEvent>& outEvents)
{
    if (!m_animation || !m_playing || m_animation->frames.empty())
        return;

    m_elapsedMs += std::max(0.0f, dtSeconds) * 1000.0f;
    while (m_animation && m_playing && m_elapsedMs >= DurationForFrame(*m_animation, m_frameIndex))
    {
        m_elapsedMs -= static_cast<float>(DurationForFrame(*m_animation, m_frameIndex));
        const int previousFrame = m_frameIndex;
        ++m_frameIndex;

        if (m_frameIndex >= static_cast<int>(m_animation->frames.size()))
        {
            if (m_animation->playback == AnimationPlayback::OnceHoldLast)
            {
                m_frameIndex = static_cast<int>(m_animation->frames.size()) - 1;
                m_playing = false;
            }
            else
            {
                m_playing = false;
                m_frameIndex = static_cast<int>(m_animation->frames.size()) - 1;
            }
        }

        DispatchEventsCrossed(previousFrame, m_frameIndex, outEvents);
    }
}

void SmithingAnimationPlayer::DispatchEventsCrossed(
    int previousFrame,
    int nextFrame,
    std::vector<FiredAnimationEvent>& outEvents)
{
    if (!m_animation)
        return;

    for (std::size_t i = 0; i < m_animation->events.size(); ++i)
    {
        const AnimationEventDef& event = m_animation->events[i];
        if (m_eventFired[i])
            continue;
        if (event.frame > previousFrame && event.frame <= nextFrame)
        {
            m_eventFired[i] = true;
            outEvents.push_back(FiredAnimationEvent{m_animation->id, event.event});
        }
    }
}

bool NailForgingController::LoadManifest(const std::filesystem::path& manifestPath)
{
    m_manifest = LoadSmithingAnimationManifest(manifestPath, "nail");
    Reset();
    return m_manifest.has_value();
}

void NailForgingController::Reset()
{
    m_state = NailForgingWorkpieceState{};
    m_pendingStrike.reset();
    m_lastCommittedStrike.reset();
    m_tongsQuarterTurnPending = false;
    m_recentEvents.clear();
    m_hammerPlayer.Stop();
    m_tongsPlayer.Stop();
    m_effects.clear();
}

void NailForgingController::SyncVisualState(NailAnimationPhase phase, int workpieceFrameIndex, float temperature01)
{
    if (m_state.inputLocked || m_hammerPlayer.IsPlaying() || m_tongsPlayer.IsPlaying())
        return;

    m_state.phase = phase;
    m_state.workpieceFrameIndex = std::clamp(workpieceFrameIndex, 0, 15);
    m_state.temperature01 = Clamp01(temperature01);

    if (m_manifest && !m_manifest->workpieceFrames.empty())
    {
        const AnimationFrameDef& frame = m_manifest->workpieceFrames[std::clamp(
            m_state.workpieceFrameIndex,
            0,
            static_cast<int>(m_manifest->workpieceFrames.size()) - 1)];
        m_state.workpieceFrameId = frame.id;
    }
}

void NailForgingController::SetStation(SmithingStation station)
{
    m_state.station = station;
    if (station != SmithingStation::Quench)
        m_state.waterContact = false;
    if (m_state.phase == NailAnimationPhase::Material && station == SmithingStation::Forge)
        m_state.phase = NailAnimationPhase::Heating;
}

void NailForgingController::SetWaterContact(bool waterContact)
{
    m_state.waterContact = waterContact && m_state.station == SmithingStation::Quench;
}

void NailForgingController::UpdateTemperature(float dtSeconds)
{
    if (m_state.phase == NailAnimationPhase::Finished ||
        m_state.phase == NailAnimationPhase::Ruined)
    {
        return;
    }

    if (m_state.station == SmithingStation::Forge)
        m_state.temperature01 += 0.22f * dtSeconds;
    else if (m_state.station == SmithingStation::Quench && m_state.waterContact)
        m_state.temperature01 -= 0.32f * dtSeconds;
    else
        m_state.temperature01 -= 0.035f * dtSeconds;

    m_state.temperature01 = Clamp01(m_state.temperature01);

    if (m_state.temperature01 >= 0.96f)
        m_state.overheatedTimeSeconds += dtSeconds;
    else
        m_state.overheatedTimeSeconds = 0.0f;

    if (m_state.overheatedTimeSeconds >= 1.2f)
        AddDefect(NailForgingDefect::BurnedIron);

    if (m_state.phase == NailAnimationPhase::Heating && m_state.temperature01 >= 0.78f)
        m_state.phase = NailAnimationPhase::Drawing;

    if (m_state.phase == NailAnimationPhase::ReheatingForHeading && m_state.temperature01 >= 0.70f)
    {
        m_state.phase = NailAnimationPhase::Heading;
        m_state.requiresReheat = false;
    }

    if (m_state.phase == NailAnimationPhase::Cooling)
        UpdateCoolingFrame();
}

const AnimationDef* NailForgingController::FindAnimation(const std::string& id) const
{
    return m_manifest ? m_manifest->Find(id) : nullptr;
}

void NailForgingController::Update(float dtSeconds)
{
    std::vector<FiredAnimationEvent> events;
    m_hammerPlayer.Update(dtSeconds, events);
    m_tongsPlayer.Update(dtSeconds, events);
    for (ActiveEffect& effect : m_effects)
        effect.player.Update(dtSeconds, events);
    m_effects.erase(
        std::remove_if(m_effects.begin(), m_effects.end(),
            [](const ActiveEffect& effect) { return !effect.player.IsPlaying(); }),
        m_effects.end());

    m_recentEvents = events;
    for (const FiredAnimationEvent& event : events)
        HandleEvent(event);

    if (m_state.inputLocked && !m_hammerPlayer.IsPlaying() && !m_tongsPlayer.IsPlaying())
        m_state.inputLocked = false;

    UpdateTemperature(dtSeconds);
}

bool NailForgingController::RequestStrike(const StrikeInput& input)
{
    if (!m_manifest || m_state.inputLocked || m_hammerPlayer.IsPlaying() || !IsHammerPhase(m_state.phase))
        return false;
    if (m_state.requiresReheat || m_state.rotationRequired)
        return false;

    m_pendingStrike = EvaluateStrike(input);
    m_lastCommittedStrike.reset();
    m_state.inputLocked = true;
    m_hammerPlayer.Play(FindAnimation("hammer_strike_heavy"));
    return m_hammerPlayer.IsPlaying();
}

bool NailForgingController::RequestQuarterTurn()
{
    if (!m_manifest || m_state.inputLocked)
        return false;

    m_state.inputLocked = true;
    m_tongsQuarterTurnPending = true;
    m_tongsPlayer.Play(FindAnimation("tongs_rotate_quarter"));
    if (!m_tongsPlayer.IsPlaying())
    {
        m_tongsQuarterTurnPending = false;
        m_state.inputLocked = false;
        return false;
    }
    return true;
}

bool NailForgingController::RequestTongsWork()
{
    if (!m_manifest || m_state.inputLocked)
        return false;

    m_state.inputLocked = true;
    m_tongsQuarterTurnPending = false;
    m_tongsPlayer.Play(FindAnimation("tongs_rotate_quarter"));
    if (!m_tongsPlayer.IsPlaying())
    {
        m_state.inputLocked = false;
        return false;
    }
    return true;
}

bool NailForgingController::RequestQuench()
{
    if (m_state.phase != NailAnimationPhase::Cooling &&
        m_state.workpieceFrameIndex < 13)
    {
        return false;
    }

    m_state.station = SmithingStation::Quench;
    m_state.waterContact = true;

    if (m_state.temperature01 >= 0.35f && !m_state.quenchSteamPlayed)
    {
        PlayEffect("quench_steam");
        m_state.quenchSteamPlayed = true;
    }
    m_state.phase = NailAnimationPhase::Cooling;
    UpdateCoolingFrame();
    return true;
}

StrikeResult NailForgingController::EvaluateStrike(const StrikeInput& input) const
{
    StrikeResult result;
    result.contacted = input.targetX01 >= 0.0f && input.targetX01 <= 1.0f;
    if (!result.contacted)
        return result;

    float idealPosition = 0.50f;
    float minHeat = 0.55f;
    float maxHeat = 0.95f;
    float minPower = 0.45f;
    float maxPower = 0.80f;

    switch (m_state.phase)
    {
    case NailAnimationPhase::Pointing:
        idealPosition = 0.86f; minHeat = 0.50f; maxHeat = 0.88f; minPower = 0.25f; maxPower = 0.60f; break;
    case NailAnimationPhase::Cutting:
        idealPosition = 0.72f; minHeat = 0.45f; maxHeat = 0.85f; minPower = 0.50f; maxPower = 0.85f; break;
    case NailAnimationPhase::Heading:
        idealPosition = 0.12f; minHeat = 0.55f; maxHeat = 0.90f; minPower = 0.35f; maxPower = 0.70f; break;
    default:
        break;
    }

    const float positionAccuracy = Clamp01(1.0f - std::abs(input.targetX01 - idealPosition) * 3.2f);
    const float powerCenter = (minPower + maxPower) * 0.5f;
    const float powerRadius = (maxPower - minPower) * 0.5f;
    result.powerError = std::max(0.0f, std::abs(input.power01 - powerCenter) - powerRadius);
    result.temperatureError = m_state.temperature01 < minHeat
        ? minHeat - m_state.temperature01
        : std::max(0.0f, m_state.temperature01 - maxHeat);
    result.accuracy = Clamp01(positionAccuracy - result.powerError * 1.5f - result.temperatureError * 1.4f);
    result.appliesDeformation = result.accuracy >= 0.34f && m_state.temperature01 >= minHeat && input.power01 >= minPower;

    if (m_state.temperature01 < minHeat && input.power01 > 0.55f)
        result.producedDefect = NailForgingDefect::NeckCrack;
    else if (m_state.temperature01 > maxHeat + 0.08f)
        result.producedDefect = NailForgingDefect::BurnedIron;
    else if (m_state.phase == NailAnimationPhase::Drawing && input.power01 > 0.88f)
        result.producedDefect = NailForgingDefect::OverdrawnShank;
    else if (m_state.phase == NailAnimationPhase::Pointing && positionAccuracy < 0.55f)
        result.producedDefect = NailForgingDefect::PointOffAxis;
    else if (m_state.phase == NailAnimationPhase::Heading && positionAccuracy < 0.55f)
        result.producedDefect = NailForgingDefect::HeadOffCenter;
    else if (m_state.phase == NailAnimationPhase::Heading && input.power01 < minPower)
        result.producedDefect = NailForgingDefect::UndersizedHead;

    return result;
}

void NailForgingController::HandleEvent(const FiredAnimationEvent& event)
{
    if (event.animationId == "hammer_strike_heavy" && event.eventId == "impact")
    {
        CommitPendingStrike();
        return;
    }

    if (event.animationId == "tongs_rotate_quarter" && event.eventId == "rotation_complete")
    {
        if (m_tongsQuarterTurnPending)
        {
            m_state.quarterTurns = (m_state.quarterTurns + 1) % 4;
            m_state.hitsSinceRotation = 0;
            m_state.rotationRequired = false;
        }
        m_tongsQuarterTurnPending = false;
        m_state.inputLocked = false;
        return;
    }

    if (event.animationId == "tongs_rotate_quarter" && event.eventId == "grip_closed")
    {
        m_state.heldByTongs = true;
    }
}

void NailForgingController::CommitPendingStrike()
{
    if (!m_pendingStrike)
        return;

    const StrikeResult result = *m_pendingStrike;
    m_pendingStrike.reset();
    m_lastCommittedStrike = result;

    if (!result.contacted)
    {
        ++m_state.failedHits;
        return;
    }

    ++m_state.totalHits;
    PlayEffect(m_state.temperature01 >= 0.72f ? "sparks_heavy" : "sparks_small");
    PlayEffect("scale_flakes");

    if (result.producedDefect != NailForgingDefect::None)
        AddDefect(result.producedDefect);

    if (result.appliesDeformation)
    {
        ++m_state.successfulHits;
        ++m_state.phaseHits;
        ++m_state.hitsSinceRotation;
        AdvanceWorkpieceState();
        UpdateRotationRequirement();
    }
    else
    {
        ++m_state.failedHits;
    }

    m_state.temperature01 = Clamp01(m_state.temperature01 - (0.018f + 0.018f * result.accuracy));
}

void NailForgingController::AdvanceWorkpieceState()
{
    if (!m_manifest || m_manifest->workpieceFrames.empty() || m_state.workpieceFrameIndex >= 13)
        return;

    const NailAnimationPhase previousPhase = m_state.phase;
    ++m_state.workpieceFrameIndex;
    const AnimationFrameDef& frame = m_manifest->workpieceFrames[std::clamp(
        m_state.workpieceFrameIndex,
        0,
        static_cast<int>(m_manifest->workpieceFrames.size()) - 1)];

    m_state.workpieceFrameId = frame.id;
    m_state.phase = PhaseFromFramePhase(frame.phase);
    if (m_state.phase != previousPhase)
        m_state.phaseHits = 0;

    if (frame.id == "blank_cut_hot" && m_state.temperature01 < 0.62f)
    {
        m_state.phase = NailAnimationPhase::ReheatingForHeading;
        m_state.requiresReheat = true;
    }

    if (frame.id == "nail_finished_hot")
    {
        m_state.phase = NailAnimationPhase::Cooling;
        m_state.requiresReheat = false;
    }
}

void NailForgingController::UpdateCoolingFrame()
{
    if (m_state.temperature01 <= 0.08f)
    {
        m_state.workpieceFrameIndex = 15;
        m_state.workpieceFrameId = "nail_finished_cold";
        m_state.phase = NailAnimationPhase::Inspecting;
        return;
    }

    if (m_state.temperature01 <= 0.30f)
    {
        m_state.workpieceFrameIndex = 14;
        m_state.workpieceFrameId = "nail_cooling_red";
        return;
    }

    m_state.workpieceFrameIndex = 13;
    m_state.workpieceFrameId = "nail_finished_hot";
}

void NailForgingController::UpdateRotationRequirement()
{
    const int limit = PhaseHitLimit(m_state.phase);
    if (limit <= 0)
        return;

    const bool shouldRotate = 
        (m_state.phase == NailAnimationPhase::Drawing && (m_state.phaseHits == 1 || m_state.phaseHits == 2)) ||
        (m_state.phase == NailAnimationPhase::Pointing && (m_state.phaseHits == 1 || m_state.phaseHits == 2)) ||
        (m_state.phase == NailAnimationPhase::Heading && (m_state.phaseHits == 2 || m_state.phaseHits == 4));
    m_state.rotationRequired = shouldRotate && m_state.phaseHits < limit;
}

void NailForgingController::PlayEffect(const std::string& id)
{
    if (const AnimationDef* animation = FindAnimation(id))
    {
        ActiveEffect effect;
        effect.animationId = id;
        effect.player.Play(animation);
        if (effect.player.IsPlaying())
            m_effects.push_back(std::move(effect));
    }
}

void NailForgingController::AddDefect(NailForgingDefect defect)
{
    if (defect == NailForgingDefect::None)
        return;
    if (std::find(m_state.defects.begin(), m_state.defects.end(), defect) == m_state.defects.end())
        m_state.defects.push_back(defect);
    if (IsCritical(defect))
        m_state.phase = NailAnimationPhase::Ruined;
}
}
