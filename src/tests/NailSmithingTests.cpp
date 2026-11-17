#include "game/smithing/NailSmithing.h"
#include "game/smithing/NailForgingController.h"
#include "game/smithing/SmithingAnimationManifest.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::filesystem::path ProjectRoot()
{
#ifdef REMESLO_PROJECT_ROOT
    return std::filesystem::path(REMESLO_PROJECT_ROOT);
#else
    return std::filesystem::current_path();
#endif
}

nlohmann::json LoadJson(const std::filesystem::path& path)
{
    std::ifstream in(path);
    Require(in.good(), "can open " + path.string());

    nlohmann::json root;
    in >> root;
    return root;
}

void HeatToWorkingRange(smithing::NailTrainingBatchState& batch, int minute)
{
    for (int i = 0; i < 4; ++i)
        smithing::HeatNail(batch, minute, i >= 2, true);
}

void ReheatIfNeeded(smithing::NailTrainingBatchState& batch, int minute, float targetHeat)
{
    const smithing::NailWorkpieceState* nail = smithing::ActiveNail(batch);
    while (nail && nail->heat < targetHeat)
    {
        smithing::HeatNail(batch, minute, false, true);
        nail = smithing::ActiveNail(batch);
        if (!nail || nail->heat >= targetHeat)
            break;
        smithing::HeatNail(batch, minute, true, false);
        nail = smithing::ActiveNail(batch);
    }
}

void ApplyBalancedStrike(smithing::NailTrainingBatchState& batch,
                         int minute,
                         float position,
                         float force,
                         int& rotation)
{
    ReheatIfNeeded(batch, minute, 0.58f);

    smithing::NailHammerStrike strike;
    strike.position = position;
    strike.force = force;
    strike.rotationQuarterTurns = rotation;
    smithing::ApplyHammerStrike(batch, strike, minute);

    rotation = (rotation + 2) % 4;
}

void CompleteHonestNail(smithing::NailTrainingBatchState& batch, int minute)
{
    ReheatIfNeeded(batch, minute, 0.64f);
    smithing::AdvancePhase(batch, minute);

    int rotation = 0;
    for (int segment = 0; segment < 5; ++segment)
    {
        const float position = (static_cast<float>(segment) + 0.5f) / 5.0f;
        ApplyBalancedStrike(batch, minute, position, 0.45f, rotation);
        ApplyBalancedStrike(batch, minute, position, 0.45f, rotation);
    }

    smithing::AdvancePhase(batch, minute);
    for (int i = 0; i < 12; ++i)
        ApplyBalancedStrike(batch, minute, 0.86f, 0.50f, rotation);

    smithing::AdvancePhase(batch, minute);
    smithing::AdvancePhase(batch, minute);
    for (int i = 0; i < 8; ++i)
        ApplyBalancedStrike(batch, minute, 0.12f, 0.50f, rotation);

    smithing::AdvancePhase(batch, minute);
    const smithing::NailActionResult result = smithing::FinishActiveNail(batch, minute);
    Require(result.evaluation.has_value(), "finished nail returns evaluation");
    Require(result.evaluation->accepted, "balanced nail is accepted");
}

void TestColdHeavyHammeringCreatesNeckCrack()
{
    smithing::NailTrainingBatchState batch;
    smithing::StartNailTraining(batch, 10);
    smithing::NailWorkpieceState* nail = smithing::ActiveNail(batch);
    Require(nail != nullptr, "training creates active nail");
    nail->phase = smithing::NailForgePhase::DrawingShank;
    nail->heat = 0.10f;

    smithing::NailHammerStrike strike;
    strike.force = 1.0f;
    strike.position = 0.45f;
    strike.rotationQuarterTurns = 0;
    smithing::ApplyHammerStrike(batch, strike, 10);
    smithing::ApplyHammerStrike(batch, strike, 10);

    nail = smithing::ActiveNail(batch);
    Require(nail != nullptr, "active nail remains active after strike");
    Require(smithing::HasDefect(*nail, smithing::NailDefectId::NeckCrack),
        "cold heavy strikes deterministically create neck_crack");

    nail->phase = smithing::NailForgePhase::Finished;
    nail->finished = true;
    smithing::RecomputeNailDerivedState(*nail, true);
    const smithing::NailEvaluationResult result = smithing::EvaluateNail(*nail);
    Require(result.grade == smithing::NailQualityGrade::Rejected,
        "critical neck_crack rejects the nail");
}

void TestOverheatingCreatesBurnedIron()
{
    smithing::NailTrainingBatchState batch;
    for (int i = 0; i < 9; ++i)
        smithing::HeatNail(batch, 20, true, false);

    const smithing::NailWorkpieceState* nail = smithing::ActiveNail(batch);
    Require(nail != nullptr, "active nail exists after heating");
    Require(smithing::HasDefect(*nail, smithing::NailDefectId::BurnedIron),
        "long overheating deterministically creates burned_iron");
}

void TestBatchSaveLoadCoolsActiveWorkpiece()
{
    smithing::NailTrainingBatchState batch;
    HeatToWorkingRange(batch, 100);
    const float savedHeat = smithing::ActiveNail(batch)->heat;
    const nlohmann::json saved = smithing::SaveNailTrainingBatch(batch);

    smithing::NailTrainingBatchState loaded;
    smithing::LoadNailTrainingBatch(saved, loaded, 108);

    const smithing::NailWorkpieceState* nail = smithing::ActiveNail(loaded);
    Require(nail != nullptr, "loaded batch has active nail");
    Require(nail->heat < savedHeat, "load applies elapsed-game-time cooling");
}

void TestFinishedCriticalNailExtendsReplacementWork()
{
    smithing::NailTrainingBatchState batch;
    smithing::StartNailTraining(batch, 200);
    batch.targetCount = 1;
    batch.minimumAcceptedCount = 1;

    smithing::NailWorkpieceState* nail = smithing::ActiveNail(batch);
    Require(nail != nullptr, "active nail exists");
    nail->phase = smithing::NailForgePhase::Inspecting;
    smithing::ForceDefect(*nail, smithing::NailDefectId::BurnedIron);

    const smithing::NailActionResult result = smithing::FinishActiveNail(batch, 200);
    Require(result.evaluation.has_value(), "finish returns evaluation");
    Require(!result.evaluation->accepted, "critical nail is rejected");
    Require(std::find(batch.discoveredDefects.begin(),
                      batch.discoveredDefects.end(),
                      smithing::NailDefectId::BurnedIron) != batch.discoveredDefects.end(),
        "finished rejected nail records the critical discovered defect");
    Require(!batch.completed, "batch does not complete with too few accepted nails");
    Require(batch.targetCount == 2, "batch schedules a replacement nail");
    Require(smithing::ActiveNail(batch) != nullptr, "replacement nail is active");
}

void TestTenNailTrainingBatchCanComplete()
{
    smithing::NailTrainingBatchState batch;
    for (int i = 0; i < 10; ++i)
        CompleteHonestNail(batch, 300 + i);

    Require(batch.completed, "ten accepted attempts complete the batch");
    Require(batch.activeIndex == 10, "completed batch records ten attempts");
    Require(batch.acceptedCount == 10, "all balanced nails are accepted");
    Require(batch.nails.size() == 10, "each attempt remains a separate nail state");
}

void TestBlatceNpcSpawnTypesAreRegistered()
{
    const std::filesystem::path root = ProjectRoot();
    const nlohmann::json typeRoot = LoadJson(root / "data" / "npcs" / "NpcTypes.json");
    const nlohmann::json spawnRoot = LoadJson(root / "data" / "maps" / "blatce.npcs.json");

    std::set<std::string> registeredTypes;
    Require(typeRoot.contains("types") && typeRoot["types"].is_array(), "NpcTypes has types array");
    for (const nlohmann::json& type : typeRoot["types"])
    {
        if (type.contains("type_id") && type["type_id"].is_string())
            registeredTypes.insert(type["type_id"].get<std::string>());
    }

    Require(spawnRoot.contains("npcs") && spawnRoot["npcs"].is_array(), "blatce NPC spawns have npcs array");
    for (const nlohmann::json& spawn : spawnRoot["npcs"])
    {
        const std::string typeId = spawn.value("type_id", std::string());
        Require(!typeId.empty(), "NPC spawn has type_id");
        Require(registeredTypes.contains(typeId), "NPC spawn type is registered: " + typeId);
    }
}

void RequireLocalizedTextField(
    const nlohmann::json& item,
    const std::string& field,
    const std::string& itemId,
    const std::string& language)
{
    Require(item.contains(field), "localization " + language + " has " + field + " for " + itemId);
    Require(item[field].is_string(), "localization " + language + " field " + field + " is string for " + itemId);
    Require(!item[field].get<std::string>().empty(), "localization " + language + " field " + field + " is not empty for " + itemId);
}

void RequireItemLocalizationCovers(
    const nlohmann::json& itemRoot,
    const nlohmann::json& localizationRoot,
    const std::string& language,
    const std::string& sourceName)
{
    Require(localizationRoot.value("language", std::string()) == language,
        "item localization language marker is " + language);
    Require(localizationRoot.contains("items") && localizationRoot["items"].is_object(),
        "item localization has items object for " + language);
    Require(itemRoot.contains("items") && itemRoot["items"].is_array(),
        sourceName + " has items array");

    const nlohmann::json& localizedItems = localizationRoot["items"];
    for (const nlohmann::json& item : itemRoot["items"])
    {
        const std::string itemId = item.value("id", std::string());
        Require(!itemId.empty(), sourceName + " item has id");

        const auto it = localizedItems.find(itemId);
        Require(it != localizedItems.end(), "localization " + language + " covers item " + itemId + " from " + sourceName);
        Require(it->is_object(), "localization " + language + " item object for " + itemId);
        RequireLocalizedTextField(*it, "name", itemId, language);
        RequireLocalizedTextField(*it, "description", itemId, language);
        RequireLocalizedTextField(*it, "flavorText", itemId, language);
    }
}

void TestItemLocalizationsCoverRuntimeItems()
{
    const std::filesystem::path root = ProjectRoot();
    const nlohmann::json runtimeItems = LoadJson(root / "assets" / "data" / "Items.json");
    const nlohmann::json legacyItems = LoadJson(root / "data" / "items" / "Items.json");
    const nlohmann::json cs = LoadJson(root / "data" / "localization" / "cs" / "items.json");
    const nlohmann::json en = LoadJson(root / "data" / "localization" / "en" / "items.json");

    RequireItemLocalizationCovers(runtimeItems, cs, "cs", "assets/data/Items.json");
    RequireItemLocalizationCovers(runtimeItems, en, "en", "assets/data/Items.json");
    RequireItemLocalizationCovers(legacyItems, cs, "cs", "data/items/Items.json");
    RequireItemLocalizationCovers(legacyItems, en, "en", "data/items/Items.json");
}

void TestNailDefectMetadataCoversAtlas()
{
    const std::filesystem::path root = ProjectRoot();
    const std::filesystem::path atlasPath =
        root / "assets" / "smithing" / "minigames" / "assets" / "overlays" / "defects_nail" / "nail_defects.json";
    const nlohmann::json atlas = LoadJson(atlasPath);

    Require(atlas.contains("frames") && atlas["frames"].is_array(), "nail defect atlas has frames array");
    Require(atlas["frames"].size() == smithing::AllNailDefectInfos().size(), "nail defect atlas frame count matches code metadata");

    std::map<std::string, nlohmann::json> frameById;
    for (const nlohmann::json& frame : atlas["frames"])
    {
        const std::string id = frame.value("id", std::string());
        Require(!id.empty(), "nail defect frame has id");
        Require(frame.value("technical_name", std::string()) == id, "nail defect technical_name matches id for " + id);
        Require(!frame.value("image", std::string()).empty(), "nail defect frame has image for " + id);
        Require(!frame.value("name_cs", std::string()).empty(), "nail defect frame has Czech name for " + id);
        Require(!frame.value("description_cs", std::string()).empty(), "nail defect frame has Czech description for " + id);
        frameById[id] = frame;
    }

    for (const smithing::NailDefectInfo& info : smithing::AllNailDefectInfos())
    {
        const auto it = frameById.find(info.technicalId);
        Require(it != frameById.end(), std::string("atlas covers code defect metadata: ") + info.technicalId);
        Require(it->second.value("name_cs", std::string()) == info.nameCs,
            std::string("atlas Czech name matches code for ") + info.technicalId);
        Require(it->second.value("description_cs", std::string()) == info.descriptionCs,
            std::string("atlas Czech description matches code for ") + info.technicalId);
        Require(std::filesystem::exists(root / info.imagePath),
            std::string("nail defect image exists: ") + info.imagePath);
    }
}

void TestNailAnimationManifestLoadsNewPack()
{
    const std::filesystem::path root = ProjectRoot();
    const std::filesystem::path manifestPath =
        root / "assets" / "smithing" / "minigames" / "nail" / "nail_forging_animations.json";
    const std::optional<smithing::SmithingAnimationManifest> manifest =
        smithing::LoadSmithingAnimationManifest(manifestPath, "nail");

    Require(manifest.has_value(), "nail animation manifest loads from runtime assets");
    Require(manifest->cellWidth == 512 && manifest->cellHeight == 512, "nail manifest cell size is 512");
    Require(manifest->Find("hammer_strike_heavy") != nullptr, "hammer animation exists");
    Require(manifest->Find("tongs_rotate_quarter") != nullptr, "tongs animation exists");
    Require(manifest->Find("nail_workpiece_deformation") != nullptr, "workpiece state atlas exists");
    Require(manifest->FindNamespaced("nail::quench_steam") != nullptr, "namespaced quench animation resolves");
    Require(manifest->workpieceFrames.size() == 16, "workpiece atlas has sixteen states");
    Require(manifest->workpieceFrames[13].id == "nail_finished_hot", "cooling starts at frame 13");
    Require(std::filesystem::exists(manifest->Find("hammer_strike_heavy")->imagePath),
        "hammer atlas path resolves relative to manifest");
}

void TestNailForgingControllerCommitsOnImpactEvent()
{
    const std::filesystem::path root = ProjectRoot();
    smithing::NailForgingController controller;
    Require(controller.LoadManifest(root / "assets" / "smithing" / "minigames" / "nail" / "nail_forging_animations.json"),
        "controller loads nail animation manifest");

    controller.SyncVisualState(smithing::NailAnimationPhase::Drawing, 1, 0.74f);
    smithing::StrikeInput strike;
    strike.targetX01 = 0.50f;
    strike.power01 = 0.55f;
    Require(controller.RequestStrike(strike), "controller accepts drawing strike");

    controller.Update(1.0f);
    Require(controller.LastCommittedStrike().has_value(), "impact event commits pending strike even with large dt");
    Require(controller.State().successfulHits == 1, "successful hit count advances once");
    Require(controller.State().workpieceFrameIndex == 2, "workpiece frame advances once on impact");
    Require(!controller.State().inputLocked, "input unlocks after hammer animation completes");
    Require(!controller.Effects().empty(), "impact starts manifest effects");
}

void TestNailForgingControllerStationTemperatureAndTongs()
{
    const std::filesystem::path root = ProjectRoot();
    smithing::NailForgingController controller;
    Require(controller.LoadManifest(root / "assets" / "smithing" / "minigames" / "nail" / "nail_forging_animations.json"),
        "controller loads nail animation manifest for station test");

    controller.SetStation(smithing::SmithingStation::Forge);
    controller.UpdateTemperature(4.0f);
    Require(controller.State().temperature01 > 0.78f, "forge heats nail into drawing threshold");
    Require(controller.State().phase == smithing::NailAnimationPhase::Drawing, "heated nail enters drawing phase");

    Require(controller.RequestQuarterTurn(), "quarter turn animation starts");
    controller.Update(0.18f);
    Require(controller.State().heldByTongs, "grip_closed marks nail as held by tongs");
    controller.Update(1.0f);
    Require(controller.State().quarterTurns == 1, "rotation_complete advances quarter turn");

    controller.SetStation(smithing::SmithingStation::Forge);
    Require(controller.RequestTongsWork(), "generic tongs work animation starts");
    controller.Update(1.0f);
    Require(controller.State().quarterTurns == 1, "generic tongs work does not advance quarter turn");

    controller.SyncVisualState(smithing::NailAnimationPhase::Cooling, 13, 0.60f);
    Require(controller.RequestQuench(), "quench starts for hot finished nail");
    Require(controller.State().waterContact, "quench marks water contact");
    Require(!controller.Effects().empty(), "hot quench starts steam effect");
    controller.UpdateTemperature(1.0f);
    Require(controller.State().temperature01 < 0.30f, "water contact cools quickly");
}
}

int main()
{
    TestColdHeavyHammeringCreatesNeckCrack();
    TestOverheatingCreatesBurnedIron();
    TestBatchSaveLoadCoolsActiveWorkpiece();
    TestFinishedCriticalNailExtendsReplacementWork();
    TestTenNailTrainingBatchCanComplete();
    TestBlatceNpcSpawnTypesAreRegistered();
    TestItemLocalizationsCoverRuntimeItems();
    TestNailDefectMetadataCoversAtlas();
    TestNailAnimationManifestLoadsNewPack();
    TestNailForgingControllerCommitsOnImpactEvent();
    TestNailForgingControllerStationTemperatureAndTongs();
    std::cout << "NailSmithing tests passed\n";
    return 0;
}
