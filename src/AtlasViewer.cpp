#include "AtlasViewer.h"
#include <SDL.h>
#include <SDL_image.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cctype>

// ImGui
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

// -----------------------------
// Small helpers
// -----------------------------
static inline void trimInPlace(std::string& s)
{
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
}

static std::string lowerCopy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// CSV split that respects quotes (colliders_json has commas)
static bool splitCSVLine(const std::string& line, std::vector<std::string>& out)
{
    out.clear();
    std::string cur;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') { inQuotes = !inQuotes; continue; }

        if (c == ',' && !inQuotes) {
            out.push_back(cur);
            cur.clear();
        }
        else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);

    for (auto& x : out) trimInPlace(x);
    return !out.empty();
}

static bool toIntSafe(const std::string& s, int& out)
{
    try {
        size_t idx = 0;
        out = std::stoi(s, &idx);
        return idx > 0 || (idx == 1 && s.size() == 1); // allow "0"
    }
    catch (...) {
        return false;
    }
}

// Escape CSV cell (quotes if needed; double quotes inside)
static std::string csvEscape(const std::string& v)
{
    bool needQuotes = false;
    for (char c : v) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') { needQuotes = true; break; }
    }
    if (!needQuotes) return v;

    std::string out;
    out.reserve(v.size() + 8);
    out.push_back('"');
    for (char c : v) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

// -----------------------------
// Data model: store row as named columns (strings)
// -----------------------------
struct AtlasRow
{
    // keep original columns as strings (easy to edit + roundtrip)
    std::string name;
    std::string index;
    std::string atlas_x;
    std::string atlas_y;
    std::string w;
    std::string h;
    std::string source_png;
    std::string group;
    std::string local_index;
    std::string collidable;
    std::string rotation;
    std::string type;
    std::string scale;
    std::string colliders_json;

    // runtime cache
    bool isEmptyTile = false; // computed (optional)
};

static bool loadAtlasCSV_Rows(const std::string& csvPath, std::vector<AtlasRow>& outRows)
{
    outRows.clear();

    std::ifstream f(csvPath);
    if (!f) {
        std::cerr << "AtlasViewer: cannot open CSV: " << csvPath << "\n";
        return false;
    }

    std::string headerLine;
    if (!std::getline(f, headerLine)) {
        std::cerr << "AtlasViewer: CSV is empty\n";
        return false;
    }

    std::vector<std::string> header;
    splitCSVLine(headerLine, header);

    auto findCol = [&](const char* n)->int {
        std::string nn = lowerCopy(n);
        for (int i = 0; i < (int)header.size(); ++i)
            if (lowerCopy(header[i]) == nn) return i;
        return -1;
        };

    // required columns
    const int cName = findCol("name");
    const int cIdx = findCol("index");
    int cX = findCol("atlas_x"); if (cX < 0) cX = findCol("x");
    int cY = findCol("atlas_y"); if (cY < 0) cY = findCol("y");
    const int cW = findCol("w");
    const int cH = findCol("h");
    const int cSrc = findCol("source_png");
    const int cGroup = findCol("group");
    const int cLocal = findCol("local_index");
    const int cCollidable = findCol("collidable");
    const int cRot = findCol("rotation");
    const int cType = findCol("type");
    const int cScale = findCol("scale");
    const int cColl = findCol("colliders_json");

    if (cName < 0 || cIdx < 0 || cX < 0 || cY < 0 || cW < 0 || cH < 0 || cSrc < 0) {
        std::cerr << "AtlasViewer: missing required columns in header\n";
        return false;
    }

    std::string line;
    std::vector<std::string> cols;

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (!splitCSVLine(line, cols)) continue;

        int need = std::max({ cName,cIdx,cX,cY,cW,cH,cSrc });
        if ((int)cols.size() <= need) continue;

        AtlasRow r;
        r.name = cols[cName];
        r.index = cols[cIdx];
        r.atlas_x = cols[cX];
        r.atlas_y = cols[cY];
        r.w = cols[cW];
        r.h = cols[cH];
        r.source_png = cols[cSrc];

        if (cGroup >= 0 && cGroup < (int)cols.size()) r.group = cols[cGroup];
        if (cLocal >= 0 && cLocal < (int)cols.size()) r.local_index = cols[cLocal];
        if (cCollidable >= 0 && cCollidable < (int)cols.size()) r.collidable = cols[cCollidable];
        if (cRot >= 0 && cRot < (int)cols.size()) r.rotation = cols[cRot];
        if (cType >= 0 && cType < (int)cols.size()) r.type = cols[cType];
        if (cScale >= 0 && cScale < (int)cols.size()) r.scale = cols[cScale];
        if (cColl >= 0 && cColl < (int)cols.size()) r.colliders_json = cols[cColl];

        outRows.push_back(std::move(r));
    }

    std::cout << "AtlasViewer: loaded rows=" << outRows.size() << "\n";
    return !outRows.empty();
}

static bool saveAtlasCSV_Rows(const std::string& outPath, const std::vector<AtlasRow>& rows)
{
    std::ofstream o(outPath);
    if (!o) return false;

    // fixed header order (your current file)
    o << "name,index,atlas_x,atlas_y,w,h,source_png,group,local_index,collidable,rotation,type,scale,colliders_json\n";

    for (const auto& r : rows) {
        o
            << csvEscape(r.name) << ","
            << csvEscape(r.index) << ","
            << csvEscape(r.atlas_x) << ","
            << csvEscape(r.atlas_y) << ","
            << csvEscape(r.w) << ","
            << csvEscape(r.h) << ","
            << csvEscape(r.source_png) << ","
            << csvEscape(r.group) << ","
            << csvEscape(r.local_index) << ","
            << csvEscape(r.collidable) << ","
            << csvEscape(r.rotation) << ","
            << csvEscape(r.type) << ","
            << csvEscape(r.scale) << ","
            << csvEscape(r.colliders_json)
            << "\n";
    }
    return true;
}

// -----------------------------
// Texture cache
// -----------------------------
static SDL_Texture* getTextureCached(SDL_Renderer* ren,
    const std::string& basePath,
    const std::string& pngName,
    std::unordered_map<std::string, SDL_Texture*>& cache)
{
    auto it = cache.find(pngName);
    if (it != cache.end()) return it->second;

    std::string full = basePath + "/" + pngName;
    SDL_Surface* surf = IMG_Load(full.c_str());
    if (!surf) {
        std::cerr << "IMG_Load failed: " << full << " : " << IMG_GetError() << "\n";
        cache[pngName] = nullptr;
        return nullptr;
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);

    if (!tex) {
        std::cerr << "CreateTexture failed: " << full << " : " << SDL_GetError() << "\n";
        cache[pngName] = nullptr;
        return nullptr;
    }

    cache[pngName] = tex;
    return tex;
}

// -----------------------------
// ImGui helpers for editing std::string
// -----------------------------
static bool InputTextStdString(const char* label, std::string& str, ImGuiInputTextFlags flags = 0)
{
    // NOTE: this is a small helper to edit std::string with ImGui
    // uses a fixed buffer (good enough for csv editing)
    char buf[4096];
    std::snprintf(buf, sizeof(buf), "%s", str.c_str());
    if (ImGui::InputText(label, buf, sizeof(buf), flags)) {
        str = buf;
        return true;
    }
    return false;
}

int RunAtlasViewer()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    int imgFlags = IMG_INIT_PNG;
    if ((IMG_Init(imgFlags) & imgFlags) != imgFlags) {
        std::cerr << "IMG_Init failed: " << IMG_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow(
        "AtlasViewer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1400, 900,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!win) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(win);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer2_Init(ren);

    // ---- configure paths here ----
    const std::string kBasePath = "assets/Tileset";       // PNGs folder
    const std::string kCSVPath = "assets/Tileset/atlas.csv";

    std::vector<AtlasRow> rows;
    if (!loadAtlasCSV_Rows(kCSVPath, rows)) {
        std::cerr << "AtlasViewer: atlas.csv load failed\n";
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    std::unordered_map<std::string, SDL_Texture*> texCache;

    int selected = 0;
    std::string filter;
    bool skipEmpty = false;

    char savePathBuf[512];
    std::snprintf(savePathBuf, sizeof(savePathBuf), "assets/Tileset/atlas_edited.csv");
    std::string saveStatus;

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        // ImGui frame
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Atlas Editor");

        // Top bar
        InputTextStdString("Search", filter);
        ImGui::SameLine();
        ImGui::Checkbox("Skip empty tiles (manual flag)", &skipEmpty);
        ImGui::TextDisabled("Arrows: browse selection | Edit in right panel | Save As to export.");

        // Layout: left list / right editor
        ImGui::Separator();
        ImGui::BeginChild("left", ImVec2(420, 0), true);

        // list
        int visibleCount = 0;
        for (int i = 0; i < (int)rows.size(); ++i) {
            const auto& r = rows[i];

            if (!filter.empty()) {
                std::string hay = lowerCopy(r.name + " " + r.group + " " + r.source_png);
                std::string needle = lowerCopy(filter);
                if (hay.find(needle) == std::string::npos) continue;
            }

            if (skipEmpty && r.group == "EMPTY") continue; // (zatím jen "manual" — mùžeš si tak oznaèit prázdné)

            visibleCount++;
            std::string label = r.name + "  [" + r.group + "]  (" + r.source_png + ")";
            if (ImGui::Selectable(label.c_str(), selected == i)) selected = i;
        }
        ImGui::Text("Visible: %d / %d", visibleCount, (int)rows.size());
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("right", ImVec2(0, 0), true);

        if (selected < 0) selected = 0;
        if (selected >= (int)rows.size()) selected = (int)rows.size() - 1;

        AtlasRow& r = rows[selected];

        ImGui::Text("Selected: %d / %d", selected + 1, (int)rows.size());

        // draw preview tile
        {
            int x = 0, y = 0, w = 0, h = 0;
            toIntSafe(r.atlas_x, x);
            toIntSafe(r.atlas_y, y);
            toIntSafe(r.w, w);
            toIntSafe(r.h, h);

            SDL_Texture* tex = getTextureCached(ren, kBasePath, r.source_png, texCache);

            if (tex && w > 0 && h > 0) {
                SDL_Rect src{ x, y, w, h };

                // render to screen via SDL later; in ImGui we can just show a note
                ImGui::Text("Preview: (rendered in SDL background)");
            }
            else {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Preview: texture missing or invalid rect");
            }
        }

        ImGui::Separator();

        // editable fields
        InputTextStdString("name", r.name);
        InputTextStdString("index", r.index);
        InputTextStdString("source_png", r.source_png);
        InputTextStdString("group", r.group);
        InputTextStdString("type", r.type);

        InputTextStdString("atlas_x", r.atlas_x);
        InputTextStdString("atlas_y", r.atlas_y);
        InputTextStdString("w", r.w);
        InputTextStdString("h", r.h);

        InputTextStdString("local_index", r.local_index);
        InputTextStdString("collidable", r.collidable);
        InputTextStdString("rotation", r.rotation);
        InputTextStdString("scale", r.scale);

        ImGui::Separator();
        ImGui::Text("colliders_json (can be long):");
        InputTextStdString("##colliders_json", r.colliders_json, ImGuiInputTextFlags_AllowTabInput);

        ImGui::Separator();

        // save
        ImGui::InputText("Save As", savePathBuf, sizeof(savePathBuf));
        if (ImGui::Button("Save CSV")) {
            if (saveAtlasCSV_Rows(savePathBuf, rows)) saveStatus = std::string("Saved OK: ") + savePathBuf;
            else saveStatus = std::string("SAVE FAILED: ") + savePathBuf;
        }
        ImGui::SameLine();
        if (ImGui::Button("Mark selected as EMPTY (group=EMPTY)")) {
            r.group = "EMPTY";
        }

        if (!saveStatus.empty()) ImGui::Text("%s", saveStatus.c_str());

        ImGui::EndChild();
        ImGui::End();

        // SDL background render (tile preview in center)
        SDL_SetRenderDrawColor(ren, 20, 20, 25, 255);
        SDL_RenderClear(ren);

        // draw selected tile big in center
        {
            AtlasRow& rr = rows[selected];
            int x = 0, y = 0, w = 0, h = 0;
            toIntSafe(rr.atlas_x, x);
            toIntSafe(rr.atlas_y, y);
            toIntSafe(rr.w, w);
            toIntSafe(rr.h, h);

            SDL_Texture* tex = getTextureCached(ren, kBasePath, rr.source_png, texCache);
            if (tex && w > 0 && h > 0) {
                SDL_Rect src{ x, y, w, h };

                int ww = 0, wh = 0;
                SDL_GetRendererOutputSize(ren, &ww, &wh);

                int scale = 10;
                int dstW = w * scale;
                int dstH = h * scale;

                SDL_Rect dst{ (ww - dstW) / 2, (wh - dstH) / 2, dstW, dstH };
                SDL_RenderCopy(ren, tex, &src, &dst);

                SDL_SetRenderDrawColor(ren, 255, 255, 255, 80);
                SDL_RenderDrawRect(ren, &dst);
            }
        }

        // ImGui draw
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);

        SDL_RenderPresent(ren);
    }

    // cleanup
    for (auto& kv : texCache) {
        if (kv.second) SDL_DestroyTexture(kv.second);
    }
    texCache.clear();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
