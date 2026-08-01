#include <fstream>
#include <nlohmann/json_fwd.hpp>

#include "Editor.h"
#include "imgui.h"
#include "JsonUtils.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

bool Editor::loadZonesForMap(const std::string& mapPath)
{
    m_npcZones.clear();

    fs::path p(mapPath);
    p.replace_extension(".zones.json");

    if (!fs::exists(p))
    {
        json root;
        root["zones"] = json::array();

        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);

        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            m_lastIoStatus = "Zones create failed: " + p.string();
            return false;
        }

        f << root.dump(2);
        f.close();

        m_selectedNpcZoneIndex = -1;
        m_lastIoStatus = "Zones file created: " + p.string();
        return true;
    }

    nlohmann::json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(p.string(), root, err))
    {
        m_lastIoStatus = "Zones parse failed: " + err;
        return false;
    }

    if (!root.contains("zones") || !root["zones"].is_array())
    {
        root = json{};
        root["zones"] = json::array();

        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            m_lastIoStatus = "Zones repair failed: " + p.string();
            return false;
        }

        f << root.dump(2);
        f.close();

        m_selectedNpcZoneIndex = -1;
        m_lastIoStatus = "Zones file repaired: " + p.string();
        return true;
    }

    for (const auto& jz : root["zones"])
    {
        EditorNpcZone z;
        z.id = jz.value("id", "");

        if (jz.contains("rect") && jz["rect"].is_array() && jz["rect"].size() >= 4)
        {
            z.minX = jz["rect"][0].get<int>();
            z.minY = jz["rect"][1].get<int>();
            z.maxX = jz["rect"][2].get<int>();
            z.maxY = jz["rect"][3].get<int>();
        }

        if (!z.id.empty())
            m_npcZones.push_back(std::move(z));
    }

    m_selectedNpcZoneIndex = m_npcZones.empty() ? -1 : 0;
    return true;
}

bool Editor::saveZonesForMap(const std::string& mapPath)
{
    fs::path p(mapPath);
    p.replace_extension(".zones.json");

    json root;
    root["zones"] = json::array();

    for (const auto& z : m_npcZones)
    {
        if (z.id.empty())
            continue;

        json jz;
        jz["id"] = z.id;
        jz["rect"] = json::array({ z.minX, z.minY, z.maxX, z.maxY });
        root["zones"].push_back(std::move(jz));
    }

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        m_lastIoStatus = "Zones save failed: " + p.string();
        return false;
    }

    f << root.dump(2);
    return true;
}

void Editor::renderNpcZoneEditor()
{
    if (!ImGui::Begin("NPC Zone Editor"))
    {
        ImGui::End();
        return;
    }

    renderNpcZoneEditorContents();
    ImGui::End();
}

void Editor::renderNpcZoneEditorContents()
{
    if (!ImGui::Begin("NPC Zones"))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("New zone"))
    {
        EditorNpcZone z;
        z.id = "zone_new";
        m_npcZones.push_back(std::move(z));
        m_selectedNpcZoneIndex = (int)m_npcZones.size() - 1;
    }

    ImGui::SameLine();

    if (ImGui::Button("Save zones"))
    {
        if (saveZonesForMap(m_mapPath))
            m_lastIoStatus = "Zones saved.";
    }

    if (!m_zonePickMode)
    {
        if (ImGui::Button("Pick zone from map"))
        {
            if (m_selectedNpcZoneIndex >= 0 && m_selectedNpcZoneIndex < (int)m_npcZones.size())
            {
                m_zonePickMode = true;
                m_zonePickHasStart = false;
                m_lastIoStatus = "Klikni na prvni roh zony.";
            }
            else
            {
                m_lastIoStatus = "Nejdriv vyber nebo vytvor zonu.";
            }
        }
    }
    else
    {
        if (ImGui::Button("Cancel zone pick"))
        {
            m_zonePickMode = false;
            m_zonePickHasStart = false;
            m_lastIoStatus = "Vyber zony zrusen.";
        }

        if (!m_zonePickHasStart)
            ImGui::TextUnformatted("Pick mode: klikni na prvni roh.");
        else
            ImGui::Text("Pick mode: druhy roh od (%d, %d)", m_zonePickStartX, m_zonePickStartY);
    }

    ImGui::Checkbox("Show zones overlay", &m_showNpcZonesOverlay);
    ImGui::Checkbox("Only selected zone", &m_showOnlySelectedZoneOverlay);

    ImGui::Separator();

    for (int i = 0; i < (int)m_npcZones.size(); ++i)
    {
        const bool selected = (i == m_selectedNpcZoneIndex);
        std::string label = m_npcZones[i].id.empty() ? ("zone##" + std::to_string(i)) : m_npcZones[i].id;
        if (ImGui::Selectable(label.c_str(), selected))
            m_selectedNpcZoneIndex = i;
    }

    if (m_selectedNpcZoneIndex >= 0 && m_selectedNpcZoneIndex < (int)m_npcZones.size())
    {
        auto& z = m_npcZones[m_selectedNpcZoneIndex];

        char buf[128];
        strncpy_s(buf, z.id.c_str(), _TRUNCATE);
        if (ImGui::InputText("Zone id", buf, IM_ARRAYSIZE(buf)))
        {
            z.id = buf;
            m_mapDirty = true;
        }

        if (ImGui::InputInt("minX", &z.minX)) m_mapDirty = true;
        if (ImGui::InputInt("minY", &z.minY)) m_mapDirty = true;
        if (ImGui::InputInt("maxX", &z.maxX)) m_mapDirty = true;
        if (ImGui::InputInt("maxY", &z.maxY)) m_mapDirty = true;
        ImGui::Text("Rect: (%d, %d) -> (%d, %d)", z.minX, z.minY, z.maxX, z.maxY);
        ImGui::Text("Size: %d x %d tiles", (z.maxX - z.minX + 1), (z.maxY - z.minY + 1));

        if (ImGui::Button("Normalize rect"))
        {
            if (z.minX > z.maxX) std::swap(z.minX, z.maxX);
            if (z.minY > z.maxY) std::swap(z.minY, z.maxY);
            m_mapDirty = true;
        }

        if (ImGui::Button("Delete zone"))
        {
            pushUndoState("delete zone");
            m_npcZones.erase(m_npcZones.begin() + m_selectedNpcZoneIndex);
            m_selectedNpcZoneIndex = m_npcZones.empty() ? -1 : 0;
            m_mapDirty = true;
        }
    }

    ImGui::End();
}