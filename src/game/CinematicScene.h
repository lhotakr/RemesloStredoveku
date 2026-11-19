#pragma once

#include "BuildInteriorEngine.h"
#include "sequence/SequenceRuntime.h"
#include "sequence/SequenceMarkers.h"

#include <SDL.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class CharacterManager;
struct CharacterDef;

class CinematicScene
{
public:
    ~CinematicScene();
    struct Status
    {
        std::string sceneId;
        int actorCount = 0;
        int propCount = 0;
        double cameraX = 0.0;
        double cameraY = 0.0;
        double cameraAngle = 0.0;
        double cameraPitch = 0.0;
        double cameraHeight = 0.56;
        std::string cameraFollowActor;
    };

    bool init(SDL_Window* window, SDL_Renderer* renderer, const CharacterManager* characterManager);
    void shutdown();

    bool loadScene(const std::string& sceneIdOrPath, const std::function<void(const std::string&)>& log, const nlohmann::json* document = nullptr);
    void clear();
    void update(float dt);
    void render();
    bool isImageScene() const { return m_imageScene; }
    SDL_FPoint imageCanvasSize() const { return {m_canvasWidth, m_canvasHeight}; }
    SDL_FRect imageViewport(float width, float height) const;
    bool imageEntityBounds(const std::string& id, SDL_FRect& bounds) const;
    std::vector<std::string> imageLayerOrder() const;

    bool resolveMarker(const std::string& markerId, sequence::Vec2& out) const;
    bool hasActor(const std::string& id) const;
    bool hasProp(const std::string& id) const;
    bool getActorPosition(const std::string& id, sequence::Vec2& out) const;
    bool setActorPosition(const std::string& id, sequence::Vec2 pos);
    bool setActorVisible(const std::string& id, bool visible);
    bool setActorAnimation(const std::string& id, const std::string& animationId, const std::function<void(const std::string&)>& log);
    bool faceActor(const std::string& id, const std::string& directionOrTarget);

    bool upsertActor(const sequence::ActorSpawnDef& def, const std::function<void(const std::string&)>& log);
    bool upsertProp(const sequence::ActorSpawnDef& def, const std::function<void(const std::string&)>& log);
    void removeActorOrProp(const std::string& id);

    void setCameraPosition(sequence::Vec2 pos);
    bool resolveCameraMarker(const std::string& id, sequence::CameraPose& pose) const;
    sequence::CameraPose cameraPose() const;
    void setCameraPose(const sequence::CameraPose& pose);
    void setEditorMarkers(const std::vector<sequence::SequenceMarker>& markers);
    bool setEditorEntity(const nlohmann::json& item, bool prop, const std::function<void(const std::string&)>& log = {});
    const BuildInteriorEngine& environment() const { return m_engine; }
    void setCameraFollow(const std::string& actorId, const sequence::CameraFollowSettings& settings = {});
    void setCameraShake(float strength, float duration, float frequency);

    const std::string& sceneId() const { return m_sceneId; }
    Status status() const;

private:
    struct Entity
    {
        std::string id;
        std::string marker;
        std::string characterId;
        std::string texture;
        std::string animation = "idle_down";
        bool prop = false;
        bool visible = true;
        double x = 0.0;
        double y = 0.0;
        double zOffset = 0.0;
        double markerHeight = 0.0;
        double scale = 0.75;
        double yawDegrees = 0.0;
        bool voxel = false;
        bool floor = false;
        double floorWidth = 0.0, floorDepth = 0.0;
        float animTime = 0.0f;
        int animFrame = 0;
        bool textLayer = false;
        std::string text;
        float width = 0, height = 0, fontSize = 32, opacity = 1;
        int layer = 0;
        SDL_Color color{255, 255, 255, 255};
    };

    bool loadCharacterFramePixels(
        const Entity& entity,
        std::vector<std::uint32_t>& outPixels,
        int& outW,
        int& outH,
        std::string& outAnimationUsed,
        const std::function<void(const std::string&)>& log,
        SDL_FPoint* anchor = nullptr) const;

    bool pushEntityToEngine(Entity& entity, const std::function<void(const std::string&)>& log);
    bool loadImageScene(const nlohmann::json& root, const std::function<void(const std::string&)>& log);
    bool prepareImageEntity(Entity& entity, const std::function<void(const std::string&)>& log);
    void renderImageScene();
    void clearImageScene();
    struct ImageTexture { SDL_Texture* texture = nullptr; int width = 0, height = 0; };
    bool m_imageScene = false;
    float m_canvasWidth = 1672, m_canvasHeight = 941;
    SDL_Texture* m_backgroundImage = nullptr;
    SDL_Texture* m_fontAtlas = nullptr;
    int m_fontAtlasId = -1, m_fontAtlasWidth = 0, m_fontAtlasHeight = 0;
    std::unordered_map<std::string, ImageTexture> m_imageTextures;
    void updateCamera(float dt);
    sequence::Vec2 markerOrPosition(const nlohmann::json& root, const char* markerKey, sequence::Vec2 fallback) const;

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    const CharacterManager* m_characterManager = nullptr;
    BuildInteriorEngine m_engine;
    std::string m_sceneId;
    std::unordered_map<std::string, sequence::SequenceMarker> m_markers;
    std::unordered_map<std::string, Entity> m_entities;
    std::string m_cameraFollowActor;
    sequence::CameraFollowSettings m_cameraFollowSettings;
    sequence::Vec2 m_cameraFollowOffset;
    bool m_cameraFollowNeedsOffset = false;
    double m_cameraX = 2.5;
    double m_cameraY = 2.5;
    double m_cameraAngle = 0.0;
    double m_cameraPitch = 0.0;
    double m_cameraHeight = 0.56;
    double m_cameraShakeStrength = 0.0;
    double m_cameraShakeRemaining = 0.0;
    double m_cameraShakeFrequency = 18.0;
    double m_cameraShakePhase = 0.0;
};
