#include "Campaign.h"

#include "PathUtils.h"
#include "Utf8.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace
{
    static float Clamp01(float v)
    {
        return std::clamp(v, 0.0f, 1.0f);
    }

    static std::filesystem::path ResolveSequenceMapPath(const std::string& map)
    {
        if (map.empty())
            return {};

        std::filesystem::path p(map);
        if (p.is_absolute())
            return p;

        if (p.has_parent_path())
            return pathutils::ProjectRoot() / p;

        if (p.extension().empty())
            p.replace_extension(".rvm");

        return pathutils::MapsDir() / p;
    }

    static int HourForNamedTime(const std::string& value, int fallback)
    {
        if (value == "dawn") return 5;
        if (value == "morning") return 8;
        if (value == "forenoon") return 10;
        if (value == "noon") return 12;
        if (value == "afternoon") return 15;
        if (value == "late_afternoon" || value == "late afternoon") return 17;
        if (value == "evening") return 20;
        if (value == "night") return 23;
        return fallback;
    }
}

void Campaign::updateSequenceRuntime(float dt)
{
    if (m_sequenceTextRemaining > 0.0f)
    {
        m_sequenceTextRemaining = std::max(0.0f, m_sequenceTextRemaining - dt);
        if (m_sequenceTextRemaining <= 0.0f)
            m_sequenceText.clear();
    }

    if (m_sequenceCameraShakeRemaining > 0.0f)
        m_sequenceCameraShakeRemaining = std::max(0.0f, m_sequenceCameraShakeRemaining - dt);

    m_sequenceManager.update(dt);

    if (m_sequenceCameraFollow && !m_sequenceCameraActor.empty())
    {
        sequence::Vec2 pos;
        if (getActorPosition(m_sequenceCameraActor, pos))
        {
            m_sequenceCameraPos = pos;
            m_sequenceCameraOverride = true;
        }
    }
}

void Campaign::renderSequenceActors()
{
    for (const auto& kv : m_sequenceActors)
    {
        const auto& actor = kv.second;
        if (!actor.visible)
            continue;

        if (!actor.sprite.empty())
        {
            const auto* def = m_objCatalog.FindById(actor.sprite);
            SDL_Texture* tex = def ? textureForObject(*def) : nullptr;
            if (def && tex)
            {
                const int sx = (int)std::lround(actor.x) - m_camX;
                const int sy = (int)std::lround(actor.y) - m_camY;
                gameobj::RenderObjectAtPivot(m_renderer, tex, *def, sx, sy, actor.scale * def->scale);
                continue;
            }
        }

        const std::string characterId = !actor.characterId.empty() ? actor.characterId : m_player.characterId;
        const CharacterDef* ch = m_characterManager.getCharacter(characterId);
        SDL_Texture* tex = m_characterManager.getTextureForCharacter(characterId);
        if (ch && tex)
        {
            std::string anim = actor.animation.empty() ? "idle_down" : actor.animation;
            auto it = ch->animations.find(anim);
            if (it == ch->animations.end() || it->second.frames.empty())
                it = ch->animations.find("idle_down");
            if (it != ch->animations.end() && !it->second.frames.empty())
            {
                const auto& fr = it->second.frames[0];
                SDL_Rect src{fr.x, fr.y, fr.w, fr.h};
                SDL_Rect dst{
                    (int)std::lround(actor.x - fr.w * actor.scale * 0.5f) - m_camX,
                    (int)std::lround(actor.y - fr.h * actor.scale) - m_camY,
                    (int)std::lround(fr.w * actor.scale),
                    (int)std::lround(fr.h * actor.scale)
                };
                SDL_RenderCopy(m_renderer, tex, &src, &dst);
                continue;
            }
        }

        SDL_Rect fallback{
            (int)std::lround(actor.x) - m_camX - 8,
            (int)std::lround(actor.y) - m_camY - 16,
            16,
            16
        };
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 255, 220, 80, 190);
        SDL_RenderFillRect(m_renderer, &fallback);
    }
}

void Campaign::renderSequenceOverlay()
{
    int screenW = 0, screenH = 0;
    SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    if (m_sequenceFadeAlpha > 0.001f)
    {
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, (Uint8)std::lround(255.0f * Clamp01(m_sequenceFadeAlpha)));
        SDL_Rect r{0, 0, screenW, screenH};
        SDL_RenderFillRect(m_renderer, &r);
    }

    if (m_sequenceFlashAlpha > 0.001f)
    {
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, (Uint8)std::lround(255.0f * Clamp01(m_sequenceFlashAlpha)));
        SDL_Rect r{0, 0, screenW, screenH};
        SDL_RenderFillRect(m_renderer, &r);
    }

    if (!m_sequenceText.empty())
    {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const ImVec2 textSize = ImGui::CalcTextSize(m_sequenceText.c_str());
        const float x = (screenW - textSize.x) * 0.5f;
        const float y = screenH * 0.16f;
        fg->AddText(ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 220), m_sequenceText.c_str());
        fg->AddText(ImVec2(x, y), IM_COL32(245, 238, 220, 255), m_sequenceText.c_str());
    }
}

void Campaign::loadSequenceMarkersForCurrentMap()
{
    std::string err;
    if (!sequence::LoadMarkersForMap(m_currentMapPath, m_sequenceMarkers, &err))
        consoleLog("Sequence markers: " + err);
}

void Campaign::clearSequenceActors()
{
    m_sequenceActors.clear();
    m_sequenceActorAliases.clear();
}

void Campaign::sequenceLog(const std::string& message)
{
    consoleLog("[sequence] " + message);
    SDL_Log("[sequence] %s", message.c_str());
}

bool Campaign::resolveMarker(const std::string& markerId, sequence::Vec2& out) const
{
    const auto* marker = sequence::FindMarker(m_sequenceMarkers, markerId);
    if (!marker)
        return false;
    out.x = marker->x;
    out.y = marker->y;
    return true;
}

bool Campaign::spawnActor(const sequence::ActorSpawnDef& def)
{
    if (def.actor.empty())
    {
        sequenceLog("spawn_actor missing actor id.");
        return false;
    }

    if (def.kind == "player")
    {
        m_sequenceActorAliases[def.actor] = "player";
        if (def.hasPosition)
            setPlayerPosition(def.position.x, def.position.y);
        return true;
    }

    if (def.kind == "npc")
    {
        m_sequenceActorAliases[def.actor] = "npc:" + (def.ref.empty() ? def.actor : def.ref);
        if (def.hasPosition)
            setActorPosition(def.actor, def.position);
        return true;
    }

    SequenceActorRuntime actor;
    actor.id = def.actor;
    actor.kind = def.kind.empty() ? "cinematic" : def.kind;
    actor.ref = def.ref;
    actor.sprite = def.sprite;
    actor.characterId = def.characterId;
    actor.visible = def.visible;
    actor.rotation = def.rotation;
    actor.scale = def.scale <= 0.0f ? 1.0f : def.scale;
    actor.x = def.position.x;
    actor.y = def.position.y;
    m_sequenceActors[actor.id] = std::move(actor);
    m_sequenceActorAliases[def.actor] = "temp:" + def.actor;
    return true;
}

void Campaign::removeActor(const std::string& actorId)
{
    m_sequenceActors.erase(actorId);
    m_sequenceActorAliases.erase(actorId);
}

bool Campaign::getActorPosition(const std::string& actorId, sequence::Vec2& out) const
{
    std::string resolved = actorId;
    const auto alias = m_sequenceActorAliases.find(actorId);
    if (alias != m_sequenceActorAliases.end())
        resolved = alias->second;

    if (resolved == "player")
    {
        out.x = m_player.x;
        out.y = m_player.y;
        return true;
    }

    if (resolved.rfind("npc:", 0) == 0)
    {
        const std::string id = resolved.substr(4);
        for (const auto& npc : m_npcManager.npcs())
        {
            if (npc.id == id || npc.npcId == id)
            {
                out.x = npc.x;
                out.y = npc.y;
                return true;
            }
        }
        return false;
    }

    if (resolved.rfind("temp:", 0) == 0)
        resolved = resolved.substr(5);

    const auto it = m_sequenceActors.find(resolved);
    if (it == m_sequenceActors.end())
        return false;
    out.x = it->second.x;
    out.y = it->second.y;
    return true;
}

bool Campaign::setActorPosition(const std::string& actorId, sequence::Vec2 pos)
{
    std::string resolved = actorId;
    const auto alias = m_sequenceActorAliases.find(actorId);
    if (alias != m_sequenceActorAliases.end())
        resolved = alias->second;

    if (resolved == "player")
    {
        setPlayerPosition(pos.x, pos.y);
        return true;
    }

    if (resolved.rfind("npc:", 0) == 0)
    {
        const std::string id = resolved.substr(4);
        for (auto& npc : m_npcManager.npcs())
        {
            if (npc.id == id || npc.npcId == id)
            {
                npc.x = pos.x;
                npc.y = pos.y;
                npc.targetX = pos.x;
                npc.targetY = pos.y;
                return true;
            }
        }
        return false;
    }

    if (resolved.rfind("temp:", 0) == 0)
        resolved = resolved.substr(5);

    auto it = m_sequenceActors.find(resolved);
    if (it == m_sequenceActors.end())
        return false;
    it->second.x = pos.x;
    it->second.y = pos.y;
    return true;
}

void Campaign::setActorAnimation(const std::string& actorId, const std::string& animationId)
{
    auto it = m_sequenceActors.find(actorId);
    if (it != m_sequenceActors.end())
        it->second.animation = animationId;
    else
    {
        const auto alias = m_sequenceActorAliases.find(actorId);
        if (actorId == "player" || (alias != m_sequenceActorAliases.end() && alias->second == "player"))
            m_player.currentAnim = animationId.empty() ? m_player.currentAnim : animationId;
    }
}

void Campaign::faceActor(const std::string& actorId, const std::string& directionOrTarget)
{
    sequence::Vec2 pos;
    sequence::Vec2 target;
    const bool hasPos = getActorPosition(actorId, pos);
    const bool hasTarget = getActorPosition(directionOrTarget, target);

    auto setPlayerFacing = [&](Player::Facing facing)
    {
        const auto alias = m_sequenceActorAliases.find(actorId);
        if (actorId == "player" || (alias != m_sequenceActorAliases.end() && alias->second == "player"))
            m_player.facing = facing;
    };

    if (hasPos && hasTarget)
    {
        const float dx = target.x - pos.x;
        const float dy = target.y - pos.y;
        if (std::fabs(dx) > std::fabs(dy))
            setPlayerFacing(dx >= 0.0f ? Player::Facing::Right : Player::Facing::Left);
        else
            setPlayerFacing(dy >= 0.0f ? Player::Facing::Down : Player::Facing::Up);
        return;
    }

    if (directionOrTarget == "left") setPlayerFacing(Player::Facing::Left);
    else if (directionOrTarget == "right") setPlayerFacing(Player::Facing::Right);
    else if (directionOrTarget == "up") setPlayerFacing(Player::Facing::Up);
    else if (directionOrTarget == "down") setPlayerFacing(Player::Facing::Down);
}

void Campaign::setCameraPosition(sequence::Vec2 pos)
{
    m_sequenceCameraOverride = true;
    m_sequenceCameraFollow = false;
    m_sequenceCameraPos = pos;
}

void Campaign::setCameraFollow(const std::string& actorId)
{
    m_sequenceCameraFollow = !actorId.empty();
    m_sequenceCameraActor = actorId;
}

void Campaign::setCameraShake(float intensity, float duration)
{
    m_sequenceCameraShakeIntensity = std::max(0.0f, intensity);
    m_sequenceCameraShakeRemaining = std::max(0.0f, duration);
}

void Campaign::setFadeAlpha(float alpha)
{
    m_sequenceFadeAlpha = Clamp01(alpha);
}

void Campaign::setFlashAlpha(float alpha)
{
    m_sequenceFlashAlpha = Clamp01(alpha);
}

void Campaign::showSequenceText(const std::string& text, float duration)
{
    m_sequenceText = text;
    m_sequenceTextRemaining = duration > 0.0f ? duration : 999999.0f;
}

void Campaign::hideSequenceText()
{
    m_sequenceText.clear();
    m_sequenceTextRemaining = 0.0f;
}

void Campaign::playMusic(const std::string& id, int loops, int fadeMs)
{
    if (id.empty())
        return;
    std::filesystem::path audioName(id);
    if (!audioName.has_extension())
        audioName.replace_extension(".ogg");
    const std::filesystem::path path = std::filesystem::path(id).has_parent_path()
        ? (pathutils::ProjectRoot() / id)
        : (pathutils::AssetsDir() / "audio" / audioName);
    if (!m_audioManager.playMusic(path.string(), loops, fadeMs))
        sequenceLog("Music failed: " + id + " | " + m_audioManager.lastError());
}

void Campaign::stopMusic(int fadeMs)
{
    m_audioManager.stopMusic(fadeMs);
}

void Campaign::playSfx(const std::string& id)
{
    if (!id.empty())
        m_audioManager.playSfx(id);
}

void Campaign::playVoice(const std::string& id)
{
    if (!id.empty())
        m_audioManager.playVoice(id);
}

void Campaign::setTimeOfDay(const std::string& value)
{
    if (value.empty())
        return;

    if (value.find(':') != std::string::npos)
    {
        m_gameTime.setTimeFromString(value);
    }
    else
    {
        const auto& now = m_gameTime.now();
        setGameDateTime(now.day, now.month, now.year, HourForNamedTime(value, now.hour), 0);
    }
    updateWeather();
}

void Campaign::setWeather(const nlohmann::json& data)
{
    m_weatherDebug.enabled = true;
    if (data.contains("rain"))
    {
        m_weatherDebug.overrideRain = true;
        m_weatherDebug.forcedRain = data.value("rain", false);
        m_weatherDebug.forcedRainIntensity = data.value("rain_intensity", data.value("intensity", m_weatherDebug.forcedRain ? 0.7f : 0.0f));
    }
    if (data.contains("wind"))
    {
        m_weatherDebug.overrideWind = true;
        m_weatherDebug.forcedWind = data.value("wind", 0.0f);
    }
    if (data.contains("cloudiness"))
    {
        m_weatherDebug.overrideCloudiness = true;
        m_weatherDebug.forcedCloudiness = data.value("cloudiness", 0.0f);
    }
    if (data.contains("fog"))
    {
        m_weatherDebug.overrideFog = true;
        m_weatherDebug.forcedFog = data.value("fog", false);
    }
    updateWeather();
}

bool Campaign::loadMapForSequence(const std::string& mapIdOrPath, const std::string& spawnId)
{
    const auto path = ResolveSequenceMapPath(mapIdOrPath);
    if (path.empty())
        return false;
    const bool ok = loadMap(path.string(), spawnId);
    if (!ok)
        sequenceLog("load_map failed: " + path.string());
    return ok;
}

void Campaign::teleportPlayer(sequence::Vec2 pos)
{
    setPlayerPosition(pos.x, pos.y);
}

void Campaign::enablePlayerControl(bool enabled)
{
    m_sequencePlayerControlEnabled = enabled;
    if (!enabled)
    {
        m_player.vx = 0.0f;
        m_player.vy = 0.0f;
    }
}

void Campaign::startDialog(const std::string& npcOrDialogId)
{
    m_activeDialog = nullptr;
    m_activeDialogNodeId.clear();
    m_dialogNpcIndex = -1;

    for (int i = 0; i < (int)m_npcManager.npcs().size(); ++i)
    {
        const auto& npc = m_npcManager.npcs()[i];
        if (npc.id == npcOrDialogId || npc.npcId == npcOrDialogId)
        {
            m_dialogNpcIndex = i;
            if (!npc.scriptId.empty())
                m_activeDialog = m_dialogManager.findDialog(npc.scriptId);
            break;
        }
    }

    if (!m_activeDialog)
        m_activeDialog = m_dialogManager.findDialog(npcOrDialogId);

    if (m_activeDialog)
    {
        m_activeDialogNodeId = m_activeDialog->startNodeId;
        m_npcDialogOpen = true;
    }
    else
    {
        sequenceLog("start_dialog failed: " + npcOrDialogId);
    }
}

void Campaign::changeRendererMode(const std::string& mode)
{
    if (mode == "2d" || mode == "2D")
        m_sequenceRendererMode = "2d";
    else if (mode == "2_5d" || mode == "2.5d" || mode == "2_5D")
        m_sequenceRendererMode = "2_5d";
    else
        sequenceLog("Unknown renderer mode: " + mode);
}
