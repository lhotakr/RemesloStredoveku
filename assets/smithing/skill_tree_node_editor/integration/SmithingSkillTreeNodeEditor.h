#pragma once

#include "imgui.h"
#include "imgui_node_editor.h"

namespace SmithingSkillTree {

namespace ed = ax::NodeEditor;

enum class NodeState {
    Completed,
    Active,
    Available,
    LockedNear,
    LockedFar,
    Secret
};

struct ViewState {
    ed::EditorContext* editor = nullptr;
    int selectedNodeId = 120;
    bool navigateOnNextFrame = true;
};

// Call once when the owning screen is created/destroyed. Settings are not
// persisted because the gameplay layout is intentionally fixed.
void Initialize(ViewState& state);
void Shutdown(ViewState& state);

// Draws only the pannable/zoomable graph. Draw the detail inspector next to it,
// outside the node-editor Begin/End block.
void DrawTree(ViewState& state, ImTextureID atlasTexture, const ImVec2& canvasSize);

// Example inspector for the selected node. Production code should replace its
// Czech literals with localization keys from smithing_skill_tree.json.
void DrawInspector(const ViewState& state, ImTextureID atlasTexture, const ImVec2& size);

} // namespace SmithingSkillTree
