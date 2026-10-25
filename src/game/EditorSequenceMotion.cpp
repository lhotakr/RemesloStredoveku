#include "Editor.h"
#include "game/sequence/SequenceTimeline.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace
{
using json = nlohmann::json;

bool ChooseId(const char* label, std::string& value, const std::vector<std::string>& ids, bool allowEmpty = false)
{
    bool changed = false;
    if (ImGui::BeginCombo(label, value.empty() ? "<vyberte>" : value.c_str()))
    {
        if (allowEmpty && ImGui::Selectable("<bez postavy / bodu>", value.empty()))
        {
            value.clear(); changed = true;
        }
        for (const auto& id : ids)
            if (ImGui::Selectable(id.c_str(), id == value)) { value = id; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

void EditId(const char* label, json& data, const char* key, const std::vector<std::string>& ids)
{
    auto value = data.value(key, std::string());
    if (ChooseId(label, value, ids, true)) data[key] = value;
    std::array<char, 256> text{};
    std::snprintf(text.data(), text.size(), "%s", value.c_str());
    if (ImGui::InputText((std::string("ID##") + key).c_str(), text.data(), text.size())) data[key] = text.data();
}

void EditBool(const char* label, json& data, const char* key, bool fallback)
{
    bool value = data.value(key, fallback);
    if (ImGui::Checkbox(label, &value)) data[key] = value;
}

void EditNumber(const char* label, json& data, const char* key, float fallback, float minimum, float maximum)
{
    float value = data.value(key, fallback);
    if (ImGui::InputFloat(label, &value)) data[key] = std::clamp(value, minimum, maximum);
}

void OptionalNumber(const char* label, json& data, const char* key, float fallback)
{
    ImGui::PushID(key);
    bool enabled = data.contains(key);
    if (ImGui::Checkbox("##override", &enabled))
    {
        if (enabled) data[key] = fallback;
        else data.erase(key);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!enabled);
    float value = data.value(key, fallback);
    if (ImGui::InputFloat(label, &value)) data[key] = value;
    ImGui::EndDisabled();
    ImGui::PopID();
}
}

void Editor::renderSequenceMotionEasing(nlohmann::json& data)
{
    const char* values[] = {"smooth", "linear", "ease_in", "ease_out"};
    const char* labels[] = {"Plynulý rozjezd i dojezd", "Rovnoměrně", "Pomalý rozjezd", "Pomalý dojezd"};
    const auto current = data.value("easing", std::string("smooth"));
    int selected = 0;
    for (int i = 0; i < 4; ++i) if (current == values[i]) selected = i;
    if (ImGui::Combo("Průběh pohybu", &selected, labels, 4)) data["easing"] = values[selected];
}

void Editor::insertSequenceMotion(sequence::ActionDef action)
{
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= static_cast<int>(m_sequences.size())) return;
    auto& seq = m_sequences[m_selectedSequenceIndex];
    if (m_selectedSequenceNodeIndex < 0 || m_selectedSequenceNodeIndex >= static_cast<int>(seq.nodes.size())) return;
    markSequenceDirty("insert camera or actor motion");
    stopSequencePreview();
    m_sequenceTimelineSelection = sequence::InsertTimedAction(seq.nodes[m_selectedSequenceNodeIndex],
        std::move(action), m_sequenceTimelineCursor);
    m_sequenceTimelineNode = m_selectedSequenceNodeIndex;
    m_sequenceTimelineSequence = m_selectedSequenceIndex;
    m_selectedSequenceActionIndex = m_sequenceTimelineSelection.front();
    m_sequenceSelectionKind = SequenceEditorSelectionKind::Action;
    m_lastIoStatus = "Pohyb vložen v čase kurzoru. Jeho nastavení najdete v inspektoru; Save uloží změny.";
}

void Editor::renderSequenceMotionTools()
{
    if (ImGui::Button("Kamera / postava")) ImGui::OpenPopup("Pohyby sekvence");
    if (!ImGui::BeginPopup("Pohyby sekvence")) return;
    const auto actors = selectedSequenceActorIds();
    const auto markers = selectedSequenceMarkerIds();
    if (std::find(actors.begin(), actors.end(), m_sequenceMotionActor) == actors.end())
        m_sequenceMotionActor = actors.empty() ? "" : actors.front();
    ImGui::Text("Vložit v čase %.2f s", std::max(0.0f, m_sequenceTimelineCursor));
    ChooseId("Postava", m_sequenceMotionActor, actors);
    ImGui::InputFloat("Délka pohybu (s)", &m_sequenceMotionDuration);
    m_sequenceMotionDuration = std::clamp(m_sequenceMotionDuration, .01f, 3600.f);
    ImGui::SeparatorText("Kamera");
    ImGui::BeginDisabled(isSequenceImageScene());
    ImGui::InputFloat("Odstup od postavy", &m_sequenceMotionDistance);
    m_sequenceMotionDistance = std::clamp(m_sequenceMotionDistance, .1f, 8192.f);
    ImGui::Checkbox("Po dojezdu sledovat postavu", &m_sequenceMotionFollow);
    ImGui::BeginDisabled(m_sequenceMotionActor.empty());
    if (ImGui::Button("Nájezd na postavu"))
    {
        sequence::ActionDef action;
        action.type = "camera_move";
        action.data = {{"type", action.type}, {"target_actor", m_sequenceMotionActor},
            {"distance", m_sequenceMotionDistance}, {"follow_after", m_sequenceMotionFollow}, {"easing", "smooth"}};
        sequence::SetActionDuration(action, m_sequenceMotionDuration);
        insertSequenceMotion(std::move(action));
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button("Sledovat od aktuálního záběru"))
    {
        sequence::ActionDef action;
        action.type = "camera_follow";
        action.data = {{"type", action.type}, {"actor", m_sequenceMotionActor}, {"preserve_offset", true}};
        insertSequenceMotion(std::move(action));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Uvolnit kameru"))
    {
        sequence::ActionDef action;
        action.type = "camera_follow";
        action.data = {{"type", action.type}, {"enabled", false}};
        insertSequenceMotion(std::move(action));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    if (isSequenceImageScene()) ImGui::TextDisabled("Nájezd kamery je určen pro 2.5D interiér.");
    ImGui::SeparatorText("Postava");
    ChooseId("Cílový bod", m_sequenceMotionMarker, markers, true);
    ImGui::BeginDisabled(m_sequenceMotionActor.empty() || m_sequenceMotionMarker.empty());
    if (ImGui::Button("Přesunout pouze postavu"))
    {
        sequence::ActionDef action;
        action.type = "move_actor";
        action.data = {{"type", action.type}, {"actor", m_sequenceMotionActor},
            {"target_marker", m_sequenceMotionMarker}, {"easing", "smooth"}};
        sequence::SetActionDuration(action, m_sequenceMotionDuration);
        insertSequenceMotion(std::move(action));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Překrývající se pohyby běží současně na samostatných stopách.\n"
        "Pohyb postavy ovlivní kameru jen při zapnutém sledování.");
    ImGui::EndPopup();
}

void Editor::renderSequenceCameraAction(sequence::ActionDef& action)
{
    auto& data = action.data;
    const auto actors = selectedSequenceActorIds();
    if (isSequenceImageScene()) ImGui::TextWrapped("Pohyb kamery se používá v 2.5D interiérech.");
    if (action.type == "camera_follow")
    {
        EditBool("Sledování zapnuto", data, "enabled", true);
        EditId("Sledovaná postava", data, "actor", actors);
        EditBool("Zachovat dosažený odstup", data, "preserve_offset", true);
        if (!data.value("preserve_offset", true)) EditNumber("Odstup", data, "distance", 2.2f, .1f, 8192.f);
    }
    else
    {
        const bool move = action.type == "camera_move";
        const char* markerKey = move ? "target_marker" : "marker";
        if (move)
        {
            int mode = data.value("target_actor", std::string()).empty() ? 0 : 1;
            const char* modes[] = {"Kamerový bod / souřadnice", "Nájezd na postavu"};
            if (ImGui::Combo("Cíl kamery", &mode, modes, 2))
            {
                if (mode == 0) data.erase("target_actor");
                else
                {
                    data["target_actor"] = actors.empty() ? "player" : actors.front();
                    data.erase("target_marker");
                }
            }
            if (mode == 1)
            {
                EditId("Cílová postava", data, "target_actor", actors);
                EditNumber("Konečný odstup", data, "distance", 2.2f, .1f, 8192.f);
            }
        }
        if (!move || data.value("target_actor", std::string()).empty())
        {
            EditId("Kamerový bod", data, markerKey, selectedSequenceMarkerIds());
            if (data.value(markerKey, std::string()).empty())
            {
                OptionalNumber("X", data, "x", m_sequencePreviewScene.cameraPose().position.x);
                OptionalNumber("Y", data, "y", m_sequencePreviewScene.cameraPose().position.y);
            }
        }
        ImGui::TextDisabled("Zaškrtnuté hodnoty přepíší bod; ostatní se převezmou z bodu nebo aktuální kamery.");
        const auto pose = m_sequencePreviewScene.cameraPose();
        OptionalNumber("Výška", data, "height", pose.height);
        OptionalNumber("Směr (stupně)", data, "angle_degrees", pose.angleDegrees);
        OptionalNumber("Náklon", data, "pitch", pose.pitch);
        if (move)
        {
            renderSequenceMotionEasing(data);
            EditId("Dívat se na postavu", data, "look_at", actors);
            ImGui::TextDisabled("Prázdná volba při nájezdu na postavu zaměří cíl nájezdu.");
            EditBool("Po dojezdu sledovat postavu", data, "follow_after", false);
            if (data.value("follow_after", false))
            {
                EditId("Postava pro sledování", data, "follow_actor", actors);
                ImGui::TextDisabled("Prázdná volba použije cíl nájezdu nebo zaměření. Odstup se zachová.");
            }
        }
        if (!move || !data.value("follow_after", false)) return;
    }
    EditBool("Natáčet za postavou", data, "look_at_actor", true);
    EditNumber("Vyhlazení sledování (0 = pevně)", data, "smoothing", 0.f, 0.f, 100.f);
}
