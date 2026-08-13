#include "SmithingSkillTreeNodeEditor.h"

#include "SmithingSkillTreeAtlas.generated.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace SmithingSkillTree {
namespace A = SmithingSkillTreeAtlas;

namespace {

struct Sprite {
    ImVec2 uv0;
    ImVec2 uv1;
};

struct Node {
    int id;
    int inputPin;
    int outputPin;
    ImVec2 position;
    NodeState state;
    const char* title;
    const char* subtitle;
    const char* icon;
    float progress;
};

struct Link {
    int id;
    int startPin;
    int endPin;
    NodeState state;
};

const ImVec2 kNodeSize(270.0f, 132.0f);

ImVec2 Add(const ImVec2& a, const ImVec2& b) {
    return {a.x + b.x, a.y + b.y};
}

const std::array<Node, 9> kNodes{{
    {100, 0,    1001, {0,    380}, NodeState::Completed,  "Pozorovatel",     "Základy výhně",        "observer",          1.00f},
    {110, 1100, 1101, {330,  380}, NodeState::Completed,  "Učeň",            "První výrobky",        "apprentice",        1.00f},
    {120, 1200, 1201, {660,  380}, NodeState::Active,     "Tovaryš",         "Samostatná práce",     "journeyman",        0.46f},
    {131, 1310, 1311, {1020, 140}, NodeState::LockedNear, "Nástrojařství",   "Volba specializace",   "toolmaking",        0.00f},
    {132, 1320, 1321, {1020, 380}, NodeState::LockedNear, "Podkovářství",    "Volba specializace",   "farriery",          0.00f},
    {133, 1330, 1331, {1020, 620}, NodeState::LockedNear, "Stavební kování", "Volba specializace",   "building_ironwork", 0.00f},
    {140, 1400, 1401, {1380, 380}, NodeState::LockedFar,  "Mistr",           "Vedení dílny",         "master",            0.00f},
    {151, 1510, 0,    {1740, 260}, NodeState::Secret,     "",                "",                     "innovator",         0.00f},
    {152, 1520, 0,    {1740, 500}, NodeState::Secret,     "",                "",                     "workshop_master",    0.00f},
}};

const std::array<Link, 10> kLinks{{
    {200, 1001, 1100, NodeState::Completed},
    {201, 1101, 1200, NodeState::Active},
    {210, 1201, 1310, NodeState::LockedNear},
    {211, 1201, 1320, NodeState::LockedNear},
    {212, 1201, 1330, NodeState::LockedNear},
    {220, 1311, 1400, NodeState::LockedFar},
    {221, 1321, 1400, NodeState::LockedFar},
    {222, 1331, 1400, NodeState::LockedFar},
    {230, 1401, 1510, NodeState::Secret},
    {231, 1401, 1520, NodeState::Secret},
}};

Sprite MakeSprite(const A::Rect& rect) {
    return {{rect.x / float(A::Width), rect.y / float(A::Height)},
            {(rect.x + rect.w) / float(A::Width), (rect.y + rect.h) / float(A::Height)}};
}

Sprite NodeSprite(NodeState state, bool selected) {
    if (selected) return MakeSprite(A::nodes_selected);
    switch (state) {
        case NodeState::Completed:  return MakeSprite(A::nodes_completed);
        case NodeState::Active:     return MakeSprite(A::nodes_active);
        case NodeState::Available:  return MakeSprite(A::nodes_available);
        case NodeState::LockedNear: return MakeSprite(A::nodes_locked_near);
        case NodeState::LockedFar:  return MakeSprite(A::nodes_locked_far);
        case NodeState::Secret:     return MakeSprite(A::nodes_secret);
    }
    return MakeSprite(A::nodes_locked_far);
}

Sprite PinSprite(NodeState state) {
    switch (state) {
        case NodeState::Completed:  return MakeSprite(A::pins_completed);
        case NodeState::Active:     return MakeSprite(A::pins_active);
        case NodeState::Available:  return MakeSprite(A::pins_available);
        case NodeState::LockedNear:
        case NodeState::LockedFar:  return MakeSprite(A::pins_locked);
        case NodeState::Secret:     return MakeSprite(A::pins_secret);
    }
    return MakeSprite(A::pins_locked);
}

Sprite IconSprite(const char* name) {
    if (!std::strcmp(name, "observer"))          return MakeSprite(A::icons_observer);
    if (!std::strcmp(name, "apprentice"))        return MakeSprite(A::icons_apprentice);
    if (!std::strcmp(name, "journeyman"))        return MakeSprite(A::icons_journeyman);
    if (!std::strcmp(name, "toolmaking"))        return MakeSprite(A::icons_toolmaking);
    if (!std::strcmp(name, "farriery"))          return MakeSprite(A::icons_farriery);
    if (!std::strcmp(name, "building_ironwork")) return MakeSprite(A::icons_building_ironwork);
    if (!std::strcmp(name, "master"))            return MakeSprite(A::icons_master);
    if (!std::strcmp(name, "innovator"))         return MakeSprite(A::icons_innovator);
    return MakeSprite(A::icons_workshop_master);
}

ImColor LinkColor(NodeState state) {
    switch (state) {
        case NodeState::Completed: return ImColor(143, 166, 121, 255);
        case NodeState::Active:    return ImColor(240, 122, 49, 255);
        case NodeState::Available: return ImColor(196, 154, 76, 255);
        case NodeState::LockedNear:return ImColor(98, 95, 91, 210);
        case NodeState::LockedFar: return ImColor(72, 70, 67, 165);
        case NodeState::Secret:    return ImColor(62, 61, 58, 110);
    }
    return ImColor(98, 95, 91, 210);
}

float LinkThickness(NodeState state) {
    return state == NodeState::Active ? 5.0f : 3.0f;
}

const Node* FindNode(int id) {
    const auto it = std::find_if(kNodes.begin(), kNodes.end(), [id](const Node& node) { return node.id == id; });
    return it == kNodes.end() ? &kNodes[2] : &*it;
}

void DrawAtlasImage(ImTextureID texture, const Sprite& sprite, ImVec2 size) {
    ImGui::Image(texture, size, sprite.uv0, sprite.uv1);
}

void DrawPin(ImTextureID texture, int pinId, ed::PinKind kind, const Sprite& sprite,
             const ImVec2& screenPosition) {
    if (!pinId) return;
    ImGui::SetCursorScreenPos(screenPosition);
    ed::BeginPin(ed::PinId(pinId), kind);
    DrawAtlasImage(texture, sprite, {24, 24});
    ed::EndPin();
}

void DrawNode(const Node& node, ImTextureID atlasTexture) {
    const ed::NodeId nodeId(node.id);
    const bool selected = ed::IsNodeSelected(nodeId);
    const Sprite frame = NodeSprite(node.state, selected);
    const Sprite pin = PinSprite(node.state);

    ed::BeginNode(nodeId);
    ImGui::PushID(node.id);
    const ImVec2 topLeft = ImGui::GetCursorScreenPos();
    DrawAtlasImage(atlasTexture, frame, kNodeSize);

    DrawPin(atlasTexture, node.inputPin, ed::PinKind::Input, pin,
            Add(topLeft, ImVec2(-12, kNodeSize.y * 0.5f - 12)));
    DrawPin(atlasTexture, node.outputPin, ed::PinKind::Output, pin,
            Add(topLeft, ImVec2(kNodeSize.x - 12, kNodeSize.y * 0.5f - 12)));

    if (node.state == NodeState::Secret) {
        ImGui::SetCursorScreenPos(Add(topLeft, ImVec2(kNodeSize.x * .5f - 18, kNodeSize.y * .5f - 18)));
        DrawAtlasImage(atlasTexture, MakeSprite(A::badges_question), {36, 36});
    } else {
        ImGui::SetCursorScreenPos(Add(topLeft, ImVec2(16, 42)));
        DrawAtlasImage(atlasTexture, IconSprite(node.icon), {58, 58});

        ImGui::SetCursorScreenPos(Add(topLeft, ImVec2(16, 14)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.87f, 0.78f, 1.0f));
        ImGui::TextUnformatted(node.title);
        ImGui::PopStyleColor();

        ImGui::SetCursorScreenPos(Add(topLeft, ImVec2(84, 46)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.61f, 0.55f, 1.0f));
        ImGui::TextUnformatted(node.subtitle);
        ImGui::PopStyleColor();

        const ImVec2 barMin = Add(topLeft, ImVec2(84, 82));
        const ImVec2 barMax = Add(topLeft, ImVec2(250, 94));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(barMin, barMax, IM_COL32(15, 12, 10, 255), 6.0f);
        draw->AddRect(barMin, barMax, IM_COL32(102, 83, 65, 255), 6.0f, 0, 1.0f);
        if (node.progress > 0.0f) {
            const ImU32 color = node.progress >= .999f ? IM_COL32(143,166,121,255) : IM_COL32(240,122,49,255);
            draw->AddRectFilled(Add(barMin, ImVec2(2,2)), {barMin.x + 2 + (barMax.x-barMin.x-4)*node.progress, barMax.y-2}, color, 4.0f);
        }
    }

    // Keep the custom image inside the node's layout bounds without a child window.
    ImGui::SetCursorScreenPos(Add(Add(topLeft, kNodeSize), ImVec2(0, 1)));
    ImGui::Dummy({1, 1});
    ImGui::PopID();
    ed::EndNode();
}

} // namespace

void Initialize(ViewState& state) {
    if (state.editor) return;
    ed::Config config;
    config.SettingsFile = nullptr;
    state.editor = ed::CreateEditor(&config);
    state.navigateOnNextFrame = true;
}

void Shutdown(ViewState& state) {
    if (!state.editor) return;
    ed::DestroyEditor(state.editor);
    state.editor = nullptr;
}

void DrawTree(ViewState& state, ImTextureID atlasTexture, const ImVec2& canvasSize) {
    if (!state.editor) Initialize(state);
    ed::SetCurrentEditor(state.editor);
    ed::EnableShortcuts(false);

    // The atlas already supplies the node border and fill.
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0, 0, 0, 0));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0, 0, 0, 0));
    ed::PushStyleColor(ed::StyleColor_HovNodeBorder, ImVec4(0, 0, 0, 0));
    ed::PushStyleColor(ed::StyleColor_SelNodeBorder, ImVec4(0, 0, 0, 0));
    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));

    ed::Begin("SmithingSkillTree", canvasSize);

    for (const Node& node : kNodes) {
        // Reasserting positions gives us a viewer, not a player-editable graph.
        ed::SetNodePosition(ed::NodeId(node.id), node.position);
        DrawNode(node, atlasTexture);
    }

    for (const Link& link : kLinks) {
        ed::Link(ed::LinkId(link.id), ed::PinId(link.startPin), ed::PinId(link.endPin),
                 LinkColor(link.state), LinkThickness(link.state));
    }

    if (state.navigateOnNextFrame) {
        ed::NavigateToContent(0.0f);
        state.navigateOnNextFrame = false;
    }

    std::array<ed::NodeId, 1> selection{};
    if (ed::GetSelectedNodes(selection.data(), selection.size()) > 0)
        state.selectedNodeId = static_cast<int>(selection[0].Get());

    ed::End();
    ed::PopStyleVar();
    ed::PopStyleColor(4);
    ed::SetCurrentEditor(nullptr);
}

void DrawInspector(const ViewState& state, ImTextureID atlasTexture, const ImVec2& size) {
    const Node& node = *FindNode(state.selectedNodeId);
    ImGui::BeginChild("SmithingSkillInspector", size, true);
    ImGui::TextColored(ImVec4(0.94f, 0.48f, 0.19f, 1.0f), "%s", node.title[0] ? node.title : "Neodhalená větev");
    if (node.state == NodeState::Secret) {
        ImGui::TextDisabled("Tato větev se zobrazí až po odhalení.");
        ImGui::EndChild();
        return;
    }

    DrawAtlasImage(atlasTexture, IconSprite(node.icon), {72,72});
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(node.subtitle);
    ImGui::ProgressBar(node.progress, {-1, 0});
    ImGui::TextDisabled("Postup potvrzuje skutečná práce a výsledek.");
    ImGui::EndGroup();

    ImGui::Separator();
    ImGui::TextUnformatted("Co se učíš");
    if (node.id == 120) {
        ImGui::BulletText("Sekery, dláta, nože, motyky a podkovy");
        ImGui::BulletText("Svařování ohněm");
        ImGui::BulletText("Tepelné zpracování");
        ImGui::BulletText("Odhad materiálu");
    } else {
        ImGui::TextWrapped("Konkrétní milníky načti z pole lessons v smithing_skill_tree.json.");
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Podmínka postupu");
    ImGui::TextWrapped("Dovednost se nekupuje za body. Odemkne ji pozorování mistra, správný postup a prokazatelný výsledek práce.");
    ImGui::EndChild();
}

} // namespace SmithingSkillTree
