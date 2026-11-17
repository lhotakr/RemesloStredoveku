#include "Campaign.h"
#include "ui/MedievalUI.h"
#include <algorithm>
#include <cmath>

void Campaign::renderQuestJournal()
{
    if (m_questJournalFocus)
    {
        ImGui::SetNextWindowFocus();
        m_questJournalFocus = false;
    }

    auto& ui = medieval::UI::Get();
    medieval::PlaceWindow({660 * ui.scale(), 560 * ui.scale()});
    medieval::Window window(ui, "Ukolnicek", medieval::Surface::Parchment);
    if (!window || !medieval::WindowHeader(ui, "Deník", m_questJournalOpen)) return;
    if (ui.button("##documents", "Listiny a dopisy")) ui.openDocuments();

    ui.heading("Aktivní úkoly");
    ImGui::Separator();

    bool anyActive = false;

    for (const auto& q : m_questDefs)
    {
        if (!isQuestActive(q))
            continue;

        anyActive = true;

        ImGui::PushID(q.id.c_str());

        ui.heading(q.title);
        ImGui::TextWrapped("%s", q.description.c_str());

        if (isQuestReadyToTurnIn(q))
            ImGui::TextColored(ImVec4(0.34f, 0.29f, 0.12f, 1.0f), "Stav: připraveno k odevzdání");
        else
            ImGui::TextColored(ImVec4(0.24f, 0.31f, 0.23f, 1.0f), "Stav: probíhá");

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Podrobnosti vývoje"))
        {
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
        }
        ImGui::PopID();
    }

    if (!anyActive)
        ImGui::TextDisabled("Žádné aktivní úkoly.");

    if (ImGui::CollapsingHeader("Dokončené úkoly"))
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
            ImGui::TextDisabled("Zatím žádné.");
    }
}

void Campaign::renderInventoryUI()
{
    if (!m_inventoryOpen)
        return;

    if (!m_dragItem.active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);

    auto& ui = medieval::UI::Get();
    medieval::PlaceWindow({900 * ui.scale(), 570 * ui.scale()});
    if (m_inventoryFocus)
    {
        ImGui::SetNextWindowFocus();
        m_inventoryFocus = false;
    }

    auto& inv = m_player.inventory;

    {
        medieval::Window window(ui, "Inventar", medieval::Surface::Parchment);
        if (!window || !medieval::WindowHeader(ui, "Inventář", m_inventoryOpen)) return;
        const float slot = 44 * ui.scale();

        ImGui::Columns(2, nullptr, true);

        ui.heading("Vybavení");
        ImGui::Separator();

        drawLabeledSlot("Hlava", "head", inv.equipped.head, slot);
        drawLabeledSlot("Spodní oděv", "torsoInner", inv.equipped.torsoInner, slot);
        drawLabeledSlot("Svrchní oděv", "torsoOuter", inv.equipped.torsoOuter, slot);
        drawLabeledSlot("Nohavice", "legs", inv.equipped.legs, slot);
        drawLabeledSlot("Obuv", "feet", inv.equipped.feet, slot);
        drawLabeledSlot("Plášť", "cloak", inv.equipped.cloak, slot);
        drawLabeledSlot("Opasek", "beltEquip", inv.equipped.belt, slot);
        drawLabeledSlot("Batoh", "back", inv.equipped.back, slot);

        ImGui::NextColumn();

        ui.heading("Batoh");
        ImGui::Separator();

        inv.backpack.compact();
        const int backpackVisibleSlots = std::max(8, (int)inv.backpack.items.size() + 1);
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int gridColumns = std::clamp(static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (slot + spacing)), 1, 6);

        for (int i = 0; i < backpackVisibleSlots; ++i)
        {
            std::string id = "bp_" + std::to_string(i);
            drawContainerSlot(id.c_str(), inv.backpack, i, m_itemDefs, slot);

            if ((i + 1) % gridColumns != 0)
                ImGui::SameLine();
        }

        ImGui::Spacing();
        ui.heading("Kapsy");
        ImGui::Separator();

        inv.pockets.compact();
        const int pocketVisibleSlots = std::max(4, (int)inv.pockets.items.size() + 1);

        for (int i = 0; i < pocketVisibleSlots; ++i)
        {
            std::string id = "pocket_" + std::to_string(i);
            drawContainerSlot(id.c_str(), inv.pockets, i, m_itemDefs, slot);

            if ((i + 1) % gridColumns != 0)
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

            ImGui::Text("Držíš: %s x%d", heldName, m_dragItem.stack.count);
            ImGui::TextDisabled("LMB = celá zásoba, RMB = polovina / položit jeden kus");
        }

    } // End the window before drawing the held item on the foreground layer.

    renderDraggedItemIcon();
}

