#include "Editor.h"
#include "game/sequence/SequenceTimeline.h"

#include <utility>

EditorUndoState Editor::captureUndoState(const char* label) const
{
    EditorUndoState s;
    s.map = m_map;
    s.npcSpawns = m_npcSpawns;
    s.npcZones = m_npcZones;
    s.npcSchedules = m_npcSchedules;
    s.forageSpawns = m_forageSpawns;
    s.sequences = m_sequences;
    s.sequenceMarkers = m_sequenceMarkers;
    s.sequenceSceneMarkers = m_sequenceSceneMarkers;
    for (const auto& [id, layout] : m_sequenceGraphLayout) s.sequenceGraphLayout[id] = {layout.x, layout.y};
    s.sequenceSceneDocument = m_sequenceSceneDocument;
    s.sequenceSceneDrafts = m_sequenceSceneDrafts;
    s.editingSceneId = m_sequenceEditingSceneId;
    s.editingSceneOwner = m_sequenceEditingSceneOwner;
    s.selectedSequenceMarkerIndex = m_selectedSequenceMarkerIndex;
    s.selectedSequenceEntityIndex = m_selectedSequenceEntityIndex;
    s.sequenceSelectionKind = static_cast<int>(m_sequenceSelectionKind);
    s.selectedNpcSpawnIndex = m_selectedNpcSpawnIndex;
    s.selectedNpcZoneIndex = m_selectedNpcZoneIndex;
    s.selectedNpcScheduleIndex = m_selectedNpcScheduleIndex;
    s.selectedForageSpawnIndex = m_selectedForageSpawnIndex;
    s.selectedSequenceIndex = m_selectedSequenceIndex;
    s.selectedSequenceNodeIndex = m_selectedSequenceNodeIndex;
    s.selectedSequenceActionIndex = m_selectedSequenceActionIndex;
    if (m_sequenceTimelineSequence == m_selectedSequenceIndex && m_sequenceTimelineNode == m_selectedSequenceNodeIndex)
        s.sequenceTimelineSelection = m_sequenceTimelineSelection;
    s.label = label ? label : "";
    return s;
}

void Editor::restoreUndoState(EditorUndoState&& s)
{
    m_map = std::move(s.map);
    m_npcSpawns = std::move(s.npcSpawns);
    m_npcZones = std::move(s.npcZones);
    m_npcSchedules = std::move(s.npcSchedules);
    m_forageSpawns = std::move(s.forageSpawns);
    m_sequences = std::move(s.sequences);
    m_sequenceMarkers = std::move(s.sequenceMarkers);
    m_sequenceSceneMarkers = std::move(s.sequenceSceneMarkers);
    m_sequenceGraphLayout.clear();
    for (const auto& [id, point] : s.sequenceGraphLayout) m_sequenceGraphLayout[id] = {point.x, point.y};
    m_sequenceGraphLinkSource = -1;
    m_sequenceDraggingNodeIndex = -1;
    if (m_sequenceEditorOpen) m_sequenceLayoutDirty = true;
    m_sequenceSceneDocument = std::move(s.sequenceSceneDocument);
    m_sequenceSceneDrafts = std::move(s.sequenceSceneDrafts);
    m_sequenceEditingSceneId = std::move(s.editingSceneId);
    m_sequenceEditingSceneOwner = std::move(s.editingSceneOwner);
    m_sequenceDraggingImageLayer = -1;
    m_selectedSequenceMarkerIndex = s.selectedSequenceMarkerIndex;
    m_selectedSequenceEntityIndex = s.selectedSequenceEntityIndex;
    m_sequenceSelectionKind = static_cast<SequenceEditorSelectionKind>(s.sequenceSelectionKind);
    m_selectedNpcSpawnIndex = s.selectedNpcSpawnIndex;
    m_selectedNpcZoneIndex = s.selectedNpcZoneIndex;
    m_selectedNpcScheduleIndex = s.selectedNpcScheduleIndex;
    m_selectedForageSpawnIndex = s.selectedForageSpawnIndex;
    m_selectedSequenceIndex = s.selectedSequenceIndex;
    m_selectedSequenceNodeIndex = s.selectedSequenceNodeIndex;
    m_selectedSequenceActionIndex = s.selectedSequenceActionIndex;
    m_sequenceDraggingMarker = -1;
    m_sequenceTimelineSelection = std::move(s.sequenceTimelineSelection);
    m_sequenceTimelineNode = m_selectedSequenceNodeIndex;
    m_sequenceTimelineSequence = m_selectedSequenceIndex;
    if (!m_sequenceTimelineSelection.empty())
    {
        const bool valid = m_selectedSequenceIndex >= 0 && m_selectedSequenceIndex < static_cast<int>(m_sequences.size()) &&
            m_selectedSequenceNodeIndex >= 0 && m_selectedSequenceNodeIndex < static_cast<int>(m_sequences[m_selectedSequenceIndex].nodes.size()) &&
            m_sequenceTimelineSelection.front() == m_selectedSequenceActionIndex &&
            sequence::FindTimelineAction(m_sequences[m_selectedSequenceIndex].nodes[m_selectedSequenceNodeIndex], m_sequenceTimelineSelection);
        if (!valid) m_sequenceTimelineSelection.clear();
    }
    m_sequenceTimelineDragPath.clear();
    m_sequencePickMarkerMode = false;
    if (m_sequenceEditorOpen) syncSequenceMarkerScene(true);

    m_tileSize = m_map.tileSize();
    m_mapDirty = true;
}

void Editor::pushUndoState(const char* label)
{
    m_undoStack.push_back(captureUndoState(label));
    m_redoStack.clear();

    if ((int)m_undoStack.size() > kMaxUndoStates)
        m_undoStack.erase(m_undoStack.begin());
}

bool Editor::canUndo() const
{
    return !m_undoStack.empty();
}

bool Editor::canRedo() const
{
    return !m_redoStack.empty();
}

void Editor::undoLastStep()
{
    if (m_undoStack.empty())
        return;

    EditorUndoState previous = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    m_redoStack.push_back(captureUndoState("redo snapshot"));
    if ((int)m_redoStack.size() > kMaxUndoStates)
        m_redoStack.erase(m_redoStack.begin());

    const std::string label = previous.label;
    restoreUndoState(std::move(previous));
    m_lastIoStatus = label.empty() ? "Undo." : ("Undo: " + label);
}

void Editor::redoLastStep()
{
    if (m_redoStack.empty())
        return;

    EditorUndoState next = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    m_undoStack.push_back(captureUndoState("undo snapshot"));
    if ((int)m_undoStack.size() > kMaxUndoStates)
        m_undoStack.erase(m_undoStack.begin());

    restoreUndoState(std::move(next));
    m_lastIoStatus = "Redo.";
}
