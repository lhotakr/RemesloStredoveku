#include "CinematicScene.h"

#include "CharacterManager.h"
#include "JsonUtils.h"
#include "PathUtils.h"

#include <SDL_image.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
    constexpr double kPiLocal = 3.14159265358979323846;

    static fs::path ResolveScenePath(const std::string& sceneIdOrPath)
    {
        fs::path p(sceneIdOrPath);
        if (p.is_absolute())
            return p;
        if (p.has_parent_path())
            return pathutils::ProjectRoot() / p;
        if (p.extension().empty())
            p.replace_extension(".json");
        return pathutils::AssetsDir() / "sequences" / "scenes" / p;
    }

    static fs::path ResolveInteriorPath(const std::string& interior)
    {
        fs::path p(interior);
        if (p.is_absolute() || interior.rfind("castle:", 0) == 0)
            return p;
        if (p.has_parent_path())
            return pathutils::ProjectRoot() / p;
        if (p.extension().empty())
            p.replace_extension(".json");
        return pathutils::DataDir() / "interiors" / p;
    }

    static double DegToRad(double degrees)
    {
        return degrees * kPiLocal / 180.0;
    }

    static std::string JsonStringAny(const json& root, std::initializer_list<const char*> keys)
    {
        for (const char* key : keys)
        {
            if (root.contains(key) && root.at(key).is_string())
                return root.at(key).get<std::string>();
        }
        return {};
    }

    static double JsonDoubleAny(const json& root, double fallback, std::initializer_list<const char*> keys)
    {
        for (const char* key : keys)
        {
            if (root.contains(key) && root.at(key).is_number())
                return root.at(key).get<double>();
        }
        return fallback;
    }
}

bool CinematicScene::init(SDL_Window* window, SDL_Renderer* renderer, const CharacterManager* characterManager)
{
    m_window = window;
    m_renderer = renderer;
    m_characterManager = characterManager;
    if (!m_engine.init(window, renderer))
        return false;
    m_engine.setEditorMode(false);
    m_engine.setRuntimeOverlayVisible(false);
    m_engine.setMouseLookSuppressed(true);
    return true;
}

void CinematicScene::shutdown()
{
    clear();
    m_engine.shutdown();
    m_window = nullptr;
    m_renderer = nullptr;
    m_characterManager = nullptr;
}

void CinematicScene::clear()
{
    clearImageScene();
    m_engine.clearRuntimeSprites();
    m_entities.clear();
    m_markers.clear();
    m_sceneId.clear();
    m_cameraFollowActor.clear();
    m_cameraFollowOffset = {};
    m_cameraFollowNeedsOffset = false;
    m_cameraShakeRemaining = 0;
    m_cameraShakePhase = 0;
}

bool CinematicScene::loadScene(const std::string& sceneIdOrPath, const std::function<void(const std::string&)>& log, const json* document)
{
    const fs::path path = ResolveScenePath(sceneIdOrPath);
    json root;
    std::string err;
    if (document) root = *document;
    else if (!jsonutils::LoadJsonFileSafe(path.string(), root, err))
    {
        if (log) log("Cinematic scene load failed: " + path.string() + " | " + err);
        return false;
    }

    if (!root.is_object())
    {
        if (log) log("Cinematic scene must contain a JSON object: " + path.string());
        return false;
    }
    clear();
    m_sceneId = root.value("id", path.stem().string());

    m_imageScene = root.value("type", std::string()) == "image_2d";
    if (m_imageScene)
    {
        if (!loadImageScene(root, log)) return false;
    }
    else
    {
        const std::string interior = root.value("interior", root.value("background", std::string("test_chamber")));
        const fs::path interiorPath = ResolveInteriorPath(interior);
        if (!m_engine.loadInterior(interiorPath.string()))
        {
            if (log) log("2.5D interior load failed for scene '" + m_sceneId + "': " + m_engine.lastError());
            return false;
        }
        m_cameraHeight = m_engine.sceneEyeHeight();
        m_engine.getPlayerPose(m_cameraX, m_cameraY, m_cameraAngle, m_cameraPitch);

    }

    if (root.contains("markers") && root["markers"].is_array())
    {
        for (const auto& jm : root["markers"])
        {
            if (!jm.is_object())
                continue;
            auto marker = sequence::MarkerFromJson(jm);
            if (!marker.id.empty())
                m_markers[marker.id] = marker;
        }
    }

    if (!m_imageScene && root.contains("camera") && root["camera"].is_object())
    {
        const auto& cam = root["camera"];
        sequence::Vec2 pos = markerOrPosition(cam, "marker", sequence::Vec2{2.5f, 2.5f});
        m_cameraX = pos.x;
        m_cameraY = pos.y;
        m_cameraAngle = DegToRad(cam.value("angle_degrees", cam.value("angle", 0.0)));
        m_cameraPitch = cam.value("pitch", 0.0);
        sequence::CameraPose markerPose;
        if (resolveCameraMarker(cam.value("marker", std::string()), markerPose))
        {
            m_cameraHeight = markerPose.height;
            m_cameraAngle = DegToRad(markerPose.angleDegrees);
            m_cameraPitch = markerPose.pitch;
        }
        m_cameraHeight = cam.value("height", m_cameraHeight);
        m_engine.setSceneEyeHeight(m_cameraHeight);
        m_cameraFollowActor = cam.value("follow", std::string());
        m_cameraFollowSettings = sequence::CameraFollowFromJson(cam);
        m_cameraFollowNeedsOffset = !m_cameraFollowActor.empty();
        m_engine.setPlayerPose(m_cameraX, m_cameraY, m_cameraAngle, m_cameraPitch);
    }

    auto loadEntityArray = [&](const char* key, bool prop)
    {
        if (!root.contains(key) || !root[key].is_array())
            return;
        for (const auto& item : root[key])
        {
            if (!item.is_object())
                continue;
            setEditorEntity(item, prop, log);
        }
    };

    loadEntityArray("props", true);
    loadEntityArray("actors", false);
    return true;
}

void CinematicScene::update(float dt)
{
    if (!m_imageScene) m_engine.update(dt);

    for (auto& kv : m_entities)
    {
        Entity& entity = kv.second;
        if (entity.characterId.empty() || !entity.visible)
            continue;
        const CharacterDef* ch = m_characterManager ? m_characterManager->getCharacter(entity.characterId) : nullptr;
        if (!ch)
            continue;
        auto animIt = ch->animations.find(entity.animation);
        if (animIt == ch->animations.end())
            animIt = ch->animations.find("idle_down");
        if (animIt == ch->animations.end() || animIt->second.frames.size() <= 1)
            continue;
        const auto& anim = animIt->second;
        const float frameDuration = anim.fps > 0 ? 1.0f / static_cast<float>(anim.fps) : 0.15f;
        entity.animTime += std::max(0.0f, dt);
        if (entity.animTime >= frameDuration)
        {
            const int steps = static_cast<int>(entity.animTime / frameDuration);
            entity.animTime = std::fmod(entity.animTime, frameDuration);
            const int nextFrame = entity.animFrame + steps;
            const int frame = anim.loop ? nextFrame % static_cast<int>(anim.frames.size())
                : std::min(nextFrame, static_cast<int>(anim.frames.size()) - 1);
            if (frame != entity.animFrame)
            {
                entity.animFrame = frame;
                pushEntityToEngine(entity, nullptr);
            }
        }
    }

    if (!m_imageScene) updateCamera(dt);
}

void CinematicScene::render()
{
    if (m_imageScene) renderImageScene();
    else m_engine.render();
}

bool CinematicScene::resolveMarker(const std::string& markerId, sequence::Vec2& out) const
{
    const auto it = m_markers.find(markerId);
    if (it == m_markers.end())
        return false;
    out.x = static_cast<float>(it->second.x);
    out.y = static_cast<float>(it->second.y);
    return true;
}

bool CinematicScene::hasActor(const std::string& id) const
{
    const auto it = m_entities.find(id);
    return it != m_entities.end() && !it->second.prop;
}

bool CinematicScene::hasProp(const std::string& id) const
{
    const auto it = m_entities.find(id);
    return it != m_entities.end() && it->second.prop;
}

bool CinematicScene::getActorPosition(const std::string& id, sequence::Vec2& out) const
{
    const auto it = m_entities.find(id);
    if (it == m_entities.end())
    {
        double x, y, z;
        if (m_imageScene || !m_engine.getRuntimeSpritePosition(id, x, y, z)) return false;
        out = {static_cast<float>(x), static_cast<float>(y)};
        return true;
    }
    out.x = static_cast<float>(it->second.x);
    out.y = static_cast<float>(it->second.y);
    return true;
}

bool CinematicScene::setActorPosition(const std::string& id, sequence::Vec2 pos)
{
    auto it = m_entities.find(id);
    if (it == m_entities.end())
        return false;
    it->second.x = pos.x;
    it->second.y = pos.y;
    return m_imageScene || m_engine.setRuntimeSpritePosition(id, it->second.x, it->second.y, it->second.zOffset);
}

bool CinematicScene::setActorVisible(const std::string& id, bool visible)
{
    auto it = m_entities.find(id);
    if (it == m_entities.end())
        return !m_imageScene && m_engine.setRuntimeSpriteVisible(id, visible);
    it->second.visible = visible;
    return m_imageScene || m_engine.setRuntimeSpriteVisible(id, visible);
}

bool CinematicScene::setActorAnimation(const std::string& id, const std::string& animationId, const std::function<void(const std::string&)>& log)
{
    auto it = m_entities.find(id);
    if (it == m_entities.end())
        return false;
    it->second.animation = animationId.empty() ? "idle_down" : animationId;
    it->second.animFrame = 0;
    it->second.animTime = 0.0f;
    return pushEntityToEngine(it->second, log);
}

bool CinematicScene::faceActor(const std::string& id, const std::string& directionOrTarget)
{
    auto it = m_entities.find(id);
    if (it == m_entities.end())
        return false;

    if (directionOrTarget == "left")
        it->second.yawDegrees = 270.0;
    else if (directionOrTarget == "right")
        it->second.yawDegrees = 90.0;
    else if (directionOrTarget == "up")
        it->second.yawDegrees = 180.0;
    else if (directionOrTarget == "down")
        it->second.yawDegrees = 0.0;
    else
    {
        sequence::Vec2 target;
        if (!getActorPosition(directionOrTarget, target))
            return false;
        it->second.yawDegrees = std::atan2(target.y - it->second.y, target.x - it->second.x) * 180.0 / kPiLocal;
    }
    return pushEntityToEngine(it->second, nullptr);
}

bool CinematicScene::upsertActor(const sequence::ActorSpawnDef& def, const std::function<void(const std::string&)>& log)
{
    Entity entity;
    entity.id = def.actor;
    entity.prop = false;
    entity.characterId = def.characterId;
    entity.texture = def.sprite;
    entity.visible = def.visible;
    entity.x = def.position.x;
    entity.y = def.position.y;
    entity.scale = def.scale <= 0.0f ? 0.80 : def.scale;
    entity.yawDegrees = def.rotation;
    if (entity.id.empty())
        return false;
    if (!pushEntityToEngine(entity, log))
        return false;
    m_entities[entity.id] = std::move(entity);
    return true;
}

bool CinematicScene::upsertProp(const sequence::ActorSpawnDef& def, const std::function<void(const std::string&)>& log)
{
    Entity entity;
    entity.id = def.actor;
    entity.prop = true;
    entity.texture = def.sprite.empty() ? def.ref : def.sprite;
    entity.visible = def.visible;
    entity.x = def.position.x;
    entity.y = def.position.y;
    entity.scale = def.scale <= 0.0f ? 0.85 : def.scale;
    entity.yawDegrees = def.rotation;
    if (entity.id.empty())
        return false;
    if (!pushEntityToEngine(entity, log))
        return false;
    m_entities[entity.id] = std::move(entity);
    return true;
}

void CinematicScene::removeActorOrProp(const std::string& id)
{
    if (auto it = m_imageTextures.find(id); it != m_imageTextures.end())
    {
        SDL_DestroyTexture(it->second.texture);
        m_imageTextures.erase(it);
    }
    m_entities.erase(id);
    m_engine.removeRuntimeSprite(id);
}

void CinematicScene::setCameraPosition(sequence::Vec2 pos)
{
    m_cameraFollowActor.clear();
    m_cameraX = pos.x;
    m_cameraY = pos.y;
    m_engine.setPlayerPose(m_cameraX, m_cameraY, m_cameraAngle, m_cameraPitch);
}

bool CinematicScene::resolveCameraMarker(const std::string& id, sequence::CameraPose& pose) const
{
    const auto it = m_markers.find(id);
    if (it == m_markers.end() || it->second.type != "camera") return false;
    const auto& marker = it->second;
    pose = {{marker.x, marker.y}, marker.z, marker.angleDegrees, marker.pitch};
    return true;
}

sequence::CameraPose CinematicScene::cameraPose() const
{
    return {{static_cast<float>(m_cameraX), static_cast<float>(m_cameraY)}, static_cast<float>(m_cameraHeight),
        static_cast<float>(m_cameraAngle * 180.0 / 3.14159265358979323846), static_cast<float>(m_cameraPitch)};
}

void CinematicScene::setCameraPose(const sequence::CameraPose& pose)
{
    if (!std::isfinite(pose.position.x) || !std::isfinite(pose.position.y) || !std::isfinite(pose.height) ||
        !std::isfinite(pose.angleDegrees) || !std::isfinite(pose.pitch)) return;
    m_cameraAngle = DegToRad(pose.angleDegrees);
    m_cameraPitch = std::clamp(static_cast<double>(pose.pitch), -0.52, 0.52);
    m_cameraHeight = std::clamp(static_cast<double>(pose.height), 0.05, 128.0);
    m_engine.setSceneEyeHeight(m_cameraHeight);
    setCameraPosition(pose.position);
}

bool CinematicScene::setEditorEntity(const json& item, bool prop, const std::function<void(const std::string&)>& log)
{
    if (!item.is_object() || item.value("id", std::string()).empty()) return false;
    Entity entity;
    entity.id = item.value("id", "");
    entity.prop = prop;
    entity.characterId = JsonStringAny(item, {"character", "character_id"});
    entity.texture = JsonStringAny(item, {"texture", "sprite", "object"});
    entity.animation = item.value("animation", std::string("idle_down"));
    entity.visible = item.value("visible", true);
    entity.scale = item.value("scale", m_imageScene ? 1.0 : (prop ? 0.85 : 0.80));
    entity.textLayer = item.value("type", std::string()) == "text";
    entity.text = item.value("text", std::string());
    entity.width = std::clamp(item.value("width", 0.0f), 0.0f, 32768.0f);
    entity.height = std::clamp(item.value("height", 0.0f), 0.0f, 32768.0f);
    entity.fontSize = std::clamp(item.value("font_size", 32.0f), 1.0f, 512.0f);
    entity.layer = item.value("layer", 0);
    entity.opacity = std::clamp(item.value("opacity", 1.0f), 0.0f, 1.0f);
    if (item.contains("color") && item["color"].is_array() && item["color"].size() >= 3)
    {
        auto channel = [&](int i) { return static_cast<Uint8>(std::clamp(item["color"][i].get<int>(), 0, 255)); };
        entity.color = {channel(0), channel(1), channel(2), item["color"].size() > 3 ? channel(3) : Uint8(255)};
    }
    entity.zOffset = item.value("z_offset", 0.0);
    entity.marker = item.value("marker", std::string());
    if (const auto found = m_markers.find(entity.marker); found != m_markers.end())
    {
        entity.markerHeight = found->second.z;
        entity.zOffset += entity.markerHeight;
    }
    entity.yawDegrees = item.value("yaw_degrees", item.value("rotation", 0.0));
    const auto renderMode = item.value("render_mode", item.value("voxel", false) ? std::string("voxel") : std::string("billboard"));
    entity.voxel = renderMode == "voxel";
    entity.floor = renderMode == "floor";
    entity.floorWidth = std::clamp(item.value("floor_width", 0.0), 0.0, 128.0);
    entity.floorDepth = std::clamp(item.value("floor_depth", 0.0), 0.0, 128.0);
    const auto pos = markerOrPosition(item, "marker", {2.5f, 2.5f});
    entity.x = pos.x; entity.y = pos.y;
    const bool rendered = pushEntityToEngine(entity, log);
    if (!rendered) m_engine.removeRuntimeSprite(entity.id);
    m_entities[entity.id] = std::move(entity);
    return rendered;
}

void CinematicScene::setEditorMarkers(const std::vector<sequence::SequenceMarker>& markers)
{
    m_markers.clear();
    for (const auto& marker : markers) m_markers[marker.id] = marker;
    for (auto& [id, entity] : m_entities)
    {
        const auto found = m_markers.find(entity.marker);
        if (found == m_markers.end()) continue;
        const auto& marker = found->second;
        entity.x = marker.x; entity.y = marker.y;
        entity.zOffset += marker.z - entity.markerHeight;
        const bool heightChanged = std::abs(marker.z - entity.markerHeight) > 0.00001;
        entity.markerHeight = marker.z;
        if (heightChanged) pushEntityToEngine(entity, {});
        else if (!m_imageScene) m_engine.setRuntimeSpritePosition(id, entity.x, entity.y);
    }
}

void CinematicScene::setCameraFollow(const std::string& actorId, const sequence::CameraFollowSettings& settings)
{
    m_cameraFollowActor = actorId;
    m_cameraFollowSettings = settings;
    m_cameraFollowNeedsOffset = !actorId.empty();
    sequence::Vec2 position;
    if (!actorId.empty() && getActorPosition(actorId, position))
    {
        m_cameraFollowOffset = {float(m_cameraX - position.x), float(m_cameraY - position.y)};
        if (!settings.preserveOffset)
        {
            const float length = std::hypot(m_cameraFollowOffset.x, m_cameraFollowOffset.y);
            if (length > .0001f)
            {
                m_cameraFollowOffset.x *= settings.distance / length;
                m_cameraFollowOffset.y *= settings.distance / length;
            }
            else m_cameraFollowOffset = {float(-std::cos(m_cameraAngle) * settings.distance), float(-std::sin(m_cameraAngle) * settings.distance)};
        }
        m_cameraFollowNeedsOffset = false;
    }
}

void CinematicScene::setCameraShake(float strength, float duration, float frequency)
{
    m_cameraShakeStrength = std::max(0.0f, strength);
    m_cameraShakeRemaining = std::max(0.0f, duration);
    m_cameraShakeFrequency = std::max(1.0f, frequency);
}

CinematicScene::Status CinematicScene::status() const
{
    Status out;
    out.sceneId = m_sceneId;
    for (const auto& kv : m_entities)
    {
        if (kv.second.prop) ++out.propCount;
        else ++out.actorCount;
    }
    out.cameraX = m_cameraX;
    out.cameraY = m_cameraY;
    out.cameraAngle = m_cameraAngle;
    out.cameraPitch = m_cameraPitch;
    out.cameraHeight = m_cameraHeight;
    out.cameraFollowActor = m_cameraFollowActor;
    return out;
}

bool CinematicScene::loadCharacterFramePixels(
    const Entity& entity,
    std::vector<std::uint32_t>& outPixels,
    int& outW,
    int& outH,
    std::string& outAnimationUsed,
    const std::function<void(const std::string&)>& log,
    SDL_FPoint* anchor) const
{
    outPixels.clear();
    outW = 0;
    outH = 0;
    const CharacterDef* ch = m_characterManager ? m_characterManager->getCharacter(entity.characterId) : nullptr;
    if (!ch)
        return false;

    auto animIt = ch->animations.find(entity.animation);
    outAnimationUsed = entity.animation;
    if (animIt == ch->animations.end() || animIt->second.frames.empty())
    {
        if (log) log("Unknown animation '" + entity.animation + "' for character '" + entity.characterId + "', using idle_down.");
        animIt = ch->animations.find("idle_down");
        outAnimationUsed = "idle_down";
    }
    if (animIt == ch->animations.end() || animIt->second.frames.empty())
        return false;

    const int frameIndex = std::clamp(entity.animFrame, 0, static_cast<int>(animIt->second.frames.size()) - 1);
    const AnimFrame& frame = animIt->second.frames[frameIndex];
    const fs::path sheetPath = pathutils::AssetsDir() / "characters" / ch->sheet;
    SDL_Surface* loaded = IMG_Load(sheetPath.string().c_str());
    if (!loaded)
        return false;
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(loaded);
    if (!converted)
        return false;

    outW = frame.w;
    outH = frame.h;
    if (anchor)
    {
        const auto pivot = ch->frameAnchor(frame);
        *anchor = {pivot.x / frame.w, pivot.y / frame.h};
    }
    outPixels.assign(static_cast<std::size_t>(outW) * outH, 0u);
    for (int y = 0; y < outH; ++y)
    {
        const int srcY = frame.y + y;
        if (srcY < 0 || srcY >= converted->h)
            continue;
        const auto* src = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(converted->pixels) + srcY * converted->pitch);
        for (int x = 0; x < outW; ++x)
        {
            const int srcX = frame.x + x;
            if (srcX >= 0 && srcX < converted->w)
                outPixels[static_cast<std::size_t>(y) * outW + x] = src[srcX];
        }
    }
    SDL_FreeSurface(converted);
    return true;
}

bool CinematicScene::pushEntityToEngine(Entity& entity, const std::function<void(const std::string&)>& log)
{
    if (m_imageScene) return prepareImageEntity(entity, log);
    BuildInteriorEngine::RuntimeSpriteDef sprite;
    sprite.id = entity.id;
    sprite.texturePath = entity.texture;
    sprite.x = entity.x;
    sprite.y = entity.y;
    sprite.zOffset = entity.zOffset;
    sprite.scale = entity.scale;
    sprite.yawDegrees = entity.yawDegrees;
    sprite.visible = entity.visible;
    sprite.voxel = entity.voxel;
    sprite.floor = entity.floor;
    sprite.floorWidth = entity.floorWidth;
    sprite.floorDepth = entity.floorDepth;

    if (!entity.characterId.empty())
    {
        std::string animUsed;
        if (loadCharacterFramePixels(entity, sprite.pixels, sprite.pixelW, sprite.pixelH, animUsed, log, &sprite.billboardAnchor))
            entity.animation = animUsed;
    }

    if (sprite.pixels.empty() && sprite.texturePath.empty())
    {
        if (log) log("Cinematic entity '" + entity.id + "' has no character frame or texture.");
        return false;
    }

    if (!m_engine.upsertRuntimeSprite(sprite))
    {
        if (log) log("Failed to create cinematic sprite: " + entity.id);
        return false;
    }
    return true;
}

void CinematicScene::updateCamera(float dt)
{
    if (!m_cameraFollowActor.empty())
    {
        sequence::Vec2 pos;
        if (getActorPosition(m_cameraFollowActor, pos))
        {
            if (m_cameraFollowNeedsOffset) setCameraFollow(m_cameraFollowActor, m_cameraFollowSettings);
            const double follow = m_cameraFollowSettings.smoothing <= 0 ? 1.0 :
                1.0 - std::exp(-std::max(0.0f, dt) * m_cameraFollowSettings.smoothing);
            m_cameraX += (pos.x + m_cameraFollowOffset.x - m_cameraX) * follow;
            m_cameraY += (pos.y + m_cameraFollowOffset.y - m_cameraY) * follow;
            if (m_cameraFollowSettings.lookAtActor && std::hypot(pos.x - m_cameraX, pos.y - m_cameraY) > .0001)
                m_cameraAngle = std::atan2(pos.y - m_cameraY, pos.x - m_cameraX);
        }
    }

    double shakeX = 0.0;
    double shakeY = 0.0;
    if (m_cameraShakeRemaining > 0.0)
    {
        m_cameraShakeRemaining = std::max(0.0, m_cameraShakeRemaining - static_cast<double>(dt));
        m_cameraShakePhase += static_cast<double>(dt) * m_cameraShakeFrequency;
        shakeX = std::sin(m_cameraShakePhase * 1.7) * m_cameraShakeStrength * 0.04;
        shakeY = std::cos(m_cameraShakePhase * 2.1) * m_cameraShakeStrength * 0.04;
    }

    m_engine.setSceneEyeHeight(m_cameraHeight);
    m_engine.setPlayerPose(m_cameraX + shakeX, m_cameraY + shakeY, m_cameraAngle, m_cameraPitch);
}

sequence::Vec2 CinematicScene::markerOrPosition(const json& root, const char* markerKey, sequence::Vec2 fallback) const
{
    const std::string marker = root.value(markerKey, std::string());
    if (!marker.empty())
    {
        sequence::Vec2 out;
        if (resolveMarker(marker, out))
            return out;
    }

    if (root.contains("position") && root["position"].is_array() && root["position"].size() >= 2)
        return sequence::Vec2{root["position"][0].get<float>(), root["position"][1].get<float>()};

    fallback.x = static_cast<float>(JsonDoubleAny(root, fallback.x, {"x"}));
    fallback.y = static_cast<float>(JsonDoubleAny(root, fallback.y, {"y"}));
    return fallback;
}
