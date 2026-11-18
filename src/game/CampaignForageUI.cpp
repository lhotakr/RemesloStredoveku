#include "Campaign.h"
#include "ui/MedievalUI.h"
#include <algorithm>
#include <cmath>

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
    const int r = (int)std::lround(150.0f * (1.0f - t) + 76.0f * t);
    const int g = (int)std::lround(82.0f * (1.0f - t) + 111.0f * t);
    const int b = (int)std::lround(51.0f * (1.0f - t) + 65.0f * t);
    return IM_COL32(r, g, b, 255);
}

static void DrawIdentificationScoreBar(const ForageExaminationResult& result, float width)
{
    width = std::max(width, 1.0f);
    const float height = ImGui::GetTextLineHeight() + 10 * medieval::UI::Get().scale();
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

    auto& ui = medieval::UI::Get();
    medieval::PlaceWindow({800 * ui.scale(), 680 * ui.scale()});
    const bool known = species && isForageSpeciesKnown(species->id);
    const std::string title = known
        ? ("Přírodnina – " + (species->trueName.empty() ? species->id : species->trueName))
        : (archetype->displayUnknown.empty() ? "Zkoumání nálezu" : ("Zkoumání – " + archetype->displayUnknown));
    medieval::Window window(ui, "##forage-examination", medieval::Surface::Wood);
    if (!window || !medieval::WindowHeader(ui, title, m_forageWindowOpen)) return;

    std::vector<std::string> visibleSlots;
    visibleSlots.reserve(species ? species->traits.size() : 0);

    if (species && !archetype->examinationSlots.empty())
    {
        for (const std::string& slotId : archetype->examinationSlots)
        {
            auto itTrait = species->traits.find(slotId);
            if (itTrait != species->traits.end() && !itTrait->second.empty())
                visibleSlots.push_back(slotId);
        }
    }

    if (species) for (const auto& kv : species->traits)
    {
        if (kv.second.empty())
            continue;
        if (std::find(visibleSlots.begin(), visibleSlots.end(), kv.first) == visibleSlots.end())
            visibleSlots.push_back(kv.first);
    }


    {
        // Notes scroll independently; the gathering/evaluation controls stay visible.
        medieval::Child notes(ui, "##forage-notes", {0, -58 * ui.scale()}, medieval::Surface::Parchment);
        if (notes)
        {
            const std::string spriteId = species
                ? (!species->detailSprite.empty() ? species->detailSprite : (!species->inventorySprite.empty() ? species->inventorySprite : species->herbariumSprite))
                : resolveForageSpriteId(*activeSpawn);
            auto sprite = m_forageSprites.find(spriteId);
            if (sprite != m_forageSprites.end())
            {
                ui.heading(known ? "Známý druh" : "Detail nálezu");
                const float extent = std::min(128 * ui.scale(), ImGui::GetContentRegionAvail().x);
                DrawForageSpriteImage(textureForForageSprite(sprite->second), sprite->second.src, extent, extent);
                ImGui::Separator();
            }
            if (!species)
            {
                ImGui::TextWrapped("Tento nález zatím nemá přiřazený konkrétní druh. Lze ho sebrat jako obecný vzorek.");
                if (ImGui::CollapsingHeader("Podrobnosti vývoje"))
                {
                    ImGui::Text("Spawn: %s", activeSpawn->id.c_str());
                    ImGui::Text("Archetype: %s", activeSpawn->archetypeId.c_str());
                    ImGui::Text("Scale: %.2f", resolveForageMapScale(*activeSpawn));
                }
            }
            else if (known)
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
            }
            else
            {
                ImGui::TextWrapped("Ohledáváš nalezenou přírodninu. Zapiš znaky, svůj volný popis a zkus odhadnout název.");
                ImGui::Spacing();
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
                    ImGui::TextWrapped("Sem napiš vlastní dojem: barva, tvar, místo nálezu, vůně, stav rostliny/houby...");
                    ImGui::InputTextMultiline("##forage_free_description", m_activeForageDescriptionText, IM_ARRAYSIZE(m_activeForageDescriptionText), ImVec2(-1.0f, 90 * ui.scale()));
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
                        ImGui::TextColored(ImVec4(0.22f, 0.34f, 0.20f, 1.0f), "Ověřeno: %s", species->trueName.empty() ? species->id.c_str() : species->trueName.c_str());
                        ImGui::Text("Jedlost: %s", species->edibility.c_str());
                        ImGui::Text("Léčivost: %s", species->medicinalValue.c_str());
                        ImGui::Text("Toxicita: %d", species->toxicityLevel);
                    }
                }

            }
        }
    }

    const bool canIdentify = species && !known;
    if (ImGui::BeginTable("##forage-actions", canIdentify ? 2 : 1, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        if (canIdentify)
        {
            if (ui.button("##identify", "Vyhodnotit určení"))
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

            ImGui::TableNextColumn();
        }
        if (ui.button("##gather", species ? "Sebrat" : "Sebrat vzorek"))
        {
            if (species)
            {
                const bool nowKnown = isForageSpeciesKnown(species->id);
                const std::string label = nowKnown
                    ? (species->trueName.empty() ? species->id : species->trueName)
                    : (archetype->displayPartial.empty() ? archetype->displayUnknown : archetype->displayPartial);

                // Pass the real species even when it is not identified yet. The inventory label can stay
                // generic, but eating an unidentified death cap must still poison the player.
                if (addForageToInventory(species, *archetype, std::max(1, activeSpawn->quantityMin)))
                {
                    m_player.playAnimation("pickup", m_characterManager);
                    consoleLog("Sebral jsi: " + label);
                    markForageSpawnGathered(*activeSpawn);
                    m_forageWindowOpen = false;
                }
            }
            else if (addForageToInventory(nullptr, *archetype, std::max(1, activeSpawn->quantityMin)))
            {
                m_player.playAnimation("pickup", m_characterManager);
                consoleLog("Sebral jsi vzorek: " + (archetype->displayUnknown.empty() ? activeSpawn->archetypeId : archetype->displayUnknown));
                markForageSpawnGathered(*activeSpawn);
                m_forageWindowOpen = false;
            }
        }
        ImGui::EndTable();
    }
}
