#include "Editor.h"

#include <utility>

EditorUndoState Editor::captureUndoState(const char* label) const
{
    EditorUndoState s;
    s.map = m_map;
    s.npcSpawns = m_npcSpawns;
    s.npcZones = m_npcZones;
    s.npcSchedules = m_npcSchedules;
    s.forageSpawns = m_forageSpawns;
    s.selectedNpcSpawnIndex = m_selectedNpcSpawnIndex;
    s.selectedNpcZoneIndex = m_selectedNpcZoneIndex;
    s.selectedNpcScheduleIndex = m_selectedNpcScheduleIndex;
    s.selectedForageSpawnIndex = m_selectedForageSpawnIndex;
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
    m_selectedNpcSpawnIndex = s.selectedNpcSpawnIndex;
    m_selectedNpcZoneIndex = s.selectedNpcZoneIndex;
    m_selectedNpcScheduleIndex = s.selectedNpcScheduleIndex;
    m_selectedForageSpawnIndex = s.selectedForageSpawnIndex;

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
