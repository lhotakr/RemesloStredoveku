#include <filesystem>
#include <fstream>

#include "Editor.h"
#include "imgui.h"
#include "JsonUtils.h"
#include "PathUtils.h"

namespace fs = std::filesystem;
using json = nlohmann::json;


static const char* kSchedulePhases[] = {
    "dawn",
    "morning",
    "forenoon",
    "noon",
    "afternoon",
    "lateday",
    "evening",
    "night"
};

static const int kSchedulePhaseCount =
    (int)(sizeof(kSchedulePhases) / sizeof(kSchedulePhases[0]));

static bool PhaseAlreadyUsed(
    const std::vector<EditorNpcPhaseEntry>& entries,
    const std::string& phase,
    int ignoreIndex = -1)
{
    for (int i = 0; i < (int)entries.size(); ++i)
    {
        if (i == ignoreIndex)
            continue;

        if (entries[i].phase == phase)
            return true;
    }

    return false;
}

static int FindSchedulePhaseIndex(const std::string& phase)
{
    for (int i = 0; i < kSchedulePhaseCount; ++i)
    {
        if (phase == kSchedulePhases[i])
            return i;
    }
    return 0;
}

static std::string FirstUnusedPhase(const std::vector<EditorNpcPhaseEntry>& entries)
{
    for (int i = 0; i < kSchedulePhaseCount; ++i)
    {
        if (!PhaseAlreadyUsed(entries, kSchedulePhases[i]))
            return kSchedulePhases[i];
    }

    return "";
}

static void LoadEditorPhaseEntries(const json& arr, std::vector<EditorNpcPhaseEntry>& out)
{
    out.clear();

    if (!arr.is_array())
        return;

    for (const auto& e : arr)
    {
        EditorNpcPhaseEntry pe;
        pe.phase = e.value("phase", "");
        pe.zoneId = e.value("zone", "");

        if (!pe.phase.empty() && !pe.zoneId.empty())
            out.push_back(std::move(pe));
    }
}

static json SaveEditorPhaseEntries(const std::vector<EditorNpcPhaseEntry>& entries)
{
    json arr = json::array();

    for (const auto& e : entries)
    {
        if (e.phase.empty() || e.zoneId.empty())
            continue;

        arr.push_back({
            {"phase", e.phase},
            {"zone", e.zoneId}
        });
    }

    return arr;
}

static bool DrawPhaseEntryList(
    const char* label,
    std::vector<EditorNpcPhaseEntry>& entries,
    const std::vector<EditorNpcZone>& zones)
{
    bool changed = false;

    if (ImGui::TreeNode(label))
    {
        for (int i = 0; i < (int)entries.size(); ++i)
        {
            ImGui::PushID(i);

            int phaseIndex = FindSchedulePhaseIndex(entries[i].phase);
            const char* currentPhaseLabel =
                (phaseIndex >= 0 && phaseIndex < kSchedulePhaseCount)
                ? kSchedulePhases[phaseIndex]
                : "<invalid>";

            if (ImGui::BeginCombo("Phase", currentPhaseLabel))
            {
                for (int p = 0; p < kSchedulePhaseCount; ++p)
                {
                    const bool alreadyUsed = PhaseAlreadyUsed(entries, kSchedulePhases[p], i);
                    const bool sel = (phaseIndex == p);

                    if (alreadyUsed)
                        ImGui::BeginDisabled();

                    if (ImGui::Selectable(kSchedulePhases[p], sel) && !alreadyUsed)
                    {
                        entries[i].phase = kSchedulePhases[p];
                        changed = true;
                    }

                    if (alreadyUsed)
                        ImGui::EndDisabled();

                    if (sel)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }

            const char* currentZoneLabel =
                entries[i].zoneId.empty() ? "<none>" : entries[i].zoneId.c_str();

            if (ImGui::BeginCombo("Zone", currentZoneLabel))
            {
                for (const auto& z : zones)
                {
                    const bool sel = (entries[i].zoneId == z.id);
                    if (ImGui::Selectable(z.id.c_str(), sel))
                    {
                        entries[i].zoneId = z.id;
                        changed = true;
                    }
                    if (sel)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            bool zoneExists = false;
            for (const auto& z : zones)
            {
                if (z.id == entries[i].zoneId)
                {
                    zoneExists = true;
                    break;
                }
            }

            if (!zoneExists && !entries[i].zoneId.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Unknown zone!");
            }

            if (ImGui::Button("Delete"))
            {
                entries.erase(entries.begin() + i);
                ImGui::PopID();
                changed = true;
                break;
            }

            ImGui::Separator();
            ImGui::PopID();
        }

        const std::string unusedPhase = FirstUnusedPhase(entries);
        const bool canAdd = !unusedPhase.empty();

        if (!canAdd)
            ImGui::BeginDisabled();

        if (ImGui::Button(("Add##" + std::string(label)).c_str()) && canAdd)
        {
            EditorNpcPhaseEntry e;
            e.phase = unusedPhase;
            if (!zones.empty())
                e.zoneId = zones.front().id;

            entries.push_back(std::move(e));
            changed = true;
        }

        if (!canAdd)
            ImGui::EndDisabled();

        if (!canAdd)
            ImGui::TextDisabled("All day phases are already used.");

        ImGui::TreePop();
    }

    return changed;
}

bool Editor::loadNpcSchedules()
{
    m_npcSchedules.clear();

    fs::path p = pathutils::NpcsDir() / "schedules.json";

    if (!fs::exists(p))
        return true;

    nlohmann::json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(p.string(), root, err))
    {
        m_lastIoStatus = "Schedules parse failed: " + err;
        return false;
    }

    if (!root.contains("schedules") || !root["schedules"].is_array())
        return true;

    for (const auto& js : root["schedules"])
    {
        EditorNpcSchedule s;
        s.npcId = js.value("npc_id", "");
        if (s.npcId.empty())
            continue;

        if (js.contains("seasonal_schedule") && js["seasonal_schedule"].is_object())
        {
            const auto& ss = js["seasonal_schedule"];
            LoadEditorPhaseEntries(ss.value("spring", json::array()), s.spring);
            LoadEditorPhaseEntries(ss.value("summer", json::array()), s.summer);
            LoadEditorPhaseEntries(ss.value("autumn", json::array()), s.autumn);
            LoadEditorPhaseEntries(ss.value("winter", json::array()), s.winter);
        }

        LoadEditorPhaseEntries(js.value("sunday_schedule", json::array()), s.sunday);

        if (js.contains("feast_schedule") && js["feast_schedule"].is_object())
        {
            const auto& fsj = js["feast_schedule"];
            LoadEditorPhaseEntries(fsj.value("default", json::array()), s.feastDefault);
        }

        m_npcSchedules.push_back(std::move(s));
    }

    m_selectedNpcScheduleIndex = m_npcSchedules.empty() ? -1 : 0;
    return true;
}

bool Editor::saveNpcSchedules()
{
    fs::path p = pathutils::NpcsDir() / "schedules.json";

    json root;
    root["schedules"] = json::array();

    for (const auto& s : m_npcSchedules)
    {
        if (s.npcId.empty())
            continue;

        json js;
        js["npc_id"] = s.npcId;

        js["seasonal_schedule"] = {
            {"spring", SaveEditorPhaseEntries(s.spring)},
            {"summer", SaveEditorPhaseEntries(s.summer)},
            {"autumn", SaveEditorPhaseEntries(s.autumn)},
            {"winter", SaveEditorPhaseEntries(s.winter)}
        };

        js["sunday_schedule"] = SaveEditorPhaseEntries(s.sunday);

        js["feast_schedule"] = {
            {"default", SaveEditorPhaseEntries(s.feastDefault)}
        };

        root["schedules"].push_back(std::move(js));
    }

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        m_lastIoStatus = "Schedules save failed: " + p.string();
        return false;
    }

    f << root.dump(2);
    return true;
}

EditorNpcSchedule* Editor::findScheduleForNpc(const std::string& npcId)
{
    for (auto& s : m_npcSchedules)
    {
        if (s.npcId == npcId)
            return &s;
    }
    return nullptr;
}

const EditorNpcSchedule* Editor::findScheduleForNpc(const std::string& npcId) const
{
    for (const auto& s : m_npcSchedules)
    {
        if (s.npcId == npcId)
            return &s;
    }
    return nullptr;
}
void Editor::renderNpcScheduleEditor()
{
    if (!ImGui::Begin("NPC Schedule Editor"))
    {
        ImGui::End();
        return;
    }

    renderNpcScheduleEditorContents();
    ImGui::End();
}

void Editor::renderNpcScheduleEditorContents()
{
    if (ImGui::Button("Save schedules"))
    {
        if (saveNpcSchedules())
            m_lastIoStatus = "Schedules saved.";
    }

    ImGui::Separator();

    std::string selectedNpcId;

    if (m_selectedNpcScheduleIndex >= 0 &&
        m_selectedNpcScheduleIndex < (int)m_npcSchedules.size())
    {
        selectedNpcId = m_npcSchedules[m_selectedNpcScheduleIndex].npcId;
    }
    else
    {
        selectedNpcId = m_npcId;
        if (selectedNpcId.empty() && !m_npcSpawns.empty())
            selectedNpcId = m_npcSpawns.front().id;
    }

    std::vector<std::string> npcIds;
    npcIds.reserve(m_npcSpawns.size());

    for (const auto& npc : m_npcSpawns)
        npcIds.push_back(npc.id);

    if (!npcIds.empty())
    {
        int currentNpcIndex = 0;
        for (int i = 0; i < (int)npcIds.size(); ++i)
        {
            if (npcIds[i] == selectedNpcId)
            {
                currentNpcIndex = i;
                break;
            }
        }

        std::vector<const char*> npcLabels;
        npcLabels.reserve(npcIds.size());
        for (const auto& id : npcIds)
            npcLabels.push_back(id.c_str());

        if (ImGui::Combo("NPC", &currentNpcIndex, npcLabels.data(), (int)npcLabels.size()))
        {
            selectedNpcId = npcIds[currentNpcIndex];
            strncpy_s(m_npcId, selectedNpcId.c_str(), _TRUNCATE);

            bool foundExisting = false;
            for (int i = 0; i < (int)m_npcSchedules.size(); ++i)
            {
                if (m_npcSchedules[i].npcId == selectedNpcId)
                {
                    m_selectedNpcScheduleIndex = i;
                    foundExisting = true;
                    break;
                }
            }

            if (!foundExisting)
            {
                EditorNpcSchedule s;
                s.npcId = selectedNpcId;
                m_npcSchedules.push_back(std::move(s));
                m_selectedNpcScheduleIndex = (int)m_npcSchedules.size() - 1;
            }
        }
    }

    if (!selectedNpcId.empty())
    {
        EditorNpcSchedule* sched = findScheduleForNpc(selectedNpcId);
        if (!sched)
        {
            EditorNpcSchedule s;
            s.npcId = selectedNpcId;
            m_npcSchedules.push_back(std::move(s));
            sched = &m_npcSchedules.back();
            m_selectedNpcScheduleIndex = (int)m_npcSchedules.size() - 1;
        }

        ImGui::Text("NPC: %s", selectedNpcId.c_str());

        bool changed = false;
        changed |= DrawPhaseEntryList("Spring", sched->spring, m_npcZones);
        changed |= DrawPhaseEntryList("Summer", sched->summer, m_npcZones);
        changed |= DrawPhaseEntryList("Autumn", sched->autumn, m_npcZones);
        changed |= DrawPhaseEntryList("Winter", sched->winter, m_npcZones);
        changed |= DrawPhaseEntryList("Sunday", sched->sunday, m_npcZones);
        changed |= DrawPhaseEntryList("Feast default", sched->feastDefault, m_npcZones);

        if (changed)
            m_mapDirty = true;
    }
    else
    {
        ImGui::TextUnformatted("Select NPC first.");
    }
}