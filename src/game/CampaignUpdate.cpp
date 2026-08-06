#include "Campaign.h"

#include "Utf8.h"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <sstream>

#include "PathUtils.h"

static bool AABB_Intersect(const SDL_Rect& a, const SDL_Rect& b)
{
    return (a.x < b.x + b.w) &&
           (a.x + a.w > b.x) &&
           (a.y < b.y + b.h) &&
           (a.y + a.h > b.y);
}

bool Campaign::shouldBlockLetterHotkeys() const
{
    const ImGuiIO& io = ImGui::GetIO();

    // kdy� se p�e do UI textov�ho pole, p�smenkov� hotkeys blokujeme
    if (io.WantTextInput)
        return true;

    // p?�padn? i obecn� blokov�n� kl�vesnice pro UI
    if (io.WantCaptureKeyboard && m_consoleOpen)
        return true;

    return false;
}

void Campaign::handleEvent(const SDL_Event& e)
{
    // 1) inspect p?es Alt + lev� klik
    if (e.type == SDL_MOUSEBUTTONDOWN &&
        e.button.button == SDL_BUTTON_LEFT &&
        isInspectHeld())
    {
        tryInspectWorld();
        return;
    }

    // d�l u� �e��me jen kl�vesnici
    if (e.type != SDL_KEYDOWN || e.key.repeat != 0)
        return;

    const SDL_Keycode key = e.key.keysym.sym;

    // glob�ln� kl�vesy
    if (key == SDLK_SEMICOLON)
    {
        m_consoleOpen = !m_consoleOpen;
        if (m_consoleOpen)
            m_consoleFocusInput = true;
        return;
    }

    // kdy? UI p�?e text, blokuj p�smenkov� hotkeys
    if (shouldBlockLetterHotkeys())
        return;

    if (key == SDLK_i && !m_npcDialogOpen)
    {
        m_inventoryOpen = !m_inventoryOpen;
        m_inventoryFocus = m_inventoryOpen;
        return;
    }

    // open Journal
    if (key == SDLK_q && !m_npcDialogOpen)
    {
        m_questJournalOpen = !m_questJournalOpen;
        m_questJournalFocus = m_questJournalOpen;
        return;
    }

    // budouc� hotkeys...
}

void Campaign::startFoodPoisoning(const std::string& sourceName, bool fatal)
{
    // A fatal poisoning overrides a mild one. A mild poisoning does not cancel an already fatal course.
    if (m_activeFoodPoisoning.active && m_activeFoodPoisoning.fatal && !fatal)
    {
        consoleLog("Už v tobě působí mnohem horší otrava.");
        return;
    }

    m_activeFoodPoisoning = ActiveFoodPoisoning{};
    m_activeFoodPoisoning.active = true;
    m_activeFoodPoisoning.fatal = fatal;
    m_activeFoodPoisoning.sourceName = sourceName.empty() ? "neznámý jed" : sourceName;
    m_activeFoodPoisoning.lastLoggedHour = -1;
    m_activeFoodPoisoning.lastLoggedMilestone = -1;

    if (fatal)
    {
        // Amanita phalloides style curve: long deceptive latency, then organ damage.
        // Timeline is balanced for gameplay testing: visible symptoms after 8h, death by 48h without treatment.
        m_activeFoodPoisoning.onsetHours = 8.0f;
        m_activeFoodPoisoning.durationHours = 48.0f;
        m_activeFoodPoisoning.healthDrainPerHour = 2.50f;     // 40h active damage window -> fatal around 48h total
        m_activeFoodPoisoning.hydrationDrainPerHour = 1.85f;

        // Latent phase: do not use the generic poisoned drain yet, but make it visible in UI/console.
        m_player.stats.condition.poisoned = false;
        m_player.stats.condition.morale = PlayerStats::Clamp01To100(m_player.stats.condition.morale - 2.0f);
        m_player.stats.condition.stress = PlayerStats::Clamp01To100(m_player.stats.condition.stress + 3.0f);
        consoleLog("Těžká otrava založena: " + m_activeFoodPoisoning.sourceName + ". Prvních několik hodin může být zrádně klidných.");
    }
    else
    {
        // Mild poisoning should be clearly testable almost immediately.
        m_activeFoodPoisoning.onsetHours = 0.10f; // ~6 min
        m_activeFoodPoisoning.durationHours = 8.0f;
        m_activeFoodPoisoning.healthDrainPerHour = 0.85f;
        m_activeFoodPoisoning.hydrationDrainPerHour = 3.00f;
        m_player.stats.condition.poisoned = true;
        m_player.stats.condition.morale = PlayerStats::Clamp01To100(m_player.stats.condition.morale - 4.0f);
        m_player.stats.condition.stress = PlayerStats::Clamp01To100(m_player.stats.condition.stress + 6.0f);
        consoleLog("Lehká otrava založena: " + m_activeFoodPoisoning.sourceName + ". Nevolnost se projeví během chvíle.");
    }
}

void Campaign::clearFoodPoisoning(const std::string& reason)
{
    const bool wasActive = m_activeFoodPoisoning.active;
    m_activeFoodPoisoning = ActiveFoodPoisoning{};
    m_player.stats.condition.poisoned = false;

    if (wasActive && !reason.empty())
        consoleLog(reason);
}

std::string Campaign::activeFoodPoisoningStatusText() const
{
    if (!m_activeFoodPoisoning.active)
        return std::string();

    std::ostringstream ss;
    const float elapsed = m_activeFoodPoisoning.elapsedHours;

    if (elapsed < m_activeFoodPoisoning.onsetHours)
    {
        const float toOnset = std::max(0.0f, m_activeFoodPoisoning.onsetHours - elapsed);
        ss << (m_activeFoodPoisoning.fatal ? "Latentní těžká otrava" : "Začínající otrava")
           << ": " << m_activeFoodPoisoning.sourceName
           << " | příznaky za ~" << (int)std::ceil(toOnset) << " h";
        return ss.str();
    }

    const float remaining = std::max(0.0f, m_activeFoodPoisoning.durationHours - elapsed);
    ss << (m_activeFoodPoisoning.fatal ? "Těžká otrava" : "Lehká otrava")
       << ": " << m_activeFoodPoisoning.sourceName
       << " | běží " << (int)std::floor(elapsed) << " h"
       << " | zbývá ~" << (int)std::ceil(remaining) << " h";
    return ss.str();
}

void Campaign::updateFoodPoisoning(int elapsedGameMinutes)
{
    if (!m_activeFoodPoisoning.active || elapsedGameMinutes <= 0)
        return;

    const float stepHours = (float)elapsedGameMinutes / 60.0f;
    const float before = m_activeFoodPoisoning.elapsedHours;
    m_activeFoodPoisoning.elapsedHours += stepHours;
    const float elapsed = m_activeFoodPoisoning.elapsedHours;

    // A little debug readability: when using skip, tell the player where the poison curve is.
    const int elapsedWholeHours = (int)std::floor(elapsed);
    if (elapsedWholeHours != m_activeFoodPoisoning.lastLoggedHour && elapsedWholeHours > 0)
    {
        m_activeFoodPoisoning.lastLoggedHour = elapsedWholeHours;

        if (m_activeFoodPoisoning.fatal && elapsed < m_activeFoodPoisoning.onsetHours)
        {
            std::ostringstream ss;
            ss << "Otrava zatím běží skrytě (" << elapsedWholeHours << " h od požití, příznaky kolem 8 h).";
            consoleLog(ss.str());
        }
    }

    if (elapsed < m_activeFoodPoisoning.onsetHours)
        return;

    if (!m_activeFoodPoisoning.onsetLogged)
    {
        m_activeFoodPoisoning.onsetLogged = true;

        if (m_activeFoodPoisoning.fatal)
            consoleLog("Po zhruba osmi hodinách se rozjíždí těžká otrava. Teď už začínají tikat hodiny.");
        else
            consoleLog("Otrava se projevila naplno. Bolí tě břicho a rychleji ztrácíš tekutiny.");
    }

    // Damage should apply only for the part of the step after onset, not for the hidden latency.
    const float activeBefore = std::max(0.0f, before - m_activeFoodPoisoning.onsetHours);
    const float activeAfter = std::max(0.0f, elapsed - m_activeFoodPoisoning.onsetHours);
    const float activeStepHours = std::max(0.0f, activeAfter - activeBefore);

    if (activeStepHours <= 0.0f)
        return;

    m_player.stats.condition.poisoned = true;

    // Fatal poisoning escalates a little after the first day of symptoms.
    float severity = 1.0f;
    if (m_activeFoodPoisoning.fatal)
    {
        const float activeHours = activeAfter;
        if (activeHours >= 24.0f)
            severity = 1.35f;
        else if (activeHours >= 12.0f)
            severity = 1.15f;
    }

    m_player.stats.condition.health = PlayerStats::Clamp01To100(
        m_player.stats.condition.health - m_activeFoodPoisoning.healthDrainPerHour * severity * activeStepHours);
    m_player.stats.condition.hydration = PlayerStats::Clamp01To100(
        m_player.stats.condition.hydration - m_activeFoodPoisoning.hydrationDrainPerHour * severity * activeStepHours);
    m_player.stats.condition.stress = PlayerStats::Clamp01To100(
        m_player.stats.condition.stress + (m_activeFoodPoisoning.fatal ? 0.75f : 0.45f) * activeStepHours);
    m_player.stats.condition.morale = PlayerStats::Clamp01To100(
        m_player.stats.condition.morale - (m_activeFoodPoisoning.fatal ? 0.35f : 0.20f) * activeStepHours);

    if (m_activeFoodPoisoning.fatal)
    {
        const int milestone =
            elapsed >= 44.0f ? 44 :
            elapsed >= 36.0f ? 36 :
            elapsed >= 24.0f ? 24 :
            elapsed >= 12.0f ? 12 :
            elapsed >= 8.0f  ? 8  : -1;

        if (milestone >= 0 && milestone != m_activeFoodPoisoning.lastLoggedMilestone)
        {
            m_activeFoodPoisoning.lastLoggedMilestone = milestone;
            std::ostringstream ss;
            ss << "Těžká otrava postupuje: " << milestone << " h od požití"
               << " | HP=" << (int)std::lround(m_player.stats.condition.health)
               << " HYD=" << (int)std::lround(m_player.stats.condition.hydration) << ".";
            consoleLog(ss.str());
        }
    }

    if (m_activeFoodPoisoning.fatal && elapsed >= m_activeFoodPoisoning.durationHours)
    {
        m_player.stats.condition.health = 0.0f;
        if (!m_activeFoodPoisoning.endLogged)
        {
            consoleLog("Otrava dokončila své dílo. Bez léčby to nešlo přežít.");
            m_activeFoodPoisoning.endLogged = true;
        }
        return;
    }

    if (!m_activeFoodPoisoning.fatal && elapsed >= m_activeFoodPoisoning.durationHours)
    {
        clearFoodPoisoning("Lehká otrava konečně odezněla.");
    }
}

void Campaign::updatePlayerNeeds(int elapsedGameMinutes)
{
    if (elapsedGameMinutes <= 0)
        return;

    if (m_godMode) {
        m_player.stats.condition.health = 100.0f;
        m_player.stats.condition.stamina = 100.0f;
        m_player.stats.condition.fatigue = 0.0f;
        m_player.stats.condition.nutrition = 100.0f;
        m_player.stats.condition.hydration = 100.0f;
        m_player.stats.condition.hygiene = 100.0f;
        m_player.stats.condition.bodyTemperature = 50.0f;
        return;
    }

    const bool moving =
        std::abs(m_player.vx) > 1.0f ||
        std::abs(m_player.vy) > 1.0f;

    const Uint8* ks = SDL_GetKeyboardState(nullptr);

    const bool running =
        ks[SDL_SCANCODE_LCTRL] ||
        ks[SDL_SCANCODE_RCTRL];

    const float gm = static_cast<float>(elapsedGameMinutes);

    // ===== ZAKLADNI POTREBY =====
    // nutrition/hydration: 100 = OK, 0 = problem
    // Ladeno pro normalni denni tempo:
    // - hlad z 100 na 0 priblizne za 3 herni dny
    // - zizen z 100 na 0 priblizne za 1 herni den
    // Pohyb a beh to mirne zrychluji, horko zrychluje hlavne zizen.
    constexpr float kNutritionDrainPerMinute = 100.0f / (72.0f * 60.0f); // 3 dny
    constexpr float kHydrationDrainPerMinute = 100.0f / (24.0f * 60.0f); // 1 den

    float activityFactor = 1.0f;
    if (moving)
        activityFactor += running ? 0.28f : 0.12f;

    if (m_player.stats.isOverweight())
        activityFactor += 0.10f;

    // Hlad drzi dlouhy rytmus, aktivita ho zrychluje jen opatrne.
    const float nutritionDrain = kNutritionDrainPerMinute * (1.0f + (activityFactor - 1.0f) * 0.55f);

    // Zizen reaguje citelneji na pohyb a hlavne na teplotu.
    float heatFactor = 1.0f;
    const float tempC = m_runtimeWeather.currentTemp;

    if (tempC > 20.0f)
    {
        // +3 % za kazdy stupen nad 20 C, zastropovano kvuli hratelnosti.
        heatFactor += std::min(0.85f, (tempC - 20.0f) * 0.03f);
    }
    else if (tempC < 5.0f)
    {
        // V chladu se tolik nepotis, ale nechceme zizen vypnout uplne.
        heatFactor -= std::min(0.20f, (5.0f - tempC) * 0.01f);
    }

    if (m_player.stats.condition.bodyTemperature > 58.0f)
        heatFactor += std::min(0.25f, (m_player.stats.condition.bodyTemperature - 58.0f) * 0.01f);

    const float hydrationDrain = kHydrationDrainPerMinute * activityFactor * heatFactor;

    m_player.stats.condition.nutrition =
        PlayerStats::Clamp01To100(
            m_player.stats.condition.nutrition - nutritionDrain * gm);

    m_player.stats.condition.hydration =
        PlayerStats::Clamp01To100(
            m_player.stats.condition.hydration - hydrationDrain * gm);

    // fatigue: 0 = odpocaty, 100 = vycerpany
    m_player.stats.condition.fatigue =
        PlayerStats::Clamp01To100(
            m_player.stats.condition.fatigue + ((moving ? 0.45f : 0.12f) * gm));

    // stamina: 100 = plna
    if (moving)
    {
        float staminaDrain =
            running
            ? 1.6f * gm
            : 0.9f * gm;

        if (m_player.stats.isOverweight())
            staminaDrain += 0.45f * gm;

        m_player.stats.condition.stamina =
            PlayerStats::Clamp01To100(
                m_player.stats.condition.stamina - staminaDrain);
    }
    else
    {
        m_player.stats.condition.stamina =
            PlayerStats::Clamp01To100(
                m_player.stats.condition.stamina + 1.20f * gm);
    }

    // hygiena: 100 = cisty, 0 = spinavy
    m_player.stats.condition.hygiene =
        PlayerStats::Clamp01To100(
            m_player.stats.condition.hygiene - ((moving ? 0.10f : 0.02f) * gm));

    // stres / moralka
    if (moving)
    {
        m_player.stats.condition.stress =
            PlayerStats::Clamp01To100(m_player.stats.condition.stress + 0.03f * gm);

        m_player.stats.condition.morale =
            PlayerStats::Clamp01To100(m_player.stats.condition.morale - 0.01f * gm);
    }
    else
    {
        m_player.stats.condition.stress =
            PlayerStats::Clamp01To100(m_player.stats.condition.stress - 0.04f * gm);

        m_player.stats.condition.morale =
            PlayerStats::Clamp01To100(m_player.stats.condition.morale + 0.01f * gm);
    }

    // ===== MODIFIKATORY =====
    if (m_player.stats.isOverweight())
    {
        m_player.stats.condition.fatigue =
            PlayerStats::Clamp01To100(m_player.stats.condition.fatigue + 0.20f * gm);
    }

    if (m_player.stats.condition.bleeding && !m_player.stats.condition.treatedWound)
    {
        m_player.stats.condition.health =
            PlayerStats::Clamp01To100(m_player.stats.condition.health - 0.25f * gm);
    }

    if (m_player.stats.condition.poisoned && !m_activeFoodPoisoning.active)
    {
        // Generic poison drain for future wounds/venoms. Food poisoning has its own timed curve above.
        m_player.stats.condition.health =
            PlayerStats::Clamp01To100(m_player.stats.condition.health - 0.15f * gm);

        m_player.stats.condition.hydration =
            PlayerStats::Clamp01To100(m_player.stats.condition.hydration - 0.25f * gm);
    }

    const float hunger = m_player.stats.legacyHunger();
    const float thirst = m_player.stats.legacyThirst();

    if (hunger >= 85.0f)
    {
        m_player.stats.condition.health =
            PlayerStats::Clamp01To100(m_player.stats.condition.health - 0.08f * gm);
    }

    if (thirst >= 80.0f)
    {
        m_player.stats.condition.health =
            PlayerStats::Clamp01To100(m_player.stats.condition.health - 0.15f * gm);
    }

    // kolaps z vycerpani
    if (m_player.stats.condition.fatigue >= 100.0f)
    {
        m_player.stats.condition.fatigue = 65.0f;
        m_player.stats.condition.stamina = 20.0f;
        m_player.stats.condition.nutrition =
            PlayerStats::Clamp01To100(m_player.stats.condition.nutrition - 5.0f);
        m_player.stats.condition.hydration =
            PlayerStats::Clamp01To100(m_player.stats.condition.hydration - 7.0f);
    }

    // ===== TEPLOTA =====
    float targetTemp = 50.0f; // neutral

    if (hunger > 80.0f || thirst > 80.0f)
        targetTemp -= 3.0f;

    if (m_player.stats.condition.poisoned)
        targetTemp += 6.0f;

    m_player.stats.condition.bodyTemperature +=
        (targetTemp - m_player.stats.condition.bodyTemperature)
        * std::min(1.0f, 0.02f * gm);

    m_player.stats.condition.bodyTemperature =
        std::clamp(m_player.stats.condition.bodyTemperature, 0.0f, 100.0f);

    // inventar
    m_player.stats.carryWeight =
        m_player.inventory.computeTotalWeight(m_itemDefs);

    // ===== POCASI =====
    if (m_runtimeWeather.isRaining)
    {
        m_player.stats.condition.wetness =
            PlayerStats::Clamp01To100(
                m_player.stats.condition.wetness + (0.45f + m_runtimeWeather.rainIntensity * 0.90f) * gm);
    }
    else
    {
        float drying = 0.10f;

        if (m_runtimeWeather.currentTemp > 14.0f)
            drying += 0.08f;

        if (m_runtimeWeather.windNow > 10.0f)
            drying += 0.06f;

        m_player.stats.condition.wetness =
            PlayerStats::Clamp01To100(
                m_player.stats.condition.wetness - drying * gm);
    }

    float coldPull = 0.0f;

    if (m_runtimeWeather.currentTemp < 8.0f)
        coldPull += (8.0f - m_runtimeWeather.currentTemp) * 0.08f;

    coldPull += m_runtimeWeather.windNow * 0.015f;
    coldPull += m_player.stats.condition.wetness * 0.012f;

    m_player.stats.condition.bodyTemperature =
        PlayerStats::Clamp01To100(
            m_player.stats.condition.bodyTemperature - coldPull * gm);

    // v�t�� riziko nemoci p�i chladu a promo�en�
    if (m_player.stats.condition.bodyTemperature < 40.0f && m_player.stats.condition.wetness > 45.0f)
    {
        m_player.stats.condition.diseaseLoad =
            PlayerStats::Clamp01To100(
                m_player.stats.condition.diseaseLoad + 0.04f * gm);
    }
}

void Campaign::tryInteractWithNpc()
{
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    const bool interactNow = ks[SDL_SCANCODE_E] != 0;

    if (interactNow && !m_interactPressedLastFrame)
    {
        if (m_nearNpcIndex >= 0 && !m_npcDialogOpen)
        {
            const auto& npcs = m_npcManager.npcs();
            const auto& npc = npcs[m_nearNpcIndex];

            const NpcDefinition* def = nullptr;
            if (!npc.npcId.empty())
                def = m_npcDefinitions.findByNpcId(npc.npcId);

            bool allowTalk = true;
            std::string denyReason;

            if (def)
            {
                const std::string& zone = npc.currentZone;

                const bool inHouse =
                    zone.find("house") != std::string::npos ||
                    zone.find("hut") != std::string::npos ||
                    zone.find("chapel_house") != std::string::npos;

                const bool inYard =
                    zone.find("yard") != std::string::npos ||
                    zone.find("farmyard") != std::string::npos ||
                    zone.find("mill_yard") != std::string::npos ||
                    zone.find("forge_yard") != std::string::npos;

                if (inHouse)
                {
                    if (!def->accessProfile.allowsHouseTalk)
                    {
                        allowTalk = false;
                        denyReason = npc.displayName() + " si te meri pohledem, ale zjevne nestoji o rozhovor uvnitr.";
                    }
                    else if (def->accessProfile.requiresTrustForHouse &&
                        m_player.stats.villageAcceptance < def->accessProfile.minTrustForHouse)
                    {
                        allowTalk = false;
                        denyReason = npc.displayName() + " zjevne nechce hovorit tak duverne pod strechou.";
                    }
                }
                else if (inYard)
                {
                    if (!def->accessProfile.allowsYardTalk)
                    {
                        allowTalk = false;
                        denyReason = npc.displayName() + " te na dvore odbyde kratkym pohledem.";
                    }
                }
                else
                {
                    if (!def->accessProfile.allowsPublicTalk)
                    {
                        allowTalk = false;
                        denyReason = npc.displayName() + " se s tebou na tom miste nechce davat do reci.";
                    }
                }
            }

            if (!allowTalk)
            {
                if (!denyReason.empty())
                    consoleLog(denyReason);
                else
                    consoleLog(npc.displayName() + " nema zajem mluvit.");
            }
            else
            {
                m_dialogNpcIndex = m_nearNpcIndex;
                m_npcDialogOpen = true;

                m_activeDialog = nullptr;
                m_activeDialogNodeId.clear();

                if (!npc.scriptId.empty())
                {
                    m_activeDialog = m_dialogManager.findDialog(npc.scriptId);

                    if (m_activeDialog)
                        m_activeDialogNodeId = m_activeDialog->startNodeId;
                }
            }
        }
    }

}

void Campaign::tryUseMapLinkUnderPlayer()
{
    if (!m_pendingInteriorTransitionId.empty())
        return;

    const SDL_Rect playerRect = m_player.worldAABB();

    for (int y = 0; y < m_map.height(); ++y)
    {
        for (int x = 0; x < m_map.width(); ++x)
        {
            const auto* def = m_map.getObjDefAt(m_objCatalog, x, y);
            if (!def)
                continue;


            if (!def->HasTag("map_link"))
                continue;

            SDL_Rect tileRect{
                x * m_tileSize,
                y * m_tileSize,
                m_tileSize,
                m_tileSize
            };

            if (!AABB_Intersect(playerRect, tileRect))
                continue;

            const int linkId = (int)m_map.getObjVar(x, y);
            if (linkId <= 0)
                return;

            for (const auto& link : m_currentMapLinks)
            {
                if (link.id != linkId)
                    continue;

                if (!link.targetLocation.empty())
                {
                    m_pendingInteriorTransitionId = link.targetLocation;
                    m_pendingInteriorTransitionSpawnId = link.targetSpawnId;
                    consoleLog(U8("Vstupuješ do 2.5D lokace: ") + link.targetLocation);
                    return;
                }

                if (link.targetMap.empty())
                    return;

                std::filesystem::path target =
                    pathutils::MapsDir() / link.targetMap;

                if (loadMap(target.string(), link.targetSpawnId))
                {
                    m_currentMapPath = target.string();
                    loadMapLinksForCurrentMap();
                }


                return;
            }
        }
    }
}

void Campaign::update(float dt)
{
    m_gameTime.setPaused(m_consoleOpen || m_npcDialogOpen);

    const int elapsedGameMinutes = m_gameTime.update(dt);

    // nejd?�v v?dy aktualizovat po?as� podle aktu�ln�ho ?asu
    updateWeather();

    const Uint8* ks = SDL_GetKeyboardState(nullptr);

    if (m_consoleOpen || m_npcDialogOpen)
    {
        m_player.vx = 0.0f;
        m_player.vy = 0.0f;
        return;
    }

    float ax = 0.0f;
    float ay = 0.0f;

    if (ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT])  ax -= 1.0f;
    if (ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT]) ax += 1.0f;
    if (ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP])    ay -= 1.0f;
    if (ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN])  ay += 1.0f;

    const float lenSq = ax * ax + ay * ay;
    if (lenSq > 0.0001f)
    {
        const float len = std::sqrt(lenSq);
        ax /= len;
        ay /= len;
    }

    const bool running =
        ks[SDL_SCANCODE_LCTRL] ||
        ks[SDL_SCANCODE_RCTRL];

    m_player.isSprinting = running;

    float speed = running
        ? m_player.stats.getRunSpeed()
        : m_player.stats.getWalkSpeed();

    if (m_player.stats.condition.nutrition <= 40.0f) speed -= 12.0f;
    if (m_player.stats.condition.hydration <= 50.0f) speed -= 18.0f;
    if (m_player.stats.condition.fatigue >= 70.0f)   speed -= 15.0f;
    if (m_player.stats.carryWeight > m_player.stats.carryCapacity) speed -= 12.0f;

    speed = std::max(20.0f, speed);

    m_player.vx = ax * speed;
    m_player.vy = ay * speed;

    const float oldX = m_player.x;
    const float oldY = m_player.y;

    const float nextX = oldX + m_player.vx * dt;
    const float nextY = oldY + m_player.vy * dt;

    // pohyb po ose X
    if (!wouldPlayerCollideWithTerrainAt(nextX, oldY) &&
        !wouldPlayerCollideWithObjectsAt(nextX, oldY) &&
        !wouldPlayerCollideWithNPCsAt(nextX, oldY))
    {
        m_player.x = nextX;
    }
    else
    {
        m_player.x = oldX;
    }

    // pohyb po ose Y
    if (!wouldPlayerCollideWithTerrainAt(m_player.x, nextY) &&
        !wouldPlayerCollideWithObjectsAt(m_player.x, nextY) &&
        !wouldPlayerCollideWithNPCsAt(m_player.x, nextY))
    {
        m_player.y = nextY;
    }
    else
    {
        m_player.y = oldY;
    }

    clampPlayerToMap();
    tryUseMapLinkUnderPlayer();

    updateNearbyNpc();
    tryInteractWithNpc();
    tryInteractWithForage();

    {
        const Uint8* interactKs = SDL_GetKeyboardState(nullptr);
        m_interactPressedLastFrame = interactKs[SDL_SCANCODE_E] != 0;
    }

    updatePlayerNeeds(elapsedGameMinutes);
    updateFoodPoisoning(elapsedGameMinutes);

    // HUD
    m_dayHudAnimTime += dt;
    if (m_dayHudAnimTime > 1000.0f)
        m_dayHudAnimTime = std::fmod(m_dayHudAnimTime, 1000.0f);

    m_player.update(dt, m_characterManager);

    updateSkyOverlay(dt);
    updateFogOfWar();

    const auto& now = m_gameTime.now();

    const bool isSunday = (m_gameTime.currentWeekDayIndexMondayFirst() == 6);
    const std::string feastId = m_liturgicalCalendar.primaryTitle(now.day, now.month, now.year);
    const std::vector<std::string> feastTags = m_liturgicalCalendar.tagsForDate(now.day, now.month, now.year);

    m_npcManager.updateSchedules(
        seasonToString(getSeason()),
        dayPhaseToString(getDynamicDayPhase()),
        isSunday,
        feastId,
        feastTags
    );

    m_npcManager.updateMovement(dt, m_tileSize);
    m_npcManager.stepMovement(dt);
    m_npcManager.updateReactions(
        m_player.x,
        m_player.y,
        m_audioManager,
        m_gameTime.currentDayPeriodIndex());
    tryUseQuestTrigger();
}

bool Campaign::isPlayerNearQuestObject(const std::string& objectId, int radiusPx) const
{
    const SDL_Rect playerRect = m_player.worldAABB();

    for (int y = 0; y < m_map.height(); ++y)
    {
        for (int x = 0; x < m_map.width(); ++x)
        {
            const auto* def = m_map.getObjDefAt(m_objCatalog, x, y);
            if (!def)
                continue;

            if (def->id != objectId)
                continue;

            int wx = 0, wy = 0;
            m_map.getObjPivotWorld(x, y, wx, wy);

            SDL_Rect r{
                wx - radiusPx,
                wy - radiusPx,
                radiusPx * 2,
                radiusPx * 2
            };

            if ((playerRect.x < r.x + r.w) &&
                (playerRect.x + playerRect.w > r.x) &&
                (playerRect.y < r.y + r.h) &&
                (playerRect.y + playerRect.h > r.y))
            {
                return true;
            }
        }
    }

    return false;
}

void Campaign::tryUseQuestTrigger()
{
    if (!hasStoryFlag("quest_matej_water_started"))
        return;

    if (hasStoryFlag("quest_matej_water_ready"))
        return;

    const int footTx = (int)std::floor(m_player.x / (float)m_tileSize);
    const int footTy = (int)std::floor((m_player.y - 1.0f) / (float)m_tileSize);

    for (int oy = -1; oy <= 1; ++oy)
    {
        for (int ox = -1; ox <= 1; ++ox)
        {
            const int tx = footTx + ox;
            const int ty = footTy + oy;

            if (tx < 0 || ty < 0 || tx >= m_map.width() || ty >= m_map.height())
                continue;

            const auto* def = m_map.getObjDefAt(m_objCatalog, tx, ty);
            if (!def)
                continue;

            if (def->id == "quest_matej_water_source")
            {
                setStoryFlag("quest_matej_water_ready");
                m_questJournalOpen = true;
                m_questJournalFocus = true;
                consoleLog("Nabral jsi vodu. Vrat se za Matejem.");
                return;
            }
        }
    }
}

void Campaign::applyQuestRewardEffects(const std::string& flag)
{
    if (flag == "quest_matej_voda_rewarded")
    {
        m_player.stats.condition.nutrition =
            std::clamp(m_player.stats.condition.nutrition + 20.0f, 0.0f, 100.0f);

        m_player.stats.condition.hydration =
            std::clamp(m_player.stats.condition.hydration + 15.0f, 0.0f, 100.0f);

        m_player.stats.condition.morale =
            std::clamp(m_player.stats.condition.morale + 5.0f, 0.0f, 100.0f);

        setStoryFlag("matej_house_sleep_allowed");
        consoleLog("Mat?j ti dal naj�st a m??e? p?espat ve stodole.");
    }
}

void Campaign::tryInteractWithForage()
{
    if (m_npcDialogOpen || m_forageWindowOpen || m_nearNpcIndex >= 0)
        return;

    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    const bool interactNow = ks[SDL_SCANCODE_E] != 0;

    if (!interactNow || m_interactPressedLastFrame)
        return;

    const ForageSpawnDef* spawn = findNearbyForageSpawn(42);
    if (!spawn)
        return;

    const ForageSpeciesDef* species = resolveForageSpeciesForSpawn(*spawn);

    m_activeForageSpawnId = spawn->id;
    m_activeForageTileX = spawn->tileX;
    m_activeForageTileY = spawn->tileY;
    m_activeForageSpeciesId = species ? species->id : std::string{};
    m_activeForageAnswers.clear();
    m_activeForageTraitText.clear();
    m_activeForageDescriptionText[0] = '\0';
    m_activeForageNameGuess[0] = '\0';
    m_hasLastForageExaminationResult = false;
    m_lastForageExaminationResult = ForageExaminationResult{};
    m_forageWindowOpen = true;
    m_forageWindowFocus = true;
}
