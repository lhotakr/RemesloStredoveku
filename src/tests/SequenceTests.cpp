#include "game/sequence/SequenceManager.h"
#include "game/sequence/SequenceTimeline.h"
#include "game/sequence/SequenceAudio.h"
#include "game/sequence/SequenceMarkers.h"
#include "audio/AudioManager.h"
#include <SDL_mixer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_set>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

const std::filesystem::path Root = REMESLO_PROJECT_ROOT;

nlohmann::json ReadJson(const std::filesystem::path& path)
{
    std::ifstream input(path);
    Require(input.good(), "fixture exists");
    return nlohmann::json::parse(input);
}

struct Context : sequence::ISequenceContext
{
    std::unordered_set<std::string> flags;
    std::unordered_map<std::string, sequence::Vec2> actors, markers;
    std::vector<std::string> modes;
    std::vector<float> walkPositions;
    std::vector<std::string> audio;
    std::vector<nlohmann::json> pickups;
    std::vector<float> duckLevels;
    std::unordered_map<std::string, bool> visibility;
    std::string map, text, sceneId;
    float musicLevel = 1.0f;
    bool control = true;
    float fade = 0.0f;

    void sequenceLog(const std::string&) override {}
    bool hasStoryFlag(const std::string& flag) const override { return flags.contains(flag); }
    void setStoryFlag(const std::string& flag) override { flags.insert(flag); }
    bool loadScene(const std::string& id) override
    {
        const auto scene = ReadJson(Root / "assets/sequences/scenes" / (id + ".json"));
        sceneId = id;
        actors.clear();
        markers.clear();
        for (const auto& marker : scene.at("markers"))
            markers[marker.at("id")] = {marker.at("x"), marker.at("y")};
        for (const auto& actor : scene.at("actors"))
            actors[actor.at("id")] = markers.at(actor.at("marker"));
        if (scene.contains("interior"))
        {
            const auto interior = ReadJson(Root / "data/interiors" / (scene.at("interior").get<std::string>() + ".json"));
            for (const auto& sprite : interior.at("sprites"))
                actors[sprite.at("id")] = {sprite.at("x"), sprite.at("y")};
        }
        control = false;
        return true;
    }
    bool resolveMarker(const std::string& id, sequence::Vec2& out) const override
    {
        const auto it = markers.find(id);
        if (it == markers.end()) return false;
        out = it->second;
        return true;
    }
    bool spawnActor(const sequence::ActorSpawnDef& actor) override { actors[actor.actor] = actor.position; return true; }
    void removeActor(const std::string& id) override { actors.erase(id); }
    void setActorVisible(const std::string& id, bool visible) override { visibility[id] = visible; }
    bool getActorPosition(const std::string& id, sequence::Vec2& out) const override
    {
        const auto it = actors.find(id);
        if (it == actors.end()) return false;
        out = it->second;
        return true;
    }
    bool setActorPosition(const std::string& id, sequence::Vec2 pos) override
    {
        actors[id] = pos;
        if (id == "patrik") walkPositions.push_back(pos.x);
        return true;
    }
    void setActorAnimation(const std::string&, const std::string&) override {}
    void faceActor(const std::string&, const std::string&) override {}
    void setCameraPosition(sequence::Vec2) override {}
    void setCameraFollow(const std::string&) override {}
    void setCameraShake(float, float, float) override {}
    void setFadeAlpha(float alpha) override { fade = alpha; }
    void setFlashAlpha(float) override {}
    void showSequenceText(const std::string& value, float) override { text = value; }
    void hideSequenceText() override { text.clear(); }
    void playMusic(const std::string& id, int, int) override { audio.push_back(id); }
    void stopMusic(int) override {}
    void playSfx(const std::string& id) override { audio.push_back(id); }
    void playVoice(const std::string& id) override { audio.push_back(id); }
    void setSequenceMusicVolume(float volume, float) override { musicLevel = volume; }
    void duckSequenceMusic(float volume, float, float, float) override { duckLevels.push_back(volume); }
    bool pickupSequenceItem(const std::string& item, int count, const std::string& prop, const std::string& flag) override
    {
        Require(actors.contains(prop), "pickup refers to an existing scene or interior object");
        pickups.push_back({{"item", item}, {"count", count}, {"prop", prop}, {"once_flag", flag},
            {"x", actors.at("patrik").x}, {"y", actors.at("patrik").y}});
        setActorVisible(prop, false);
        return true;
    }
    void setTimeOfDay(const std::string&) override {}
    void setWeather(const nlohmann::json&) override {}
    bool loadMapForSequence(const std::string& id, const std::string&) override
    {
        map = id;
        auto path = Root / "data/maps" / id;
        path.replace_extension(".sequence_markers.json");
        markers.clear();
        const auto root = ReadJson(path);
        for (const auto& marker : root.at("markers"))
            markers[marker.at("id")] = {marker.at("x"), marker.at("y")};
        return true;
    }
    void teleportPlayer(sequence::Vec2 pos) override { actors["player"] = pos; }
    void enablePlayerControl(bool enabled) override { control = enabled; }
    void startDialog(const std::string&) override {}
    void changeRendererMode(const std::string& mode) override { modes.push_back(mode); }
};

void TestPreviewSnapshotAndControls()
{
    auto loaded = sequence::SequenceFromJson(nlohmann::json::parse(R"({
        "id":"unsaved_preview", "start":"start", "nodes":[
            {"id":"start", "next":"finish", "actions":[{"type":"wait", "duration":0.2}]},
            {"id":"finish", "actions":[{"type":"set_flag", "flag":"original"}]}
        ]
    })"), "test");
    Require(loaded.ok, "preview script parses");
    Context context;
    sequence::SequenceManager manager;
    Require(manager.startDefinition(loaded.sequence, context), "unsaved script starts");
    loaded.sequence.nodes.clear();
    manager.setPaused(true);
    manager.update(1.0f);
    Require(manager.status().elapsed == 0.0f && context.flags.empty(), "pause freezes execution");
    manager.setPaused(false);
    for (int frame = 0; frame < 20 && manager.isRunning(); ++frame)
        manager.update(0.1f);
    Require(context.flags.contains("original") && manager.status().completed, "preview owns an independent snapshot");

    context.flags.clear();
    Require(manager.startAtNode("unsaved_preview", "finish", context), "start from selected node");
    manager.stepAction();
    Require(context.flags.contains("original"), "step executes an action");
    manager.stop();
    Require(!manager.isRunning(), "stop ends playback");
    Require(manager.loadDirectory(Root / "assets/sequences"), "sequence directory loads");
    Require(manager.start("intro_houska", context), "intro starts before reload");
    Require(manager.reload() && !manager.isRunning(), "reload releases the running definition safely");
}

void TestIntroHandoff()
{
    auto loaded = sequence::LoadSequenceFromFile((Root / "assets/sequences/intro_houska.json").string());
    Require(loaded.ok, "intro parses");
    Context context;
    sequence::SequenceManager manager;
    Require(manager.startDefinition(loaded.sequence, context), "intro starts");
    manager.update(0.0f);
    Require(!context.control && context.sceneId == "Houska_first_view", "intro starts in its opening scene with controls locked");
    for (int frame = 0; frame < 6000 && manager.isRunning(); ++frame)
        manager.update(0.05f);
    Require(manager.status().completed && !manager.isRunning(), "intro finishes within its expected duration");
    Require(context.flags.contains("intro_houska_completed"), "intro sets its completion flag");
    Require(context.map == "blatce.rvm" && context.modes.back() == "2d", "intro enters the campaign map");
    Require(context.control && context.text.empty() && std::abs(context.fade) < 0.001f, "intro hands over controls with a clear screen");
    Require(context.musicLevel == 0.55f && context.duckLevels == std::vector<float>(4, 0.3f),
        "intro lowers the soundtrack and ducks it for all four voice lines");
    Require(context.pickups.size() == 1 && context.pickups[0]["item"] == "artifact_hoe" &&
        context.pickups[0]["count"] == 1 && context.pickups[0]["once_flag"] == "intro_houska_hoe_picked_up" &&
        !context.visibility.at("interior_object_3"), "intro requests one story hoe and hides its floor object");
    const auto spawn = context.markers.at("forest_morning_spawn");
    const auto player = context.actors.at("player");
    Require(player.x == spawn.x && player.y == spawn.y, "player wakes at the designated map marker");
    const auto camp = ReadJson(Root / "assets/sequences/scenes/intro_houska_camp.json");
    for (const auto& marker : camp.at("markers"))
    {
        if (marker.at("id") == "camp_rest")
            Require(std::abs(context.actors.at("patrik").x - marker.at("x").get<float>()) < 0.001f,
                "Patrik reaches his resting position after setting up camp");
        if (marker.at("id") == "hoe_inspect")
            Require(context.pickups[0]["x"] == marker.at("x") && context.pickups[0]["y"] == marker.at("y"),
                "Patrik picks up the hoe while standing at the inspection point");
    }
}

void TestTimelineEditing()
{
    auto loaded = sequence::SequenceFromJson(nlohmann::json::parse(R"({
        "id":"timeline", "start":"start", "nodes":[{"id":"start", "actions":[
            {"type":"wait", "start_delay":1, "duration":2},
            {"type":"parallel", "actions":[
                {"type":"move_actor", "actor":"patrik", "x":10, "duration":4},
                {"type":"play_voice", "id":"voice.wav", "startDelay":1, "blocking":false},
                {"type":"wait", "startDelay":10, "duration":10, "blocking":false}
            ]},
            {"type":"show_text", "text":"Hello", "duration":20},
            {"type":"wait", "duration":1}
        ]}]})"), "timeline test");
    Require(loaded.ok, "timeline fixture parses");
    auto& node = loaded.sequence.nodes.front();
    auto clips = sequence::BuildTimeline(node);
    Require(clips.size() == 7 && clips[1].start == 3 && clips[1].duration == 4,
        "parallel timeline accounts for blocking children and start delays");
    Require(clips[3].start == 4 && clips.back().start == 7,
        "audio child has its own start and text display does not block the timeline");
    Require(!sequence::FindTimelineAction(node, {1, 99}), "stale timeline selection is rejected");
    sequence::SetActionDelay(node.actions[0], 0);
    sequence::SetActionDuration(node.actions[0], 3);
    const auto saved = sequence::SequenceToJson(loaded.sequence);
    const auto roundTrip = sequence::SequenceFromJson(saved, "round trip");
    Require(roundTrip.ok && roundTrip.sequence.nodes[0].actions[0].startDelay == 0 &&
        roundTrip.sequence.nodes[0].actions[0].duration == 3, "cleared delay and resized duration survive save and reload");
    Require(!saved["nodes"][0]["actions"][0].contains("start_delay"), "obsolete delay alias cannot restore a removed delay");

    sequence::ActionDef audio;
    audio.type = "play_sfx";
    audio.data = {{"type", "play_sfx"}, {"id", "new.wav"}};
    const auto path = sequence::InsertTimedAction(node, audio, 4);
    Require(path.size() == 2 && path[0] == 1, "audio inserted during movement joins its parallel group");
    auto* inserted = sequence::FindTimelineAction(node, path);
    Require(inserted && inserted->startDelay == 1 && !inserted->blocking, "inserted audio preserves its start time");
    clips = sequence::BuildTimeline(node);
    Require(clips.back().start == 7, "adding overlapping audio does not move later actions");
    Context context;
    context.actors["patrik"] = {0, 0};
    sequence::SequenceManager manager;
    Require(manager.startDefinition(loaded.sequence, context), "edited timeline starts in runtime");
    for (int i = 0; i < 420; ++i) manager.update(0.01f);
    Require(std::find(context.audio.begin(), context.audio.end(), "new.wav") != context.audio.end(),
        "inserted audio executes at the chosen time during movement");
    Require(context.actors["patrik"].x > 0 && context.actors["patrik"].x < 10, "audio does not interrupt movement");

    sequence::NodeDef simple;
    sequence::ActionDef wait;
    wait.type = "wait";
    sequence::SetActionDelay(wait, 2);
    sequence::SetActionDuration(wait, 4);
    simple.actions.push_back(wait);
    auto result = sequence::InsertTimedAction(simple, audio, 1);
    Require(result == std::vector<int>{0} && sequence::BuildTimeline(simple).back().start == 2,
        "inserting in a delay preserves the next clip's original start");
    result = sequence::InsertTimedAction(simple, audio, 3);
    Require(result.size() == 2 && sequence::BuildTimeline(simple).back().start == 3,
        "inserting during a sequential action wraps it without cutting it");
    result = sequence::InsertTimedAction(simple, audio, 9);
    const auto tail = sequence::BuildTimeline(simple).back();
    Require(tail.start == 9, "audio can be inserted beyond the last action");

    auto nested = sequence::SequenceFromJson(nlohmann::json::parse(R"({"id":"nested","start":"start","nodes":[
        {"id":"start","actions":[{"type":"parallel","actions":[{"type":"parallel","actions":[
            {"type":"wait","duration":1},{"type":"play_voice","id":"nested.wav"}]}]}]}]})"), "nested test");
    Context nestedContext;
    Require(manager.startDefinition(nested.sequence, nestedContext), "nested parallel timeline starts");
    manager.update(0);
    Require(nestedContext.audio == std::vector<std::string>{"nested.wav"} && manager.isRunning(),
        "nested parallel clips execute and preserve blocking siblings");
    nested.sequence.nodes[0].actions[0].children.clear();
    const auto emptyGroup = sequence::SequenceFromJson(sequence::SequenceToJson(nested.sequence), "empty group");
    Require(emptyGroup.ok && emptyGroup.sequence.nodes[0].actions[0].children.empty(),
        "deleting the last parallel clip does not restore its original JSON on save");

    sequence::NodeDef conversation;
    sequence::ActionDef duck;
    duck.type = "duck_music";
    duck.data = {{"type", "duck_music"}, {"volume", 0.3f}};
    sequence::SetActionDuration(duck, 4.0f);
    conversation.actions.push_back(duck);
    conversation.actions.push_back(audio);
    clips = sequence::BuildTimeline(conversation);
    Require(clips[0].duration == 4.0f && clips[1].start == 0.0f,
        "ducking is resizable on the timeline and does not delay the voice clip");
}

void TestSpatialMarkers()
{
    namespace fs = std::filesystem;
    const auto legacy = sequence::MarkerFromJson({{"id", "old"}, {"x", 3}, {"y", 4}});
    Require(legacy.id == "old" && legacy.z == 0 && legacy.type == "sequence", "legacy XY marker remains a floor point");
    auto camera = sequence::MarkerFromJson({{"id", "shot"}, {"type", "camera"}, {"x", 8}, {"y", 6},
        {"z", 2.4}, {"angle_degrees", 350}, {"pitch", -0.2}});
    const auto temp = fs::temp_directory_path() / ("remeslo-spatial-markers-" + std::to_string(SDL_GetPerformanceCounter()));
    fs::create_directories(temp);
    std::string error;
    const auto map = temp / "fixture.rvm";
    Require(sequence::SaveMarkersForMap(map, {legacy, camera}, &error), "spatial markers save");
    std::vector<sequence::SequenceMarker> markers;
    Require(sequence::LoadMarkersForMap(map, markers, &error) && markers.size() == 2, "spatial markers reload");
    Require(sequence::MarkerToJson(markers[1]) == sequence::MarkerToJson(camera), "camera height and orientation survive disk round trip");
    Require(sequence::UniqueMarkerId(markers, "shot") == "shot_2", "duplicate camera gets a unique ID");
    fs::remove_all(temp);
    auto loaded = sequence::SequenceFromJson(nlohmann::json::parse(R"({"id":"rename","start":"start","nodes":[
        {"id":"start","actions":[{"type":"parallel","actions":[{"type":"camera_move","target_marker":"shot","duration":1},
        {"type":"spawn_actor","actor":"shot","marker":"shot"}]}]}],
        "on_skip":[{"type":"set_camera","marker":"shot"}]})"), "rename markers");
    Require(loaded.ok, "marker references fixture parses");
    sequence::RenameMarkerReferences(loaded.sequence, "shot", "new_shot");
    const auto& actions = loaded.sequence.nodes[0].actions[0].children;
    Require(actions[0].data["target_marker"] == "new_shot" && actions[1].data["marker"] == "new_shot" &&
        actions[1].data["actor"] == "shot", "renaming traverses parallel children without renaming unrelated IDs");
    Require(loaded.sequence.onSkipActions.size() == 1 && loaded.sequence.onSkipActions[0].data["marker"] == "new_shot", "renaming includes skip actions");
    nlohmann::json scene = {{"camera", {{"marker", "shot"}}}, {"actors", {{{"id", "actor"}, {"marker", "shot"}}}}};
    sequence::RenameMarkerReferences(scene, "shot", "new_shot");
    Require(scene["camera"]["marker"] == "new_shot" && scene["actors"][0]["marker"] == "new_shot", "scene camera and actor references follow renames");
}

void TestCameraPoses()
{
    struct CameraContext : Context
    {
        sequence::CameraPose pose{{2, 3}, 1, 350, 0};
        sequence::CameraPose target{{6, 7}, 3, 10, 0.2f};
        bool getCameraPose(sequence::CameraPose& out) const override { out = pose; return true; }
        bool resolveCameraMarker(const std::string& id, sequence::CameraPose& out) const override
        { if (id != "shot") return false; out = target; return true; }
        void setCameraPose(const sequence::CameraPose& value) override { pose = value; }
    } context;
    auto loaded = sequence::SequenceFromJson(nlohmann::json::parse(R"({"id":"camera","start":"start","nodes":[
        {"id":"start","actions":[{"type":"camera_move","target_marker":"shot","duration":2,"easing":"linear"}]}]})"), "camera pose");
    Require(loaded.ok, "camera movement parses");
    sequence::SequenceManager manager;
    Require(manager.startDefinition(loaded.sequence, context), "camera pose sequence starts");
    manager.update(0);
    manager.update(1);
    Require(std::abs(context.pose.position.x - 4) < 0.001f && std::abs(context.pose.height - 2) < 0.001f &&
        std::abs(context.pose.pitch - 0.1f) < 0.001f, "camera interpolates position, height and pitch");
    Require(std::abs(context.pose.angleDegrees - 360) < 0.001f, "camera turns over zero by the shortest route");
    manager.update(1);
    Require(std::abs(context.pose.position.x - 6) < 0.001f && std::abs(context.pose.height - 3) < 0.001f, "camera reaches the full spatial target");
    loaded.sequence.nodes[0].actions[0].type = "set_camera";
    loaded.sequence.nodes[0].actions[0].data = {{"type", "set_camera"}, {"marker", "shot"}, {"height", 4}};
    Require(manager.startDefinition(loaded.sequence, context), "set camera starts");
    manager.update(0);
    Require(context.pose.height == 4 && context.pose.angleDegrees == 10, "set_camera applies marker orientation and explicit height override");
    loaded.sequence.nodes[0].actions[0].data = {{"x", 9}, {"y", 8}};
    Require(manager.startDefinition(loaded.sequence, context), "legacy camera coordinates start");
    manager.update(0);
    Require(context.pose.position.x == 9 && context.pose.position.y == 8 && context.pose.height == 4,
        "legacy XY camera actions preserve current height and direction");
}

void TestAudioImport()
{
    namespace fs = std::filesystem;
    const auto temp = fs::temp_directory_path() / ("remeslo-sequence-audio-" + std::to_string(SDL_GetPerformanceCounter()));
    fs::create_directories(temp / "source");
    fs::create_directories(temp / "project");
    const auto wav = temp / "source/new voice.WAV";
    // A real, short PCM WAV, so the import exercises the decoder as well as copying.
    {
        std::ofstream output(wav, std::ios::binary);
        auto u32 = [&](unsigned value) {
            for (int shift = 0; shift < 32; shift += 8) output.put(static_cast<char>((value >> shift) & 255));
        };
        const unsigned bytes = 22050 * 2 * 4;
        output << "RIFF"; u32(36 + bytes); output << "WAVEfmt "; u32(16);
        const char format[] = {1,0,1,0}; output.write(format, sizeof(format));
        u32(22050); u32(44100);
        const char block[] = {2,0,16,0}; output.write(block, sizeof(block));
        output << "data"; u32(bytes);
        const std::vector<char> silence(bytes, 0);
        output.write(silence.data(), silence.size());
    }
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_setenv("REMESLO_ALLOW_DUMMY_AUDIO", "1", 1);
    AudioManager player;
    player.setMusicVolume(80);
    player.setMusicGain(0.5f);
    Require(player.init(22050, 1, 512), "test audio initializes");
    Require(Mix_VolumeMusic(-1) == 40, "initialization preserves configured music gain");
    std::string asset, error;
    const auto project = temp / "project";
    Require(sequence::ImportAudioFile(wav, project, asset, error), "real WAV imports");
    Require(asset == "assets/audio/sequences/new voice.WAV" && fs::file_size(project / asset) == fs::file_size(wav),
        "import creates a portable project asset and preserves the source");
    Require(sequence::ImportAudioFile(wav, project, asset, error) && asset.find("_2.WAV") != std::string::npos,
        "a filename collision never overwrites an existing asset");
    const auto localAsset = asset;
    Require(sequence::ImportAudioFile(project / asset, project, asset, error) && asset == localAsset,
        "an existing project asset is reused without duplicating it");
    Require(sequence::ListAudioFiles(project).size() == 2, "library discovers nested audio assets");
    const auto invalid = temp / "source/broken.wav";
    std::ofstream(invalid) << "not audio";
    Require(!sequence::ImportAudioFile(invalid, project, asset, error) && !error.empty(), "corrupt audio is rejected before copying");
    Require(sequence::ListAudioFiles(project).size() == 2, "a failed import leaves the library untouched");
    Require(sequence::ResolveAudioPath(localAsset, project) == project / localAsset &&
        sequence::ResolveAudioPath("menu", project) == project / "assets/audio/menu.ogg" &&
        sequence::ResolveAudioPath(wav.string(), project) == wav, "audio paths work for imported assets and legacy names");
    Require(player.loadSfx("imported", (project / localAsset).string()) && player.hasSfx("imported"),
        "imported asset loads for effects and voices");
    player.playSfx("imported", 0, 30);
    Require(Mix_Playing(30), "imported sound actually starts in mixer");
    // SDL_mixer stores a pause timestamp; tick zero is also its unpaused sentinel.
    // Allow the dummy device's clock to advance before checking a paused channel.
    SDL_Delay(2);
    player.pausePlayback(true);
    Require(Mix_Paused(30), "pause freezes sequence audio channels");
    player.pausePlayback(false);
    Require(!Mix_Paused(30), "resume restores sequence audio channels");
    player.stopSfxChannel(30);
    Require(!Mix_Playing(30), "audition can be stopped");
    Require(player.playMusic((project / localAsset).string(), -1, 0), "imported music starts");
    player.setMusicVolume(100);
    player.setMusicGain(0.6f);
    player.duckMusic(0.25f, 1.0f, 0.5f, 0.5f);
    player.update(0.25f);
    Require(Mix_VolumeMusic(-1) == 38, "music fades toward the ducked level");
    player.pausePlayback(true);
    player.update(10.0f);
    Require(Mix_VolumeMusic(-1) == 38, "pause freezes the duck envelope");
    player.pausePlayback(false);
    player.update(0.25f);
    Require(Mix_VolumeMusic(-1) == 15, "duck gain multiplies the sequence and player levels");
    player.setMusicVolume(80);
    player.setMusicGain(0.5f);
    Require(Mix_VolumeMusic(-1) == 10, "changing the base volume preserves active ducking");
    player.update(1.25f);
    Require(Mix_VolumeMusic(-1) == 25, "a frame crossing the hold boundary advances the release correctly");
    player.update(0.25f);
    Require(Mix_VolumeMusic(-1) == 40, "ducking restores the current base volume automatically");
    player.duckMusic(0.1f, 10.0f, 0.0f, 0.5f);
    player.restoreMusic(0.5f);
    player.update(0.5f);
    Require(Mix_VolumeMusic(-1) == 40, "manual restore cancels the outstanding hold");
    player.duckMusic(0.2f, 1.0f, 0.3f, 0.5f);
    player.update(10.0f);
    Require(Mix_VolumeMusic(-1) == 40, "a large simulation step completes ducking and restoration");
    player.setMusicGain(0.0f, 0.5f);
    player.update(0.25f);
    Require(Mix_VolumeMusic(-1) == 20, "base music volume supports gradual changes");
    player.update(0.25f);
    Require(Mix_VolumeMusic(-1) == 0, "music can fade to silence");
    player.resetSequenceMix();
    Require(Mix_VolumeMusic(-1) == 80, "reset preserves the player's music setting");

    player.setSfxVolume(80);
    player.setSfxGain(0.5f);
    player.playSfx("imported", -1, 29, 128);
    player.playSfx("imported", -1, 30, 64);
    Require(Mix_Volume(29, -1) == 40 && Mix_Volume(30, -1) == 20,
        "simultaneous instances of one sound retain separate clip volumes");
    player.setSfxVolume(100);
    Require(Mix_Volume(29, -1) == 50 && Mix_Volume(30, -1) == 25,
        "sound settings update playing channels without losing clip gains");
    player.stopSfxChannel(29);
    player.stopSfxChannel(30);
    player.resetSequenceMix();
    player.seekMusic(1.0, -1);
    player.pausePlayback(true);
    Require(Mix_PausedMusic(), "music stays paused after seeking");
    player.pausePlayback(false);
    Require(!Mix_PausedMusic() && Mix_PlayingMusic(), "music resumes from a scrubbed preview");

    const bool missingAccepted = player.playMusic((temp / "missing.ogg").string(), -1, 0);
    const bool playingAfterMissing = Mix_PlayingMusic() != 0;
    Require(!missingAccepted, "a missing replacement track reports failure");
    Require(playingAfterMissing, "a missing replacement never stops the current soundtrack");
    player.pausePlayback(true);
    Require(!player.playMusic(invalid.string(), -1, 0), "a corrupt replacement track reports failure");
    Require(Mix_PlayingMusic() && Mix_PausedMusic(), "a corrupt replacement preserves playback and its paused state");
    player.pausePlayback(false);
    Require(player.playMusic(wav.string(), -1, 0) && Mix_PlayingMusic(), "a valid replacement still changes the soundtrack");
    Require(player.lastError().empty(), "successful music playback clears an earlier load error");

    struct MusicContext : Context
    {
        AudioManager& player;
        explicit MusicContext(AudioManager& value) : player(value) {}
        bool loadScene(const std::string&) override { return true; }
        void playMusic(const std::string& path, int loops, int fadeMs) override
        {
            Require(player.playMusic(path, loops, fadeMs), "sequence music starts in the mixer");
            audio.push_back(path);
        }
        void stopMusic(int fadeMs) override { player.stopMusic(fadeMs); }
    } context(player);
    auto soundtrack = sequence::SequenceFromJson({{"id", "soundtrack"}, {"start", "first"}, {"nodes", {
        {{"id", "first"}, {"next", "second"}, {"actions", {
            {{"type", "play_music"}, {"id", wav.string()}, {"fade_ms", 0}},
            {{"type", "load_scene"}, {"scene", "first_scene"}},
            {{"type", "wait"}, {"duration", .1}}
        }}},
        {{"id", "second"}, {"next", "third"}, {"actions", {
            {{"type", "load_scene"}, {"scene", "second_scene"}},
            {{"type", "wait"}, {"duration", .1}}
        }}},
        {{"id", "third"}, {"actions", {{{"type", "wait"}, {"duration", .1}}}}}
    }}}, "continuous soundtrack");
    Require(soundtrack.ok, "soundtrack fixture parses");
    sequence::SequenceManager manager;
    Require(manager.startDefinition(soundtrack.sequence, context), "multi-node soundtrack starts");
    for (int frame = 0; frame < 40 && manager.isRunning(); ++frame)
    {
        manager.update(.025f);
        Require(Mix_PlayingMusic() && !Mix_PausedMusic(), "music continues across every node and scene transition");
    }
    Require(manager.status().completed && context.audio.size() == 1, "one music action covers the whole sequence without restarting");
    context.stopMusic(0);
    Require(!Mix_PlayingMusic(), "an explicit music stop still ends playback");
    player.shutdown();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    fs::remove_all(temp);
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--audio")
    {
        TestAudioImport();
        std::cout << "Sequence audio and continuous soundtrack tests passed\n";
        return 0;
    }
    TestPreviewSnapshotAndControls();
    TestIntroHandoff();
    TestTimelineEditing();
    TestSpatialMarkers();
    TestCameraPoses();
    TestAudioImport();
    std::cout << "Sequence tests passed\n";
}
