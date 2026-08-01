#include "Campaign.h"
#include <algorithm>
#include <vector>
#include <cmath>
#include <SDL_image.h>
#include <cstring>

static bool AABB_Intersect(const SDL_Rect& a, const SDL_Rect& b)
{
    return (a.x < b.x + b.w) &&
        (a.x + a.w > b.x) &&
        (a.y < b.y + b.h) &&
        (a.y + a.h > b.y);
}

static std::string LowerAsciiForId(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static bool ContainsIdToken(const std::string& id, const char* token)
{
    return LowerAsciiForId(id).find(token) != std::string::npos;
}

static bool HasForageEffect(const ForageSpeciesDef& species, const std::string& effect)
{
    for (const auto& e : species.effectsOnEat)
    {
        if (e == effect)
            return true;
    }
    return false;
}

static SDL_Rect GetObjectWorldRect(const gameobj::ObjectDef& def, int wx, int wy, float scale)
{
    SDL_Rect r{};
    r.w = (int)std::lround(def.src.w * scale);
    r.h = (int)std::lround(def.src.h * scale);
    r.x = (int)std::lround(wx - def.pivot.x * scale);
    r.y = (int)std::lround(wy - def.pivot.y * scale);
    return r;
}

void Campaign::ensureCameraOnPlayer(int screenW, int screenH)
{
    const int mapPixelW = m_map.width() * m_tileSize;
    const int mapPixelH = m_map.height() * m_tileSize;

    int targetCamX = (int)(m_player.x - screenW * 0.5f);
    int targetCamY = (int)(m_player.y - screenH * 0.5f);

    const int maxCamX = std::max(0, mapPixelW - screenW);
    const int maxCamY = std::max(0, mapPixelH - screenH);

    m_camX = std::clamp(targetCamX, 0, maxCamX);
    m_camY = std::clamp(targetCamY, 0, maxCamY);
}

static bool IntersectRects(const SDL_Rect& a, const SDL_Rect& b, SDL_Rect& out)
{
    const int x1 = std::max(a.x, b.x);
    const int y1 = std::max(a.y, b.y);
    const int x2 = std::min(a.x + a.w, b.x + b.w);
    const int y2 = std::min(a.y + a.h, b.y + b.h);

    if (x2 <= x1 || y2 <= y1)
        return false;

    out.x = x1;
    out.y = y1;
    out.w = x2 - x1;
    out.h = y2 - y1;
    return true;
}

static void RenderObjectWorldSubRectAlpha(
    SDL_Renderer* renderer,
    SDL_Texture* tex,
    const gameobj::ObjectDef& def,
    int wx,
    int wy,
    float scale,
    const SDL_Rect& worldSubRect,
    int camX,
    int camY,
    Uint8 alpha)
{
    SDL_Rect objWorld{};
    objWorld.x = (int)std::lround(wx - def.pivot.x * scale);
    objWorld.y = (int)std::lround(wy - def.pivot.y * scale);
    objWorld.w = (int)std::lround(def.src.w * scale);
    objWorld.h = (int)std::lround(def.src.h * scale);

    const float invScale = 1.0f / scale;

    SDL_Rect clipped{};
    if (!IntersectRects(objWorld, worldSubRect, clipped))
        return;

    const int relXSrc = (int)std::lround((clipped.x - objWorld.x) * invScale);
    const int relYSrc = (int)std::lround((clipped.y - objWorld.y) * invScale);
    const int srcW = std::max(1, (int)std::lround(clipped.w * invScale));
    const int srcH = std::max(1, (int)std::lround(clipped.h * invScale));

    SDL_Rect src{};
    src.x = def.src.x + relXSrc;
    src.y = def.src.y + relYSrc;
    src.w = std::min(srcW, def.src.w - relXSrc);
    src.h = std::min(srcH, def.src.h - relYSrc);

    SDL_Rect dst{};
    dst.x = clipped.x - camX;
    dst.y = clipped.y - camY;
    dst.w = clipped.w;
    dst.h = clipped.h;

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex, alpha);
    SDL_RenderCopy(renderer, tex, &src, &dst);
    SDL_SetTextureAlphaMod(tex, 255);
}

struct FadeOverlay
{
    SDL_Texture* tex = nullptr;
    const gameobj::ObjectDef* def = nullptr;
    int wx = 0;
    int wy = 0;
    SDL_Rect fadeRectWorld{};
    Uint8 alpha = 140;
    float scale = 1.0f;
};

static const char* DebugBackgroundName(PlayerStats::Background background)
{
    switch (background)
    {
    case PlayerStats::Background::Survivalist:    return "Zalesak";
    case PlayerStats::Background::ScholarAthlete: return "Student historie";
    case PlayerStats::Background::SocialAdaptable:return "Mestan";
    }
    return "Neznamy";
}

static void DebugProgress(const char* label, float value01)
{
    ImGui::TextUnformatted(label);
    ImGui::ProgressBar(std::clamp(value01, 0.0f, 1.0f), ImVec2(220.0f, 0.0f));
}

static void DebugBulletValue(const char* label, float value)
{
    ImGui::BulletText("%s: %.1f", label, value);
}

static SDL_Texture* loadTexture(SDL_Renderer* r, const char* path, int& outW, int& outH)
{
    outW = outH = 0;

    SDL_Surface* surf = IMG_Load(path);
    if (!surf)
        return nullptr;

    outW = surf->w;
    outH = surf->h;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);

    return tex;
}

void Campaign::renderDebugHud()
{
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.78f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("DEBUG HUD", nullptr, flags))
    {
        const auto& now = m_gameTime.now();
        const auto moon = m_moonCycle.getMoonInfo(now.day, now.month, now.year);
        const auto sun = m_sunCycle.getDayInfo(now.day, now.month, now.hour, now.minute);
        const auto& s = m_player.stats;

        const int weekDayIndex = m_gameTime.currentWeekDayIndexMondayFirst();
        const std::string weekDay = ui::WeekDayCz(weekDayIndex);

        const DayPhase dynamicPhase = getDynamicDayPhase();
        const std::string dynamicPhaseText = dayPhaseToString(dynamicPhase);

        const std::string feastTitle = m_liturgicalCalendar.primaryTitle(now.day, now.month, now.year);
        const std::string feastId = m_liturgicalCalendar.primaryId(now.day, now.month, now.year);
        const std::string feastImportance = m_liturgicalCalendar.primaryImportance(now.day, now.month, now.year);
        const std::vector<std::string> feastTags = m_liturgicalCalendar.tagsForDate(now.day, now.month, now.year);

        const float hp01 = m_player.stats.condition.health / 100.0f;
        const float stamina01 = m_player.stats.condition.stamina / 100.0f;
        const float hunger01 = m_player.stats.legacyHunger() / 100.0f;
        const float thirst01 = m_player.stats.legacyThirst() / 100.0f;
        const float fatigue01 = m_player.stats.condition.fatigue / 100.0f;
        const float dirtiness01 = m_player.stats.legacyHygiene() / 100.0f;
        const float stress01 = m_player.stats.condition.stress / 100.0f;
        const float morale01 = m_player.stats.condition.morale / 100.0f;
        const float medievalFeel01 = m_player.stats.legacyMedievalFeel() / 100.0f;

        const std::string docDate = m_liturgicalCalendar.formatMedievalDate(
            now.day, now.month, now.year, weekDay,
            LiturgicalCalendar::Style::Documentary);

        const std::string spokenDate = m_liturgicalCalendar.formatMedievalDate(
            now.day, now.month, now.year, weekDay,
            LiturgicalCalendar::Style::Spoken);

        const std::string latinDate = m_liturgicalCalendar.formatMedievalDate(
            now.day, now.month, now.year, weekDay,
            LiturgicalCalendar::Style::Latin);

        std::string tagsJoined;
        for (size_t i = 0; i < feastTags.size(); ++i)
        {
            if (i > 0)
                tagsJoined += ", ";
            tagsJoined += feastTags[i];
        }

        ImGui::Text("=== CAS ===");
        ImGui::Text("Civilni datum: %s %s", weekDay.c_str(), m_gameTime.formatDateCz().c_str());
        ImGui::Text("Cas: %s", m_gameTime.formatTime().c_str());
        ImGui::Text("Dynamicka cast dne: %s", dynamicPhaseText.c_str());
        ImGui::Text("Je nedele: %s", weekDayIndex == 6 ? "ano" : "ne");

        ImGui::Separator();

        ImGui::Text("=== LITURGICKY KALENDAR ===");
        ImGui::Text("Hlavni svatek: %s", feastTitle.empty() ? "-" : feastTitle.c_str());
        ImGui::Text("Feast ID: %s", feastId.empty() ? "-" : feastId.c_str());
        ImGui::Text("Importance: %s", feastImportance.empty() ? "-" : feastImportance.c_str());
        ImGui::TextWrapped("Tagy: %s", tagsJoined.empty() ? "-" : tagsJoined.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("Dokumentarni: %s", docDate.c_str());
        ImGui::TextWrapped("Mluvena forma: %s", spokenDate.c_str());
        ImGui::TextWrapped("Latinsky: %s", latinDate.c_str());

        ImGui::Separator();

        ImGui::Text("=== SLUNCE A MESIC ===");
        ImGui::Text("Vychod slunce: %s", SunCycle::formatMinutesHhMm(sun.sunriseMinutes).c_str());
        ImGui::Text("Zapad slunce: %s", SunCycle::formatMinutesHhMm(sun.sunsetMinutes).c_str());
        ImGui::Text("Je den: %s", sun.isDay ? "ano" : "ne");
        ImGui::Text("Faze mesice: %s", moon.phase.c_str());
        ImGui::Text("Jas mesice: %.1f%%", moon.brightness * 100.0f);

        ImGui::Separator();

        ImGui::Text("=== PLAYER ===");
        ImGui::Text("Jmeno: %s", m_player.fullName().c_str());
        ImGui::Text("Profil: %s", DebugBackgroundName(s.background));
        ImGui::Text("Move speed: %.1f", m_player.currentMoveSpeed());
        ImGui::Text("Base / mult: %.1f / %.2f", s.moveSpeedBase, s.getMoveSpeedMultiplier());

        ui::ProgressStat("HP", hp01);
        ui::ProgressStat("Stamina", stamina01);
        ui::ProgressStat(ui::text::Hunger(), hunger01);
        ui::ProgressStat(ui::text::Thirst(), thirst01);
        ui::ProgressStat(ui::text::Fatigue(), fatigue01);
        ui::ProgressStat(ui::text::Hygiene(), dirtiness01);
        ui::ProgressStat("Stres", stress01);
        ui::ProgressStat("Moralka", morale01);
        ui::ProgressStat(ui::text::Social(), medievalFeel01);

        ImGui::Spacing();
        ImGui::Text("Telesna tepelna rovnovaha: %.1f / 100", m_player.stats.condition.bodyTemperature);
        ImGui::Text("Nosnost: %.1f / %.1f", m_player.stats.carryWeight, m_player.stats.carryCapacity);
        ImGui::Text("Objem: %.1f / %.1f", m_player.stats.carryVolume, m_player.stats.carryVolumeCapacity);
        ImGui::Text("Rychlost pohybu: %.1f", m_player.stats.getMoveSpeed());

        ImGui::Separator();
        ImGui::Text("ATRIBUTY");
        DebugBulletValue("Sila", s.attributes.strength);
        DebugBulletValue("Vydrz", s.attributes.endurance);
        DebugBulletValue("Obratnost", s.attributes.dexterity);
        DebugBulletValue("Vnimani", s.attributes.perception);
        DebugBulletValue("Inteligence", s.attributes.intelligence);
        DebugBulletValue("Charisma", s.attributes.charisma);
        DebugBulletValue("Vule", s.attributes.willpower);

        ImGui::Separator();
        ImGui::Text("SKILLY");
        DebugBulletValue("Ohen", s.survival.fireMaking);
        DebugBulletValue("Drevo", s.survival.woodProcessing);
        DebugBulletValue("Pristresek", s.survival.shelterBuilding);
        DebugBulletValue("Voda", s.survival.waterPurification);
        DebugBulletValue("Opravy", s.craft.toolRepair);
        DebugBulletValue("Improvizace", s.craft.improvisation);
        DebugBulletValue("Jednani", s.social.negotiation);
        DebugBulletValue("Empatie", s.social.empathy);
        DebugBulletValue("Historie", s.knowledge.history);
        DebugBulletValue("Latina", s.knowledge.latin);
        DebugBulletValue("Nemcina", s.knowledge.german);
        DebugBulletValue("Stredoveka cestina", s.knowledge.medievalCzech);
        DebugBulletValue("Beh", s.physical.running);
        DebugBulletValue("Noseni bremen", s.physical.loadHandling);
        DebugBulletValue("Soustredeni", s.mental.focus);
        DebugBulletValue("Pozorovani", s.mental.observation);

        ImGui::Separator();
        ImGui::Text("KOMFORT A LOADOUT");
        DebugBulletValue("Priroda", s.comfort.natureComfort);
        DebugBulletValue("Mesto", s.comfort.urbanComfort);
        DebugBulletValue("Spolecnost", s.comfort.socialComfort);
        DebugBulletValue("Citlivost hygiena", s.comfort.hygieneSensitivity);
        DebugBulletValue("Adaptace na stredovek", s.comfort.medievalAdaptation);
        ImGui::Text("Hamaka / plachta / filtr: %s / %s / %s", s.loadout.hammock ? "ano" : "ne", s.loadout.tarp ? "ano" : "ne", s.loadout.waterFilter ? "ano" : "ne");
        ImGui::Text("Drevak / plyn / chemicky ohrivac: %s / %s / %s", s.loadout.woodStove ? "ano" : "ne", s.loadout.gasStove ? "ano" : "ne", s.loadout.rationHeater ? "ano" : "ne");
        ImGui::Text("Powerbanka / solar / mobil: %s / %s / %s", s.loadout.powerBank ? "ano" : "ne", s.loadout.solarPanel ? "ano" : "ne", s.loadout.smartphoneOffline ? "ano" : "ne");
        ImGui::Text("Cestovni sprcha / hygiena kit: %s / %s", s.loadout.travelShower ? "ano" : "ne", s.loadout.hygieneKit ? "ano" : "ne");
        ImGui::Text("Nadoby na vodu: %d", s.loadout.waterContainers);
        ImGui::Text("Zatepleni / plachta kvalita: %.1f / %.1f", s.loadout.beddingWarmth, s.loadout.tarpQuality);

        ImGui::Separator();
        ImGui::Text("INVENTAR");

        auto drawContainer = [&](const char* title, const ContainerInventory& inv)
        {
            ImGui::Text("%s", title);

            if (inv.items.empty())
            {
                ImGui::BulletText("-");
                return;
            }

            for (const auto& stack : inv.items)
            {
                if (stack.empty())
                    continue;

                auto it = m_itemDefs.find(stack.itemId);
                const char* itemName = stack.itemId.c_str();
                if (it != m_itemDefs.end())
                    itemName = it->second.name.c_str();

                ImGui::BulletText("%s x%d", itemName, stack.count);
            }
        };

        drawContainer("Kapsy", m_player.inventory.pockets);
        drawContainer("Batoh", m_player.inventory.backpack);

        if (m_activeFoodPoisoning.active)
        {
            const std::string poisonStatus = activeFoodPoisoningStatusText();
            ui::ColoredStatus(
                m_activeFoodPoisoning.fatal
                    ? ImVec4(1.0f, 0.25f, 0.25f, 1.0f)
                    : ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                poisonStatus.c_str());
        }

        if (m_player.stats.condition.poisoned)
            ui::ColoredStatus(ImVec4(0.7f, 1.0f, 0.3f, 1.0f), ui::text::Poisoned());

        if (m_player.stats.condition.injured)
            ui::ColoredStatus(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), ui::text::Injured());

        if (m_player.stats.condition.fracture)
            ui::ColoredStatus(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), ui::text::Fracture());

        if (m_player.stats.condition.bleeding)
            ui::ColoredStatus(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), ui::text::Bleeding());

        if (m_player.stats.condition.treatedWound)
            ui::ColoredStatus(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), ui::text::TreatedWound());

        ImGui::Separator();
        ImGui::Text("=== POCASI ===");
        ImGui::Text("Min / Max: %.1f / %.1f C", m_todayWeather.minTemp, m_todayWeather.maxTemp);
        ImGui::Text("Aktualni teplota: %.1f C", m_runtimeWeather.currentTemp);
        ImGui::Text("Oblacnost: %.0f %%", m_todayWeather.cloudiness);
        ImGui::Text("Sance srazek: %.0f %%", m_todayWeather.precipitationChance);
        ImGui::Text("Typ srazek: %s", WeatherSystem::precipitationTypeToString(m_todayWeather.precipitationType));
        ImGui::Text("Intenzita srazek: %.2f", m_todayWeather.precipitationIntensity);
        ImGui::Text("Prsi: %s", m_runtimeWeather.isRaining ? "ano" : "ne");
        ImGui::Text("Intenzita deste: %.2f", m_runtimeWeather.rainIntensity);
        ImGui::Text("Mlha: %s", m_runtimeWeather.isFoggy ? "ano" : "ne");
        ImGui::Text("Vitr dnes / ted: %.1f / %.1f km/h", m_todayWeather.wind.avg, m_runtimeWeather.windNow);
        ImGui::Text("Ranni mlha: %.0f %%", m_todayWeather.fogMorning);
        ImGui::Text("Vlhkost zeme: %.0f %%", m_todayWeather.groundWetness);
        ImGui::Text("Nepohoda: %.1f", m_runtimeWeather.discomfortIndex);
        ImGui::Text("Fronta: %s", m_todayWeather.frontType.c_str());

        if (m_godMode)
            ui::ColoredStatus(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "GOD MODE ON");
    }

    ImGui::End();
}

void Campaign::renderNpcDialog()
{
    if (!m_npcDialogOpen)
        return;

    if (m_dialogNpcIndex < 0 || m_dialogNpcIndex >= (int)m_npcManager.npcs().size())
    {
        m_npcDialogOpen = false;
        m_dialogNpcIndex = -1;
        m_activeDialog = nullptr;
        m_activeDialogNodeId.clear();
        return;
    }

    const auto& npc = m_npcManager.npcs()[m_dialogNpcIndex];
    const std::string displayName = npc.displayName();

    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(40, 500), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin(("Rozhovor##npc_" + npc.id).c_str(), nullptr, flags))
    {
        if (m_activeDialog && !m_activeDialogNodeId.empty())
        {
            const DialogNode* node = m_dialogManager.findNode(*m_activeDialog, m_activeDialogNodeId);

            if (!node || !isDialogNodeAvailable(*node))
            {
                ImGui::Text("Chyba dialogu.");
                if (ImGui::Button("Zavrit"))
                {
                    m_npcDialogOpen = false;
                    m_dialogNpcIndex = -1;
                    m_activeDialog = nullptr;
                    m_activeDialogNodeId.clear();
                }
            }
            else
            {
                const std::string speaker = node->speaker.empty() ? displayName : node->speaker;

                ImGui::Text("%s", speaker.c_str());
                ImGui::Text("Naladeni: %d", npc.mood);
                ImGui::Separator();
                ImGui::TextWrapped("%s", node->text.c_str());
                ImGui::Spacing();

                for (int i = 0; i < (int)node->choices.size(); ++i)
                {
                    const auto& ch = node->choices[i];

                    if (!isDialogChoiceAvailable(ch))
                        continue;

                    std::string label = formatChoiceLabel(ch);
                    std::string btnId = label + "##choice_" + std::to_string(i);

                    if (ImGui::Button(btnId.c_str(), ImVec2(-1, 0)))
                    {
                        applyDialogChoiceEffects(ch);

                        if (ch.closeDialog || ch.nextNodeId.empty())
                        {
                            m_npcDialogOpen = false;
                            m_dialogNpcIndex = -1;
                            m_activeDialog = nullptr;
                            m_activeDialogNodeId.clear();
                        }
                        else
                        {
                            m_activeDialogNodeId = ch.nextNodeId;
                        }
                    }
                }

                ImGui::Spacing();
                if (ImGui::Button("Ukoncit rozhovor", ImVec2(-1, 0)))
                {
                    m_npcDialogOpen = false;
                    m_dialogNpcIndex = -1;
                    m_activeDialog = nullptr;
                    m_activeDialogNodeId.clear();
                }
            }
        }
        else
        {
            ImGui::Text("NPC: %s", displayName.c_str());

            ImGui::Separator();
            ImGui::TextWrapped("%s", npc.greeting.empty()
                ? "NPC na tebe mlcky hledi."
                : npc.greeting.c_str());

            ImGui::Spacing();
            if (ImGui::Button("Odejit", ImVec2(-1, 0)))
            {
                m_npcDialogOpen = false;
                m_dialogNpcIndex = -1;
                m_activeDialog = nullptr;
                m_activeDialogNodeId.clear();
            }
        }
    }

    ImGui::End();
}

void Campaign::renderQuestJournal()
{
    if (m_questJournalFocus)
    {
        ImGui::SetNextWindowFocus();
        m_questJournalFocus = false;
    }

    ImGui::SetNextWindowPos(ImVec2(80, 80), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Ukolnicek", &m_questJournalOpen))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Aktivni ukoly");
    ImGui::Separator();

    bool anyActive = false;

    for (const auto& q : m_questDefs)
    {
        if (!isQuestActive(q))
            continue;

        anyActive = true;

        ImGui::PushID(q.id.c_str());

        ImGui::Text("%s", q.title.c_str());
        ImGui::TextWrapped("%s", q.description.c_str());

        if (isQuestReadyToTurnIn(q))
            ImGui::TextColored(ImVec4(0.85f, 0.80f, 0.35f, 1.0f), "Stav: pripraveno k odevzdani");
        else
            ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.95f, 1.0f), "Stav: probiha");

        ImGui::Separator();

        ImGui::Separator();
        ImGui::Text("DEBUG");
        ImGui::Text("started: %s", hasStoryFlag("quest_matej_water_started") ? "ANO" : "NE");
        ImGui::Text("ready: %s", hasStoryFlag("quest_matej_water_ready") ? "ANO" : "NE");
        ImGui::Text("done: %s", hasStoryFlag("quest_matej_water_done") ? "ANO" : "NE");

        const int footTx = (int)std::floor(m_player.x / (float)m_tileSize);
        const int footTy = (int)std::floor((m_player.y - 1.0f) / (float)m_tileSize);
        ImGui::Text("player foot tile: %d,%d", footTx, footTy);

        const auto* dbgDef = m_map.getObjDefAt(m_objCatalog, footTx, footTy);
        ImGui::Text("tile obj: %s", dbgDef ? dbgDef->id.c_str() : "-");
        ImGui::PopID();
    }

    if (!anyActive)
        ImGui::TextDisabled("Zadne aktivni ukoly.");

    if (ImGui::CollapsingHeader("Dokoncene ukoly"))
    {
        bool anyDone = false;

        for (const auto& q : m_questDefs)
        {
            if (!isQuestDone(q))
                continue;

            anyDone = true;
            ImGui::BulletText("%s", q.title.c_str());
        }

        if (!anyDone)
            ImGui::TextDisabled("Zat�m zadne.");
    }

    ImGui::End();
}

// Vykresl� item slot pro invent�?, v?etn? drag&drop logiky a tooltipu. Vrac� true pokud byl slot kliknut (pro p?�padn� dal?� akce, nap?. otev?en� detailn�ho pohledu na item).
void Campaign::drawLabeledSlot(
    const char* label,
    const char* id,
    ItemStack& slotRef,
    float slotSize)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(170.0f);
    drawItemSlot(id, slotRef, m_itemDefs, slotSize);
}

void Campaign::renderDraggedItemIcon()
{
    if (!m_dragItem.active || m_dragItem.stack.empty())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        return;
    }

    ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    if (!m_defaultItemIcon)
        return;

    ImGuiIO& io = ImGui::GetIO();
    auto drawList = ImGui::GetForegroundDrawList();

    const float size = 32.0f;
    ImVec2 pos = io.MousePos;

    ImVec2 topLeft(pos.x - size * 0.5f, pos.y - size * 0.5f);
    ImVec2 bottomRight(topLeft.x + size, topLeft.y + size);

    // ghost background
    drawList->AddRectFilled(
        topLeft,
        bottomRight,
        IM_COL32(20, 20, 20, 120));

    // gold frame
    drawList->AddRect(
        topLeft,
        bottomRight,
        IM_COL32(255, 215, 80, 255),
        0.0f,
        0,
        2.0f);

    // icon
    auto it = m_itemDefs.find(m_dragItem.stack.itemId);

    SDL_Texture* tex = m_defaultItemIcon;

    if (it != m_itemDefs.end())
        tex = getItemIconTexture(it->second);

    drawList->AddImage(
        (ImTextureID)tex,
        topLeft,
        bottomRight);

    // stack count
    if (m_dragItem.stack.count > 1)
    {
        std::string txt = std::to_string(m_dragItem.stack.count);
        drawList->AddText(
            ImVec2(bottomRight.x - 14.0f, bottomRight.y - 16.0f),
            IM_COL32(255, 255, 0, 255),
            txt.c_str());
    }
}

bool Campaign::drawContainerSlot(
    const char* id,
    ContainerInventory& container,
    int index,
    const std::unordered_map<std::string, ItemDef>& defs,
    float size)
{
    container.compact();

    if (index < (int)container.items.size())
    {
        bool result = drawItemSlot(id, container.items[index], defs, size);
        container.compact();
        return result;
    }

    ItemStack virtualEmpty;

    ImGui::PushID(id);

    bool leftClicked = false;
    bool rightClicked = false;

    ImGui::Button("##slot", ImVec2(size, size));

    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    bool canDropHere = true;
    if (m_dragItem.active && !m_dragItem.stack.empty())
    {
        auto it = defs.find(m_dragItem.stack.itemId);
        if (it != defs.end())
        {
            const ItemDef& def = it->second;
            canDropHere = container.canAccept(def, 1, defs);
        }
    }

    if (m_dragItem.active && ImGui::IsItemHovered())
    {
        ImU32 fillColor = canDropHere
            ? IM_COL32(80, 180, 90, 50)
            : IM_COL32(200, 70, 70, 50);

        ImGui::GetWindowDrawList()->AddRectFilled(min, max, fillColor);
    }

    ImU32 borderColor = IM_COL32(90, 120, 170, 255);
    if (ImGui::IsItemHovered())
        borderColor = canDropHere
        ? IM_COL32(170, 200, 255, 255)
        : IM_COL32(220, 80, 80, 255);

    ImGui::GetWindowDrawList()->AddRect(min, max, borderColor, 0.0f, 0, 2.0f);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        leftClicked = true;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        rightClicked = true;

    if (leftClicked && m_dragItem.active && !m_dragItem.stack.empty())
    {
        auto it = defs.find(m_dragItem.stack.itemId);
        if (it != defs.end())
        {
            const ItemDef& def = it->second;
            if (container.canAccept(def, m_dragItem.stack.count, defs))
            {
                container.items.push_back(m_dragItem.stack);
                m_dragItem.stack.clear();
                m_dragItem.active = false;
                m_dragItem.source = nullptr;
                container.compact();
            }
        }
    }

    if (rightClicked && m_dragItem.active && !m_dragItem.stack.empty())
    {
        auto it = defs.find(m_dragItem.stack.itemId);
        if (it != defs.end())
        {
            const ItemDef& def = it->second;
            if (container.canAccept(def, 1, defs))
            {
                ItemStack s = m_dragItem.stack;
                s.count = 1;
                container.items.push_back(s);

                m_dragItem.stack.count -= 1;
                if (m_dragItem.stack.count <= 0)
                {
                    m_dragItem.stack.clear();
                    m_dragItem.active = false;
                    m_dragItem.source = nullptr;
                }

                container.compact();
            }
        }
    }

    ImGui::PopID();
    return leftClicked || rightClicked;
}

bool Campaign::isEquipmentSlotId(const std::string& slotId) const
{
    return
        slotId == "head" ||
        slotId == "torsoInner" ||
        slotId == "torsoOuter" ||
        slotId == "legs" ||
        slotId == "feet" ||
        slotId == "cloak" ||
        slotId == "beltEquip" ||
        slotId == "back" ||
        slotId == "leftHand" ||
        slotId == "rightHand" ||
        slotId == "qa_leftHand" ||
        slotId == "qa_rightHand";
}

void Campaign::refreshInventoryDerivedStats()
{
    m_player.stats.carryWeight = m_player.inventory.computeTotalWeight(m_itemDefs);
    m_player.stats.carryVolume = m_player.inventory.computeTotalVolume(m_itemDefs);
}

bool Campaign::isInventoryItemUsable(const ItemDef& def, const ItemStack& stack) const
{
    if (stack.empty())
        return false;

    const std::string& id = stack.itemId;

    if (id.rfind("forage_", 0) == 0 || id.rfind("forage:", 0) == 0)
        return true;

    if (id == "MRE" || id == "food_ration_basic")
        return true;

    if (ContainsIdToken(id, "water_bottle") ||
        ContainsIdToken(id, "canteen") ||
        ContainsIdToken(id, "cutora") ||
        ContainsIdToken(id, "cútora") ||
        ContainsIdToken(id, "čutora"))
    {
        return true;
    }

    return def.quickUsable;
}

bool Campaign::applyInventoryItemUseEffects(const std::string& itemId, const ItemDef& def, bool& outConsumeOne)
{
    outConsumeOne = false;

    auto clampNeeds = [&]()
    {
        m_player.stats.condition.health = PlayerStats::Clamp01To100(m_player.stats.condition.health);
        m_player.stats.condition.nutrition = PlayerStats::Clamp01To100(m_player.stats.condition.nutrition);
        m_player.stats.condition.hydration = PlayerStats::Clamp01To100(m_player.stats.condition.hydration);
        m_player.stats.condition.morale = PlayerStats::Clamp01To100(m_player.stats.condition.morale);
        m_player.stats.condition.stress = PlayerStats::Clamp01To100(m_player.stats.condition.stress);
    };

    if (ContainsIdToken(itemId, "water_bottle") ||
        ContainsIdToken(itemId, "canteen") ||
        ContainsIdToken(itemId, "cutora") ||
        ContainsIdToken(itemId, "cútora") ||
        ContainsIdToken(itemId, "čutora"))
    {
        const float before = m_player.stats.condition.hydration;
        m_player.stats.condition.hydration += 45.0f;
        m_player.stats.condition.morale += 1.0f;
        clampNeeds();

        if (m_player.stats.condition.hydration > before + 0.1f)
            consoleLog("Napil ses z nádoby. Žízeň polevila.");
        else
            consoleLog("Napil ses, ale už jsi byl dobře hydratovaný.");

        // TODO: later replace this with container charges / water quality.
        outConsumeOne = false;
        return true;
    }

    if (itemId == "MRE")
    {
        m_player.stats.condition.nutrition = 100.0f;
        m_player.stats.condition.morale += 3.0f;
        clampNeeds();
        consoleLog("Snědl jsi MRE. Vlastní ohřev udělal svoje — hlad je pryč.");
        outConsumeOne = true;
        return true;
    }

    if (itemId == "food_ration_basic")
    {
        m_player.stats.condition.nutrition += 38.0f;
        m_player.stats.condition.hydration -= 2.0f;
        clampNeeds();
        consoleLog("Snědl jsi základní dávku jídla.");
        outConsumeOne = true;
        return true;
    }

    if (itemId.rfind("forage_", 0) == 0)
    {
        const std::string speciesId = itemId.substr(7);
        const ForageSpeciesDef* species = m_forageDb.findSpecies(speciesId);

        if (!species)
        {
            m_player.stats.condition.nutrition += 2.0f;

            // Safety fallback for older saves/items where the dynamic forage item id no longer
            // maps exactly back to species.json. It lets Amanita test items still trigger poisoning.
            const std::string probe = itemId + " " + def.name + " " + def.description;
            if (ContainsIdToken(probe, "zelen") || ContainsIdToken(probe, "phalloides") || ContainsIdToken(probe, "kalich"))
            {
                startFoodPoisoning(def.name.empty() ? itemId : def.name, true);
                consoleLog("Snědl jsi neznámý, ale velmi nebezpečný vzorek. Otrava byla spuštěna podle názvu předmětu.");
            }
            else if (ContainsIdToken(probe, "červen") || ContainsIdToken(probe, "cerven") || ContainsIdToken(probe, "muscaria"))
            {
                startFoodPoisoning(def.name.empty() ? itemId : def.name, false);
                consoleLog("Snědl jsi podezřelý vzorek. Spouštím lehkou otravu podle názvu předmětu.");
            }
            else
            {
                consoleLog("Ochutnal jsi neznámý přírodní vzorek. Moc tě to nezasytilo.");
            }

            clampNeeds();
            outConsumeOne = true;
            return true;
        }

        const bool knownSpecies = isForageSpeciesKnown(species->id);
        const std::string trueName = species->trueName.empty() ? species->id : species->trueName;
        const std::string eatenLabel = knownSpecies
            ? trueName
            : (def.name.empty() ? std::string("neznámý přírodní vzorek") : def.name);

        // Raw mushrooms are intentionally weak as food until cooking/crafting exists.
        if (species->archetypeId == "generic_mushroom_patch")
        {
            m_player.stats.condition.nutrition += 6.0f;
            m_player.stats.condition.morale -= 1.0f;
        }
        else if (species->edibility == "edible")
        {
            m_player.stats.condition.nutrition += 8.0f;
        }
        else
        {
            m_player.stats.condition.nutrition += 2.0f;
        }

        if (HasForageEffect(*species, "poison_deadly_48h") || species->toxicityLevel >= 90 ||
            species->id.find("zelen") != std::string::npos || species->trueName.find("zelen") != std::string::npos)
        {
            startFoodPoisoning(eatenLabel, true);
            consoleLog("Snědl jsi " + eatenLabel + ". Zpočátku se nemusí stát nic, ale tohle byla velmi špatná volba.");
        }
        else if (HasForageEffect(*species, "poison_mild") || species->toxicityLevel >= 25 ||
            species->id.find("červen") != std::string::npos || species->trueName.find("červen") != std::string::npos)
        {
            startFoodPoisoning(eatenLabel, false);
            consoleLog("Snědl jsi " + eatenLabel + ". Začíná ti být divně od žaludku.");
        }
        else
        {
            consoleLog("Snědl jsi " + eatenLabel + ". Syrové tě to zasytilo jen trochu.");
        }

        clampNeeds();
        outConsumeOne = true;
        return true;
    }

    if (def.quickUsable)
    {
        consoleLog("Tenhle předmět zatím nemá přiřazený konkrétní účinek použití.");
        return false;
    }

    return false;
}

bool Campaign::useInventoryItem(ItemStack& stack)
{
    if (stack.empty())
        return false;

    auto it = m_itemDefs.find(stack.itemId);
    if (it == m_itemDefs.end())
        return false;

    bool consumeOne = false;
    if (!applyInventoryItemUseEffects(stack.itemId, it->second, consumeOne))
        return false;

    if (consumeOne)
    {
        stack.count -= 1;
        if (stack.count <= 0)
            stack.clear();
    }

    refreshInventoryDerivedStats();
    return true;
}

bool Campaign::canPlaceItemIntoSlot(
    const ItemStack& movingStack,
    const ItemStack& targetSlot,
    const std::string& slotId,
    const std::unordered_map<std::string, ItemDef>& defs) const
{
    if (movingStack.empty())
        return false;

    auto it = defs.find(movingStack.itemId);
    if (it == defs.end())
        return false;

    const ItemDef& def = it->second;

    // container sloty (batoh/kapsy) ? tam m??e skoro v?echno
    if (slotId.rfind("bp_", 0) == 0 || slotId.rfind("pocket_", 0) == 0)
        return true;

    // belt sloty
    if (slotId == "qa_knife")
        return def.beltCompatible && def.preferredBeltSlot == BeltSlot::Knife;

    if (slotId == "qa_pouch")
        return def.beltCompatible && def.preferredBeltSlot == BeltSlot::Pouch;

    if (slotId == "qa_util1" || slotId == "qa_util2")
        return def.beltCompatible &&
        (def.preferredBeltSlot == BeltSlot::Utility1 ||
            def.preferredBeltSlot == BeltSlot::Utility2 ||
            def.preferredBeltSlot == BeltSlot::None);

    // ruce
    if (slotId == "leftHand" || slotId == "rightHand" ||
        slotId == "qa_leftHand" || slotId == "qa_rightHand")
    {
        return def.equipSlot == EquipSlot::MainHand ||
            def.equipSlot == EquipSlot::OffHand ||
            def.category == ItemCategory::Tool ||
            def.category == ItemCategory::WeaponTool;
    }

    // equipment sloty
    if (slotId == "head")       return def.equippable && def.equipSlot == EquipSlot::Head;
    if (slotId == "torsoInner") return def.equippable && def.equipSlot == EquipSlot::TorsoInner;
    if (slotId == "torsoOuter") return def.equippable && def.equipSlot == EquipSlot::TorsoOuter;
    if (slotId == "legs")       return def.equippable && def.equipSlot == EquipSlot::Legs;
    if (slotId == "feet")       return def.equippable && def.equipSlot == EquipSlot::Feet;
    if (slotId == "cloak")      return def.equippable && def.equipSlot == EquipSlot::Cloak;
    if (slotId == "beltEquip")  return def.equippable && def.equipSlot == EquipSlot::Belt;
    if (slotId == "back")       return def.equippable && def.equipSlot == EquipSlot::Back;

    return false;
}

bool Campaign::drawItemSlot(
    const char* id,
    ItemStack& slot,
    const std::unordered_map<std::string, ItemDef>& defs,
    float size)
{
    ImGui::PushID(id);

    const std::string slotKey = id;

    bool leftClicked = false;
    bool rightClicked = false;

    auto itDef = defs.find(slot.itemId);
    const ItemDef* slotDef = (!slot.empty() && itDef != defs.end()) ? &itDef->second : nullptr;
    const bool itemLocked = (slotDef != nullptr) ? slotDef->lockedInInventory : false;

    ImGuiIO& io = ImGui::GetIO();
    const bool shiftHeld = io.KeyShift;

    ImGui::Button("##slot", ImVec2(size, size));

    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    bool canDropHere = true;
    if (m_dragItem.active && !m_dragItem.stack.empty())
        canDropHere = canPlaceItemIntoSlot(m_dragItem.stack, slot, slotKey, defs);

    ImU32 borderColor = IM_COL32(90, 120, 170, 255);

    if (ImGui::IsItemHovered())
        borderColor = canDropHere
        ? IM_COL32(170, 200, 255, 255)
        : IM_COL32(220, 80, 80, 255);

    if (m_dragItem.active && m_dragItem.source == &slot)
        borderColor = IM_COL32(255, 215, 80, 255);

    if (m_dragItem.active && ImGui::IsItemHovered())
    {
        ImU32 fillColor = canDropHere
            ? IM_COL32(80, 180, 90, 50)
            : IM_COL32(200, 70, 70, 50);

        ImGui::GetWindowDrawList()->AddRectFilled(min, max, fillColor);
    }

    ImGui::GetWindowDrawList()->AddRect(min, max, borderColor, 0.0f, 0, 2.0f);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        leftClicked = true;

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        rightClicked = true;

    const bool inspectClicked =
        ImGui::IsItemHovered() &&
        io.KeyAlt &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    bool drewAtlasIcon = false;
    if (!slot.empty() && slotDef && slotDef->spriteId.rfind("forage:", 0) == 0)
    {
        const std::string forageSpriteId = slotDef->spriteId.substr(7);
        auto itSprite = m_forageSprites.find(forageSpriteId);
        if (itSprite != m_forageSprites.end())
        {
            SDL_Texture* tex = textureForForageSprite(itSprite->second);
            if (tex)
            {
                int texW = 1, texH = 1;
                SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);

                const float iconSize = 32.0f;
                const float iconX = min.x + (size - iconSize) * 0.5f;
                const float iconY = min.y + (size - iconSize) * 0.5f;

                const SDL_Rect& src = itSprite->second.src;
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)tex,
                    ImVec2(iconX, iconY),
                    ImVec2(iconX + iconSize, iconY + iconSize),
                    ImVec2((float)src.x / (float)texW, (float)src.y / (float)texH),
                    ImVec2((float)(src.x + src.w) / (float)texW, (float)(src.y + src.h) / (float)texH));

                drewAtlasIcon = true;
            }
        }
    }

    SDL_Texture* iconTex = m_defaultItemIcon;
    if (!drewAtlasIcon && slotDef)
        iconTex = getItemIconTexture(*slotDef);

    if (!slot.empty() && !drewAtlasIcon && iconTex)
    {
        const float iconSize = 32.0f;
        const float iconX = min.x + (size - iconSize) * 0.5f;
        const float iconY = min.y + (size - iconSize) * 0.5f;

        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)iconTex,
            ImVec2(iconX, iconY),
            ImVec2(iconX + iconSize, iconY + iconSize));
    }

    if (!slot.empty() && slot.count > 1)
    {
        std::string countText = std::to_string(slot.count);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(min.x + size - 16.0f, min.y + size - 18.0f),
            IM_COL32(255, 220, 80, 255),
            countText.c_str());
    }

    if (!slot.empty() && ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 18.0f);
        ImGui::SetWindowFontScale(1.0f);

        if (slotDef)
        {
            ImGui::TextUnformatted(slotDef->name.c_str());

            if (!slotDef->description.empty())
            {
                ImGui::Separator();
                ImGui::TextWrapped("%s", slotDef->description.c_str());
            }

            if (!slotDef->flavorText.empty())
            {
                ImGui::Separator();
                ImGui::TextWrapped("\"%s\"", slotDef->flavorText.c_str());
            }

            ImGui::Separator();
            ImGui::Text("Vaha: %.2f", slotDef->weight);
            ImGui::Text("Objem: %.2f", slotDef->volume);
            ImGui::Text("Odolnost: %.0f %%", slot.durability);

            if (slotDef->lockedInInventory)
            {
                ImGui::Separator();
                ImGui::TextDisabled("Tento predmet nelze odlozit.");
            }

            if (!slotDef->audioNoteSfx.empty())
            {
                ImGui::Separator();
                ImGui::TextDisabled("Alt + LMB = přehrát poznámku");
            }

            if (isInventoryItemUsable(*slotDef, slot))
            {
                ImGui::Separator();
                ImGui::TextDisabled("Ctrl + LMB / dvojklik = použít");
            }
        }

        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    const bool useClicked =
        !m_dragItem.active &&
        !slot.empty() &&
        slotDef &&
        isInventoryItemUsable(*slotDef, slot) &&
        ((leftClicked && io.KeyCtrl) ||
         (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)));

    if (useClicked)
    {
        const bool used = useInventoryItem(slot);
        ImGui::PopID();
        return used;
    }

	// ALT + LEVY KLIK = prehrat zvukovou poznamku
    if (inspectClicked)
    {
        if (slotDef && !slotDef->audioNoteSfx.empty())
            m_audioManager.playSfx(slotDef->audioNoteSfx);

        ImGui::PopID();
        return true;
    }

    // LEVY KLIK = cely stack / merge / swap
    if (leftClicked)
    {
        if (!m_dragItem.active && !slot.empty() && !itemLocked)
        {
            if (shiftHeld && slot.count > 1)
            {
                m_dragItem.active = true;
                m_dragItem.source = &slot;
                m_dragItem.stack = slot;

                const int take = (slot.count + 1) / 2;
                m_dragItem.stack.count = take;
                slot.count -= take;

                if (slot.count <= 0)
                    slot.clear();
            }
            else
            {
                m_dragItem.active = true;
                m_dragItem.stack = slot;
                m_dragItem.source = &slot;
                slot.clear();
            }
        }
        else if (m_dragItem.active)
        {
            if (canPlaceItemIntoSlot(m_dragItem.stack, slot, slotKey, defs))
            {
                if (!slot.empty() && slot.itemId == m_dragItem.stack.itemId)
                {
                    auto it = defs.find(slot.itemId);
                    if (it != defs.end() && it->second.stackable)
                    {
                        const int maxStack = std::max(1, it->second.maxStack);
                        const int freeSpace = maxStack - slot.count;

                        if (freeSpace > 0)
                        {
                            const int moveCount = std::min(freeSpace, m_dragItem.stack.count);
                            slot.count += moveCount;
                            m_dragItem.stack.count -= moveCount;

                            if (m_dragItem.stack.count <= 0)
                            {
                                m_dragItem.stack.clear();
                                m_dragItem.active = false;
                                m_dragItem.source = nullptr;
                            }
                        }
                        else
                        {
                            ItemStack temp = slot;
                            slot = m_dragItem.stack;
                            m_dragItem.stack = temp;
                        }
                    }
                    else
                    {
                        ItemStack temp = slot;
                        slot = m_dragItem.stack;
                        m_dragItem.stack = temp;
                    }
                }
                else
                {
                    ItemStack temp = slot;
                    slot = m_dragItem.stack;
                    m_dragItem.stack = temp;

                    if (m_dragItem.stack.empty())
                    {
                        m_dragItem.active = false;
                        m_dragItem.source = nullptr;
                    }
                }
            }
        }
    }

    // PRAVY KLIK = split stack / poloz 1 kus
    if (rightClicked)
    {
        if (!m_dragItem.active && !slot.empty() && slot.count > 1 && !itemLocked)
        {
            m_dragItem.active = true;
            m_dragItem.source = &slot;
            m_dragItem.stack = slot;

            const int take = (slot.count + 1) / 2;
            m_dragItem.stack.count = take;
            slot.count -= take;

            if (slot.count <= 0)
                slot.clear();
        }
        else if (!m_dragItem.active && !slot.empty() && slot.count == 1 && !itemLocked)
        {
            m_dragItem.active = true;
            m_dragItem.source = &slot;
            m_dragItem.stack = slot;
            slot.clear();
        }
        else if (m_dragItem.active)
        {
            if (!canPlaceItemIntoSlot(m_dragItem.stack, slot, slotKey, defs))
            {
                // nic
            }
            else if (slot.empty())
            {
                slot = m_dragItem.stack;
                slot.count = 1;
                m_dragItem.stack.count -= 1;

                if (m_dragItem.stack.count <= 0)
                {
                    m_dragItem.stack.clear();
                    m_dragItem.active = false;
                    m_dragItem.source = nullptr;
                }
            }
            else if (slot.itemId == m_dragItem.stack.itemId)
            {
                auto it = defs.find(slot.itemId);
                if (it != defs.end())
                {
                    const int maxStack = std::max(1, it->second.maxStack);
                    if (slot.count < maxStack)
                    {
                        slot.count += 1;
                        m_dragItem.stack.count -= 1;

                        if (m_dragItem.stack.count <= 0)
                        {
                            m_dragItem.stack.clear();
                            m_dragItem.active = false;
                            m_dragItem.source = nullptr;
                        }
                    }
                }
            }
        }
    }

    ImGui::PopID();
    return leftClicked || rightClicked || inspectClicked;
}

void Campaign::renderInventoryUI()
{
    if (!m_inventoryOpen)
        return;

    if (!m_dragItem.active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);

    ImGui::SetNextWindowPos(ImVec2(60, 120), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(900, 520), ImGuiCond_Appearing);
    ImGui::SetNextWindowFocus();

    auto& inv = m_player.inventory;

    if (!ImGui::Begin("Inventar", &m_inventoryOpen))
    {
        ImGui::End();
        return;
    }
    const float slot = 40.0f;

    ImGui::Columns(2, nullptr, true);

    ImGui::Text("Vybaveni");
    ImGui::Separator();

    drawLabeledSlot("Hlava", "head", inv.equipped.head, slot);
    drawLabeledSlot("Spodni odev", "torsoInner", inv.equipped.torsoInner, slot);
    drawLabeledSlot("Svrchni odev", "torsoOuter", inv.equipped.torsoOuter, slot);
    drawLabeledSlot("Nohavice", "legs", inv.equipped.legs, slot);
    drawLabeledSlot("Obuv", "feet", inv.equipped.feet, slot);
    drawLabeledSlot("Plast", "cloak", inv.equipped.cloak, slot);
    drawLabeledSlot("Opasek", "beltEquip", inv.equipped.belt, slot);
    drawLabeledSlot("Batoh", "back", inv.equipped.back, slot);

    ImGui::NextColumn();

    ImGui::Text("Batoh");
    ImGui::Separator();

	inv.backpack.compact();
    const int backpackVisibleSlots = std::max(8, (int)inv.backpack.items.size() + 1);

    for (int i = 0; i < backpackVisibleSlots; ++i)
    {
        std::string id = "bp_" + std::to_string(i);
        drawContainerSlot(id.c_str(), inv.backpack, i, m_itemDefs, slot);

        if ((i + 1) % 4 != 0)
            ImGui::SameLine();
    }

    ImGui::Spacing();
    ImGui::Text("Kapsy");
    ImGui::Separator();

	inv.pockets.compact();
    const int pocketVisibleSlots = std::max(4, (int)inv.pockets.items.size() + 1);

    for (int i = 0; i < pocketVisibleSlots; ++i)
    {
        std::string id = "pocket_" + std::to_string(i);
        drawContainerSlot(id.c_str(), inv.pockets, i, m_itemDefs, slot);

        if ((i + 1) % 4 != 0)
            ImGui::SameLine();
    }

    ImGui::Columns(1);

    if (m_activeFoodPoisoning.active)
    {
        ImGui::Separator();
        const std::string poisonStatus = activeFoodPoisoningStatusText();
        ImGui::TextColored(
            m_activeFoodPoisoning.fatal
                ? ImVec4(1.0f, 0.25f, 0.25f, 1.0f)
                : ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
            "%s", poisonStatus.c_str());
        ImGui::TextDisabled("Debug: konzole -> poison / skip 8h / skip 24h. IDDQD muze ucinky maskovat.");
    }

    if (m_dragItem.active && !m_dragItem.stack.empty())
    {
        ImGui::Separator();

        auto it = m_itemDefs.find(m_dragItem.stack.itemId);
        const char* heldName =
            (it != m_itemDefs.end())
            ? it->second.name.c_str()
            : m_dragItem.stack.itemId.c_str();

        ImGui::Text("Drzis: %s x%d", heldName, m_dragItem.stack.count);
        ImGui::TextDisabled("LMB = cely stack, RMB = pulka / poloz 1 kus");
    }

    ImGui::End();

    renderDraggedItemIcon();
}

void Campaign::renderQuickAccessBar()
{
    const float slot = 40.0f;
    const float spacing = 6.0f;
    const float totalWidth = slot * 6.0f + spacing * 5.0f;

    int screenW = 0, screenH = 0;
    SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);

    ImGui::SetNextWindowPos(
        ImVec2((screenW - totalWidth) * 0.5f, screenH - 90.0f),
        ImGuiCond_Always);

    ImGui::SetNextWindowBgAlpha(0.35f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("QuickAccessBar", nullptr, flags))
    {
        auto& inv = m_player.inventory;

		inv.backpack.compact();
		inv.pockets.compact();

        drawItemSlot("qa_leftHand", inv.equipped.offHand, m_itemDefs, slot);
        ImGui::SameLine();

        drawItemSlot("qa_knife", inv.beltSlots.knife, m_itemDefs, slot);
        ImGui::SameLine();

        drawItemSlot("qa_pouch", inv.beltSlots.pouch, m_itemDefs, slot);
        ImGui::SameLine();

        drawItemSlot("qa_util1", inv.beltSlots.utility1, m_itemDefs, slot);
        ImGui::SameLine();

        drawItemSlot("qa_util2", inv.beltSlots.utility2, m_itemDefs, slot);
        ImGui::SameLine();

        drawItemSlot("qa_rightHand", inv.equipped.mainHand, m_itemDefs, slot);
    }

    ImGui::End();
}

SDL_Texture* Campaign::getItemIconTexture(const ItemDef& def)
{
    if (def.spriteId.empty())
        return m_defaultItemIcon;

    auto it = m_itemIconCache.find(def.spriteId);
    if (it != m_itemIconCache.end())
        return it->second ? it->second : m_defaultItemIcon;

    const std::string path = "assets/data/items/" + def.spriteId + ".png";

    int w = 0, h = 0;
    SDL_Texture* tex = loadTexture(m_renderer, path.c_str(), w, h);

    m_itemIconCache[def.spriteId] = tex;
    return tex ? tex : m_defaultItemIcon;
}

void Campaign::unloadItemIcons()
{
    for (auto& [id, tex] : m_itemIconCache)
    {
        if (tex)
            SDL_DestroyTexture(tex);
    }
    m_itemIconCache.clear();
}

static SDL_Rect GetObjectWorldRectScaled(const gameobj::ObjectDef& def, int wx, int wy, float scale)
{
    SDL_Rect r{};
    r.w = (int)std::lround(def.src.w * scale);
    r.h = (int)std::lround(def.src.h * scale);
    r.x = (int)std::lround(wx - def.pivot.x * scale);
    r.y = (int)std::lround(wy - def.pivot.y * scale);
    return r;
}



static void DrawForageSpriteImage(SDL_Texture* tex, const SDL_Rect& src, float maxW, float maxH)
{
    if (!tex)
        return;

    int tw = 1;
    int th = 1;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);

    const float scale = std::min(maxW / std::max(1, src.w), maxH / std::max(1, src.h));
    const ImVec2 size(std::max(1.0f, src.w * scale), std::max(1.0f, src.h * scale));
    const ImVec2 uv0((float)src.x / (float)tw, (float)src.y / (float)th);
    const ImVec2 uv1((float)(src.x + src.w) / (float)tw, (float)(src.y + src.h) / (float)th);
    ImGui::Image((ImTextureID)tex, size, uv0, uv1);
}


static std::string JoinForageValues(const std::vector<std::string>& values)
{
    std::string out;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (values[i].empty())
            continue;
        if (!out.empty())
            out += "; ";
        out += values[i];
    }
    return out;
}

static const char* ForageTraitLabelCzFallback(const std::string& id)
{
    if (id == "cap")     return "Klobouk";
    if (id == "stem")    return "Třeň / stonek";
    if (id == "gills")   return "Lupeny / rourky";
    if (id == "smell")   return "Vůně po promnutí";
    if (id == "habitat") return "Místo nálezu";
    if (id == "flower")  return "Květ";
    if (id == "leaf")    return "List";
    if (id == "root")    return "Kořen";
    if (id == "seeds")   return "Semena";
    if (id == "milk")    return "Mléko / šťáva";
    return nullptr;
}

static std::string ForageTraitLabelForUi(const ForageTraitGroup* group, const std::string& id)
{
    if (const char* cz = ForageTraitLabelCzFallback(id))
        return cz;

    if (group && !group->label.empty())
        return group->label;

    return id;
}

static ImU32 ScoreColor(int score)
{
    score = std::clamp(score, 0, 100);
    const float t = score / 100.0f;
    const int r = (int)std::lround(220.0f * (1.0f - t) + 60.0f * t);
    const int g = (int)std::lround(70.0f * (1.0f - t) + 210.0f * t);
    const int b = (int)std::lround(50.0f * (1.0f - t) + 90.0f * t);
    return IM_COL32(r, g, b, 255);
}

static void DrawIdentificationScoreBar(const ForageExaminationResult& result, float width)
{
    width = std::max(width, 220.0f);
    const float height = 22.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 q(p.x + width, p.y + height);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(p, q, IM_COL32(30, 25, 20, 220), 6.0f);

    const float fillW = width * std::clamp(result.scorePercent / 100.0f, 0.0f, 1.0f);
    if (fillW > 1.0f)
        dl->AddRectFilled(p, ImVec2(p.x + fillW, q.y), ScoreColor(result.scorePercent), 6.0f);

    dl->AddRect(p, q, IM_COL32(210, 170, 90, 220), 6.0f, 0, 1.4f);

    const std::string label = "Přesnost určení: " + std::to_string(result.scorePercent) + "%";
    const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    dl->AddText(ImVec2(p.x + (width - ts.x) * 0.5f, p.y + (height - ts.y) * 0.5f), IM_COL32(255, 245, 220, 255), label.c_str());

    ImGui::Dummy(ImVec2(width, height + 4.0f));
}

bool Campaign::isForageSpeciesKnown(const std::string& speciesId) const
{
    if (speciesId.empty())
        return false;

    auto it = m_forageKnowledge.entries.find(speciesId);
    if (it == m_forageKnowledge.entries.end())
        return false;

    return it->second.knowledgeLevel >= KnowledgeLevel::Verified;
}

const ForageSpeciesDef* Campaign::resolveForageSpeciesForSpawn(const ForageSpawnDef& spawn) const
{
    if (const ForageSpeciesDef* picked = m_forageSystem.pickSpeciesForSpawn(spawn, m_forageDb, 1337u))
        return picked;

    // Authoring safety net: when a direct-species spawn was saved with a concrete
    // sprite but without species_pool, recover the species from that sprite.
    if (!spawn.genericMapSpriteOverride.empty())
    {
        for (const auto& kv : m_forageDb.species())
        {
            const ForageSpeciesDef& sp = kv.second;
            if (sp.archetypeId != spawn.archetypeId)
                continue;

            if (sp.detailSprite == spawn.genericMapSpriteOverride ||
                sp.inventorySprite == spawn.genericMapSpriteOverride ||
                sp.herbariumSprite == spawn.genericMapSpriteOverride)
            {
                return &sp;
            }
        }
    }

    // Demo-data fallback: if an archetype has exactly one species, it is still
    // safe to examine that species. Once one archetype contains multiple herbs
    // or mushrooms, the editor should save species_pool explicitly.
    const ForageSpeciesDef* only = nullptr;
    for (const auto& kv : m_forageDb.species())
    {
        const ForageSpeciesDef& sp = kv.second;
        if (sp.archetypeId != spawn.archetypeId)
            continue;

        if (only)
            return nullptr;

        only = &sp;
    }

    return only;
}

std::string Campaign::resolveForageSpriteId(const ForageSpawnDef& spawn) const
{
    const ForageArchetypeDef* archetype = m_forageDb.findArchetype(spawn.archetypeId);
    if (archetype && !archetype->genericMapSprite.empty())
        return archetype->genericMapSprite;

    if (!spawn.genericMapSpriteOverride.empty())
        return spawn.genericMapSpriteOverride;

    return {};
}

std::string Campaign::resolveForageDisplaySpriteId(const ForageSpawnDef& spawn) const
{
    const ForageSpeciesDef* species =
        resolveForageSpeciesForSpawn(spawn);

    if (species && isForageSpeciesKnown(species->id))
    {
        if (!species->detailSprite.empty())
            return species->detailSprite;
        if (!species->inventorySprite.empty())
            return species->inventorySprite;
        if (!species->herbariumSprite.empty())
            return species->herbariumSprite;
    }

    // Unknown spawns stay visually generic. This is important for gameplay: the
    // player should not learn the exact species from the map before identification.
    return resolveForageSpriteId(spawn);
}

float Campaign::resolveForageMapScale(const ForageSpawnDef& spawn) const
{
    // Prefer the value parsed directly into runtime data. The editor saves these fields
    // into forage_archetypes.json / *.forage.json.
    if (spawn.mapScaleOverride > 0.0f)
        return std::clamp(spawn.mapScaleOverride, 0.03f, 4.0f);

    const ForageArchetypeDef* archetype = m_forageDb.findArchetype(spawn.archetypeId);
    if (archetype && archetype->mapScale > 0.0f)
        return std::clamp(archetype->mapScale, 0.03f, 4.0f);

    // Backward compatibility with older cache-based runtime patches.
    auto itSpawn = m_forageSpawnScaleOverride.find(spawn.id);
    if (itSpawn != m_forageSpawnScaleOverride.end() && itSpawn->second > 0.0f)
        return std::clamp(itSpawn->second, 0.03f, 4.0f);

    auto itArch = m_forageArchetypeMapScale.find(spawn.archetypeId);
    if (itArch != m_forageArchetypeMapScale.end() && itArch->second > 0.0f)
        return std::clamp(itArch->second, 0.03f, 4.0f);

    return 0.20f;
}

int Campaign::currentForageDaySerial() const
{
    const auto& now = m_gameTime.now();
    return now.year * 372 + now.month * 31 + now.day;
}

std::string Campaign::forageSpawnRuntimeKey(const ForageSpawnDef& spawn) const
{
    return spawn.id + "@" + std::to_string(spawn.tileX) + "," + std::to_string(spawn.tileY);
}

bool Campaign::isForageSpawnAvailable(const ForageSpawnDef& spawn) const
{
    const std::string key = forageSpawnRuntimeKey(spawn);
    if (m_depletedForageSpawnIds.contains(key))
        return false;

    auto it = m_forageRespawnAvailableDay.find(key);
    if (it == m_forageRespawnAvailableDay.end())
        return true;

    return currentForageDaySerial() >= it->second;
}

void Campaign::markForageSpawnGathered(const ForageSpawnDef& spawn)
{
    const std::string key = forageSpawnRuntimeKey(spawn);

    if (spawn.gatherOnce || spawn.respawnDays <= 0)
    {
        m_depletedForageSpawnIds.insert(key);
        return;
    }

    m_forageRespawnAvailableDay[key] = currentForageDaySerial() + std::max(1, spawn.respawnDays);
}

bool Campaign::addForageToInventory(const ForageSpeciesDef* species, const ForageArchetypeDef& archetype, int count)
{
    count = std::clamp(count, 1, kForageStackMax);

    // IMPORTANT: even an unidentified plant/mushroom keeps its hidden species id in the item id.
    // The UI may call it "neznámá bylina", but eating it still uses the real species effects.
    const bool hasSpecies = species != nullptr;
    const bool knownSpecies = hasSpecies && isForageSpeciesKnown(species->id);
    const std::string logicalId = hasSpecies ? species->id : archetype.id;
    const std::string itemId = std::string("forage_") + logicalId;

    const std::string unknownName = archetype.displayPartial.empty()
        ? (archetype.displayUnknown.empty() ? archetype.id : archetype.displayUnknown)
        : archetype.displayPartial;
    const std::string displayName =
        knownSpecies && !species->trueName.empty()
        ? species->trueName
        : unknownName;

    auto applyForageDefData = [&](ItemDef& target)
    {
        target.id = itemId;
        target.name = displayName.empty() ? logicalId : displayName;
        target.description = hasSpecies
            ? (knownSpecies
                ? "Sebraná přírodnina. Vlastnosti určuje znalost hráče a pozdější zpracování."
                : "Neurčený vzorek z přírody. Skutečný druh je skrytý, ale účinky při pozření zůstávají reálné.")
            : "Neurčený vzorek z přírody.";
        target.flavorText = "Nasbíráno v krajině kolem Blatců.";

        std::string invSpriteId;
        if (hasSpecies)
        {
            if (!species->inventorySprite.empty())
                invSpriteId = species->inventorySprite;
            else if (!species->detailSprite.empty())
                invSpriteId = species->detailSprite;
            else if (!species->herbariumSprite.empty())
                invSpriteId = species->herbariumSprite;
        }

        if (invSpriteId.empty() && !archetype.detailPlaceholderSprite.empty())
            invSpriteId = archetype.detailPlaceholderSprite;
        if (invSpriteId.empty() && !archetype.genericMapSprite.empty())
            invSpriteId = archetype.genericMapSprite;

        // forage:<id> means the inventory slot renders an atlas sub-rect from assets/Foraging.
        // Normal items keep the existing assets/data/items/<spriteId>.png path.
        target.spriteId = invSpriteId.empty() ? "item_default" : ("forage:" + invSpriteId);

        if (knownSpecies && species->edibility == "edible")
            target.category = ItemCategory::Food;
        else if (knownSpecies && species->medicinalValue != "none")
            target.category = ItemCategory::Medicine;
        else
            target.category = ItemCategory::Material;

        const float inheritedWeight = archetype.weight > 0.0f ? archetype.weight : 0.03f;
        const float inheritedVolume = archetype.volume > 0.0f ? archetype.volume : 0.05f;

        target.weight = hasSpecies && species->weight > 0.0f ? species->weight : inheritedWeight;
        target.volume = hasSpecies && species->volume > 0.0f ? species->volume : inheritedVolume;
        target.stackable = true;
        target.maxStack = std::clamp(hasSpecies ? species->maxStack : archetype.maxStack, 1, kForageStackMax);
        target.quickUsable = true;
    };

    ItemDef def;
    auto itExisting = m_itemDefs.find(itemId);
    if (itExisting != m_itemDefs.end())
    {
        def = itExisting->second;
        // When the player learns a species later, refresh item display/category for future UI draws.
        // The hidden item id stays the same, so existing stacks do not break.
        applyForageDefData(def);
        m_itemDefs[itemId] = def;
    }
    else
    {
        applyForageDefData(def);
        m_itemDefs[itemId] = def;
    }

    // Default target for gathered nature items is the backpack. If it is full,
    // fall back to the general inventory helper so the player does not lose the item.
    bool added = false;
    if (m_player.inventory.backpack.canAccept(def, count, m_itemDefs))
        added = m_player.inventory.backpack.addItem(def, count, m_itemDefs);

    if (!added)
        added = m_player.inventory.addItem(def, count, m_itemDefs);

    if (added)
    {
        m_player.stats.carryWeight = m_player.inventory.computeTotalWeight(m_itemDefs);
        m_player.stats.carryVolume = m_player.inventory.computeTotalVolume(m_itemDefs);
        return true;
    }

    consoleLog("Nemáš místo v batohu pro: " + def.name);
    return false;
}

const ForageSpawnDef* Campaign::findNearbyForageSpawn(int radiusPx) const
{
    const ForageSpawnDef* best = nullptr;
    float bestDistSq = (float)(radiusPx * radiusPx);

    for (const auto& spawn : m_forageSystem.spawns())
    {
        if (!isForageSpawnAvailable(spawn))
            continue;

        const std::string spriteId = resolveForageDisplaySpriteId(spawn);
        auto itSprite = m_forageSprites.find(spriteId);

        float sx = spawn.tileX * (float)m_tileSize + m_tileSize * 0.5f;
        float sy = spawn.tileY * (float)m_tileSize + m_tileSize * 0.5f;

        // If we know the actual rendered sprite rectangle, use its visual center for
        // interaction. This keeps the prompt aligned with scaled sprites, not with
        // a hardcoded tile center.
        if (itSprite != m_forageSprites.end())
        {
            const auto& sp = itSprite->second;
            const float scale = resolveForageMapScale(spawn);
            const float pivotWorldX = spawn.tileX * (float)m_tileSize + m_tileSize * 0.5f;
            const float pivotWorldY = spawn.tileY * (float)m_tileSize + m_tileSize;

            const float left = pivotWorldX - sp.pivotX * scale;
            const float top = pivotWorldY - sp.pivotY * scale;
            const float w = sp.src.w * scale;
            const float h = sp.src.h * scale;

            sx = left + w * 0.5f;
            sy = top + h * 0.5f;
        }

        const float dx = sx - m_player.x;
        const float dy = sy - m_player.y;
        const float d2 = dx * dx + dy * dy;

        if (d2 <= bestDistSq)
        {
            bestDistSq = d2;
            best = &spawn;
        }
    }

    return best;
}

void Campaign::renderForageSpawns(const TerrainRenderer::View& view)
{
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    const ForageSpawnDef* nearby = findNearbyForageSpawn(42);

    for (const auto& spawn : m_forageSystem.spawns())
    {
        if (!isForageSpawnAvailable(spawn))
            continue;

        const std::string spriteId = resolveForageDisplaySpriteId(spawn);
        auto itSprite = m_forageSprites.find(spriteId);
        const bool selected = (&spawn == nearby);

        if (itSprite == m_forageSprites.end())
        {
            SDL_Rect tileRect{ spawn.tileX * m_tileSize - view.camX, spawn.tileY * m_tileSize - view.camY, m_tileSize, m_tileSize };
            SDL_SetRenderDrawColor(m_renderer, 40, 180, 80, selected ? 170 : 110);
            SDL_RenderFillRect(m_renderer, &tileRect);
            SDL_SetRenderDrawColor(m_renderer, 120, 255, 150, 230);
            SDL_RenderDrawRect(m_renderer, &tileRect);
            continue;
        }

        const auto& sp = itSprite->second;
        SDL_Texture* tex = textureForForageSprite(sp);
        if (!tex)
            continue;

        const float scale = resolveForageMapScale(spawn);
        const int pivotWorldX = spawn.tileX * m_tileSize + m_tileSize / 2;
        const int pivotWorldY = spawn.tileY * m_tileSize + m_tileSize;

        SDL_Rect dst{};
        dst.w = std::max(1, (int)std::lround(sp.src.w * scale));
        dst.h = std::max(1, (int)std::lround(sp.src.h * scale));
        dst.x = (int)std::lround(pivotWorldX - sp.pivotX * scale) - view.camX;
        dst.y = (int)std::lround(pivotWorldY - sp.pivotY * scale) - view.camY;

        SDL_RenderCopy(m_renderer, tex, &sp.src, &dst);

        if (selected)
        {
            SDL_SetRenderDrawColor(m_renderer, 120, 255, 150, 210);
            SDL_RenderDrawRect(m_renderer, &dst);
        }
    }
}

void Campaign::renderForagePrompt()
{
    if (m_forageWindowOpen || m_npcDialogOpen || m_nearNpcIndex >= 0)
        return;

    const ForageSpawnDef* spawn = findNearbyForageSpawn(42);
    if (!spawn)
        return;

    const ForageArchetypeDef* archetype = m_forageDb.findArchetype(spawn->archetypeId);
    const ForageSpeciesDef* species = resolveForageSpeciesForSpawn(*spawn);
    const bool known = species && isForageSpeciesKnown(species->id);

    std::string label;
    if (known)
        label = species->trueName.empty() ? species->id : species->trueName;
    else
        label = archetype && !archetype->displayUnknown.empty() ? archetype->displayUnknown : std::string("přírodnina");

    int screenW = 0, screenH = 0;
    SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const std::string text = known ? ("E - sebrat: " + label) : ("E - prozkoumat: " + label);
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const ImVec2 pos((screenW - textSize.x) * 0.5f, screenH - 96.0f);
    const ImVec2 pad(14.0f, 8.0f);

    dl->AddRectFilled(ImVec2(pos.x - pad.x, pos.y - pad.y), ImVec2(pos.x + textSize.x + pad.x, pos.y + textSize.y + pad.y), IM_COL32(8, 12, 8, 190), 8.0f);
    dl->AddRect(ImVec2(pos.x - pad.x, pos.y - pad.y), ImVec2(pos.x + textSize.x + pad.x, pos.y + textSize.y + pad.y), IM_COL32(120, 230, 140, 220), 8.0f);
    dl->AddText(pos, IM_COL32(210, 255, 210, 255), text.c_str());
}

void Campaign::renderForageWindow()
{
    if (!m_forageWindowOpen)
        return;

    const ForageSpawnDef* activeSpawn = nullptr;
    for (const auto& spawn : m_forageSystem.spawns())
    {
        if (spawn.id == m_activeForageSpawnId &&
            spawn.tileX == m_activeForageTileX &&
            spawn.tileY == m_activeForageTileY)
        {
            activeSpawn = &spawn;
            break;
        }
    }

    // Backward fallback for saves/windows opened before this patch.
    if (!activeSpawn)
    {
        for (const auto& spawn : m_forageSystem.spawns())
        {
            if (spawn.id == m_activeForageSpawnId)
            {
                activeSpawn = &spawn;
                break;
            }
        }
    }

    if (!activeSpawn || !isForageSpawnAvailable(*activeSpawn))
    {
        m_forageWindowOpen = false;
        return;
    }

    const ForageArchetypeDef* archetype = m_forageDb.findArchetype(activeSpawn->archetypeId);
    const ForageSpeciesDef* species = m_forageDb.findSpecies(m_activeForageSpeciesId);
    if (!species)
        species = resolveForageSpeciesForSpawn(*activeSpawn);

    if (!archetype && species)
        archetype = m_forageDb.findArchetype(species->archetypeId);

    if (!archetype)
    {
        m_forageWindowOpen = false;
        return;
    }

    if (m_forageWindowFocus)
    {
        ImGui::SetNextWindowFocus();
        m_forageWindowFocus = false;
    }

    ImGui::SetNextWindowSize(ImVec2(600, 560), ImGuiCond_FirstUseEver);

    const bool known = species && isForageSpeciesKnown(species->id);
    const std::string title = known
        ? ("Přírodnina - " + (species->trueName.empty() ? species->id : species->trueName))
        : (archetype->displayUnknown.empty() ? "Zkoumání nálezu" : ("Zkoumání - " + archetype->displayUnknown));

    if (!ImGui::Begin(title.c_str(), &m_forageWindowOpen))
    {
        ImGui::End();
        return;
    }

    const std::string exactSpriteId = species
        ? (!species->detailSprite.empty() ? species->detailSprite : (!species->inventorySprite.empty() ? species->inventorySprite : species->herbariumSprite))
        : resolveForageSpriteId(*activeSpawn);

    if (!exactSpriteId.empty())
    {
        ImGui::Spacing();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(species ? (known ? "Známý druh" : "Detail nálezu") : "Nález");
        auto it = m_forageSprites.find(exactSpriteId);
        if (it != m_forageSprites.end())
            DrawForageSpriteImage(textureForForageSprite(it->second), it->second.src, 128.0f, 128.0f);
        ImGui::EndGroup();
        ImGui::Separator();
    }

    if (!species)
    {
        ImGui::TextWrapped("Tento spawn zatím nemá přiřazený konkrétní druh ve species_pool. Lze ho sebrat jako obecný vzorek.");
        ImGui::Text("Spawn: %s", activeSpawn->id.c_str());
        ImGui::Text("Archetype: %s", activeSpawn->archetypeId.c_str());
        ImGui::Text("Scale: %.2f", resolveForageMapScale(*activeSpawn));
        ImGui::Spacing();

        if (ImGui::Button("Sebrat vzorek"))
        {
            if (addForageToInventory(nullptr, *archetype, std::max(1, activeSpawn->quantityMin)))
            {
                consoleLog("Sebral jsi vzorek: " + (archetype->displayUnknown.empty() ? activeSpawn->archetypeId : archetype->displayUnknown));
                markForageSpawnGathered(*activeSpawn);
                m_forageWindowOpen = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Zavřít"))
            m_forageWindowOpen = false;

        ImGui::End();
        return;
    }

    if (known)
    {
        ImGui::TextWrapped("Tuhle přírodninu už poznáš. Není potřeba ji znovu určovat.");
        ImGui::Spacing();
        ImGui::Text("Druh: %s", species->trueName.empty() ? species->id.c_str() : species->trueName.c_str());
        if (!species->folkNames.empty())
        {
            std::string folk;
            for (size_t i = 0; i < species->folkNames.size(); ++i)
            {
                if (i > 0) folk += ", ";
                folk += species->folkNames[i];
            }
            ImGui::TextWrapped("Lidové názvy: %s", folk.c_str());
        }
        if (!species->description.empty())
            ImGui::TextWrapped("Popis: %s", species->description.c_str());
        ImGui::Text("Jedlost: %s", species->edibility.c_str());
        ImGui::Text("Léčivost: %s", species->medicinalValue.c_str());
        ImGui::Text("Toxicita: %d", species->toxicityLevel);
        ImGui::Separator();

        if (ImGui::Button("Sebrat"))
        {
            const std::string label = species->trueName.empty() ? species->id : species->trueName;
            if (addForageToInventory(species, *archetype, std::max(1, activeSpawn->quantityMin)))
            {
                consoleLog("Sebral jsi: " + label);
                markForageSpawnGathered(*activeSpawn);
                m_forageWindowOpen = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Zavřít"))
            m_forageWindowOpen = false;

        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Ohledáváš nalezenou přírodninu. Zapiš znaky, svůj volný popis a zkus odhadnout název.");
    ImGui::Spacing();

    std::vector<std::string> visibleSlots;
    visibleSlots.reserve(species->traits.size());

    if (!archetype->examinationSlots.empty())
    {
        for (const std::string& slotId : archetype->examinationSlots)
        {
            auto itTrait = species->traits.find(slotId);
            if (itTrait != species->traits.end() && !itTrait->second.empty())
                visibleSlots.push_back(slotId);
        }
    }

    for (const auto& kv : species->traits)
    {
        if (kv.second.empty())
            continue;
        if (std::find(visibleSlots.begin(), visibleSlots.end(), kv.first) == visibleSlots.end())
            visibleSlots.push_back(kv.first);
    }

    if (visibleSlots.empty())
    {
        ImGui::TextDisabled("Pro tento druh zatím nejsou v editoru nastavené rozpoznávací znaky.");
        ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("1) Pozorované znaky", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const std::string& slotId : visibleSlots)
        {
            const ForageTraitGroup* group = m_forageDb.findTraitGroup(slotId);
            const std::string label = ForageTraitLabelForUi(group, slotId);

            auto& buf = m_activeForageTraitText[slotId];
            ImGui::PushID(slotId.c_str());
            ImGui::TextUnformatted(label.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##trait_text", buf.data(), buf.size());
            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    if (ImGui::CollapsingHeader("2) Volný popis", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("Sem napiš vlastní dojem: barva, tvar, místo nálezu, vůně, stav rostliny/houby...");
        ImGui::InputTextMultiline("##forage_free_description", m_activeForageDescriptionText, IM_ARRAYSIZE(m_activeForageDescriptionText), ImVec2(-1.0f, 78.0f));
    }

    if (ImGui::CollapsingHeader("3) Odhad názvu", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##forage_name_guess", m_activeForageNameGuess, IM_ARRAYSIZE(m_activeForageNameGuess));
    }

    if (m_hasLastForageExaminationResult)
    {
        ImGui::Separator();
        DrawIdentificationScoreBar(m_lastForageExaminationResult, ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", m_lastForageExaminationResult.feedbackText.c_str());
        for (const auto& line : m_lastForageExaminationResult.feedbackLines)
            ImGui::BulletText("%s", line.c_str());

        if (ImGui::TreeNode("Správné znaky / popis"))
        {
            for (const std::string& slotId : visibleSlots)
            {
                auto itTrait = species->traits.find(slotId);
                if (itTrait == species->traits.end() || itTrait->second.empty())
                    continue;

                const ForageTraitGroup* group = m_forageDb.findTraitGroup(slotId);
                const std::string label = ForageTraitLabelForUi(group, slotId);
                const std::string values = JoinForageValues(itTrait->second);
                if (!values.empty())
                    ImGui::BulletText("%s: %s", label.c_str(), values.c_str());
            }

            if (!species->description.empty())
                ImGui::TextWrapped("Správný popis: %s", species->description.c_str());

            ImGui::TreePop();
        }

        if (m_lastForageExaminationResult.verifiedSpecies)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.55f, 1.0f), "Ověřeno: %s", species->trueName.empty() ? species->id.c_str() : species->trueName.c_str());
            ImGui::Text("Jedlost: %s", species->edibility.c_str());
            ImGui::Text("Léčivost: %s", species->medicinalValue.c_str());
            ImGui::Text("Toxicita: %d", species->toxicityLevel);
        }
    }

    ImGui::Separator();

    if (ImGui::Button("Vyhodnotit určení"))
    {
        ForageIdentificationInput input;
        for (const auto& slotId : visibleSlots)
        {
            auto it = m_activeForageTraitText.find(slotId);
            if (it != m_activeForageTraitText.end())
                input.traitTexts[slotId] = it->second.data();
        }
        input.descriptionText = m_activeForageDescriptionText;
        input.nameGuess = m_activeForageNameGuess;

        auto& entry = m_forageKnowledge.entries[species->id];
        m_lastForageExaminationResult = m_forageSystem.identify(
            *species,
            input,
            entry,
            m_player.stats.survival.foraging,
            m_player.stats.mental.observation,
            m_player.stats.mental.focus,
            m_player.stats.mental.memory,
            m_player.stats.comfort.natureComfort);
        m_hasLastForageExaminationResult = true;
        consoleLog(m_lastForageExaminationResult.feedbackText);
    }

    ImGui::SameLine();

    if (ImGui::Button("Sebrat"))
    {
        const bool nowKnown = isForageSpeciesKnown(species->id);
        const std::string label = nowKnown && !species->trueName.empty()
            ? species->trueName
            : (archetype->displayPartial.empty() ? archetype->displayUnknown : archetype->displayPartial);

        // Pass the real species even when it is not identified yet. The inventory label can stay
        // generic, but eating an unidentified death cap must still poison the player.
        if (addForageToInventory(species, *archetype, std::max(1, activeSpawn->quantityMin)))
        {
            consoleLog("Sebral jsi: " + label);
            markForageSpawnGathered(*activeSpawn);
            m_forageWindowOpen = false;
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Zavřít"))
        m_forageWindowOpen = false;

    ImGui::End();
}

void Campaign::render()
{
    int screenW = 0, screenH = 0;
    SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);

    ensureCameraOnPlayer(screenW, screenH);

    TerrainRenderer::View view;
    view.camX = m_camX;
    view.camY = m_camY;
    view.tileSize = m_tileSize;
    view.screenW = screenW;
    view.screenH = screenH;

    m_terrainRenderer.renderTerrain(m_renderer, m_tileset, m_map, view);

    renderForageSpawns(view);

    const SDL_Rect playerWorld = m_player.worldAABB();
    std::vector<FadeOverlay> fadeOverlays;

    {
        ImDrawList* fg = ImGui::GetForegroundDrawList();

        SDL_Rect screenWorldRect{
            view.camX,
            view.camY,
            view.screenW,
            view.screenH
        };

        auto intersectsScreen = [&](const SDL_Rect& r) -> bool
        {
            return (r.x < screenWorldRect.x + screenWorldRect.w) &&
                   (r.x + r.w > screenWorldRect.x) &&
                   (r.y < screenWorldRect.y + screenWorldRect.h) &&
                   (r.y + r.h > screenWorldRect.y);
        };

        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

        for (int mapY = 0; mapY < m_map.height(); ++mapY)
        {
            for (int mapX = 0; mapX < m_map.width(); ++mapX)
            {
                const auto* def = m_map.getObjDefAt(m_objCatalog, mapX, mapY);
                if (!def)
                    continue;
                if (def->HasTag(("spawn"))|| def->id == "spawn_player")
                    continue;

                int wx = 0, wy = 0;
                m_map.getObjPivotWorld(mapX, mapY, wx, wy);

                const float objScale = m_map.getObjScale(mapX, mapY) * def->scale;
                SDL_Rect objWorldRect = GetObjectWorldRectScaled(*def, wx, wy, objScale);

                if (!intersectsScreen(objWorldRect))
                    continue;

                const int sx = wx - view.camX;
                const int sy = wy - view.camY;

                if (!def->has_sprite)
                {
                    SDL_Rect tileRect{
                        mapX * m_tileSize - view.camX,
                        mapY * m_tileSize - view.camY,
                        m_tileSize,
                        m_tileSize
                    };

                    SDL_SetRenderDrawColor(m_renderer, 0, 180, 0, 80);
                    SDL_RenderFillRect(m_renderer, &tileRect);

                    SDL_SetRenderDrawColor(m_renderer, 0, 255, 0, 255);
                    SDL_RenderDrawRect(m_renderer, &tileRect);

                    fg->AddText(
                        ImVec2((float)tileRect.x + 3.0f, (float)tileRect.y + 3.0f),
                        IM_COL32(0, 255, 0, 255),
                        def->name.c_str()
                    );
                    continue;
                }

                SDL_Texture* tex = textureForObject(*def);
                if (tex)
                {
                    // 1) cel� objekt norm�ln?
                    gameobj::RenderObjectAtPivot(m_renderer, tex, *def, sx, sy, objScale);

                    // 2) aktivn� fade z�ny si ulo?�me pro overlay nad hr�?em
                    std::vector<SDL_Rect> fadeRects = gameobj::GetWorldFadeRects(*def, wx, wy, objScale);

                    // fallback: pokud objekt nema definovane fade_rects,
                    if (fadeRects.empty() && def->has_sprite)
                    {
                        SDL_Rect full = GetObjectWorldRectScaled(*def, wx, wy, objScale);

                        // 1) Zkusime fallback podle horni hrany collideru
                        const auto colliderRects = gameobj::GetWorldColliderRects(*def, wx, wy, objScale);

                        if (!colliderRects.empty())
                        {
                            int colliderTop = colliderRects[0].y;

                            for (const SDL_Rect& r : colliderRects)
                                colliderTop = std::min(colliderTop, r.y);

                            // Fade oblast = cast objektu NAD horn� hranou collideru
                            SDL_Rect autoFade{};
                            autoFade.x = full.x;
                            autoFade.y = full.y;
                            autoFade.w = full.w;
                            autoFade.h = std::max(0, colliderTop - full.y);

                            if (autoFade.w > 0 && autoFade.h > 0)
                                fadeRects.push_back(autoFade);
                        }
                        else
                        {
                            // 2) Kdyz neni collider, nech fallback na horni cast sprite
                            SDL_Rect autoFade{};
                            autoFade.x = full.x;
                            autoFade.y = full.y;
                            autoFade.w = full.w;
                            autoFade.h = (int)std::lround(full.h * 0.55f);

                            if (autoFade.w > 0 && autoFade.h > 0)
                                fadeRects.push_back(autoFade);
                        }
                    }

                    for (const SDL_Rect& fr : fadeRects)
                    {
                        if (AABB_Intersect(playerWorld, fr))
                        {
                            FadeOverlay ov;
                            ov.tex = tex;
                            ov.def = def;
                            ov.wx = wx;
                            ov.wy = wy;
                            ov.fadeRectWorld = fr;
                            ov.alpha = 140;
                            ov.scale = objScale;
                            fadeOverlays.push_back(ov);
                        }
                    }
                }

                if (m_drawObjColliders)
                {
                    const auto rects = gameobj::GetWorldColliderRects(*def, wx, wy, objScale);
                    SDL_SetRenderDrawColor(m_renderer, 0, 255, 0, 255);

                    for (SDL_Rect r : rects)
                    {
                        r.x -= view.camX;
                        r.y -= view.camY;
                        SDL_RenderDrawRect(m_renderer, &r);
                    }

                    const auto walkRects = gameobj::GetWorldWalkableRects(*def, wx, wy, objScale);
                    SDL_SetRenderDrawColor(m_renderer, 0, 220, 255, 255);

                    for (SDL_Rect r : walkRects)
                    {
                        r.x -= view.camX;
                        r.y -= view.camY;
                        SDL_RenderDrawRect(m_renderer, &r);
                    }
                }
            }
        }
    }

    m_npcManager.render(m_renderer, m_camX, m_camY, m_characterManager);
    m_player.render(m_renderer, m_camX, m_camY, m_characterManager);

    // overlay pr?chod? / bran nad hr�?em
    for (const auto& ov : fadeOverlays)
    {
        RenderObjectWorldSubRectAlpha(
    m_renderer,
    ov.tex,
    *ov.def,
    ov.wx,
    ov.wy,
    ov.scale,
    ov.fadeRectWorld,
    view.camX,
    view.camY,
    ov.alpha
);
    }

    if (m_nearNpcIndex >= 0 && !m_npcDialogOpen)
    {
        const auto& npcs = m_npcManager.npcs();
        const auto& npc = npcs[m_nearNpcIndex];

        const int sx = (int)std::lround(npc.x) - m_camX;
        const int sy = (int)std::lround(npc.y) - m_camY - m_tileSize;

        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 255, 230, 80, 220);

        SDL_Rect r{ sx - 5, sy - 10, 10, 10 };
        SDL_RenderFillRect(m_renderer, &r);
    }

    if (m_nearNpcIndex >= 0)
    {
        auto& npc = m_npcManager.npcs()[m_nearNpcIndex];

        const float dx = m_player.x - npc.x;

        if (fabs(dx) > 4.0f)
        {
            npc.facing = dx > 0 ? 1.0f : 3.0f;
        }
    }

    const float darkness = computeSkyDarkness();

    ensureLightMask(screenW, screenH);
    renderSkyOverlay();
    renderLightMask(screenW, screenH, darkness);
    ensureFowMask(screenW, screenH);
    renderFogOfWar(screenW, screenH);
    if (m_showDebugHud)
        renderDebugHud();
    if (m_questJournalOpen)
        renderQuestJournal();
    renderHud();
    renderConsole();
    renderForagePrompt();
    renderNpcDialog();
    renderForageWindow();
    renderInventoryUI();
	renderQuickAccessBar();
    updateInspectCursor();
}