#pragma once

#include "SequenceDefinition.h"

#include <SDL.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sequence
{
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct ActorSpawnDef
    {
        std::string actor;
        std::string kind;
        std::string ref;
        std::string sprite;
        std::string characterId;
        Vec2 position;
        bool hasPosition = false;
        bool visible = true;
        float rotation = 0.0f;
        float scale = 1.0f;
    };

    struct CameraPose
    {
        Vec2 position;
        float height = 0.56f;
        float angleDegrees = 0.0f;
        float pitch = 0.0f;
    };

    struct CameraFollowSettings
    {
        bool preserveOffset = true;
        bool lookAtActor = true;
        float smoothing = 0.0f;
        float distance = 2.2f;
    };
    CameraFollowSettings CameraFollowFromJson(const nlohmann::json& data);

    struct Status
    {
        bool running = false;
        bool paused = false;
        bool completed = false;
        std::string sequenceId;
        std::string nodeId;
        std::string nextNodeId;
        int actionIndex = -1;
        std::vector<int> activeActionIndices;
        float elapsed = 0.0f;
        float actionElapsed = 0.0f;
    };

    class ISequenceContext
    {
    public:
        virtual ~ISequenceContext() = default;

        virtual void sequenceLog(const std::string& message) = 0;
        virtual bool hasStoryFlag(const std::string& flag) const = 0;
        virtual void setStoryFlag(const std::string& flag) = 0;
        virtual bool loadScene(const std::string& sceneId) = 0;

        virtual bool resolveMarker(const std::string& markerId, Vec2& out) const = 0;
        virtual bool spawnActor(const ActorSpawnDef& def) = 0;
        virtual void removeActor(const std::string& actorId) = 0;
        virtual void setActorVisible(const std::string& actorId, bool visible) = 0;
        virtual bool getActorPosition(const std::string& actorId, Vec2& out) const = 0;
        virtual bool setActorPosition(const std::string& actorId, Vec2 pos) = 0;
        virtual void setActorAnimation(const std::string& actorId, const std::string& animationId) = 0;
        virtual void faceActor(const std::string& actorId, const std::string& directionOrTarget) = 0;

        virtual void setCameraPosition(Vec2 pos) = 0;
        virtual bool resolveCameraMarker(const std::string&, CameraPose&) const { return false; }
        virtual bool getCameraPose(CameraPose&) const { return false; }
        virtual void setCameraPose(const CameraPose& pose) { setCameraPosition(pose.position); }
        virtual void setCameraFollow(const std::string& actorId) = 0;
        virtual void setCameraFollow(const std::string& actorId, const CameraFollowSettings&) { setCameraFollow(actorId); }
        virtual void setCameraShake(float intensity, float duration, float frequency) = 0;

        virtual void setFadeAlpha(float alpha) = 0;
        virtual void setFlashAlpha(float alpha) = 0;
        virtual void showSequenceText(const std::string& text, float duration) = 0;
        virtual void hideSequenceText() = 0;

        virtual void playMusic(const std::string& id, int loops, int fadeMs) = 0;
        virtual void stopMusic(int fadeMs) = 0;
        virtual void playSfx(const std::string& id) = 0;
        virtual void playVoice(const std::string& id) = 0;
        virtual void playSfx(const std::string& id, float volume) { playSfx(id); }
        virtual void playVoice(const std::string& id, float volume) { playVoice(id); }
        virtual void setSequenceMusicVolume(float volume, float fadeSeconds) {}
        virtual void setSequenceSfxVolume(float volume) {}
        virtual void duckSequenceMusic(float volume, float holdSeconds, float fadeOutSeconds, float fadeInSeconds) {}
        virtual void restoreSequenceMusic(float fadeSeconds) {}
        virtual bool pickupSequenceItem(const std::string& item, int count, const std::string& prop, const std::string& onceFlag) { return false; }

        virtual void setTimeOfDay(const std::string& value) = 0;
        virtual void setWeather(const nlohmann::json& data) = 0;
        virtual bool loadMapForSequence(const std::string& mapIdOrPath, const std::string& spawnId) = 0;
        virtual void teleportPlayer(Vec2 pos) = 0;
        virtual void enablePlayerControl(bool enabled) = 0;
        virtual void startDialog(const std::string& npcOrDialogId) = 0;
        virtual void changeRendererMode(const std::string& mode) = 0;
    };

    class SequenceRuntime
    {
    public:
        SequenceRuntime() = default;

        bool start(const SequenceDefinition* definition, ISequenceContext* context);
        bool startAtNode(const SequenceDefinition* definition, ISequenceContext* context, const std::string& nodeId);
        void stop();
        void setPaused(bool paused) { m_paused = paused; }
        void update(float dt);
        void stepAction();
        bool skip();

        const Status status() const;
        bool isRunning() const { return m_running; }
        bool isCompleted() const { return m_completed; }
        const std::string& lastError() const { return m_lastError; }

    private:
        struct ActionRuntime
        {
            const ActionDef* def = nullptr;
            bool started = false;
            bool completed = false;
            float elapsed = 0.0f;
            float delayElapsed = 0.0f;
            Vec2 from;
            Vec2 to;
            bool hasFrom = false;
            CameraPose cameraFrom, cameraTo;
            bool hasCameraPose = false;
            Vec2 cameraTargetOffset;
            bool hasCameraTarget = false;
            std::vector<ActionRuntime> children;
        };

        enum class ActionResult
        {
            Running,
            Complete,
            Jumped,
            Ended
        };

        void enterNode(const std::string& nodeId);
        ActionResult updateAction(ActionRuntime& runtime, float dt, int actionIndex);
        ActionResult updateParallel(ActionRuntime& runtime, float dt);
        ActionResult beginAction(ActionRuntime& runtime, int actionIndex);
        ActionResult tickAction(ActionRuntime& runtime, float dt, int actionIndex);
        void finishNode();
        bool isActionBlocking(const ActionRuntime& runtime) const;
        void collectActiveActions(const ActionRuntime& runtime, int actionIndex, std::vector<int>& out) const;
        void runFinalizationActions(const std::vector<ActionDef>& actions);

        Vec2 readPosition(const nlohmann::json& data, const char* markerKey, const char* arrayKey, const char* xKey, const char* yKey, Vec2 fallback) const;
        float readDuration(const ActionDef& def, float fallback = 0.0f) const;
        CameraPose readCameraPose(const nlohmann::json& data, const char* markerKey, CameraPose fallback) const;

        const SequenceDefinition* m_definition = nullptr;
        const NodeDef* m_node = nullptr;
        ISequenceContext* m_context = nullptr;
        std::vector<ActionRuntime> m_actions;
        int m_currentAction = 0;
        bool m_running = false;
        bool m_paused = false;
        bool m_completed = false;
        bool m_stepRequested = false;
        float m_elapsed = 0.0f;
        std::string m_lastError;
    };
}
