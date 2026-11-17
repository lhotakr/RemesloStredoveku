#include "ui/MedievalUI.h"
#include "game/DialogManager.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(std::string(__func__) + ": " + #expression); } while (false)
namespace fs = std::filesystem;
namespace
{
const fs::path Root = REMESLO_PROJECT_ROOT;
fs::path Capture;

struct Harness
{
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    medieval::UI ui;
    int width, height;
    explicit Harness(int w, int h, const fs::path& root = Root) : width(w), height(h)
    {
        surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
        CHECK(surface);
        renderer = SDL_CreateSoftwareRenderer(surface);
        CHECK(renderer);
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        CHECK(ImGui_ImplSDLRenderer2_Init(renderer));
        ui.initialize(renderer, root);
    }
    ~Harness()
    {
        ui.shutdown();
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(surface);
    }
    void frame(const std::function<void()>& content)
    {
        auto& io = ImGui::GetIO();
        io.DisplaySize = {static_cast<float>(width), static_cast<float>(height)};
        io.DeltaTime = 1.0f / 60;
        io.FontGlobalScale = 0.7f; // Match the campaign's legacy global setting.
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui::NewFrame();
        const auto colorCount = GImGui->ColorStack.Size, varCount = GImGui->StyleVarStack.Size;
        const auto fontCount = GImGui->FontStack.Size;
        const auto style = ImGui::GetStyle();
        content();
        CHECK(GImGui->ColorStack.Size == colorCount);
        CHECK(GImGui->StyleVarStack.Size == varCount);
        CHECK(GImGui->FontStack.Size == fontCount);
        CHECK(ImGui::GetStyle().Colors[ImGuiCol_Text].x == style.Colors[ImGuiCol_Text].x);
        CHECK(ImGui::GetStyle().WindowPadding.x == style.WindowPadding.x);
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 42, 48, 37, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
    }
    void capture(const std::string& name)
    {
        if (Capture.empty()) return;
        fs::create_directories(Capture);
        CHECK(SDL_SaveBMP(surface, (Capture / (name + ".bmp")).string().c_str()) == 0);
    }
    bool backgroundAt(int x, int y)
    {
        CHECK(SDL_LockSurface(surface) == 0);
        auto* pixel = reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels) + y * surface->pitch) + x;
        Uint8 r, g, b;
        SDL_GetRGB(*pixel, surface->format, &r, &g, &b);
        SDL_UnlockSurface(surface);
        return r == 42 && g == 48 && b == 37;
    }
};

void TestNineSliceAndLayout()
{
    const auto normal = medieval::NineSliceGeometry({800, 200}, {512, 512}, {24, 24, 24, 24}, 0.5f);
    CHECK(normal[0].max.x == 12 && normal[0].max.y == 12);
    CHECK(normal[8].min.x == 788 && normal[8].min.y == 188);
    for (const auto size : {ImVec2(8, 3), ImVec2(1920, 1080), ImVec2(0, 0)})
        for (const auto& slice : medieval::NineSliceGeometry(size, {512, 512}, {24, 24, 24, 24}, 1))
        {
            CHECK(slice.max.x >= slice.min.x && slice.max.y >= slice.min.y);
            CHECK(slice.uvMin.x >= 0 && slice.uvMax.x <= 1);
            CHECK(slice.uvMin.y >= 0 && slice.uvMax.y <= 1);
        }
    const auto invalid = medieval::NineSliceGeometry({100, 100}, {0, 0}, {}, 1);
    CHECK(invalid[0].max.x == 0);
    for (const auto size : {ImVec2(640, 480), ImVec2(1280, 720), ImVec2(1920, 1080), ImVec2(3440, 1440)})
    {
        const auto rect = medieval::DialogueRect({20, 10}, size);
        CHECK(rect.pos.x >= 20 && rect.pos.y >= 10);
        CHECK(rect.pos.x + rect.size.x <= 20 + size.x);
        CHECK(rect.pos.y + rect.size.y <= 10 + size.y);
        CHECK(rect.size.y < size.y * 0.6f);
    }
}

medieval::DialogueView Matej()
{
    DialogManager manager;
    std::string error;
    CHECK(manager.loadFromFile((Root / "data/dialogs/dialogs.json").string(), &error));
    const auto* dialog = manager.findDialog("matej_intro");
    CHECK(dialog);
    const auto* node = manager.findNode(*dialog, dialog->startNodeId);
    CHECK(node && node->choices.size() >= 2);
    medieval::DialogueView view;
    view.speaker = node->speaker;
    view.portraitKey = "matej_sedlak";
    view.text = node->text;
    view.nodeKey = node->id;
    view.mood = 50;
    // At the start of the actual quest, only choices without required flags show.
    for (size_t i = 0; i < node->choices.size(); ++i)
        if (node->choices[i].requireFlag.empty() && node->choices[i].requireFlags.empty())
            view.choices.push_back({static_cast<int>(i), node->choices[i].text});
    CHECK(view.choices.size() == 2);
    return view;
}

void TestFontsAndDialogue()
{
    const auto matej = Matej();
    for (const auto size : {ImVec2(640, 480), ImVec2(1280, 720), ImVec2(1920, 1080)})
    {
        Harness h(static_cast<int>(size.x), static_cast<int>(size.y));
        const auto count = ImGui::GetIO().Fonts->Sources.Size;
        const auto textures = h.ui.textureCount();
        auto content = [&]
        {
            for (int i = 0; i < static_cast<int>(medieval::FontRole::Count); ++i)
            {
                const auto role = static_cast<medieval::FontRole>(i);
                medieval::Font font(h.ui, role);
                for (char32_t cp : U"ĚěŠšČčŘřŽžÝýÁáÍíÉéŮůÚúŤťĎďŇňæœāēīōū")
                    if (cp) CHECK(ImGui::GetFontBaked()->FindGlyphNoFallback(static_cast<ImWchar>(cp)));
                if (role == medieval::FontRole::Body)
                    CHECK(std::abs(ImGui::GetFontSize() - 23 * medieval::LayoutScale(size)) <= 0.51f); // ImGui rounds to pixels.
            }
            const auto action = h.ui.dialogue("Matej##test", matej);
            CHECK(action.choice == -1 && !action.close);
        };
        h.frame(content);
        h.frame(content); // Exercise persistent state and texture updates.
        CHECK(ImGui::GetIO().Fonts->Sources.Size == count);
        CHECK(h.ui.textureCount() == textures);
        h.capture("matej-" + std::to_string(h.width) + "x" + std::to_string(h.height));
        if (h.width == 1280)
        {
            auto longView = matej;
            longView.text = std::string(60, ' ') + matej.text + "\n" + matej.text + "\n" + matej.text;
            longView.choices.clear();
            for (int i = 0; i < 30; ++i)
                longView.choices.push_back({i * 3, "Velmi dlouhá odpověď: " + matej.text});
            h.frame([&] { h.ui.dialogue("Matej##test", longView); });
            h.frame([&] { h.ui.dialogue("Matej##test", longView); });
            bool scrollable = false;
            for (auto* window : GImGui->Windows)
                if (std::string(window->Name).find("##choices") != std::string::npos && window->ScrollMax.y > 0)
                    scrollable = true;
            CHECK(scrollable);
            h.capture("matej-long-answers");
        }
    }
}

void TestButtonsAndDocuments()
{
    Harness h(1280, 720);
    bool pressed = false;
    ImVec2 center;
    const auto content = [&]
    {
        ImGui::SetNextWindowPos({40, 40}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({450, 200}, ImGuiCond_Always);
        medieval::Window window(h.ui, "Input test", medieval::Surface::Parchment);
        if (window)
        {
            pressed = h.ui.button("##answer", "Přijímám nabídku a děkuji za přístřeší.", -1, false, 70);
            const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            CHECK(b.y - a.y >= 70); // Main menu can request taller controls and keep wrapping/navigation.
            center = {(a.x + b.x) / 2, (a.y + b.y) / 2};
        }
    };
    h.frame(content);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(center.x, center.y);
    h.frame(content);
    io.AddMouseButtonEvent(0, true);
    h.frame(content);
    CHECK(!pressed);
    io.AddMouseButtonEvent(0, false);
    h.frame(content);
    CHECK(pressed);
    // Switch from mouse mode to keyboard navigation before activating with Enter.
    io.AddKeyEvent(ImGuiKey_Tab, true);
    h.frame(content);
    io.AddKeyEvent(ImGuiKey_Tab, false);
    h.frame(content);
    h.frame(content);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    h.frame(content);
    CHECK(pressed); // Native ImGui keyboard activation is retained.
    io.AddKeyEvent(ImGuiKey_Enter, false);
    h.frame(content);

    auto view = Matej();
    view.choices[0].id = 41; // Filtering must not change the original choice index.
    medieval::DialogueAction answer;
    const auto conversation = [&] { answer = h.ui.dialogue("Choice mapping test", view); };
    h.frame(conversation);
    h.frame(conversation);
    ImVec2 answerPos;
    bool foundAnswer = false;
    for (auto* window : GImGui->Windows)
        if (window->Active && std::string(window->Name).find("##choices") != std::string::npos)
        {
            answerPos = {window->DC.CursorStartPos.x + 30, window->DC.CursorStartPos.y + 16};
            foundAnswer = true;
        }
    CHECK(foundAnswer);
    io.AddMousePosEvent(answerPos.x, answerPos.y);
    h.frame(conversation);
    io.AddMouseButtonEvent(0, true);
    h.frame(conversation);
    io.AddMouseButtonEvent(0, false);
    h.frame(conversation);
    CHECK(answer.choice == 41 && !answer.close);
    CHECK(h.ui.documents().size() == 3);
    for (const auto& doc : h.ui.documents())
        for (bool readable : {false, true})
        {
            bool transcript = readable;
            const auto document = [&]
            {
                medieval::PlaceWindow({800, 600});
                medieval::Window window(h.ui, "Document test", medieval::Surface::Wood);
                if (window) h.ui.documentContents(doc, transcript);
            };
            h.frame(document);
            h.frame(document);
            h.capture(doc.id + (readable ? "-transcript" : "-original"));
            if (!readable && doc.kind == medieval::DocumentKind::Letter)
            {
                auto* window = ImGui::FindWindowByName("Document test");
                CHECK(window);
                io.AddMousePosEvent(window->DC.CursorStartPos.x + 80, window->DC.CursorStartPos.y + 52);
                h.frame(document);
                io.AddMouseButtonEvent(0, true);
                h.frame(document);
                io.AddMouseButtonEvent(0, false);
                h.frame(document);
                CHECK(transcript); // The actual reader button changes the displayed text/font role.
            }
        }
    const auto clipping = [&]
    {
        ImGui::SetNextWindowPos({40, 40}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({300, 180}, ImGuiCond_Always);
        medieval::Window window(h.ui, "Clipping test", medieval::Surface::Wood);
        if (window)
        {
            medieval::Child child(h.ui, "##oversized-child", {0, 400}, medieval::Surface::Parchment);
            if (child) ImGui::TextUnformatted("Text within the clipped parent.");
        }
    };
    h.frame(clipping);
    h.frame(clipping);
    CHECK(h.backgroundAt(100, 250)); // The child frame must not draw outside its parent.
    h.ui.openDocuments();
    CHECK(h.ui.documentsOpen());
    h.frame([&] { h.ui.renderDocuments(); });
    h.ui.closeDocuments();
    CHECK(!h.ui.documentsOpen());
    h.ui.shutdown();
    CHECK(h.ui.textureCount() == 0 && h.ui.documents().empty());
    h.ui.shutdown(); // Safe on repeated shutdown, as in Game's destructor.
}

void TestMissingAndCorruptAssets()
{
    const fs::path temp = fs::temp_directory_path() / ("remeslo-medieval-ui-" + std::to_string(SDL_GetPerformanceCounter()));
    fs::create_directories(temp / "data/ui");
    {
        std::ofstream config(temp / "data/ui/medieval_ui.json");
        config << R"({"textures":{"wood":{"path":"missing.png"},"stone":{"path":"broken.png"}},"portraits":{"matej_sedlak":"missing.png"}})";
        std::ofstream(temp / "broken.png") << "not a PNG";
    }
    {
        Harness h(800, 600, temp);
        CHECK(h.ui.textureCount() == 0);
        h.frame([&] { h.ui.dialogue("Fallback test", Matej()); });
        h.frame([&] { h.ui.dialogue("Fallback test", Matej()); });
        h.capture("missing-assets");
        h.ui.openDocuments();
        h.frame([&] { h.ui.renderDocuments(); }); // Empty document library is usable.
    }
    // Load a real bitmap as a portrait and retain Noto while all historical fonts
    // are absent. This exercises the successful loader and readable fallback too.
    {
        auto* bitmap = SDL_CreateRGBSurfaceWithFormat(0, 40, 60, 32, SDL_PIXELFORMAT_RGBA32);
        CHECK(bitmap);
        SDL_FillRect(bitmap, nullptr, SDL_MapRGB(bitmap->format, 74, 65, 51));
        CHECK(SDL_SaveBMP(bitmap, (temp / "portrait.bmp").string().c_str()) == 0);
        SDL_FreeSurface(bitmap);
        std::ofstream(temp / "data/ui/medieval_ui.json") <<
            R"({"textures":{"stone":{"path":"portrait.bmp"}},"portraits":{"matej_sedlak":"portrait.bmp"}})";
        fs::create_directories(temp / "assets/Fonts");
        fs::copy_file(Root / "assets/Fonts/NotoSans-Regular.ttf", temp / "assets/Fonts/NotoSans-Regular.ttf");
        Harness h(800, 600, temp);
        CHECK(h.ui.textureCount() == 2 && h.ui.hasTexture("portrait:matej_sedlak"));
        CHECK(h.ui.font(medieval::FontRole::ChurchDocument) == h.ui.font(medieval::FontRole::Fallback));
        const auto readable = [&]
        {
            medieval::Font font(h.ui, medieval::FontRole::Body);
            for (char32_t cp : U"ěščřžýáíéůúťďň")
                if (cp) CHECK(ImGui::GetFontBaked()->FindGlyphNoFallback(static_cast<ImWchar>(cp)));
            h.ui.dialogue("Portrait and fallback test", Matej());
        };
        h.frame(readable);
        h.frame(readable);
        h.ui.shutdown();
        CHECK(h.ui.textureCount() == 0);
    }
    std::ofstream(temp / "data/ui/medieval_ui.json") << "{broken json";
    {
        Harness h(800, 600, temp);
        h.frame([&] { h.ui.dialogue("Bad config test", Matej()); });
        CHECK(h.ui.textureCount() == 0);
    }
    fs::remove_all(temp);
}
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--capture") Capture = argv[2];
    try
    {
        CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
        CHECK((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) != 0);
        TestNineSliceAndLayout();
        TestFontsAndDialogue();
        TestButtonsAndDocuments();
        TestMissingAndCorruptAssets();
        IMG_Quit();
        SDL_Quit();
        std::cout << "Medieval UI: layout, Czech/Latin glyphs, SDL rendering, input, documents and missing-assets tests passed\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
