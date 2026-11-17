#include "Editor.h"

#include "PathUtils.h"
#include "game/sequence/SequenceAudio.h"
#include "game/sequence/SequenceTimeline.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

bool Editor::importSequenceAudio(const std::string& path)
{
    std::string asset, error;
    m_sequenceAudioLibraryOpen = true;
    if (!sequence::ImportAudioFile(path, pathutils::ProjectRoot(), asset, error))
    {
        m_lastIoStatus = error;
        return false;
    }
    m_sequenceAudioFiles = sequence::ListAudioFiles(pathutils::ProjectRoot());
    m_sequenceSelectedAudio = asset;
    m_lastIoStatus = "Importováno: " + asset;
    return true;
}

void Editor::auditionSequenceAudio(const std::string& path, float volume)
{
    stopSequencePreview();
    if (!m_sequenceAudioReady)
        m_sequenceAudioReady = m_sequenceAudioPlayer.init();
    if (!m_sequenceAudioReady)
    {
        m_lastIoStatus = "Zvuk nelze přehrát: " + m_sequenceAudioPlayer.lastError();
        return;
    }
    m_sequenceAudioPlayer.stopSfxChannel(30);
    if (!m_sequenceAudioPlayer.loadSfx("sequence_audition", sequence::ResolveAudioPath(path, pathutils::ProjectRoot()).string()))
        m_lastIoStatus = "Zvuk nelze načíst: " + m_sequenceAudioPlayer.lastError();
    else
    {
        m_sequenceAudioPlayer.playSfx("sequence_audition", 0, 30, static_cast<int>(std::lround(std::clamp(volume, 0.0f, 1.0f) * 128)));
        m_lastIoStatus = "Poslech: " + path;
    }
}

void Editor::insertSequenceAudio(const std::string& path, const std::string& type)
{
    if (m_selectedSequenceIndex < 0 || m_selectedSequenceIndex >= static_cast<int>(m_sequences.size())) return;
    auto& seq = m_sequences[m_selectedSequenceIndex];
    if (m_selectedSequenceNodeIndex < 0 || m_selectedSequenceNodeIndex >= static_cast<int>(seq.nodes.size())) return;
    markSequenceDirty("insert sequence audio");
    stopSequencePreview();
    sequence::ActionDef action;
    action.type = type;
    action.data = {{"type", type}, {"id", path}};
    action.data["volume"] = m_sequenceAudioVolume;
    if (type == "play_music")
    {
        action.data["loops"] = -1;
        action.data["fade_ms"] = 500;
    }
    if (type != "play_music" && type != "play_sfx" && type != "play_voice")
    {
        action.data.erase("id");
        action.data["fade_ms"] = 500;
        if (type == "duck_music")
        {
            action.blocking = false;
            action.data["blocking"] = false;
            action.duration = 3.0f;
            action.data["duration"] = action.duration;
            action.data["volume"] = 0.3f;
            action.data["fade_out_ms"] = 300;
            action.data["fade_in_ms"] = 500;
        }
    }
    m_sequenceTimelineSelection = sequence::InsertTimedAction(seq.nodes[m_selectedSequenceNodeIndex],
        std::move(action), m_sequenceTimelineCursor);
    m_selectedSequenceActionIndex = m_sequenceTimelineSelection.front();
    m_sequenceSelectionKind = SequenceEditorSelectionKind::Action;
    m_lastIoStatus = "Audio vloženo do sekvence. Uložte změny tlačítkem Save.";
}

void Editor::renderSequenceAudioLibrary()
{
    if (!m_sequenceAudioLibraryOpen) return;
    ImGui::SetNextWindowSize(ImVec2(820, 590), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Audio knihovna", &m_sequenceAudioLibraryOpen))
    {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Importujte WAV, OGG, MP3 nebo FLAC. Soubor můžete také přetáhnout ze správce souborů do editoru.");
    ImGui::SetNextItemWidth(-110);
    ImGui::InputText("##audio_source", m_sequenceAudioImportPath, sizeof(m_sequenceAudioImportPath));
    ImGui::SameLine();
    if (ImGui::Button("Procházet..."))
    {
        if (m_sequenceAudioBrowseDirectory.empty())
            m_sequenceAudioBrowseDirectory = pathutils::ProjectRoot().string();
        ImGui::OpenPopup("Vybrat zvuk");
    }
    if (ImGui::Button("Importovat soubor"))
        importSequenceAudio(m_sequenceAudioImportPath);
    ImGui::SameLine();
    if (ImGui::Button("Obnovit knihovnu"))
        m_sequenceAudioFiles = sequence::ListAudioFiles(pathutils::ProjectRoot());
    ImGui::InputText("Filtr", m_sequenceAudioFilter, sizeof(m_sequenceAudioFilter));

    ImGui::BeginChild("AudioAssets", ImVec2(0, -200), true);
    for (const auto& file : m_sequenceAudioFiles)
    {
        if (file.find(m_sequenceAudioFilter) == std::string::npos) continue;
        if (ImGui::Selectable(file.c_str(), file == m_sequenceSelectedAudio, ImGuiSelectableFlags_AllowDoubleClick))
        {
            m_sequenceSelectedAudio = file;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) auditionSequenceAudio(file, m_sequenceAudioVolume);
        }
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("SEQ_AUDIO", file.c_str(), file.size() + 1);
            ImGui::TextUnformatted(file.c_str());
            ImGui::EndDragDropSource();
        }
    }
    if (m_sequenceAudioFiles.empty()) ImGui::TextDisabled("Knihovna je prázdná. Importujte první zvuk.");
    ImGui::EndChild();
    const char* kinds[] = {"Hudba", "Zvukový efekt", "Hlas"};
    const char* types[] = {"play_music", "play_sfx", "play_voice"};
    ImGui::SetNextItemWidth(160);
    ImGui::Combo("Typ vložení", &m_sequenceAudioKind, kinds, 3);
    float volumePercent = m_sequenceAudioVolume * 100;
    if (ImGui::SliderFloat("Hlasitost klipu", &volumePercent, 0, 100, "%.0f %%"))
        m_sequenceAudioVolume = volumePercent / 100;
    ImGui::BeginDisabled(m_sequenceSelectedAudio.empty());
    if (ImGui::Button("Poslechnout")) auditionSequenceAudio(m_sequenceSelectedAudio, m_sequenceAudioVolume);
    ImGui::SameLine();
    if (ImGui::Button("Zastavit poslech")) m_sequenceAudioPlayer.stopSfxChannel(30);
    ImGui::SameLine();
    ImGui::BeginDisabled(m_selectedSequenceIndex < 0 || m_selectedSequenceNodeIndex < 0);
    if (ImGui::Button("Vložit na kurzor osy")) insertSequenceAudio(m_sequenceSelectedAudio, types[m_sequenceAudioKind]);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(m_selectedSequenceIndex < 0 || m_selectedSequenceNodeIndex < 0);
    if (ImGui::Button("Hlasitost hudby")) insertSequenceAudio("", "set_music_volume");
    ImGui::SameLine();
    if (ImGui::Button("Hlasitost zvuků")) insertSequenceAudio("", "set_sfx_volume");
    ImGui::SameLine();
    if (ImGui::Button("Dočasně zeslabit hudbu")) insertSequenceAudio("", "duck_music");
    ImGui::SameLine();
    if (ImGui::Button("Obnovit hudbu")) insertSequenceAudio("", "restore_music");
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", m_lastIoStatus.c_str());

    ImGui::SetNextWindowSize(ImVec2(700, 480), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Vybrat zvuk", nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
        char directory[2048]{};
        std::strncpy(directory, m_sequenceAudioBrowseDirectory.c_str(), sizeof(directory) - 1);
        if (ImGui::InputText("Adresář", directory, sizeof(directory), ImGuiInputTextFlags_EnterReturnsTrue))
            m_sequenceAudioBrowseDirectory = directory;
        if (ImGui::Button("O úroveň výš"))
            m_sequenceAudioBrowseDirectory = fs::path(m_sequenceAudioBrowseDirectory).parent_path().string();
        ImGui::BeginChild("AudioFileBrowser", ImVec2(0, -40), true);
        std::error_code ec;
        std::vector<fs::directory_entry> entries;
        for (fs::directory_iterator it(m_sequenceAudioBrowseDirectory, fs::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec))
            if (it->is_directory(ec) || sequence::IsAudioFile(it->path())) entries.push_back(*it);
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });
        if (ec) ImGui::TextWrapped("Adresář nelze otevřít: %s", ec.message().c_str());
        for (const auto& entry : entries)
        {
            const bool dir = entry.is_directory(ec);
            const std::string label = (dir ? "[+] " : "") + entry.path().filename().string();
            if (ImGui::Selectable(label.c_str()))
            {
                if (dir) m_sequenceAudioBrowseDirectory = entry.path().string();
                else
                {
                    const auto source = entry.path().string();
                    std::strncpy(m_sequenceAudioImportPath, source.c_str(), sizeof(m_sequenceAudioImportPath) - 1);
                    importSequenceAudio(source);
                    ImGui::CloseCurrentPopup();
                }
                break;
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Zavřít")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

void Editor::renderSequenceAudioAction(sequence::ActionDef& action)
{
    auto& data = action.data;
    if (action.type != "stop_music" && action.type != "restore_music")
    {
        float percent = std::clamp(data.value("volume", action.type == "duck_music" ? 0.3f : 1.0f), 0.0f, 1.0f) * 100;
        if (ImGui::SliderFloat(action.type == "duck_music" ? "Úroveň při zeslabení" : "Hlasitost", &percent, 0, 100, "%.0f %%"))
            data["volume"] = percent / 100;
        ImGui::TextWrapped("Úroveň se násobí hlasitostí v nastavení hry. 0 %% znamená ztlumení.");
    }
    auto fade = [&](const char* label, const char* key, int fallback) {
        int ms = data.value(key, fallback);
        if (ImGui::InputInt(label, &ms)) data[key] = std::max(0, ms);
    };
    if (action.type == "duck_music")
    {
        fade("Plynulé zeslabení (ms)", "fade_out_ms", 300);
        fade("Plynulý návrat (ms)", "fade_in_ms", 500);
        ImGui::TextWrapped("Duration určuje dobu držení po plynulém zeslabení. Potom se hudba vrátí na nastavenou úroveň. Další akce pokračují během zeslabení.");
    }
    else if (action.type == "set_music_volume" || action.type == "restore_music" || action.type == "stop_music")
        fade(action.type == "restore_music" ? "Plynulý návrat (ms)" : "Přechod (ms)", "fade_ms", action.type == "stop_music" ? 300 : 500);
    if (action.type == "restore_music")
        ImGui::TextWrapped("Ukončí dočasné zeslabení a vrátí hudbu na nastavenou hlasitost.");
}
