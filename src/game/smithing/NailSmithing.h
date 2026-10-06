#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace smithing
{
enum class NailForgePhase
{
    RawMaterial,
    Heating,
    DrawingShank,
    ForgingPoint,
    CuttingBlank,
    FormingHead,
    Inspecting,
    Finished
};

enum class NailDefectId
{
    CrookedShank,
    BluntPoint,
    PointOffAxis,
    UndersizedHead,
    HeadOffCenter,
    NeckCrack,
    BurnedIron,
    OverdrawnShank
};

enum class NailQualityGrade
{
    Rejected,
    Provisional,
    Common,
    Honest,
    Excellent
};

enum class NailHintLevel
{
    ActiveGuidance,
    VerbalWarning,
    Independent
};

struct NailDefect
{
    NailDefectId id = NailDefectId::CrookedShank;
    float severity = 0.0f;
    bool critical = false;
    bool forced = false;
};

struct NailDefectInfo
{
    NailDefectId id = NailDefectId::CrookedShank;
    const char* technicalId = "crooked_shank";
    const char* nameCs = "";
    const char* descriptionCs = "";
    const char* imagePath = "";
};

struct NailShankSegmentState
{
    float thickness = 1.0f;
    float lateralOffset = 0.0f;
    int strikeCount = 0;
};

struct NailWorkpieceState
{
    std::string instanceId;
    NailForgePhase phase = NailForgePhase::RawMaterial;

    float heat = 0.0f;
    float heatUniformity = 1.0f;

    float straightness = 1.0f;
    float pointSharpness = 0.0f;
    float pointAlignment = 1.0f;

    float headSize = 0.0f;
    float headAlignment = 1.0f;

    float shankThickness = 1.0f;
    float materialIntegrity = 1.0f;

    int hammerStrikeCount = 0;

    std::vector<NailDefect> defects;

    bool cutFromStock = false;
    bool finished = false;

    std::array<NailShankSegmentState, 5> shankSegments{};
    float overheatExposure = 0.0f;
    float coldHammerStress = 0.0f;
    float drawingWork = 0.0f;
    float pointWork = 0.0f;
    float headWork = 0.0f;
    int rotationQuarterTurns = 0;
    int lastStrikeRotation = -1;
    int lastUpdatedGameMinute = 0;
};

struct NailEvaluationResult
{
    NailQualityGrade grade = NailQualityGrade::Rejected;
    std::optional<NailDefect> dominantDefect;
    bool accepted = false;
    std::string feedbackKey;
};

struct NailTrainingBatchState
{
    int targetCount = 10;
    int activeIndex = 0;

    std::vector<NailWorkpieceState> nails;

    int acceptedCount = 0;
    int rejectedCount = 0;
    std::vector<NailDefectId> discoveredDefects;

    bool completed = false;
    bool lessonStarted = false;
    int minimumAcceptedCount = 6;
    int lastGameMinute = 0;
};

struct NailHammerStrike
{
    float position = 0.5f;
    float force = 0.55f;
    int rotationQuarterTurns = 0;
};

struct NailActionResult
{
    bool changed = false;
    std::string messageKey;
    std::optional<NailEvaluationResult> evaluation;
};

const char* ToString(NailForgePhase phase);
const char* ToString(NailDefectId defect);
const char* ToString(NailQualityGrade grade);
const std::array<NailDefectInfo, 8>& AllNailDefectInfos();
const NailDefectInfo* FindNailDefectInfo(NailDefectId defect);
const NailDefectInfo* FindNailDefectInfo(const std::string& technicalId);
NailForgePhase NailForgePhaseFromString(const std::string& value, NailForgePhase fallback = NailForgePhase::RawMaterial);
NailDefectId NailDefectIdFromString(const std::string& value, NailDefectId fallback = NailDefectId::CrookedShank);
NailQualityGrade NailQualityGradeFromString(const std::string& value, NailQualityGrade fallback = NailQualityGrade::Rejected);

bool IsCriticalDefect(NailDefectId defect);
bool HasDefect(const NailWorkpieceState& workpiece, NailDefectId defect);

NailWorkpieceState MakeNailWorkpiece(int index, int currentGameMinute);
void StartNailTraining(NailTrainingBatchState& batch, int currentGameMinute);
NailWorkpieceState* ActiveNail(NailTrainingBatchState& batch);
const NailWorkpieceState* ActiveNail(const NailTrainingBatchState& batch);
NailHintLevel HintLevelForActiveNail(const NailTrainingBatchState& batch);

void AdvanceNailTrainingTime(NailTrainingBatchState& batch, int currentGameMinute);
void CoolWorkpiece(NailWorkpieceState& workpiece, int elapsedGameMinutes);

NailActionResult HeatNail(NailTrainingBatchState& batch, int currentGameMinute, bool bellows, bool rotateWorkpiece);
NailActionResult AdvancePhase(NailTrainingBatchState& batch, int currentGameMinute);
NailActionResult ApplyHammerStrike(NailTrainingBatchState& batch, const NailHammerStrike& strike, int currentGameMinute);
NailActionResult FinishActiveNail(NailTrainingBatchState& batch, int currentGameMinute);

void ForceDefect(NailWorkpieceState& workpiece, NailDefectId defect);
void RecomputeNailDerivedState(NailWorkpieceState& workpiece, bool finalPass);
NailEvaluationResult EvaluateNail(const NailWorkpieceState& workpiece);

nlohmann::json SaveNailTrainingBatch(const NailTrainingBatchState& batch);
void LoadNailTrainingBatch(const nlohmann::json& source, NailTrainingBatchState& outBatch, int currentGameMinute);
}
