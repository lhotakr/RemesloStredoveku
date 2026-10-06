#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace smithing
{
enum class NailAnimationPhase : std::uint8_t
{
    Material,
    Heating,
    Drawing,
    Pointing,
    Cutting,
    ReheatingForHeading,
    Heading,
    Cooling,
    Inspecting,
    Finished,
    Ruined
};

enum class SmithingStation : std::uint8_t
{
    None,
    Forge,
    Anvil,
    Quench,
    Inspection
};

enum class SmithingViewMode : std::uint8_t
{
    World,
    StationDetail
};

enum class NailForgingDefect : std::uint8_t
{
    None,
    CrookedShank,
    BluntPoint,
    PointOffAxis,
    UndersizedHead,
    HeadOffCenter,
    NeckCrack,
    BurnedIron,
    OverdrawnShank
};

struct StrikeInput
{
    float targetX01 = 0.5f;
    float power01 = 0.5f;
    float direction01 = 0.5f;
};

struct StrikeResult
{
    bool contacted = false;
    bool appliesDeformation = false;
    float accuracy = 0.0f;
    float powerError = 0.0f;
    float temperatureError = 0.0f;
    NailForgingDefect producedDefect = NailForgingDefect::None;
};

struct NailForgingWorkpieceState
{
    std::string craftId = "smithing";
    std::string itemId = "nail";

    NailAnimationPhase phase = NailAnimationPhase::Material;
    SmithingStation station = SmithingStation::None;
    int workpieceFrameIndex = 0;
    std::string workpieceFrameId = "rod_heated";
    float temperature01 = 0.0f;
    float overheatedTimeSeconds = 0.0f;

    int totalHits = 0;
    int successfulHits = 0;
    int failedHits = 0;
    int phaseHits = 0;
    int hitsSinceRotation = 0;
    int quarterTurns = 0;
    bool rotationRequired = false;

    bool requiresReheat = false;
    bool heldByTongs = false;
    bool waterContact = false;
    bool inputLocked = false;
    bool quenchSteamPlayed = false;
    std::vector<NailForgingDefect> defects;
};

const char* ToString(NailAnimationPhase phase);
const char* ToString(NailForgingDefect defect);
}
