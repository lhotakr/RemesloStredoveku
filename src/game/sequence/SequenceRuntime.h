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

    struct Status
    {
        bool running = false;
        bool paused = false;
        bool completed = false;
        std::string sequenceId;
        std::string nodeId;
        int actionIndex = -1;
        float actionElapsed = 0.0f;
    };

    class ISequenceContext
    {
    public:
        virtual ~ISequenceContext() = default;

        virtual void sequenceLog(const std::string& message) = 0;
        virtual bool hasStoryFlag(const std::string& flag) const = 0;
        virtual void setStoryFlag(const std::string& flag) = 0;

        virtual bool resolveMarker(const std::string& markerId, Vec2& out) const = 0;
        virtual bool spawnActor(const ActorSpawnDef& def) = 0;
        virtual void removeActor(const std::string& actorId) = 0;
        virtual bool getActorPosition(const std::string& actorId, Vec2& out) const = 0;
        virtual bool setActorPosition(const std::string& actorId, Vec2 pos) = 0;
        virtual void setActorAnimation(const std::string& actorId, const std::string& animationId) = 0;
        virtual void faceActor(const std::string& actorId, const std::string& directionOrTarget) = 0;

        virtual void setCameraPosition(Vec2 pos) = 0;
        virtual void setCameraFollow(const std::string& actorId) = 0;
        virtual void setCameraShake(float intensity, float duration) = 0;

        virtual void setFadeAlpha(float alpha) = 0;
        virtual void setFlashAlpha(float alpha) = 0;
        virtual void showSequenceText(const std::string& text, float duration) = 0;
        virtual void hideSequenceText() = 0;

        virtual void playMusic(const std::string& id, int loops, int fadeMs) = 0;
        virtual void stopMusic(int fadeMs) = 0;
        virtual void playSfx(const std::string& id) = 0;
        virtual void playVoice(const std::string& id) = 0;

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
        void stop();
        void setPaused(bool paused) { m_paused = paused; }
        void update(float dt);
        void stepAction();

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

        Vec2 readPosition(const nlohmann::json& data, const char* markerKey, const char* arrayKey, const char* xKey, const char* yKey, Vec2 fallback) const;
        float readDuration(const ActionDef& def, float fallback = 0.0f) const;

        const SequenceDefinition* m_definition = nullptr;
        const NodeDef* m_node = nullptr;
        ISequenceContext* m_context = nullptr;
        std::vector<ActionRuntime> m_actions;
        int m_currentAction = 0;
        bool m_running = false;
        bool m_paused = false;
        bool m_completed = false;
        bool m_stepRequested = false;
        std::string m_lastError;
    };
}
