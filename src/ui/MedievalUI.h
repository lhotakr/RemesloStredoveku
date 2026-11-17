#pragma once

#include "imgui.h"
#include <SDL.h>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace medieval
{
enum class FontRole { Body, Heading, ChurchDocument, NobleDocument, Fallback, Count };
enum class Surface { Wood, Parchment, Stone, Church, Noble };
enum class DocumentKind { Letter, Church, Noble };

struct Palette
{
    static constexpr ImVec4 Ink{0.16f, 0.11f, 0.07f, 1.0f};
    static constexpr ImVec4 Ivory{0.94f, 0.89f, 0.76f, 1.0f};
    static constexpr ImVec4 Brass{0.57f, 0.46f, 0.27f, 1.0f};
    static constexpr ImVec4 MutedInk{0.36f, 0.29f, 0.21f, 1.0f};
};

struct Borders { float left = 0, top = 0, right = 0, bottom = 0; };
struct Slice { ImVec2 min, max, uvMin, uvMax; };
// Positions are local to the panel. Small destinations shrink borders together.
std::array<Slice, 9> NineSliceGeometry(ImVec2 size, ImVec2 textureSize, Borders borders, float scale);
struct Rect { ImVec2 pos, size; };
float LayoutScale(ImVec2 display);
Rect DialogueRect(ImVec2 workPos, ImVec2 workSize);

struct DialogueChoice { int id = -1; std::string label; };
struct DialogueView
{
    std::string speaker, text, portraitKey, nodeKey;
    int mood = -1;
    std::vector<DialogueChoice> choices;
};
struct DialogueAction { int choice = -1; bool close = false; };
struct Document
{
    std::string id, title, original, transcript;
    DocumentKind kind = DocumentKind::Letter;
};

class UI
{
public:
    static UI& Get();
    UI() = default;
    UI(const UI&) = delete;
    UI& operator=(const UI&) = delete;

    // One instance per game's ImGui context. Atlas owns fonts; UI owns SDL textures.
    void initialize(SDL_Renderer* renderer, const std::filesystem::path& projectRoot);
    void shutdown(); // Before destroying the ImGui context or SDL renderer.
    ImFont* font(FontRole role) const;
    float fontSize(FontRole role) const;
    float scale() const;
    size_t textureCount() const { return m_textures.size(); }
    bool hasTexture(const std::string& id) const;
    const std::vector<Document>& documents() const { return m_documents; }

    void panel(ImDrawList* draw, ImVec2 pos, ImVec2 size, Surface surface) const;
    bool button(const char* id, const std::string& label, float width = -1, bool selected = false, float minHeight = 0) const;
    // Shared decoration for interactive widgets and zoomable map controls.
    void buttonFrame(ImDrawList* draw, ImVec2 pos, ImVec2 size, bool hovered = false,
                     bool active = false, bool focused = false, bool selected = false, bool disabled = false) const;
    void heading(const std::string& label) const;
    void portrait(const std::string& key, const std::string& name, ImVec2 size) const;
    DialogueAction dialogue(const char* id, const DialogueView& view) const;
    // Shared by campaign, editor preview and the renderer tests; no gameplay effects.
    DialogueAction dialogueContents(const DialogueView& view) const;
    void openDocuments();
    void closeDocuments() { m_documentsOpen = false; }
    bool documentsOpen() const { return m_documentsOpen; }
    void renderDocuments();
    void documentContents(const Document& document, bool& transcript) const;

private:
    struct TextureDeleter { void operator()(SDL_Texture* p) const { SDL_DestroyTexture(p); } };
    struct Texture
    {
        std::unique_ptr<SDL_Texture, TextureDeleter> handle;
        ImVec2 size{};
        Borders borders;
        float borderScale = 1.0f;
    };
    void image(ImDrawList* draw, const std::string& id, ImVec2 pos, ImVec2 size, ImU32 tint = IM_COL32_WHITE) const;
    bool loadTexture(const std::string& id, const std::filesystem::path& path, Borders borders = {}, float borderScale = 1);
    void loadFonts(const std::filesystem::path& root);
    SDL_Renderer* m_renderer = nullptr;
    std::unordered_map<std::string, Texture> m_textures;
    std::unordered_map<std::string, std::string> m_portraitAliases;
    std::array<ImFont*, static_cast<size_t>(FontRole::Count)> m_fonts{};
    std::vector<Document> m_documents;
    bool m_documentsOpen = false, m_transcript = false;
    int m_documentIndex = 0;
};

// All ImGui stacks and window/child pairs are scoped, including clipped windows.
class Font
{
public:
    Font(const UI& ui, FontRole role);
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
};
class Style
{
public:
    Style(const UI& ui, Surface surface);
    ~Style();
    Style(const Style&) = delete;
    Style& operator=(const Style&) = delete;
};
class Window
{
public:
    Window(const UI& ui, const char* id, Surface surface, ImGuiWindowFlags flags = 0);
    ~Window();
    explicit operator bool() const { return m_visible; }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
private:
    Style m_style;
    Font m_font;
    bool m_visible;
};
class Child
{
public:
    Child(const UI& ui, const char* id, ImVec2 size, Surface surface);
    ~Child();
    explicit operator bool() const { return m_visible; }
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
private:
    Style m_style;
    bool m_visible;
};
// Header for a Window. Returns false after the user presses its close button.
bool WindowHeader(const UI& ui, const std::string& title, bool& open);
void PlaceWindow(ImVec2 preferredSize, ImGuiCond condition = ImGuiCond_Appearing);
}
