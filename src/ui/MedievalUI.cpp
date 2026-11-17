#include "MedievalUI.h"
#include <SDL_image.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace medieval
{
namespace
{
constexpr int ColorCount = 24, VarCount = 9;
constexpr std::array<float, 5> FontSizes{23, 28, 27, 27, 22};
bool Paper(Surface s) { return s == Surface::Parchment || s == Surface::Church || s == Surface::Noble; }
ImU32 Color(ImVec4 value) { return ImGui::ColorConvertFloat4ToU32(value); }
ImVec2 Add(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
float Positive(float value) { return std::isfinite(value) ? std::max(0.0f, value) : 0.0f; }

void PaintWindow(const UI& ui, Surface surface)
{
    auto* draw = ImGui::GetWindowDrawList();
    const auto pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    // Cover the outer frame too, without letting its decoration escape the window.
    draw->PushClipRect(pos, Add(pos, size), false);
    ui.panel(draw, pos, size, surface);
    draw->PopClipRect();
}
}

float LayoutScale(ImVec2 display)
{
    return std::clamp(std::min(Positive(display.x) / 1280.0f, Positive(display.y) / 720.0f), 0.75f, 1.5f);
}

Rect DialogueRect(ImVec2 pos, ImVec2 size)
{
    const float s = LayoutScale(size);
    const float margin = std::min(20 * s, std::min(Positive(size.x), Positive(size.y)) * 0.05f);
    const float width = std::max(1.0f, std::min(1160 * s, size.x - margin * 2));
    const float height = std::max(1.0f, std::min(size.y - margin * 2, std::max(260 * s, size.y * 0.46f)));
    return {{pos.x + (size.x - width) / 2, pos.y + size.y - height - margin}, {width, height}};
}

std::array<Slice, 9> NineSliceGeometry(ImVec2 size, ImVec2 tex, Borders b, float scale)
{
    std::array<Slice, 9> result{};
    if (!(size.x > 0 && size.y > 0 && tex.x > 0 && tex.y > 0) ||
        !std::isfinite(size.x + size.y + tex.x + tex.y)) return result;
    b = {Positive(b.left), Positive(b.top), Positive(b.right), Positive(b.bottom)};
    const float sourceFit = std::min({1.0f, tex.x / std::max(tex.x, b.left + b.right),
                                    tex.y / std::max(tex.y, b.top + b.bottom)});
    b.left *= sourceFit; b.top *= sourceFit; b.right *= sourceFit; b.bottom *= sourceFit;
    const float targetScale = std::min({Positive(scale), size.x / std::max(1.0f, b.left + b.right),
                                       size.y / std::max(1.0f, b.top + b.bottom)});
    const float x[]{0, b.left * targetScale, size.x - b.right * targetScale, size.x};
    const float y[]{0, b.top * targetScale, size.y - b.bottom * targetScale, size.y};
    const float u[]{0, b.left / tex.x, 1 - b.right / tex.x, 1};
    const float v[]{0, b.top / tex.y, 1 - b.bottom / tex.y, 1};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            result[row * 3 + col] = {{x[col], y[row]}, {x[col + 1], y[row + 1]},
                                    {u[col], v[row]}, {u[col + 1], v[row + 1]}};
    return result;
}

UI& UI::Get() { static UI ui; return ui; }

bool UI::loadTexture(const std::string& id, const std::filesystem::path& path, Borders borders, float borderScale)
{
    if (!m_renderer || !std::filesystem::is_regular_file(path)) return false;
    Texture texture;
    texture.handle.reset(IMG_LoadTexture(m_renderer, path.string().c_str()));
    if (!texture.handle)
    {
        SDL_Log("MedievalUI: cannot load %s: %s", path.string().c_str(), IMG_GetError());
        return false;
    }
    int w = 0, h = 0;
    if (SDL_QueryTexture(texture.handle.get(), nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0) return false;
    SDL_SetTextureBlendMode(texture.handle.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture.handle.get(), SDL_ScaleModeLinear);
    texture.size = {static_cast<float>(w), static_cast<float>(h)};
    texture.borders = borders;
    texture.borderScale = Positive(borderScale);
    m_textures.insert_or_assign(id, std::move(texture));
    return true;
}

void UI::loadFonts(const std::filesystem::path& root)
{
    auto& io = ImGui::GetIO();
    static const ImWchar ranges[]{0x20, 0x024F, 0x1E00, 0x1EFF, 0x2000, 0x206F, 0};
    auto fallbackPath = root / "assets/Fonts/NotoSans-Regular.ttf";
    if (!std::filesystem::is_regular_file(fallbackPath))
        fallbackPath = root / "assets/Fonts/Medieval/Junicode-Regular.otf";
    const bool fallbackFile = std::filesystem::is_regular_file(fallbackPath);
    auto load = [&](const std::filesystem::path& path, float size, bool merge) -> ImFont*
    {
        if (!std::filesystem::is_regular_file(path)) return nullptr;
        ImFontConfig cfg;
        cfg.MergeMode = merge;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        return io.Fonts->AddFontFromFileTTF(path.string().c_str(), size, &cfg, ranges);
    };
    ImFont* fallback = fallbackFile ? load(fallbackPath, 28, false) : nullptr;
    if (!fallback) fallback = io.Fonts->AddFontDefault();
    m_fonts.fill(fallback);
    // Preserve the existing editor/menu default. Historical roles are scoped below.
    io.FontDefault = fallback;
    const char* names[]{"Junicode-Regular.otf", "MedievalSharp-Regular.ttf", "Missaali-Regular.otf", "Aboensis-Regular.otf"};
    for (size_t i = 0; i < 4; ++i)
    {
        const auto path = root / "assets/Fonts/Medieval" / names[i];
        if (auto* primary = load(path, FontSizes[i], false))
        {
            m_fonts[i] = primary;
            // ImGui resolves overlapping glyphs from the first source. Noto fills
            // holes (notably Czech glyphs in document fonts), retaining the original.
            if (fallbackFile) load(fallbackPath, FontSizes[i], true);
        }
        else SDL_Log("MedievalUI: %s missing; using readable fallback", names[i]);
    }
}

void UI::initialize(SDL_Renderer* renderer, const std::filesystem::path& root)
{
    // The game calls initialize once, immediately after creating its ImGui context.
    shutdown();
    m_renderer = renderer;
    loadFonts(root);
    try
    {
        std::ifstream input(root / "data/ui/medieval_ui.json");
        if (input)
        {
            const auto config = nlohmann::json::parse(input);
            m_portraitAliases = config.value("portrait_aliases", std::unordered_map<std::string, std::string>{});
            for (const auto& [id, entry] : config.at("textures").items())
            {
                const auto border = entry.value("borders", std::array<float, 4>{});
                loadTexture(id, root / entry.at("path").get<std::string>(),
                    {border[0], border[1], border[2], border[3]}, entry.value("border_scale", 1.0f));
            }
            const auto portraits = config.value("portraits", nlohmann::json::object());
            for (const auto& [id, path] : portraits.items())
                loadTexture("portrait:" + id, root / path.get<std::string>());
        }
    }
    catch (const std::exception& e) { SDL_Log("MedievalUI: skin configuration: %s", e.what()); }
    try
    {
        std::ifstream input(root / "data/ui/documents.json");
        if (input)
        {
            const auto config = nlohmann::json::parse(input);
            for (const auto& entry : config.at("documents"))
            {
                const auto type = entry.value("type", "letter");
                m_documents.push_back({entry.at("id"), entry.at("title"), entry.at("original"), entry.at("transcript"),
                    type == "church" ? DocumentKind::Church : type == "noble" ? DocumentKind::Noble : DocumentKind::Letter});
            }
        }
    }
    catch (const std::exception& e) { SDL_Log("MedievalUI: document configuration: %s", e.what()); }
    SDL_Log("MedievalUI: %zu textures, %zu sample documents", textureCount(), m_documents.size());
}

void UI::shutdown()
{
    m_textures.clear();
    m_portraitAliases.clear();
    m_fonts.fill(nullptr); // The ImGui atlas retains and frees font data.
    m_documents.clear();
    m_documentsOpen = false;
    m_transcript = false;
    m_documentIndex = 0;
    m_renderer = nullptr;
}

ImFont* UI::font(FontRole role) const { return m_fonts.at(static_cast<size_t>(role)); }
float UI::scale() const { return LayoutScale(ImGui::GetMainViewport()->WorkSize); }
float UI::fontSize(FontRole role) const
{
    // Game/editor use a legacy global font scale. Keep this UI in viewport units
    // without modifying that global setting or scaling it a second time.
    return FontSizes.at(static_cast<size_t>(role)) * scale() /
        std::max(0.1f, ImGui::GetStyle().FontScaleMain * ImGui::GetIO().FontGlobalScale);
}
bool UI::hasTexture(const std::string& id) const { return m_textures.contains(id); }

void UI::image(ImDrawList* draw, const std::string& id, ImVec2 pos, ImVec2 size, ImU32 tint) const
{
    const auto found = m_textures.find(id);
    if (found == m_textures.end()) return;
    const auto& tex = found->second;
    for (const auto& part : NineSliceGeometry(size, tex.size, tex.borders, tex.borderScale * scale()))
        if (part.max.x > part.min.x && part.max.y > part.min.y)
            draw->AddImage((ImTextureID)(intptr_t)tex.handle.get(), Add(pos, part.min), Add(pos, part.max), part.uvMin, part.uvMax, tint);
}

void UI::panel(ImDrawList* draw, ImVec2 pos, ImVec2 size, Surface surface) const
{
    const char* id = surface == Surface::Wood ? "wood" : surface == Surface::Stone ? "stone" :
                     surface == Surface::Church ? "church" : surface == Surface::Noble ? "noble" : "parchment";
    // Flat fallback is deliberately not represented as purchased artwork.
    draw->AddRectFilled(pos, Add(pos, size), Paper(surface) ? IM_COL32(225, 209, 175, 255) :
        surface == Surface::Stone ? IM_COL32(57, 55, 49, 255) : IM_COL32(35, 25, 18, 255));
    image(draw, id, pos, size);
    draw->AddRect(pos, Add(pos, size), Color(Palette::Brass), 2 * scale());
}

bool UI::button(const char* id, const std::string& label, float width, bool selected, float minHeight) const
{
    width = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    width = std::max(1.0f, width);
    const float padding = 10 * scale();
    const auto textSize = ImGui::CalcTextSize(label.c_str(), nullptr, false, std::max(1.0f, width - padding * 2));
    const ImVec2 size{width, std::max({38 * scale(), minHeight, textSize.y + padding * 1.5f})};
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
    if (!ImGui::IsItemVisible()) return pressed;
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive(), focused = ImGui::IsItemFocused();
    const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    auto* draw = ImGui::GetWindowDrawList();
    buttonFrame(draw, pos, size, hovered, active, focused, selected, disabled);
    const ImVec4 clip{pos.x + padding, pos.y, pos.x + size.x - padding, pos.y + size.y};
    const auto ink = disabled ? Palette::MutedInk : Palette::Ink;
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), Add(pos, {padding, (size.y - textSize.y) / 2}),
        Color(ink), label.c_str(), nullptr, std::max(1.0f, width - padding * 2), &clip);
    return pressed;
}

void UI::buttonFrame(ImDrawList* draw, ImVec2 pos, ImVec2 size, bool hovered,
                     bool active, bool focused, bool selected, bool disabled) const
{
    const char* state = disabled ? "button_disabled" : active ? "button_pressed" :
                        (hovered || selected) ? "button_hover" : "button";
    draw->AddRectFilled(pos, Add(pos, size), hovered || selected ? IM_COL32(225, 201, 150, 255) : IM_COL32(209, 193, 159, 255), 2 * scale());
    image(draw, state, pos, size);
    draw->AddRect(pos, Add(pos, size), Color(Palette::Brass), 2 * scale());
    if (focused || selected)
    {
        image(draw, "button_focus", pos, size);
        draw->AddRect(Add(pos, {2, 2}), Add(pos, {size.x - 2, size.y - 2}), IM_COL32(113, 81, 35, 255), 2, 0, 2);
    }
}

void UI::heading(const std::string& text) const
{
    Font fontScope(*this, FontRole::Heading);
    ImGui::TextWrapped("%s", text.c_str());
}

Font::Font(const UI& ui, FontRole role) { ImGui::PushFont(ui.font(role), ui.fontSize(role)); }
Font::~Font() { ImGui::PopFont(); }

Style::Style(const UI& ui, Surface surface)
{
    const bool paper = Paper(surface);
    const ImVec4 dark{0.20f, 0.15f, 0.10f, 1}, soft{0.45f, 0.36f, 0.23f, 1};
    const ImVec4 muted = paper ? Palette::MutedInk : ImVec4(0.73f, 0.66f, 0.53f, 1);
    const std::pair<ImGuiCol, ImVec4> colors[]{
        {ImGuiCol_Text, paper ? Palette::Ink : Palette::Ivory}, {ImGuiCol_TextDisabled, muted},
        {ImGuiCol_WindowBg, dark}, {ImGuiCol_ChildBg, {0, 0, 0, 0}},
        {ImGuiCol_PopupBg, paper ? ImVec4(0.91f, 0.85f, 0.73f, 1) : dark},
        {ImGuiCol_Border, Palette::Brass},
        {ImGuiCol_FrameBg, paper ? ImVec4(0.94f, 0.88f, 0.75f, 1) : soft},
        {ImGuiCol_FrameBgHovered, paper ? ImVec4(0.97f, 0.91f, 0.80f, 1) : Palette::Brass},
        {ImGuiCol_FrameBgActive, paper ? ImVec4(1.0f, 0.95f, 0.84f, 1) : soft},
        {ImGuiCol_Button, soft}, {ImGuiCol_ButtonHovered, Palette::Brass},
        {ImGuiCol_ButtonActive, dark}, {ImGuiCol_Header, paper ? ImVec4(0.76f, 0.66f, 0.49f, 1) : soft},
        {ImGuiCol_HeaderHovered, paper ? ImVec4(0.84f, 0.75f, 0.58f, 1) : Palette::Brass},
        {ImGuiCol_HeaderActive, paper ? ImVec4(0.80f, 0.70f, 0.52f, 1) : dark},
        {ImGuiCol_CheckMark, Palette::Brass}, {ImGuiCol_SliderGrab, Palette::Brass},
        {ImGuiCol_SliderGrabActive, soft}, {ImGuiCol_ScrollbarBg, {0.15f, 0.11f, 0.08f, 0.2f}},
        {ImGuiCol_ScrollbarGrab, soft}, {ImGuiCol_ScrollbarGrabHovered, Palette::Brass},
        {ImGuiCol_ScrollbarGrabActive, muted}, {ImGuiCol_Separator, Palette::Brass}, {ImGuiCol_NavCursor, Palette::Brass}
    };
    static_assert(std::size(colors) == ColorCount);
    for (const auto& [slot, color] : colors) ImGui::PushStyleColor(slot, color);
    const float s = ui.scale();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18 * s, 14 * s});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10 * s, 6 * s});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8 * s, 6 * s});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 2 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 12 * s);
}
Style::~Style() { ImGui::PopStyleVar(VarCount); ImGui::PopStyleColor(ColorCount); }

Window::Window(const UI& ui, const char* id, Surface surface, ImGuiWindowFlags flags)
    : m_style(ui, surface), m_font(ui, FontRole::Body),
      m_visible(ImGui::Begin(id, nullptr, flags | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse))
{
    if (m_visible) PaintWindow(ui, surface);
}
Window::~Window() { ImGui::End(); }
Child::Child(const UI& ui, const char* id, ImVec2 size, Surface surface)
    : m_style(ui, surface), m_visible(false)
{
    const auto available = ImGui::GetContentRegionAvail();
    const ImVec2 fitted{size.x > 0 ? size.x : std::max(1.0f, available.x + size.x),
                        size.y > 0 ? size.y : std::max(1.0f, available.y + size.y)};
    // Draw on the parent before entering the child: its clip rectangle then also
    // clips the frame when the parent scrolls or a narrow window cuts it off.
    ui.panel(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), fitted, surface);
    m_visible = ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
}
Child::~Child() { ImGui::EndChild(); }

bool WindowHeader(const UI& ui, const std::string& title, bool& open)
{
    if (ImGui::BeginTable("##heading", 2))
    {
        ImGui::TableSetupColumn("title", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("close", ImGuiTableColumnFlags_WidthFixed, 98 * ui.scale());
        ImGui::TableNextColumn();
        ui.heading(title);
        ImGui::TableNextColumn();
        if (ui.button("##close", "Zavřít")) open = false;
        ImGui::EndTable();
    }
    ImGui::Separator();
    return open;
}

void PlaceWindow(ImVec2 preferred, ImGuiCond condition)
{
    const auto* vp = ImGui::GetMainViewport();
    const float margin = 12 * LayoutScale(vp->WorkSize);
    const ImVec2 available{std::max(1.0f, vp->WorkSize.x - margin * 2), std::max(1.0f, vp->WorkSize.y - margin * 2)};
    const ImVec2 size{std::min(preferred.x, available.x), std::min(preferred.y, available.y)};
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x / 2, vp->WorkPos.y + vp->WorkSize.y / 2}, condition, {0.5f, 0.5f});
    ImGui::SetNextWindowSize(size, condition);
    ImGui::SetNextWindowSizeConstraints({std::min(280.0f, size.x), std::min(200.0f, size.y)}, available);
}
}
