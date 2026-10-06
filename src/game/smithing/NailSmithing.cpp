#include "NailSmithing.h"

#include <algorithm>
#include <cmath>
#include <limits>

using json = nlohmann::json;

namespace smithing
{
namespace
{
float Clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float ClampAbs(float value, float limit)
{
    return std::clamp(value, -limit, limit);
}

int NormalizeRotation(int quarterTurns)
{
    int value = quarterTurns % 4;
    if (value < 0)
        value += 4;
    return value;
}

float RotationBendSign(int quarterTurns)
{
    switch (NormalizeRotation(quarterTurns))
    {
    case 0: return 1.0f;
    case 1: return 0.45f;
    case 2: return -1.0f;
    case 3: return -0.45f;
    }
    return 1.0f;
}

float WorkabilityForHeat(float heat)
{
    if (heat < 0.18f)
        return 0.14f;
    if (heat < 0.42f)
        return 0.35f + heat * 0.55f;
    if (heat <= 0.82f)
        return 1.0f;
    if (heat <= 0.94f)
        return 0.75f;
    return 0.38f;
}

float DefectSeverityFloor(NailDefectId id)
{
    switch (id)
    {
    case NailDefectId::BluntPoint: return 0.44f;
    case NailDefectId::CrookedShank:
    case NailDefectId::PointOffAxis:
    case NailDefectId::UndersizedHead:
    case NailDefectId::HeadOffCenter: return 0.62f;
    case NailDefectId::NeckCrack:
    case NailDefectId::BurnedIron:
    case NailDefectId::OverdrawnShank: return 0.92f;
    }
    return 0.5f;
}

const std::array<NailDefectInfo, 8> kNailDefectInfos{{
    {
        NailDefectId::CrookedShank,
        "crooked_shank",
        "Křivý dřík",
        "Dřík hřebíku je po nerovnoměrných úderech ohnutý mimo osu. Při zatloukání se stáčí do strany a špatně drží směr.",
        "assets/smithing/minigames/assets/overlays/defects_nail/frames/00_crooked_shank.png"
    },
    {
        NailDefectId::BluntPoint,
        "blunt_point",
        "Tupá špička",
        "Špička zůstala krátká a málo vytažená. Hřebík se do dřeva špatně zakusuje a vyžaduje zbytečně silné údery.",
        "assets/smithing/minigames/assets/overlays/defects_nail/frames/01_blunt_point.png"
    },
    {
        NailDefectId::PointOffAxis,
        "point_off_axis",
        "Špička mimo osu",
        "Špička uhýbá od středové osy dříku. Při práci vede hřebík stranou a může rozštípnout dřevo.",
        "assets/smithing/minigames/assets/overlays/defects_nail/frames/02_point_off_axis.png"
    },
    {
        NailDefectId::UndersizedHead,
        "undersized_head",
        "Malá hlava",
        "Hlava nebyla dostatečně rozkovaná. Hřebík se může protáhnout spojovaným materiálem nebo nejde pevně přidržet.",
        "assets/smithing/minigames/assets/overlays/defects_nail/frames/03_undersized_head.png"
    },
    {
        NailDefectId::HeadOffCenter,
        "head_off_center",
        "Hlava mimo střed",
        "Hlava sedí stranou vůči dříku. Úder kladiva pak hřebík vychyluje a spoj se hůř dotahuje.",
        "assets/smithing/minigames/assets/overlays/defects_nail/frames/04_head_off_center.png"
    },
    {
        NailDefectId::NeckCrack,
        "neck_crack",
        "Trhlina u krčku",
        "U přechodu mezi hlavou a dříkem vznikla trhlina, obvykle po silném úderu za příliš nízké teploty. Hřebík se může ulomit.",
        "assets/smithing/minigames/assets/overlays/defects_nail/frames/05_neck_crack.png"
    },
    {
        NailDefectId::BurnedIron,
        "burned_iron",
        "Přepálené železo",
        "Železo bylo drženo v příliš vysokém žáru. Povrch je okujený, zrnitý a materiál ztrácí pevnost.",
        "assets/smithing/minigames/assets/overlays/defects_nail/frames/06_burned_iron.png"
    },
    {
        NailDefectId::OverdrawnShank,
        "overdrawn_shank",
        "Přetažený dřík",
        "Dřík je příliš vytažený a zeslabený. Hřebík se snadno ohne nebo praskne, i když na první pohled působí dlouze.",
        "assets/smithing/minigames/assets/overlays/defects_nail/frames/07_overdrawn_shank.png"
    }
}};

void AddOrUpdateDefect(NailWorkpieceState& workpiece, NailDefectId id, float severity, bool forced)
{
    severity = Clamp01(std::max(severity, DefectSeverityFloor(id)));
    const bool critical = IsCriticalDefect(id);

    for (NailDefect& defect : workpiece.defects)
    {
        if (defect.id != id)
            continue;

        defect.severity = std::max(defect.severity, severity);
        defect.critical = defect.critical || critical;
        defect.forced = defect.forced || forced;
        return;
    }

    workpiece.defects.push_back(NailDefect{id, severity, critical, forced});
}

void AddDiscoveredDefect(NailTrainingBatchState& batch, NailDefectId id)
{
    if (std::find(batch.discoveredDefects.begin(), batch.discoveredDefects.end(), id) == batch.discoveredDefects.end())
        batch.discoveredDefects.push_back(id);
}

bool PhaseAtLeast(NailForgePhase value, NailForgePhase threshold)
{
    return static_cast<int>(value) >= static_cast<int>(threshold);
}

void RemoveDerivedDefects(NailWorkpieceState& workpiece)
{
    workpiece.defects.erase(
        std::remove_if(workpiece.defects.begin(), workpiece.defects.end(),
            [](const NailDefect& defect) { return !defect.forced; }),
        workpiece.defects.end());
}

void RecomputeShapeFromSegments(NailWorkpieceState& workpiece)
{
    float thicknessTotal = 0.0f;
    float offsetTotal = 0.0f;
    float maxOffset = 0.0f;

    for (const NailShankSegmentState& segment : workpiece.shankSegments)
    {
        thicknessTotal += segment.thickness;
        const float absOffset = std::abs(segment.lateralOffset);
        offsetTotal += absOffset;
        maxOffset = std::max(maxOffset, absOffset);
    }

    const float segmentCount = static_cast<float>(workpiece.shankSegments.size());
    workpiece.shankThickness = Clamp01(thicknessTotal / segmentCount);
    const float averageOffset = offsetTotal / segmentCount;
    workpiece.straightness = Clamp01(1.0f - averageOffset * 7.5f - maxOffset * 2.0f);
}

NailEvaluationResult ResultWithFeedback(const NailWorkpieceState& workpiece, NailQualityGrade grade)
{
    NailEvaluationResult result;
    result.grade = grade;
    result.accepted = grade != NailQualityGrade::Rejected;

    const NailDefect* dominant = nullptr;
    for (const NailDefect& defect : workpiece.defects)
    {
        if (!dominant ||
            (defect.critical && !dominant->critical) ||
            (defect.critical == dominant->critical && defect.severity > dominant->severity))
        {
            dominant = &defect;
        }
    }

    if (dominant)
    {
        result.dominantDefect = *dominant;
        result.feedbackKey = ToString(dominant->id);
        return result;
    }

    switch (grade)
    {
    case NailQualityGrade::Excellent: result.feedbackKey = "excellent"; break;
    case NailQualityGrade::Honest: result.feedbackKey = "honest"; break;
    case NailQualityGrade::Common: result.feedbackKey = "common"; break;
    case NailQualityGrade::Provisional: result.feedbackKey = "provisional"; break;
    case NailQualityGrade::Rejected: result.feedbackKey = "rejected"; break;
    }
    return result;
}

json SaveDefect(const NailDefect& defect)
{
    return json{
        {"id", ToString(defect.id)},
        {"severity", defect.severity},
        {"critical", defect.critical},
        {"forced", defect.forced}
    };
}

NailDefect LoadDefect(const json& source)
{
    NailDefect defect;
    if (!source.is_object())
        return defect;

    defect.id = NailDefectIdFromString(source.value("id", std::string()));
    defect.severity = Clamp01(source.value("severity", DefectSeverityFloor(defect.id)));
    defect.critical = source.value("critical", IsCriticalDefect(defect.id));
    defect.forced = source.value("forced", false);
    return defect;
}

json SaveSegment(const NailShankSegmentState& segment)
{
    return json{
        {"thickness", segment.thickness},
        {"lateral_offset", segment.lateralOffset},
        {"strike_count", segment.strikeCount}
    };
}

NailShankSegmentState LoadSegment(const json& source)
{
    NailShankSegmentState segment;
    if (!source.is_object())
        return segment;

    segment.thickness = Clamp01(source.value("thickness", segment.thickness));
    segment.lateralOffset = ClampAbs(source.value("lateral_offset", segment.lateralOffset), 1.0f);
    segment.strikeCount = std::max(0, source.value("strike_count", segment.strikeCount));
    return segment;
}

json SaveWorkpiece(const NailWorkpieceState& workpiece)
{
    json defects = json::array();
    for (const NailDefect& defect : workpiece.defects)
        defects.push_back(SaveDefect(defect));

    json segments = json::array();
    for (const NailShankSegmentState& segment : workpiece.shankSegments)
        segments.push_back(SaveSegment(segment));

    return json{
        {"instance_id", workpiece.instanceId},
        {"phase", ToString(workpiece.phase)},
        {"heat", workpiece.heat},
        {"heat_uniformity", workpiece.heatUniformity},
        {"straightness", workpiece.straightness},
        {"point_sharpness", workpiece.pointSharpness},
        {"point_alignment", workpiece.pointAlignment},
        {"head_size", workpiece.headSize},
        {"head_alignment", workpiece.headAlignment},
        {"shank_thickness", workpiece.shankThickness},
        {"material_integrity", workpiece.materialIntegrity},
        {"hammer_strike_count", workpiece.hammerStrikeCount},
        {"defects", defects},
        {"cut_from_stock", workpiece.cutFromStock},
        {"finished", workpiece.finished},
        {"shank_segments", segments},
        {"overheat_exposure", workpiece.overheatExposure},
        {"cold_hammer_stress", workpiece.coldHammerStress},
        {"drawing_work", workpiece.drawingWork},
        {"point_work", workpiece.pointWork},
        {"head_work", workpiece.headWork},
        {"rotation_quarter_turns", workpiece.rotationQuarterTurns},
        {"last_strike_rotation", workpiece.lastStrikeRotation},
        {"last_updated_game_minute", workpiece.lastUpdatedGameMinute}
    };
}

NailWorkpieceState LoadWorkpiece(const json& source, int index, int currentGameMinute)
{
    NailWorkpieceState workpiece = MakeNailWorkpiece(index, currentGameMinute);
    if (!source.is_object())
        return workpiece;

    workpiece.instanceId = source.value("instance_id", workpiece.instanceId);
    workpiece.phase = NailForgePhaseFromString(source.value("phase", std::string()), workpiece.phase);
    workpiece.heat = Clamp01(source.value("heat", workpiece.heat));
    workpiece.heatUniformity = Clamp01(source.value("heat_uniformity", workpiece.heatUniformity));
    workpiece.straightness = Clamp01(source.value("straightness", workpiece.straightness));
    workpiece.pointSharpness = Clamp01(source.value("point_sharpness", workpiece.pointSharpness));
    workpiece.pointAlignment = Clamp01(source.value("point_alignment", workpiece.pointAlignment));
    workpiece.headSize = Clamp01(source.value("head_size", workpiece.headSize));
    workpiece.headAlignment = Clamp01(source.value("head_alignment", workpiece.headAlignment));
    workpiece.shankThickness = Clamp01(source.value("shank_thickness", workpiece.shankThickness));
    workpiece.materialIntegrity = Clamp01(source.value("material_integrity", workpiece.materialIntegrity));
    workpiece.hammerStrikeCount = std::max(0, source.value("hammer_strike_count", workpiece.hammerStrikeCount));
    workpiece.cutFromStock = source.value("cut_from_stock", workpiece.cutFromStock);
    workpiece.finished = source.value("finished", workpiece.finished);
    workpiece.overheatExposure = Clamp01(source.value("overheat_exposure", workpiece.overheatExposure));
    workpiece.coldHammerStress = Clamp01(source.value("cold_hammer_stress", workpiece.coldHammerStress));
    workpiece.drawingWork = std::max(0.0f, source.value("drawing_work", workpiece.drawingWork));
    workpiece.pointWork = std::max(0.0f, source.value("point_work", workpiece.pointWork));
    workpiece.headWork = std::max(0.0f, source.value("head_work", workpiece.headWork));
    workpiece.rotationQuarterTurns = NormalizeRotation(source.value("rotation_quarter_turns", workpiece.rotationQuarterTurns));
    workpiece.lastStrikeRotation = source.value("last_strike_rotation", workpiece.lastStrikeRotation);
    workpiece.lastUpdatedGameMinute = source.value("last_updated_game_minute", workpiece.lastUpdatedGameMinute);

    if (source.contains("shank_segments") && source["shank_segments"].is_array())
    {
        const json& segments = source["shank_segments"];
        for (std::size_t i = 0; i < workpiece.shankSegments.size() && i < segments.size(); ++i)
            workpiece.shankSegments[i] = LoadSegment(segments[i]);
    }

    workpiece.defects.clear();
    if (source.contains("defects") && source["defects"].is_array())
    {
        for (const json& item : source["defects"])
            workpiece.defects.push_back(LoadDefect(item));
    }

    RecomputeShapeFromSegments(workpiece);
    RecomputeNailDerivedState(workpiece, workpiece.finished || workpiece.phase == NailForgePhase::Finished);
    return workpiece;
}
}

const char* ToString(NailForgePhase phase)
{
    switch (phase)
    {
    case NailForgePhase::RawMaterial: return "raw_material";
    case NailForgePhase::Heating: return "heating";
    case NailForgePhase::DrawingShank: return "drawing_shank";
    case NailForgePhase::ForgingPoint: return "forging_point";
    case NailForgePhase::CuttingBlank: return "cutting_blank";
    case NailForgePhase::FormingHead: return "forming_head";
    case NailForgePhase::Inspecting: return "inspecting";
    case NailForgePhase::Finished: return "finished";
    }
    return "raw_material";
}

const char* ToString(NailDefectId defect)
{
    switch (defect)
    {
    case NailDefectId::CrookedShank: return "crooked_shank";
    case NailDefectId::BluntPoint: return "blunt_point";
    case NailDefectId::PointOffAxis: return "point_off_axis";
    case NailDefectId::UndersizedHead: return "undersized_head";
    case NailDefectId::HeadOffCenter: return "head_off_center";
    case NailDefectId::NeckCrack: return "neck_crack";
    case NailDefectId::BurnedIron: return "burned_iron";
    case NailDefectId::OverdrawnShank: return "overdrawn_shank";
    }
    return "crooked_shank";
}

const char* ToString(NailQualityGrade grade)
{
    switch (grade)
    {
    case NailQualityGrade::Rejected: return "rejected";
    case NailQualityGrade::Provisional: return "provisional";
    case NailQualityGrade::Common: return "common";
    case NailQualityGrade::Honest: return "honest";
    case NailQualityGrade::Excellent: return "excellent";
    }
    return "rejected";
}

const std::array<NailDefectInfo, 8>& AllNailDefectInfos()
{
    return kNailDefectInfos;
}

const NailDefectInfo* FindNailDefectInfo(NailDefectId defect)
{
    const auto& infos = AllNailDefectInfos();
    const auto it = std::find_if(infos.begin(), infos.end(),
        [defect](const NailDefectInfo& info) { return info.id == defect; });
    return it != infos.end() ? &(*it) : nullptr;
}

const NailDefectInfo* FindNailDefectInfo(const std::string& technicalId)
{
    const auto& infos = AllNailDefectInfos();
    const auto it = std::find_if(infos.begin(), infos.end(),
        [&technicalId](const NailDefectInfo& info) { return technicalId == info.technicalId; });
    return it != infos.end() ? &(*it) : nullptr;
}

NailForgePhase NailForgePhaseFromString(const std::string& value, NailForgePhase fallback)
{
    if (value == "raw_material") return NailForgePhase::RawMaterial;
    if (value == "heating") return NailForgePhase::Heating;
    if (value == "drawing_shank") return NailForgePhase::DrawingShank;
    if (value == "forging_point") return NailForgePhase::ForgingPoint;
    if (value == "cutting_blank") return NailForgePhase::CuttingBlank;
    if (value == "forming_head") return NailForgePhase::FormingHead;
    if (value == "inspecting") return NailForgePhase::Inspecting;
    if (value == "finished") return NailForgePhase::Finished;
    return fallback;
}

NailDefectId NailDefectIdFromString(const std::string& value, NailDefectId fallback)
{
    if (value == "crooked_shank") return NailDefectId::CrookedShank;
    if (value == "blunt_point") return NailDefectId::BluntPoint;
    if (value == "point_off_axis") return NailDefectId::PointOffAxis;
    if (value == "undersized_head") return NailDefectId::UndersizedHead;
    if (value == "head_off_center") return NailDefectId::HeadOffCenter;
    if (value == "neck_crack") return NailDefectId::NeckCrack;
    if (value == "burned_iron") return NailDefectId::BurnedIron;
    if (value == "overdrawn_shank") return NailDefectId::OverdrawnShank;
    return fallback;
}

NailQualityGrade NailQualityGradeFromString(const std::string& value, NailQualityGrade fallback)
{
    if (value == "rejected") return NailQualityGrade::Rejected;
    if (value == "provisional") return NailQualityGrade::Provisional;
    if (value == "common") return NailQualityGrade::Common;
    if (value == "honest") return NailQualityGrade::Honest;
    if (value == "excellent") return NailQualityGrade::Excellent;
    return fallback;
}

bool IsCriticalDefect(NailDefectId defect)
{
    return defect == NailDefectId::NeckCrack ||
        defect == NailDefectId::BurnedIron ||
        defect == NailDefectId::OverdrawnShank;
}

bool HasDefect(const NailWorkpieceState& workpiece, NailDefectId defect)
{
    return std::any_of(workpiece.defects.begin(), workpiece.defects.end(),
        [defect](const NailDefect& existing) { return existing.id == defect; });
}

NailWorkpieceState MakeNailWorkpiece(int index, int currentGameMinute)
{
    NailWorkpieceState workpiece;
    workpiece.instanceId = "benes_nail_" + std::to_string(index + 1);
    workpiece.lastUpdatedGameMinute = currentGameMinute;
    for (NailShankSegmentState& segment : workpiece.shankSegments)
        segment = NailShankSegmentState{};
    return workpiece;
}

void StartNailTraining(NailTrainingBatchState& batch, int currentGameMinute)
{
    if (batch.lessonStarted)
        return;

    batch = NailTrainingBatchState{};
    batch.lessonStarted = true;
    batch.lastGameMinute = currentGameMinute;
    batch.nails.push_back(MakeNailWorkpiece(0, currentGameMinute));
}

NailWorkpieceState* ActiveNail(NailTrainingBatchState& batch)
{
    if (!batch.lessonStarted || batch.completed)
        return nullptr;
    if (batch.activeIndex < 0 || batch.activeIndex >= static_cast<int>(batch.nails.size()))
        return nullptr;
    return &batch.nails[batch.activeIndex];
}

const NailWorkpieceState* ActiveNail(const NailTrainingBatchState& batch)
{
    if (!batch.lessonStarted || batch.completed)
        return nullptr;
    if (batch.activeIndex < 0 || batch.activeIndex >= static_cast<int>(batch.nails.size()))
        return nullptr;
    return &batch.nails[batch.activeIndex];
}

NailHintLevel HintLevelForActiveNail(const NailTrainingBatchState& batch)
{
    if (batch.activeIndex < 3)
        return NailHintLevel::ActiveGuidance;
    if (batch.activeIndex < 7)
        return NailHintLevel::VerbalWarning;
    return NailHintLevel::Independent;
}

void CoolWorkpiece(NailWorkpieceState& workpiece, int elapsedGameMinutes)
{
    if (elapsedGameMinutes <= 0 || workpiece.finished)
        return;

    const float elapsed = static_cast<float>(elapsedGameMinutes);
    workpiece.heat = Clamp01(workpiece.heat - elapsed * 0.038f);
    if (workpiece.heat < 0.12f)
        workpiece.heat = 0.0f;
    workpiece.heatUniformity = Clamp01(workpiece.heatUniformity - elapsed * 0.006f);
    workpiece.lastUpdatedGameMinute += elapsedGameMinutes;
}

void AdvanceNailTrainingTime(NailTrainingBatchState& batch, int currentGameMinute)
{
    if (!batch.lessonStarted)
    {
        batch.lastGameMinute = currentGameMinute;
        return;
    }

    const int elapsed = std::max(0, currentGameMinute - batch.lastGameMinute);
    if (elapsed > 0)
    {
        if (NailWorkpieceState* workpiece = ActiveNail(batch))
            CoolWorkpiece(*workpiece, elapsed);
    }
    batch.lastGameMinute = std::max(batch.lastGameMinute, currentGameMinute);
}

NailActionResult HeatNail(NailTrainingBatchState& batch, int currentGameMinute, bool bellows, bool rotateWorkpiece)
{
    StartNailTraining(batch, currentGameMinute);
    AdvanceNailTrainingTime(batch, currentGameMinute);

    NailWorkpieceState* workpiece = ActiveNail(batch);
    if (!workpiece)
        return {false, "lesson_complete", std::nullopt};

    if (workpiece->phase == NailForgePhase::Finished || workpiece->phase == NailForgePhase::Inspecting)
        return {false, "cannot_heat_finished", std::nullopt};

    if (workpiece->phase == NailForgePhase::RawMaterial)
        workpiece->phase = NailForgePhase::Heating;

    if (rotateWorkpiece)
    {
        workpiece->rotationQuarterTurns = NormalizeRotation(workpiece->rotationQuarterTurns + 1);
        workpiece->heatUniformity = Clamp01(workpiece->heatUniformity + 0.055f);
    }
    else
    {
        workpiece->heatUniformity = Clamp01(workpiece->heatUniformity - 0.035f);
    }

    const float uniformityPenalty = 1.0f - (1.0f - workpiece->heatUniformity) * 0.25f;
    const float delta = (bellows ? 0.165f : 0.105f) * uniformityPenalty;
    workpiece->heat = Clamp01(workpiece->heat + delta);

    if (workpiece->heat > 0.86f)
    {
        workpiece->overheatExposure = Clamp01(
            workpiece->overheatExposure + (workpiece->heat - 0.86f) * (bellows ? 1.35f : 0.75f));
        workpiece->materialIntegrity = Clamp01(workpiece->materialIntegrity - (workpiece->heat - 0.86f) * 0.055f);
    }

    RecomputeNailDerivedState(*workpiece, false);
    return {true, bellows ? "heated_with_bellows" : "heated", std::nullopt};
}

NailActionResult AdvancePhase(NailTrainingBatchState& batch, int currentGameMinute)
{
    StartNailTraining(batch, currentGameMinute);
    AdvanceNailTrainingTime(batch, currentGameMinute);

    NailWorkpieceState* workpiece = ActiveNail(batch);
    if (!workpiece)
        return {false, "lesson_complete", std::nullopt};

    switch (workpiece->phase)
    {
    case NailForgePhase::RawMaterial:
        workpiece->phase = NailForgePhase::Heating;
        return {true, "begin_heating", std::nullopt};

    case NailForgePhase::Heating:
        if (workpiece->heat < 0.38f)
            return {false, "too_cold_for_anvil", std::nullopt};
        workpiece->phase = NailForgePhase::DrawingShank;
        return {true, "begin_drawing", std::nullopt};

    case NailForgePhase::DrawingShank:
        workpiece->phase = NailForgePhase::ForgingPoint;
        RecomputeNailDerivedState(*workpiece, false);
        return {true, "begin_point", std::nullopt};

    case NailForgePhase::ForgingPoint:
        workpiece->phase = NailForgePhase::CuttingBlank;
        workpiece->cutFromStock = true;
        workpiece->heat = Clamp01(workpiece->heat - 0.05f);
        RecomputeNailDerivedState(*workpiece, false);
        return {true, "cut_blank", std::nullopt};

    case NailForgePhase::CuttingBlank:
        workpiece->phase = NailForgePhase::FormingHead;
        return {true, "begin_head", std::nullopt};

    case NailForgePhase::FormingHead:
        workpiece->phase = NailForgePhase::Inspecting;
        RecomputeNailDerivedState(*workpiece, true);
        return {true, "begin_inspection", std::nullopt};

    case NailForgePhase::Inspecting:
        return FinishActiveNail(batch, currentGameMinute);

    case NailForgePhase::Finished:
        return {false, "already_finished", std::nullopt};
    }

    return {false, "no_action", std::nullopt};
}

NailActionResult ApplyHammerStrike(NailTrainingBatchState& batch, const NailHammerStrike& inputStrike, int currentGameMinute)
{
    StartNailTraining(batch, currentGameMinute);
    AdvanceNailTrainingTime(batch, currentGameMinute);

    NailWorkpieceState* workpiece = ActiveNail(batch);
    if (!workpiece)
        return {false, "lesson_complete", std::nullopt};

    if (workpiece->phase != NailForgePhase::DrawingShank &&
        workpiece->phase != NailForgePhase::ForgingPoint &&
        workpiece->phase != NailForgePhase::FormingHead)
    {
        return {false, "not_on_anvil", std::nullopt};
    }

    NailHammerStrike strike;
    strike.position = Clamp01(inputStrike.position);
    strike.force = Clamp01(inputStrike.force);
    strike.rotationQuarterTurns = NormalizeRotation(inputStrike.rotationQuarterTurns);

    const float workability = WorkabilityForHeat(workpiece->heat);
    const bool sameRotation = workpiece->lastStrikeRotation == strike.rotationQuarterTurns;
    const float sideSign = RotationBendSign(strike.rotationQuarterTurns);
    workpiece->rotationQuarterTurns = strike.rotationQuarterTurns;
    workpiece->lastStrikeRotation = strike.rotationQuarterTurns;
    ++workpiece->hammerStrikeCount;

    if (workpiece->heat < 0.42f && strike.force > 0.55f)
    {
        const float stress = (0.42f - workpiece->heat) * strike.force * 0.48f;
        workpiece->coldHammerStress = Clamp01(workpiece->coldHammerStress + stress);
        workpiece->materialIntegrity = Clamp01(workpiece->materialIntegrity - stress * 0.82f);
    }

    if (workpiece->phase == NailForgePhase::DrawingShank)
    {
        const int segmentIndex = std::clamp(
            static_cast<int>(std::floor(strike.position * static_cast<float>(workpiece->shankSegments.size()))),
            0,
            static_cast<int>(workpiece->shankSegments.size()) - 1);
        NailShankSegmentState& segment = workpiece->shankSegments[segmentIndex];
        segment.thickness = Clamp01(segment.thickness - 0.034f * strike.force * workability);
        segment.lateralOffset = ClampAbs(
            segment.lateralOffset + sideSign * (0.012f + strike.force * 0.033f) * (sameRotation ? 1.18f : 0.82f),
            0.45f);
        ++segment.strikeCount;
        workpiece->drawingWork += strike.force * workability;
    }
    else if (workpiece->phase == NailForgePhase::ForgingPoint)
    {
        const float distanceFromTip = std::abs(strike.position - 0.86f);
        const float accuracy = Clamp01(1.0f - distanceFromTip * 4.0f);
        workpiece->pointSharpness = Clamp01(
            workpiece->pointSharpness + (0.035f + 0.075f * strike.force) * workability * accuracy);
        workpiece->pointAlignment = Clamp01(
            workpiece->pointAlignment - distanceFromTip * strike.force * 0.070f - (sameRotation ? 0.018f : -0.012f));
        workpiece->pointWork += strike.force * workability * (0.6f + accuracy * 0.4f);

        NailShankSegmentState& tip = workpiece->shankSegments.back();
        tip.thickness = Clamp01(tip.thickness - 0.018f * strike.force * workability * accuracy);
        tip.lateralOffset = ClampAbs(tip.lateralOffset + sideSign * distanceFromTip * 0.035f * strike.force, 0.45f);
    }
    else if (workpiece->phase == NailForgePhase::FormingHead)
    {
        const float distanceFromHead = std::abs(strike.position - 0.12f);
        const float centered = Clamp01(1.0f - distanceFromHead * 4.5f);
        workpiece->headSize = Clamp01(
            workpiece->headSize + (0.050f + 0.095f * strike.force) * workability * centered);
        workpiece->headAlignment = Clamp01(
            workpiece->headAlignment - distanceFromHead * strike.force * 0.080f - (sameRotation ? 0.012f : -0.010f));
        workpiece->headWork += strike.force * workability * (0.5f + centered * 0.5f);
    }

    workpiece->heat = Clamp01(workpiece->heat - (0.018f + 0.018f * strike.force));
    workpiece->heatUniformity = Clamp01(workpiece->heatUniformity - 0.008f);
    RecomputeNailDerivedState(*workpiece, false);
    return {true, "hammer_strike", std::nullopt};
}

NailActionResult FinishActiveNail(NailTrainingBatchState& batch, int currentGameMinute)
{
    NailWorkpieceState* workpiece = ActiveNail(batch);
    if (!workpiece)
        return {false, "lesson_complete", std::nullopt};

    AdvanceNailTrainingTime(batch, currentGameMinute);
    workpiece = ActiveNail(batch);
    if (!workpiece)
        return {false, "lesson_complete", std::nullopt};

    workpiece->phase = NailForgePhase::Finished;
    workpiece->finished = true;
    workpiece->heat = 0.0f;
    RecomputeNailDerivedState(*workpiece, true);
    NailEvaluationResult evaluation = EvaluateNail(*workpiece);

    if (evaluation.accepted)
        ++batch.acceptedCount;
    else
        ++batch.rejectedCount;

    for (const NailDefect& defect : workpiece->defects)
        AddDiscoveredDefect(batch, defect.id);

    ++batch.activeIndex;

    if (batch.activeIndex >= batch.targetCount && batch.acceptedCount < batch.minimumAcceptedCount)
        batch.targetCount = batch.activeIndex + 1;

    if (batch.activeIndex >= batch.targetCount && batch.acceptedCount >= batch.minimumAcceptedCount)
    {
        batch.completed = true;
    }
    else if (!batch.completed && batch.activeIndex >= static_cast<int>(batch.nails.size()))
    {
        batch.nails.push_back(MakeNailWorkpiece(batch.activeIndex, currentGameMinute));
    }

    batch.lastGameMinute = currentGameMinute;
    return {true, "finished_nail", evaluation};
}

void ForceDefect(NailWorkpieceState& workpiece, NailDefectId defect)
{
    AddOrUpdateDefect(workpiece, defect, 1.0f, true);
    if (defect == NailDefectId::BurnedIron ||
        defect == NailDefectId::NeckCrack ||
        defect == NailDefectId::OverdrawnShank)
    {
        workpiece.materialIntegrity = std::min(workpiece.materialIntegrity, 0.35f);
    }
}

void RecomputeNailDerivedState(NailWorkpieceState& workpiece, bool finalPass)
{
    RecomputeShapeFromSegments(workpiece);
    RemoveDerivedDefects(workpiece);

    if (workpiece.straightness < 0.72f)
        AddOrUpdateDefect(workpiece, NailDefectId::CrookedShank, 1.0f - workpiece.straightness, false);

    if (workpiece.pointAlignment < 0.72f)
        AddOrUpdateDefect(workpiece, NailDefectId::PointOffAxis, 1.0f - workpiece.pointAlignment, false);

    if (PhaseAtLeast(workpiece.phase, NailForgePhase::FormingHead) && workpiece.headAlignment < 0.72f)
        AddOrUpdateDefect(workpiece, NailDefectId::HeadOffCenter, 1.0f - workpiece.headAlignment, false);

    if ((finalPass || PhaseAtLeast(workpiece.phase, NailForgePhase::Inspecting)) && workpiece.pointSharpness < 0.62f)
        AddOrUpdateDefect(workpiece, NailDefectId::BluntPoint, 0.62f - workpiece.pointSharpness, false);

    if ((finalPass || PhaseAtLeast(workpiece.phase, NailForgePhase::Inspecting)) && workpiece.headSize < 0.58f)
        AddOrUpdateDefect(workpiece, NailDefectId::UndersizedHead, 0.58f - workpiece.headSize, false);

    if (workpiece.coldHammerStress > 0.23f || workpiece.materialIntegrity < 0.50f)
        AddOrUpdateDefect(workpiece, NailDefectId::NeckCrack, std::max(workpiece.coldHammerStress, 1.0f - workpiece.materialIntegrity), false);

    if (workpiece.overheatExposure > 0.32f)
        AddOrUpdateDefect(workpiece, NailDefectId::BurnedIron, workpiece.overheatExposure, false);

    if (workpiece.shankThickness < 0.52f || workpiece.drawingWork > 9.8f)
        AddOrUpdateDefect(workpiece, NailDefectId::OverdrawnShank, std::max(0.52f - workpiece.shankThickness, workpiece.drawingWork / 12.0f), false);
}

NailEvaluationResult EvaluateNail(const NailWorkpieceState& workpiece)
{
    for (const NailDefect& defect : workpiece.defects)
    {
        if (defect.critical)
            return ResultWithFeedback(workpiece, NailQualityGrade::Rejected);
    }

    const bool majorVisibleDefect = std::any_of(
        workpiece.defects.begin(),
        workpiece.defects.end(),
        [](const NailDefect& defect) { return defect.severity >= 0.62f; });

    if (workpiece.materialIntegrity < 0.55f || workpiece.shankThickness < 0.48f)
        return ResultWithFeedback(workpiece, NailQualityGrade::Rejected);

    if (majorVisibleDefect)
        return ResultWithFeedback(workpiece, NailQualityGrade::Provisional);

    if (workpiece.pointSharpness < 0.50f || workpiece.headSize < 0.48f || workpiece.straightness < 0.58f)
        return ResultWithFeedback(workpiece, NailQualityGrade::Rejected);

    if (workpiece.pointSharpness < 0.68f ||
        workpiece.headSize < 0.66f ||
        workpiece.straightness < 0.78f ||
        workpiece.pointAlignment < 0.78f ||
        workpiece.headAlignment < 0.78f)
    {
        return ResultWithFeedback(workpiece, NailQualityGrade::Provisional);
    }

    if (workpiece.pointSharpness > 0.90f &&
        workpiece.headSize > 0.88f &&
        workpiece.straightness > 0.94f &&
        workpiece.pointAlignment > 0.94f &&
        workpiece.headAlignment > 0.94f &&
        workpiece.materialIntegrity > 0.92f)
    {
        return ResultWithFeedback(workpiece, NailQualityGrade::Excellent);
    }

    if (workpiece.pointSharpness > 0.80f &&
        workpiece.headSize > 0.76f &&
        workpiece.straightness > 0.86f &&
        workpiece.pointAlignment > 0.86f &&
        workpiece.headAlignment > 0.86f)
    {
        return ResultWithFeedback(workpiece, NailQualityGrade::Honest);
    }

    return ResultWithFeedback(workpiece, NailQualityGrade::Common);
}

json SaveNailTrainingBatch(const NailTrainingBatchState& batch)
{
    json nails = json::array();
    for (const NailWorkpieceState& workpiece : batch.nails)
        nails.push_back(SaveWorkpiece(workpiece));

    return json{
        {"schema_version", 1},
        {"target_count", batch.targetCount},
        {"active_index", batch.activeIndex},
        {"nails", nails},
        {"accepted_count", batch.acceptedCount},
        {"rejected_count", batch.rejectedCount},
        {"discovered_defects", [&batch]() {
            json out = json::array();
            for (NailDefectId defect : batch.discoveredDefects)
                out.push_back(ToString(defect));
            return out;
        }()},
        {"completed", batch.completed},
        {"lesson_started", batch.lessonStarted},
        {"minimum_accepted_count", batch.minimumAcceptedCount},
        {"last_game_minute", batch.lastGameMinute}
    };
}

void LoadNailTrainingBatch(const json& source, NailTrainingBatchState& outBatch, int currentGameMinute)
{
    outBatch = NailTrainingBatchState{};
    if (!source.is_object())
    {
        outBatch.lastGameMinute = currentGameMinute;
        return;
    }

    outBatch.targetCount = std::max(1, source.value("target_count", outBatch.targetCount));
    outBatch.activeIndex = std::max(0, source.value("active_index", outBatch.activeIndex));
    outBatch.acceptedCount = std::max(0, source.value("accepted_count", outBatch.acceptedCount));
    outBatch.rejectedCount = std::max(0, source.value("rejected_count", outBatch.rejectedCount));
    outBatch.completed = source.value("completed", outBatch.completed);
    outBatch.lessonStarted = source.value("lesson_started", outBatch.lessonStarted);
    outBatch.minimumAcceptedCount = std::max(1, source.value("minimum_accepted_count", outBatch.minimumAcceptedCount));
    outBatch.lastGameMinute = source.value("last_game_minute", currentGameMinute);

    if (source.contains("nails") && source["nails"].is_array())
    {
        int index = 0;
        for (const json& item : source["nails"])
            outBatch.nails.push_back(LoadWorkpiece(item, index++, currentGameMinute));
    }

    if (source.contains("discovered_defects") && source["discovered_defects"].is_array())
    {
        for (const json& item : source["discovered_defects"])
        {
            if (item.is_string())
                AddDiscoveredDefect(outBatch, NailDefectIdFromString(item.get<std::string>()));
        }
    }
    else
    {
        for (const NailWorkpieceState& workpiece : outBatch.nails)
            for (const NailDefect& defect : workpiece.defects)
                AddDiscoveredDefect(outBatch, defect.id);
    }

    if (outBatch.lessonStarted && outBatch.nails.empty())
        outBatch.nails.push_back(MakeNailWorkpiece(0, currentGameMinute));

    if (outBatch.activeIndex >= static_cast<int>(outBatch.nails.size()) && !outBatch.completed)
    {
        outBatch.activeIndex = static_cast<int>(outBatch.nails.size());
        outBatch.nails.push_back(MakeNailWorkpiece(outBatch.activeIndex, currentGameMinute));
    }

    AdvanceNailTrainingTime(outBatch, currentGameMinute);
}
}
