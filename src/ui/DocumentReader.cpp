#include "MedievalUI.h"
#include <algorithm>

namespace medieval
{
void UI::openDocuments() { m_documentsOpen = true; }

void UI::documentContents(const Document& document, bool& transcript) const
{
    heading(document.title);
    if (button("##transcript", transcript ? "Zobrazit dobové písmo" : "Zobrazit čitelný přepis"))
        transcript = !transcript;
    const auto role = transcript ? FontRole::Body : document.kind == DocumentKind::Church ? FontRole::ChurchDocument :
                      document.kind == DocumentKind::Noble ? FontRole::NobleDocument : FontRole::Body;
    const auto surface = document.kind == DocumentKind::Church ? Surface::Church :
                         document.kind == DocumentKind::Noble ? Surface::Noble : Surface::Parchment;
    Child page(*this, "##document-page", {0, 0}, surface);
    if (page)
    {
        Font script(*this, role);
        ImGui::TextWrapped("%s", (transcript ? document.transcript : document.original).c_str());
    }
}

void UI::renderDocuments()
{
    if (!m_documentsOpen) return;
    PlaceWindow({850 * scale(), 620 * scale()});
    Window window(*this, "Listiny a dopisy##medieval-documents", Surface::Wood, ImGuiWindowFlags_NoSavedSettings);
    if (!window || !WindowHeader(*this, "Listiny a dopisy", m_documentsOpen)) return;
    if (m_documents.empty())
    {
        ImGui::TextWrapped("Žádné listiny nejsou dostupné.");
        return;
    }
    m_documentIndex = std::clamp(m_documentIndex, 0, static_cast<int>(m_documents.size()) - 1);
    const float listHeight = std::min(ImGui::GetContentRegionAvail().y * 0.3f, 130 * scale());
    {
        Child list(*this, "##document-list", {0, listHeight}, Surface::Wood);
        if (list)
            for (size_t i = 0; i < m_documents.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                if (button("##document", m_documents[i].title, -1, static_cast<int>(i) == m_documentIndex))
                {
                    m_documentIndex = static_cast<int>(i);
                    m_transcript = false;
                }
                ImGui::PopID();
            }
    }
    documentContents(m_documents[m_documentIndex], m_transcript);
}
}
