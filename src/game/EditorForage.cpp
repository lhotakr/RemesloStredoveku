#include "Editor.h"
#include "imgui.h"

#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <unordered_set>
#include <JsonUtils.h>

#include "PathUtils.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
    struct ForageSpriteDef
    {
        std::string id;
        std::string image;
        std::string category; // mushroom / herb / generic / ui
        int x = 0;
        int y = 0;
        int w = 32;
        int h = 32;
        int pivotX = 16;
        int pivotY = 32;
    };

    std::vector<ForageSpriteDef> g_forageSprites;
    int g_selectedForageSpriteIndex = -1;
    char g_spriteFilter[128] = "";
    char g_activeSheetName[128] = "Fungi.png";
    std::unordered_map<std::string, SDL_Texture*> g_forageTextures;
    std::unordered_map<std::string, ImVec2> g_forageTextureSizes;
    bool g_forageSpritesLoadedOnce = false;
    bool g_forageSpeciesLoadedOnce = false;
    bool g_sheetDragActive = false;
    ImVec2 g_sheetDragStart{};
    ImVec2 g_sheetDragCurrent{};

    static std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    static std::string Trim(const std::string& s)
    {
        size_t b = 0;
        size_t e = s.size();
        while (b < e && std::isspace((unsigned char)s[b])) ++b;
        while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
        return s.substr(b, e - b);
    }

    static bool ContainsNoCase(const std::string& haystack, const std::string& needle)
    {
        if (needle.empty()) return true;
        return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
    }

    static void CopyToBuf(char* dst, size_t dstSize, const std::string& src)
    {
        if (!dst || dstSize == 0) return;
        std::snprintf(dst, dstSize, "%s", src.c_str());
    }

    static std::string JoinCsv(const std::vector<std::string>& values)
    {
        std::string out;
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i > 0) out += ", ";
            out += values[i];
        }
        return out;
    }

    static std::vector<std::string> SplitCsv(const std::string& s)
    {
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            item = Trim(item);
            if (!item.empty())
                out.push_back(item);
        }
        return out;
    }

    static fs::path ForageDataDir()
    {
        return pathutils::DataDir() / "foraging";
    }

    static fs::path ForageAssetsDir()
    {
        return pathutils::ProjectRoot() / "assets" / "Foraging";
    }

    static std::vector<std::string> ListForageSheetImages()
    {
        std::vector<std::string> out;
        const fs::path dir = ForageAssetsDir();
        if (fs::exists(dir))
        {
            for (const auto& e : fs::directory_iterator(dir))
            {
                if (!e.is_regular_file())
                    continue;

                const std::string ext = ToLower(e.path().extension().string());
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp")
                    out.push_back(e.path().filename().string());
            }
        }

        std::sort(out.begin(), out.end());
        return out;
    }

    static const ForageSpriteDef* FindSpriteById(const std::string& id)
    {
        for (const auto& s : g_forageSprites)
        {
            if (s.id == id)
                return &s;
        }
        return nullptr;
    }

    static bool SpriteIdExists(const std::string& id)
    {
        return FindSpriteById(id) != nullptr;
    }

    static std::string MakeUniqueSpriteId(const std::string& prefix)
    {
        std::string clean = ToLower(prefix);
        for (char& c : clean)
        {
            if (!(std::isalnum((unsigned char)c) || c == '_'))
                c = '_';
        }
        if (clean.empty())
            clean = "forage_sprite";

        for (int i = 1; i < 10000; ++i)
        {
            char buf[256]{};
            std::snprintf(buf, sizeof(buf), "%s_%03d", clean.c_str(), i);
            if (!SpriteIdExists(buf))
                return buf;
        }

        return clean + "_x";
    }

    static SDL_Texture* LoadForageTexture(SDL_Renderer* renderer, const std::string& image)
    {
        if (!renderer || image.empty())
            return nullptr;

        auto it = g_forageTextures.find(image);
        if (it != g_forageTextures.end())
            return it->second;

        fs::path p = ForageAssetsDir() / image;
        SDL_Texture* tex = IMG_LoadTexture(renderer, p.string().c_str());
        if (!tex)
            return nullptr;

        int w = 0;
        int h = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);

        g_forageTextures[image] = tex;
        g_forageTextureSizes[image] = ImVec2((float)w, (float)h);
        return tex;
    }

    static void LoadAllReferencedForageTextures(SDL_Renderer* renderer)
    {
        for (const auto& image : ListForageSheetImages())
            LoadForageTexture(renderer, image);

        for (const auto& s : g_forageSprites)
            LoadForageTexture(renderer, s.image);
    }

    static bool LoadForageSprites(std::string& outStatus)
    {
        g_forageSprites.clear();
        fs::path p = ForageDataDir() / "forage_sprites.json";

        if (!fs::exists(p))
        {
            outStatus = "Forage sprites file not found. Use New sprite or Auto-slice.";
            g_selectedForageSpriteIndex = -1;
            return true;
        }

        json root;
        std::string err;
        if (!jsonutils::LoadJsonFileSafe(p.string(), root, err))
        {
            outStatus = "Forage sprites parse failed: " + err;
            return false;
        }

        if (!root.contains("sprites") || !root["sprites"].is_array())
        {
            outStatus = "Forage sprites missing 'sprites' array";
            return false;
        }

        for (const auto& js : root["sprites"])
        {
            ForageSpriteDef s;
            s.id = js.value("id", "");
            s.image = js.value("image", "");
            s.category = js.value("category", "");

            if (js.contains("src") && js["src"].is_object())
            {
                const auto& r = js["src"];
                s.x = r.value("x", 0);
                s.y = r.value("y", 0);
                s.w = r.value("w", 32);
                s.h = r.value("h", 32);
            }

            if (js.contains("pivot") && js["pivot"].is_object())
            {
                const auto& pv = js["pivot"];
                s.pivotX = pv.value("x", s.w / 2);
                s.pivotY = pv.value("y", s.h);
            }

            if (!s.id.empty() && !s.image.empty() && s.w > 0 && s.h > 0)
                g_forageSprites.push_back(std::move(s));
        }

        std::sort(g_forageSprites.begin(), g_forageSprites.end(),
            [](const ForageSpriteDef& a, const ForageSpriteDef& b) { return a.id < b.id; });

        g_selectedForageSpriteIndex = g_forageSprites.empty() ? -1 : 0;
        outStatus = "Forage sprites loaded.";
        return true;
    }

    static bool SaveForageSprites(std::string& outStatus)
    {
        fs::path p = ForageDataDir() / "forage_sprites.json";
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);

        json root;
        root["sprites"] = json::array();

        for (const auto& s : g_forageSprites)
        {
            if (s.id.empty() || s.image.empty() || s.w <= 0 || s.h <= 0)
                continue;

            json js;
            js["id"] = s.id;
            js["image"] = s.image;
            if (!s.category.empty())
                js["category"] = s.category;
            js["src"] = { {"x", s.x}, {"y", s.y}, {"w", s.w}, {"h", s.h} };
            js["pivot"] = { {"x", s.pivotX}, {"y", s.pivotY} };

            root["sprites"].push_back(std::move(js));
        }

        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            outStatus = "Forage sprites save failed: " + p.string();
            return false;
        }

        f << root.dump(2);
        outStatus = "Forage sprites saved.";
        return true;
    }

    static void DrawSpritePreview(SDL_Renderer* renderer, const std::string& spriteId, float maxW = 96.0f, float maxH = 96.0f)
    {
        const ForageSpriteDef* s = FindSpriteById(spriteId);
        if (!s)
        {
            ImGui::TextDisabled("No sprite");
            return;
        }

        SDL_Texture* tex = LoadForageTexture(renderer, s->image);
        if (!tex)
        {
            ImGui::TextDisabled("Texture missing: %s", s->image.c_str());
            return;
        }

        int tw = 1;
        int th = 1;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);

        const float scale = std::min(maxW / std::max(1, s->w), maxH / std::max(1, s->h));
        ImVec2 size(std::max(1.0f, s->w * scale), std::max(1.0f, s->h * scale));

        ImVec2 uv0((float)s->x / (float)tw, (float)s->y / (float)th);
        ImVec2 uv1((float)(s->x + s->w) / (float)tw, (float)(s->y + s->h) / (float)th);
        ImGui::Image((ImTextureID)tex, size, uv0, uv1);
    }

    static bool SpriteCombo(SDL_Renderer* renderer, const char* label, std::string& value)
    {
        bool changed = false;
        const char* current = value.empty() ? "<none>" : value.c_str();
        ImGui::PushID(&value);

        if (ImGui::BeginCombo(label, current))
        {
            if (ImGui::Selectable("<none>", value.empty()))
            {
                value.clear();
                changed = true;
            }

            for (const auto& s : g_forageSprites)
            {
                ImGui::PushID(s.id.c_str());
                const bool sel = (value == s.id);
                if (ImGui::Selectable(s.id.c_str(), sel))
                {
                    value = s.id;
                    changed = true;
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(s.id.c_str());
                    DrawSpritePreview(renderer, s.id, 128.0f, 128.0f);
                    ImGui::EndTooltip();
                }

                if (sel)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        ImGui::PopID();

        if (!value.empty())
        {
            ImGui::SameLine();
            DrawSpritePreview(renderer, value, 40.0f, 40.0f);
        }

        return changed;
    }

    static std::string FileStemLower(const std::string& image)
    {
        fs::path p(image);
        return ToLower(p.stem().string());
    }

    static bool AutoSliceTransparentSheet(const std::string& image, std::string& outStatus)
    {
        fs::path p = ForageAssetsDir() / image;
        SDL_Surface* surf = IMG_Load(p.string().c_str());
        if (!surf)
        {
            outStatus = "Auto-slice failed: cannot load " + p.string();
            return false;
        }

        SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(surf);
        if (!rgba)
        {
            outStatus = "Auto-slice failed: cannot convert image.";
            return false;
        }

        const int w = rgba->w;
        const int h = rgba->h;
        const int pitch = rgba->pitch;
        const unsigned char* pixels = static_cast<const unsigned char*>(rgba->pixels);

        std::vector<unsigned char> mask((size_t)w * (size_t)h, 0);
        int alphaPixels = 0;

        for (int y = 0; y < h; ++y)
        {
            const unsigned char* row = pixels + y * pitch;
            for (int x = 0; x < w; ++x)
            {
                const unsigned char a = row[x * 4 + 3];
                if (a > 16)
                {
                    mask[(size_t)y * w + x] = 1;
                    ++alphaPixels;
                }
            }
        }

        if (alphaPixels >= (w * h * 7) / 10)
        {
            SDL_FreeSurface(rgba);
            outStatus = "Auto-slice skipped: sheet seems opaque. Use drag selection for checkerboard sheets.";
            return false;
        }

        std::vector<unsigned char> visited((size_t)w * (size_t)h, 0);
        std::vector<SDL_Rect> boxes;
        std::queue<int> q;

        const int dx[4] = { 1, -1, 0, 0 };
        const int dy[4] = { 0, 0, 1, -1 };

        for (int sy = 0; sy < h; ++sy)
        {
            for (int sx = 0; sx < w; ++sx)
            {
                const int start = sy * w + sx;
                if (!mask[start] || visited[start])
                    continue;

                visited[start] = 1;
                q.push(start);

                int minX = sx;
                int maxX = sx;
                int minY = sy;
                int maxY = sy;
                int area = 0;

                while (!q.empty())
                {
                    const int idx = q.front();
                    q.pop();

                    const int x = idx % w;
                    const int y = idx / w;
                    ++area;

                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);

                    for (int k = 0; k < 4; ++k)
                    {
                        const int nx = x + dx[k];
                        const int ny = y + dy[k];
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                            continue;

                        const int nidx = ny * w + nx;
                        if (!mask[nidx] || visited[nidx])
                            continue;

                        visited[nidx] = 1;
                        q.push(nidx);
                    }
                }

                if (area < 120)
                    continue;

                const int pad = 3;
                minX = std::max(0, minX - pad);
                minY = std::max(0, minY - pad);
                maxX = std::min(w - 1, maxX + pad);
                maxY = std::min(h - 1, maxY + pad);

                SDL_Rect r{ minX, minY, maxX - minX + 1, maxY - minY + 1 };
                if (r.w >= 8 && r.h >= 8)
                    boxes.push_back(r);
            }
        }

        SDL_FreeSurface(rgba);

        std::sort(boxes.begin(), boxes.end(),
            [](const SDL_Rect& a, const SDL_Rect& b)
            {
                if (std::abs(a.y - b.y) > 20)
                    return a.y < b.y;
                return a.x < b.x;
            });

        const std::string prefix = FileStemLower(image);
        const std::string category = (prefix.find("herb") != std::string::npos) ? "herb" : "mushroom";
        int added = 0;

        for (const SDL_Rect& b : boxes)
        {
            ForageSpriteDef s;
            s.id = MakeUniqueSpriteId(prefix);
            s.image = image;
            s.category = category;
            s.x = b.x;
            s.y = b.y;
            s.w = b.w;
            s.h = b.h;
            s.pivotX = b.w / 2;
            s.pivotY = b.h;
            g_forageSprites.push_back(std::move(s));
            ++added;
        }

        g_selectedForageSpriteIndex = g_forageSprites.empty() ? -1 : (int)g_forageSprites.size() - 1;
        outStatus = "Auto-sliced " + std::to_string(added) + " sprites from " + image + ".";
        return true;
    }

    static void RenderSelectedSpritePreview(SDL_Renderer* renderer, const ForageSpriteDef& s)
    {
        SDL_Texture* tex = LoadForageTexture(renderer, s.image);
        if (!tex)
        {
            ImGui::TextDisabled("Texture missing: %s", s.image.c_str());
            return;
        }

        int tw = 1;
        int th = 1;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);

        ImVec2 uv0((float)s.x / (float)tw, (float)s.y / (float)th);
        ImVec2 uv1((float)(s.x + s.w) / (float)tw, (float)(s.y + s.h) / (float)th);
        const float scale = std::min(220.0f / std::max(1, s.w), 180.0f / std::max(1, s.h));
        ImGui::Image((ImTextureID)tex, ImVec2(s.w * scale, s.h * scale), uv0, uv1);
    }

    static void RenderSpriteSheetCanvas(SDL_Renderer* renderer)
    {
        SDL_Texture* tex = LoadForageTexture(renderer, g_activeSheetName);
        if (!tex)
        {
            ImGui::TextDisabled("Put PNG sheets into assets/Foraging first.");
            return;
        }

        int tw = 1;
        int th = 1;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);

        const float availW = std::max(320.0f, ImGui::GetContentRegionAvail().x);
        const float scale = std::min(1.0f, availW / (float)tw);
        const ImVec2 canvasSize((float)tw * scale, (float)th * scale);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + canvasSize.x, p0.y + canvasSize.y);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddImage((ImTextureID)tex, p0, p1);

        ImGui::InvisibleButton("##forage_sheet_canvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft);
        const bool hovered = ImGui::IsItemHovered();

        auto screenToImage = [&](ImVec2 p) -> ImVec2
        {
            float x = (p.x - p0.x) / std::max(0.0001f, scale);
            float y = (p.y - p0.y) / std::max(0.0001f, scale);
            x = std::clamp(x, 0.0f, (float)tw);
            y = std::clamp(y, 0.0f, (float)th);
            return ImVec2(x, y);
        };

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            g_sheetDragActive = true;
            g_sheetDragStart = ImGui::GetIO().MousePos;
            g_sheetDragCurrent = g_sheetDragStart;
        }

        if (g_sheetDragActive)
        {
            g_sheetDragCurrent = ImGui::GetIO().MousePos;
            ImVec2 a(std::min(g_sheetDragStart.x, g_sheetDragCurrent.x), std::min(g_sheetDragStart.y, g_sheetDragCurrent.y));
            ImVec2 b(std::max(g_sheetDragStart.x, g_sheetDragCurrent.x), std::max(g_sheetDragStart.y, g_sheetDragCurrent.y));
            a.x = std::clamp(a.x, p0.x, p1.x);
            a.y = std::clamp(a.y, p0.y, p1.y);
            b.x = std::clamp(b.x, p0.x, p1.x);
            b.y = std::clamp(b.y, p0.y, p1.y);

            dl->AddRectFilled(a, b, IM_COL32(80, 180, 255, 45));
            dl->AddRect(a, b, IM_COL32(80, 220, 255, 255), 0.0f, 0, 2.0f);

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                g_sheetDragActive = false;
                ImVec2 ia = screenToImage(a);
                ImVec2 ib = screenToImage(b);

                const int rx = (int)std::floor(std::min(ia.x, ib.x));
                const int ry = (int)std::floor(std::min(ia.y, ib.y));
                const int rw = (int)std::ceil(std::abs(ib.x - ia.x));
                const int rh = (int)std::ceil(std::abs(ib.y - ia.y));

                if (rw >= 4 && rh >= 4)
                {
                    if (g_selectedForageSpriteIndex >= 0 && g_selectedForageSpriteIndex < (int)g_forageSprites.size())
                    {
                        auto& s = g_forageSprites[g_selectedForageSpriteIndex];
                        s.image = g_activeSheetName;
                        s.x = rx;
                        s.y = ry;
                        s.w = rw;
                        s.h = rh;
                        s.pivotX = rw / 2;
                        s.pivotY = rh;
                    }
                    else
                    {
                        ForageSpriteDef s;
                        s.id = MakeUniqueSpriteId(FileStemLower(g_activeSheetName));
                        s.image = g_activeSheetName;
                        s.category = ToLower(g_activeSheetName).find("herb") != std::string::npos ? "herb" : "mushroom";
                        s.x = rx;
                        s.y = ry;
                        s.w = rw;
                        s.h = rh;
                        s.pivotX = rw / 2;
                        s.pivotY = rh;
                        g_forageSprites.push_back(std::move(s));
                        g_selectedForageSpriteIndex = (int)g_forageSprites.size() - 1;
                    }
                }
            }
        }

        // Existing rects for this sheet.
        for (int i = 0; i < (int)g_forageSprites.size(); ++i)
        {
            const auto& s = g_forageSprites[i];
            if (s.image != g_activeSheetName)
                continue;

            ImVec2 a(p0.x + s.x * scale, p0.y + s.y * scale);
            ImVec2 b(p0.x + (s.x + s.w) * scale, p0.y + (s.y + s.h) * scale);
            const bool selected = (i == g_selectedForageSpriteIndex);
            dl->AddRect(a, b, selected ? IM_COL32(255, 220, 80, 255) : IM_COL32(120, 255, 160, 180), 0.0f, 0, selected ? 2.5f : 1.0f);
        }
    }

    static void RenderForageSpriteEditor(SDL_Renderer* renderer, std::string& status)
    {
        if (!g_forageSpritesLoadedOnce)
        {
            LoadForageSprites(status);
            g_forageSpritesLoadedOnce = true;
        }

        LoadAllReferencedForageTextures(renderer);

        if (ImGui::Button("Load sprites"))
            LoadForageSprites(status);
        ImGui::SameLine();
        if (ImGui::Button("Save sprites"))
            SaveForageSprites(status);
        ImGui::SameLine();
        if (ImGui::Button("New sprite"))
        {
            ForageSpriteDef s;
            s.id = MakeUniqueSpriteId(FileStemLower(g_activeSheetName));
            s.image = g_activeSheetName;
            s.category = ToLower(g_activeSheetName).find("herb") != std::string::npos ? "herb" : "mushroom";
            g_forageSprites.push_back(std::move(s));
            g_selectedForageSpriteIndex = (int)g_forageSprites.size() - 1;
            status = "Forage sprite created. Drag on spritesheet to set src.";
        }

        ImGui::SameLine();
        if (ImGui::Button("Duplicate"))
        {
            if (g_selectedForageSpriteIndex >= 0 && g_selectedForageSpriteIndex < (int)g_forageSprites.size())
            {
                ForageSpriteDef s = g_forageSprites[g_selectedForageSpriteIndex];
                s.id = MakeUniqueSpriteId(s.id);
                g_forageSprites.push_back(std::move(s));
                g_selectedForageSpriteIndex = (int)g_forageSprites.size() - 1;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            if (g_selectedForageSpriteIndex >= 0 && g_selectedForageSpriteIndex < (int)g_forageSprites.size())
            {
                g_forageSprites.erase(g_forageSprites.begin() + g_selectedForageSpriteIndex);
                if (g_forageSprites.empty())
                    g_selectedForageSpriteIndex = -1;
                else if (g_selectedForageSpriteIndex >= (int)g_forageSprites.size())
                    g_selectedForageSpriteIndex = (int)g_forageSprites.size() - 1;
            }
        }

        const auto sheets = ListForageSheetImages();
        if (ImGui::BeginCombo("Active sheet", g_activeSheetName[0] ? g_activeSheetName : "<none>"))
        {
            for (const auto& img : sheets)
            {
                const bool sel = (img == g_activeSheetName);
                if (ImGui::Selectable(img.c_str(), sel))
                    CopyToBuf(g_activeSheetName, sizeof(g_activeSheetName), img);
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Auto-slice alpha sheet"))
        {
            AutoSliceTransparentSheet(g_activeSheetName, status);
        }

        ImGui::TextDisabled("Transparent sheets can be auto-sliced. Checkerboard/opaque sheets use drag selection.");
        ImGui::Separator();

        ImGui::Columns(2, "forage_sprite_cols", true);
        ImGui::SetColumnWidth(0, 280.0f);

        ImGui::InputText("Filter", g_spriteFilter, IM_ARRAYSIZE(g_spriteFilter));
        ImGui::BeginChild("forage_sprite_list", ImVec2(0, 360), true);
        for (int i = 0; i < (int)g_forageSprites.size(); ++i)
        {
            const auto& s = g_forageSprites[i];
            if (!ContainsNoCase(s.id, g_spriteFilter) && !ContainsNoCase(s.image, g_spriteFilter) && !ContainsNoCase(s.category, g_spriteFilter))
                continue;

            std::string label = s.id + "  [" + s.image + "]";
            const bool sel = (i == g_selectedForageSpriteIndex);
            if (ImGui::Selectable(label.c_str(), sel))
            {
                g_selectedForageSpriteIndex = i;
                CopyToBuf(g_activeSheetName, sizeof(g_activeSheetName), s.image);
            }
        }
        ImGui::EndChild();

        ImGui::NextColumn();

        if (g_selectedForageSpriteIndex >= 0 && g_selectedForageSpriteIndex < (int)g_forageSprites.size())
        {
            auto& s = g_forageSprites[g_selectedForageSpriteIndex];

            char idBuf[128]{};
            CopyToBuf(idBuf, sizeof(idBuf), s.id);
            if (ImGui::InputText("Sprite ID", idBuf, IM_ARRAYSIZE(idBuf)))
                s.id = idBuf;

            char imgBuf[128]{};
            CopyToBuf(imgBuf, sizeof(imgBuf), s.image);
            if (ImGui::InputText("Image", imgBuf, IM_ARRAYSIZE(imgBuf)))
                s.image = imgBuf;

            char catBuf[64]{};
            CopyToBuf(catBuf, sizeof(catBuf), s.category);
            if (ImGui::InputText("Category", catBuf, IM_ARRAYSIZE(catBuf)))
                s.category = catBuf;

            ImGui::InputInt("src x", &s.x);
            ImGui::InputInt("src y", &s.y);
            ImGui::InputInt("src w", &s.w);
            ImGui::InputInt("src h", &s.h);
            ImGui::InputInt("pivot x", &s.pivotX);
            ImGui::InputInt("pivot y", &s.pivotY);

            ImGui::TextUnformatted("Preview:");
            RenderSelectedSpritePreview(renderer, s);
        }
        else
        {
            ImGui::TextDisabled("No forage sprite selected.");
        }

        ImGui::Columns(1);
        ImGui::Separator();
        RenderSpriteSheetCanvas(renderer);
    }

    static bool LoadSpeciesVector(std::vector<EditorForageSpecies>& out, std::string& status)
    {
        out.clear();
        fs::path p = ForageDataDir() / "forage_species.json";

        if (!fs::exists(p))
        {
            status = "Forage species file not found.";
            return true;
        }

        json root;
        std::string err;
        if (!jsonutils::LoadJsonFileSafe(p.string(), root, err))
        {
            status = "Forage species parse failed: " + err;
            return false;
        }

        if (!root.contains("species") || !root["species"].is_array())
        {
            status = "Forage species missing 'species' array";
            return false;
        }

        for (const auto& js : root["species"])
        {
            EditorForageSpecies s;
            s.id = js.value("id", "");
            s.archetypeId = js.value("archetype_id", "");
            s.trueName = js.value("true_name", "");
            s.detailSprite = js.value("detail_sprite", "");
            s.inventorySprite = js.value("inventory_sprite", "");
            s.herbariumSprite = js.value("herbarium_sprite", "");
            s.difficulty = std::clamp(js.value("difficulty", 0), -100, 100);
            s.description = js.value("description", js.value("free_description", js.value("identification_description", "")));
            s.edibility = js.value("edibility", "unknown_safe");
            s.medicinalValue = js.value("medicinal_value", "none");
            s.toxicityLevel = js.value("toxicity_level", 0);
            s.weight = js.value("weight", js.value("weight_kg", 0.0f));
            s.volume = js.value("volume", js.value("volume_l", 0.0f));
            s.maxStack = std::clamp(js.value("max_stack", js.value("maxStack", 64)), 1, 64);

            if (js.contains("folk_names") && js["folk_names"].is_array())
            {
                for (const auto& v : js["folk_names"])
                    if (v.is_string()) s.folkNames.push_back(v.get<std::string>());
            }
            if (js.contains("effects_on_eat") && js["effects_on_eat"].is_array())
            {
                for (const auto& v : js["effects_on_eat"])
                    if (v.is_string()) s.effectsOnEat.push_back(v.get<std::string>());
            }
            if (js.contains("effects_on_use") && js["effects_on_use"].is_array())
            {
                for (const auto& v : js["effects_on_use"])
                    if (v.is_string()) s.effectsOnUse.push_back(v.get<std::string>());
            }
            if (js.contains("season") && js["season"].is_array())
            {
                for (const auto& v : js["season"])
                    if (v.is_string()) s.season.push_back(v.get<std::string>());
            }
            if (js.contains("traits") && js["traits"].is_object())
            {
                for (auto it = js["traits"].begin(); it != js["traits"].end(); ++it)
                {
                    std::vector<std::string> vals;
                    if (it.value().is_array())
                    {
                        for (const auto& v : it.value())
                            if (v.is_string()) vals.push_back(v.get<std::string>());
                    }
                    s.traits[it.key()] = std::move(vals);
                }
            }

            if (!s.id.empty())
                out.push_back(std::move(s));
        }

        status = "Forage species loaded.";
        return true;
    }

    static bool SaveSpeciesVector(const std::vector<EditorForageSpecies>& species, std::string& status)
    {
        fs::path p = ForageDataDir() / "forage_species.json";
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);

        json root;
        root["species"] = json::array();

        for (const auto& s : species)
        {
            if (s.id.empty())
                continue;

            json js;
            js["id"] = s.id;
            js["archetype_id"] = s.archetypeId;
            js["true_name"] = s.trueName;
            js["folk_names"] = s.folkNames;
            js["detail_sprite"] = s.detailSprite;
            js["inventory_sprite"] = s.inventorySprite;
            js["herbarium_sprite"] = s.herbariumSprite;
            js["difficulty"] = s.difficulty;
            if (!s.description.empty())
                js["description"] = s.description;
            js["edibility"] = s.edibility;
            js["medicinal_value"] = s.medicinalValue;
            js["toxicity_level"] = s.toxicityLevel;
            if (s.weight > 0.0f) js["weight"] = s.weight;
            if (s.volume > 0.0f) js["volume"] = s.volume;
            js["max_stack"] = std::clamp(s.maxStack, 1, 64);
            js["effects_on_eat"] = s.effectsOnEat;
            js["effects_on_use"] = s.effectsOnUse;
            js["season"] = s.season;

            js["traits"] = json::object();
            for (const auto& kv : s.traits)
                js["traits"][kv.first] = kv.second;

            root["species"].push_back(std::move(js));
        }

        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            status = "Forage species save failed: " + p.string();
            return false;
        }

        f << root.dump(2);
        status = "Forage species saved.";
        return true;
    }

    static void RenderArchetypeEditor(SDL_Renderer* renderer, std::vector<EditorForageArchetype>& archetypes, int& selectedIndex, std::string& status)
    {
        if (ImGui::Button("Load archetypes##inner"))
            status = "Use main Load archetypes button in this tab.";
        ImGui::SameLine();
        if (ImGui::Button("New archetype"))
        {
            EditorForageArchetype a;
            a.id = "forage_archetype_" + std::to_string((int)archetypes.size() + 1);
            a.category = "mushroom";
            a.displayUnknown = "neznámá houba";
            a.displayPartial = "houba";
            archetypes.push_back(std::move(a));
            selectedIndex = (int)archetypes.size() - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete archetype"))
        {
            if (selectedIndex >= 0 && selectedIndex < (int)archetypes.size())
            {
                archetypes.erase(archetypes.begin() + selectedIndex);
                if (archetypes.empty()) selectedIndex = -1;
                else if (selectedIndex >= (int)archetypes.size()) selectedIndex = (int)archetypes.size() - 1;
            }
        }

        ImGui::Columns(2, "forage_archetype_cols", true);
        ImGui::SetColumnWidth(0, 260.0f);

        ImGui::BeginChild("forage_archetype_list", ImVec2(0, 360), true);
        for (int i = 0; i < (int)archetypes.size(); ++i)
        {
            ImGui::PushID(i);
            const bool sel = (i == selectedIndex);
            std::string label = archetypes[i].id + " [" + archetypes[i].category + "]";
            if (ImGui::Selectable(label.c_str(), sel))
                selectedIndex = i;
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::NextColumn();
        if (selectedIndex >= 0 && selectedIndex < (int)archetypes.size())
        {
            auto& a = archetypes[selectedIndex];

            char buf[512]{};
            CopyToBuf(buf, sizeof(buf), a.id);
            if (ImGui::InputText("ID", buf, IM_ARRAYSIZE(buf))) a.id = buf;
            CopyToBuf(buf, sizeof(buf), a.category);
            if (ImGui::InputText("Category", buf, IM_ARRAYSIZE(buf))) a.category = buf;
            CopyToBuf(buf, sizeof(buf), a.displayUnknown);
            if (ImGui::InputText("Display unknown", buf, IM_ARRAYSIZE(buf))) a.displayUnknown = buf;
            CopyToBuf(buf, sizeof(buf), a.displayPartial);
            if (ImGui::InputText("Display partial", buf, IM_ARRAYSIZE(buf))) a.displayPartial = buf;

            SpriteCombo(renderer, "Generic map sprite", a.genericMapSprite);
            ImGui::SliderFloat("Map scale", &a.mapScale, 0.05f, 2.0f, "%.2f");
            ImGui::InputFloat("Weight kg", &a.weight, 0.01f, 0.10f, "%.3f");
            ImGui::InputFloat("Volume l", &a.volume, 0.01f, 0.10f, "%.3f");
            ImGui::InputInt("Max stack", &a.maxStack);
            a.maxStack = std::clamp(a.maxStack, 1, 64);
            SpriteCombo(renderer, "Detail placeholder", a.detailPlaceholderSprite);
            SpriteCombo(renderer, "Herbarium placeholder", a.herbariumPlaceholderSprite);

            std::string slots = JoinCsv(a.examinationSlots);
            CopyToBuf(buf, sizeof(buf), slots);
            if (ImGui::InputText("Examination slots CSV", buf, IM_ARRAYSIZE(buf)))
                a.examinationSlots = SplitCsv(buf);
        }
        else
        {
            ImGui::TextDisabled("No archetype selected.");
        }

        ImGui::Columns(1);
    }

    static void ArchetypeCombo(const char* label, const std::vector<EditorForageArchetype>& archetypes, std::string& value)
    {
        const char* current = value.empty() ? "<none>" : value.c_str();
        if (ImGui::BeginCombo(label, current))
        {
            if (ImGui::Selectable("<none>", value.empty()))
                value.clear();
            for (const auto& a : archetypes)
            {
                const bool sel = value == a.id;
                if (ImGui::Selectable(a.id.c_str(), sel))
                    value = a.id;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    static void RenderSpeciesEditor(SDL_Renderer* renderer, std::vector<EditorForageSpecies>& species, int& selectedIndex, const std::vector<EditorForageArchetype>& archetypes, std::string& status)
    {
        if (!g_forageSpeciesLoadedOnce)
        {
            LoadSpeciesVector(species, status);
            g_forageSpeciesLoadedOnce = true;
        }

        if (ImGui::Button("Load species"))
            LoadSpeciesVector(species, status);
        ImGui::SameLine();
        if (ImGui::Button("Save species"))
            SaveSpeciesVector(species, status);
        ImGui::SameLine();
        if (ImGui::Button("New species"))
        {
            EditorForageSpecies s;
            s.id = "species_" + std::to_string((int)species.size() + 1);
            if (!archetypes.empty())
                s.archetypeId = archetypes.front().id;
            s.trueName = "Nový druh";
            species.push_back(std::move(s));
            selectedIndex = (int)species.size() - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete species"))
        {
            if (selectedIndex >= 0 && selectedIndex < (int)species.size())
            {
                species.erase(species.begin() + selectedIndex);
                if (species.empty()) selectedIndex = -1;
                else if (selectedIndex >= (int)species.size()) selectedIndex = (int)species.size() - 1;
            }
        }

        ImGui::Columns(2, "forage_species_cols", true);
        ImGui::SetColumnWidth(0, 290.0f);
        ImGui::BeginChild("forage_species_list", ImVec2(0, 360), true);
        for (int i = 0; i < (int)species.size(); ++i)
        {
            ImGui::PushID(i);
            const auto& s = species[i];
            std::string label = s.id;
            if (!s.trueName.empty()) label += " - " + s.trueName;
            const bool sel = (i == selectedIndex);
            if (ImGui::Selectable(label.c_str(), sel))
                selectedIndex = i;
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::NextColumn();
        if (selectedIndex >= 0 && selectedIndex < (int)species.size())
        {
            auto& s = species[selectedIndex];
            char buf[512]{};

            CopyToBuf(buf, sizeof(buf), s.id);
            if (ImGui::InputText("ID", buf, IM_ARRAYSIZE(buf))) s.id = buf;
            ArchetypeCombo("Archetype", archetypes, s.archetypeId);
            CopyToBuf(buf, sizeof(buf), s.trueName);
            if (ImGui::InputText("True name", buf, IM_ARRAYSIZE(buf))) s.trueName = buf;

            std::string folk = JoinCsv(s.folkNames);
            CopyToBuf(buf, sizeof(buf), folk);
            if (ImGui::InputText("Folk names CSV", buf, IM_ARRAYSIZE(buf))) s.folkNames = SplitCsv(buf);

            SpriteCombo(renderer, "Detail sprite", s.detailSprite);
            SpriteCombo(renderer, "Inventory sprite", s.inventorySprite);
            SpriteCombo(renderer, "Herbarium sprite", s.herbariumSprite);

            ImGui::SliderInt("Difficulty / commonness (-100 rare, 0 normal, +100 common)", &s.difficulty, -100, 100);

            CopyToBuf(buf, sizeof(buf), s.description);
            if (ImGui::InputTextMultiline("Free description", buf, IM_ARRAYSIZE(buf), ImVec2(-1.0f, 72.0f)))
                s.description = buf;

            CopyToBuf(buf, sizeof(buf), s.edibility);
            if (ImGui::InputText("Edibility", buf, IM_ARRAYSIZE(buf))) s.edibility = buf;
            CopyToBuf(buf, sizeof(buf), s.medicinalValue);
            if (ImGui::InputText("Medicinal value", buf, IM_ARRAYSIZE(buf))) s.medicinalValue = buf;
            ImGui::InputInt("Toxicity", &s.toxicityLevel);
            ImGui::InputFloat("Weight kg (0 = archetype)", &s.weight, 0.01f, 0.10f, "%.3f");
            ImGui::InputFloat("Volume l (0 = archetype)", &s.volume, 0.01f, 0.10f, "%.3f");
            ImGui::InputInt("Max stack", &s.maxStack);
            s.maxStack = std::clamp(s.maxStack, 1, 64);

            std::string seasons = JoinCsv(s.season);
            CopyToBuf(buf, sizeof(buf), seasons);
            if (ImGui::InputText("Season CSV", buf, IM_ARRAYSIZE(buf))) s.season = SplitCsv(buf);

            std::string eat = JoinCsv(s.effectsOnEat);
            CopyToBuf(buf, sizeof(buf), eat);
            if (ImGui::InputText("Effects eat CSV", buf, IM_ARRAYSIZE(buf))) s.effectsOnEat = SplitCsv(buf);

            std::string use = JoinCsv(s.effectsOnUse);
            CopyToBuf(buf, sizeof(buf), use);
            if (ImGui::InputText("Effects use CSV", buf, IM_ARRAYSIZE(buf))) s.effectsOnUse = SplitCsv(buf);

            if (ImGui::TreeNode("Traits / identification signs"))
            {
                ImGui::TextDisabled("Only filled trait fields are shown in the gameplay identification minigame.");

                const char* keys[] = { "cap", "stem", "gills", "smell", "habitat", "flower", "leaf", "root" };
                for (const char* key : keys)
                {
                    std::string vals = JoinCsv(s.traits[key]);
                    CopyToBuf(buf, sizeof(buf), vals);
                    if (ImGui::InputText(key, buf, IM_ARRAYSIZE(buf)))
                        s.traits[key] = SplitCsv(buf);
                }

                ImGui::Separator();
                ImGui::TextUnformatted("Custom free trait fields");

                std::vector<std::string> fixedKeys(std::begin(keys), std::end(keys));
                std::vector<std::string> customKeys;
                for (const auto& kv : s.traits)
                {
                    if (std::find(fixedKeys.begin(), fixedKeys.end(), kv.first) == fixedKeys.end())
                        customKeys.push_back(kv.first);
                }
                std::sort(customKeys.begin(), customKeys.end());

                for (const auto& key : customKeys)
                {
                    ImGui::PushID(key.c_str());
                    std::string vals = JoinCsv(s.traits[key]);
                    CopyToBuf(buf, sizeof(buf), vals);
                    if (ImGui::InputText(key.c_str(), buf, IM_ARRAYSIZE(buf)))
                        s.traits[key] = SplitCsv(buf);
                    ImGui::SameLine();
                    if (ImGui::Button("Delete"))
                    {
                        s.traits.erase(key);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }

                static char newTraitKey[64] = "";
                ImGui::InputText("New trait key", newTraitKey, IM_ARRAYSIZE(newTraitKey));
                ImGui::SameLine();
                if (ImGui::Button("Add trait key") && newTraitKey[0] != '\0')
                {
                    std::string key = Trim(newTraitKey);
                    if (!key.empty() && !s.traits.contains(key))
                        s.traits[key] = {};
                    newTraitKey[0] = '\0';
                }

                ImGui::TreePop();
            }
        }
        else
        {
            ImGui::TextDisabled("No species selected.");
        }
        ImGui::Columns(1);
    }
}


bool Editor::loadForageSprites()
{
    const bool ok = LoadForageSprites(m_lastIoStatus);
    if (ok)
    {
        g_forageSpritesLoadedOnce = true;
        LoadAllReferencedForageTextures(m_renderer);
    }
    return ok;
}

bool Editor::loadForageSpecies()
{
    const bool ok = LoadSpeciesVector(m_forageSpecies, m_lastIoStatus);
    if (ok)
        g_forageSpeciesLoadedOnce = true;
    return ok;
}

void Editor::renderForageDefinitionEditor()
{
    if (!ImGui::Begin("Forage Definitions"))
    {
        ImGui::End();
        return;
    }

    renderForageDefinitionEditorContents();
    ImGui::End();
}

void Editor::renderForageSpawnEditor()
{
    if (!ImGui::Begin("Forage Spawn Editor"))
    {
        ImGui::End();
        return;
    }

    renderForageSpawnEditorContents();
    ImGui::End();
}

void Editor::renderForageDefinitionEditorContents()
{
    if (!g_forageSpritesLoadedOnce)
    {
        LoadForageSprites(m_lastIoStatus);
        g_forageSpritesLoadedOnce = true;
    }

    if (ImGui::Button("Save all forage data"))
    {
        SaveForageSprites(m_lastIoStatus);
        saveForageArchetypes();
        SaveSpeciesVector(m_forageSpecies, m_lastIoStatus);
    }

    ImGui::SameLine();
    if (ImGui::Button("Load all forage data"))
    {
        loadForageSprites();
        loadForageArchetypes();
        loadForageSpecies();
    }

    ImGui::Separator();

    if (ImGui::BeginTabBar("ForageDefinitionTabs"))
    {
        if (ImGui::BeginTabItem("Sprites"))
        {
            RenderForageSpriteEditor(m_renderer, m_lastIoStatus);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Archetypes"))
        {
            if (ImGui::Button("Load archetypes##top"))
                loadForageArchetypes();
            ImGui::SameLine();
            if (ImGui::Button("Save archetypes##top"))
                saveForageArchetypes();
            ImGui::Separator();
            RenderArchetypeEditor(m_renderer, m_forageArchetypes, m_selectedForageArchetypeIndex, m_lastIoStatus);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Species"))
        {
            RenderSpeciesEditor(m_renderer, m_forageSpecies, m_selectedForageSpeciesIndex, m_forageArchetypes, m_lastIoStatus);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}



std::string Editor::makeUniqueForageSpawnId() const
{
    int n = 1;
    while (true)
    {
        std::string candidate = "forage_" + std::to_string(n++);
        bool exists = false;
        for (const auto& s : m_forageSpawns)
        {
            if (s.id == candidate)
            {
                exists = true;
                break;
            }
        }

        if (!exists)
            return candidate;
    }
}

void Editor::applySelectedForageBrushToSpawn(EditorForageSpawn& spawn) const
{
    if (m_placeSelectedForageSpecies &&
        m_selectedForageSpeciesIndex >= 0 &&
        m_selectedForageSpeciesIndex < (int)m_forageSpecies.size())
    {
        const auto& species = m_forageSpecies[m_selectedForageSpeciesIndex];

        spawn.archetypeId = species.archetypeId;
        spawn.speciesPool.clear();
        spawn.speciesPool.push_back(species.id);

        // For direct species placement we want the map to show the concrete plant/fungus,
        // not the generic unknown patch. Detail sprite is the best world/map fallback,
        // then inventory/herbarium if detail is not set yet.
        if (!species.detailSprite.empty())
            spawn.genericMapSpriteOverride = species.detailSprite;
        else if (!species.inventorySprite.empty())
            spawn.genericMapSpriteOverride = species.inventorySprite;
        else if (!species.herbariumSprite.empty())
            spawn.genericMapSpriteOverride = species.herbariumSprite;
        else
            spawn.genericMapSpriteOverride.clear();

        spawn.requiresExamination = true;
        return;
    }

    if (m_selectedForageArchetypeIndex >= 0 &&
        m_selectedForageArchetypeIndex < (int)m_forageArchetypes.size())
    {
        spawn.archetypeId = m_forageArchetypes[m_selectedForageArchetypeIndex].id;
        spawn.speciesPool.clear();
        spawn.genericMapSpriteOverride.clear();
        spawn.requiresExamination = true;
        return;
    }
}

std::string Editor::selectedForageBrushLabel() const
{
    if (m_placeSelectedForageSpecies &&
        m_selectedForageSpeciesIndex >= 0 &&
        m_selectedForageSpeciesIndex < (int)m_forageSpecies.size())
    {
        const auto& species = m_forageSpecies[m_selectedForageSpeciesIndex];
        if (!species.trueName.empty())
            return std::string("species: ") + species.id + " (" + species.trueName + ")";
        return std::string("species: ") + species.id;
    }

    if (m_selectedForageArchetypeIndex >= 0 &&
        m_selectedForageArchetypeIndex < (int)m_forageArchetypes.size())
    {
        return std::string("archetype: ") + m_forageArchetypes[m_selectedForageArchetypeIndex].id;
    }

    return "<none>";
}

void Editor::renderForageSpawnEditorContents()
{
    if (ImGui::Button("Load forage spawns"))
    {
        if (loadForageSpawnsForMap(m_mapPath))
            m_lastIoStatus = "Forage spawns loaded.";
    }

    ImGui::SameLine();

    if (ImGui::Button("Save forage spawns"))
    {
        if (saveForageSpawnsForMap(m_mapPath))
            m_lastIoStatus = "Forage spawns saved.";
    }

    ImGui::SameLine();

    if (ImGui::Button("New spawn"))
    {
        EditorForageSpawn s;
        s.id = makeUniqueForageSpawnId();
        s.tileX = 0;
        s.tileY = 0;

        applySelectedForageBrushToSpawn(s);

        s.quantityMin = 1;
        s.quantityMax = 1;
        s.rarity = 50;
        s.requiresExamination = true;

        m_forageSpawns.push_back(std::move(s));
        m_selectedForageSpawnIndex = (int)m_forageSpawns.size() - 1;
        m_lastIoStatus = "Forage spawn created.";
    }

    ImGui::Separator();

    ImGui::Text("Spawns on map: %d", (int)m_forageSpawns.size());

    if (ImGui::BeginListBox("##forage_spawn_list", ImVec2(-1.0f, 120.0f)))
    {
        for (int i = 0; i < (int)m_forageSpawns.size(); ++i)
        {
            ImGui::PushID(i);
            const auto& s = m_forageSpawns[i];

            std::string label =
                s.id + " (" +
                std::to_string(s.tileX) + "," +
                std::to_string(s.tileY) + ")";

            if (!s.speciesPool.empty())
                label += " [species: " + JoinCsv(s.speciesPool) + "]";
            else if (!s.archetypeId.empty())
                label += " [" + s.archetypeId + "]";

            const bool sel = (i == m_selectedForageSpawnIndex);
            if (ImGui::Selectable(label.c_str(), sel))
                m_selectedForageSpawnIndex = i;
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }

    ImGui::Separator();

    if (m_selectedForageSpawnIndex >= 0 &&
        m_selectedForageSpawnIndex < (int)m_forageSpawns.size())
    {
        auto& s = m_forageSpawns[m_selectedForageSpawnIndex];

        char idBuf[128]{};
        CopyToBuf(idBuf, sizeof(idBuf), s.id);
        if (ImGui::InputText("ID", idBuf, IM_ARRAYSIZE(idBuf)))
            s.id = idBuf;

        ImGui::InputInt("Tile X", &s.tileX);
        ImGui::InputInt("Tile Y", &s.tileY);

        ArchetypeCombo("Archetype", m_forageArchetypes, s.archetypeId);
        SpriteCombo(m_renderer, "Map sprite override", s.genericMapSpriteOverride);
        ImGui::SliderFloat("Scale override (0 = archetype)", &s.mapScaleOverride, 0.0f, 2.0f, "%.2f");

        ImGui::TextDisabled("Current placement brush: %s", selectedForageBrushLabel().c_str());
        if (ImGui::Button("Apply selected brush to spawn"))
        {
            applySelectedForageBrushToSpawn(s);
            m_lastIoStatus = "Forage spawn brush applied.";
        }

        char csvBuf[512]{};
        CopyToBuf(csvBuf, sizeof(csvBuf), JoinCsv(s.speciesPool));
        if (ImGui::InputText("Species pool CSV", csvBuf, IM_ARRAYSIZE(csvBuf)))
            s.speciesPool = SplitCsv(csvBuf);

        CopyToBuf(csvBuf, sizeof(csvBuf), JoinCsv(s.seasonMask));
        if (ImGui::InputText("Season mask CSV", csvBuf, IM_ARRAYSIZE(csvBuf)))
            s.seasonMask = SplitCsv(csvBuf);

        ImGui::InputInt("Respawn days", &s.respawnDays);
        ImGui::InputInt("Quantity min", &s.quantityMin);
        ImGui::InputInt("Quantity max", &s.quantityMax);
        ImGui::InputInt("Rarity", &s.rarity);

        ImGui::Checkbox("Requires examination", &s.requiresExamination);
        ImGui::Checkbox("Gather once", &s.gatherOnce);

        if (ImGui::Button("Delete spawn"))
        {
            m_forageSpawns.erase(m_forageSpawns.begin() + m_selectedForageSpawnIndex);

            if (m_forageSpawns.empty())
                m_selectedForageSpawnIndex = -1;
            else if (m_selectedForageSpawnIndex >= (int)m_forageSpawns.size())
                m_selectedForageSpawnIndex = (int)m_forageSpawns.size() - 1;

            m_lastIoStatus = "Forage spawn deleted.";
        }
    }
    else
    {
        ImGui::TextUnformatted("No forage spawn selected.");
    }
}

bool Editor::loadForageSpawnsForMap(const std::string& mapPath)
{
    m_forageSpawns.clear();
    std::unordered_set<std::string> usedSpawnIds;

    fs::path p(mapPath);
    p.replace_extension(".forage.json");

    if (!fs::exists(p))
        return true;

    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(p.string(), root, err))
    {
        m_lastIoStatus = "Forage spawns parse failed: " + err;
        return false;
    }

    if (!root.contains("forage_spawns") || !root["forage_spawns"].is_array())
        return true;

    for (const auto& js : root["forage_spawns"])
    {
        EditorForageSpawn s;
        s.id = js.value("id", "");
        s.tileX = js.value("tile_x", 0);
        s.tileY = js.value("tile_y", 0);
        s.archetypeId = js.value("archetype_id", "");
        s.genericMapSpriteOverride = js.value("generic_map_sprite_override", "");
        s.mapScaleOverride = js.value("map_scale_override", 0.0f);
        s.respawnDays = js.value("respawn_days", 0);
        s.gatherOnce = js.value("gather_once", false);
        s.quantityMin = js.value("quantity_min", 1);
        s.quantityMax = js.value("quantity_max", 1);
        s.rarity = js.value("rarity", 50);
        s.requiresExamination = js.value("requires_examination", true);

        if (js.contains("species_pool") && js["species_pool"].is_array())
        {
            for (const auto& v : js["species_pool"])
            {
                if (v.is_string())
                    s.speciesPool.push_back(v.get<std::string>());
            }
        }

        if (js.contains("season_mask") && js["season_mask"].is_array())
        {
            for (const auto& v : js["season_mask"])
            {
                if (v.is_string())
                    s.seasonMask.push_back(v.get<std::string>());
            }
        }

        if (s.id.empty())
            s.id = "forage_" + std::to_string((int)m_forageSpawns.size() + 1);

        if (usedSpawnIds.contains(s.id))
        {
            int n = 1;
            std::string base = s.id + "_";
            std::string candidate;
            do
            {
                candidate = base + std::to_string(n++);
            }
            while (usedSpawnIds.contains(candidate));
            s.id = candidate;
        }

        usedSpawnIds.insert(s.id);
        m_forageSpawns.push_back(std::move(s));
    }

    m_selectedForageSpawnIndex = m_forageSpawns.empty() ? -1 : 0;
    m_lastIoStatus = "Forage spawns loaded";
    return true;
}

bool Editor::saveForageSpawnsForMap(const std::string& mapPath)
{
    fs::path p(mapPath);
    p.replace_extension(".forage.json");

    json root;
    root["forage_spawns"] = json::array();

    for (const auto& s : m_forageSpawns)
    {
        json js;
        js["id"] = s.id;
        js["tile_x"] = s.tileX;
        js["tile_y"] = s.tileY;
        js["archetype_id"] = s.archetypeId;
        js["species_pool"] = json::array();
        js["generic_map_sprite_override"] = s.genericMapSpriteOverride;
        if (s.mapScaleOverride > 0.0f)
            js["map_scale_override"] = s.mapScaleOverride;
        js["season_mask"] = json::array();
        js["respawn_days"] = s.respawnDays;
        js["gather_once"] = s.gatherOnce;
        js["quantity_min"] = s.quantityMin;
        js["quantity_max"] = s.quantityMax;
        js["rarity"] = s.rarity;
        js["requires_examination"] = s.requiresExamination;

        for (const auto& id : s.speciesPool)
            js["species_pool"].push_back(id);

        for (const auto& season : s.seasonMask)
            js["season_mask"].push_back(season);

        root["forage_spawns"].push_back(std::move(js));
    }

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        m_lastIoStatus = "Forage spawns save failed: " + p.string();
        return false;
    }

    f << root.dump(2);

    m_lastIoStatus = "Forage spawns saved";
    return true;
}

EditorForageSpawn* Editor::findForageSpawnAt(int tileX, int tileY)
{
    for (auto& spawn : m_forageSpawns)
    {
        if (spawn.tileX == tileX && spawn.tileY == tileY)
            return &spawn;
    }
    return nullptr;
}

const EditorForageSpawn* Editor::findForageSpawnAt(int tileX, int tileY) const
{
    for (const auto& spawn : m_forageSpawns)
    {
        if (spawn.tileX == tileX && spawn.tileY == tileY)
            return &spawn;
    }
    return nullptr;
}

int Editor::findForageSpawnIndexAt(int tileX, int tileY) const
{
    for (int i = 0; i < (int)m_forageSpawns.size(); ++i)
    {
        if (m_forageSpawns[i].tileX == tileX && m_forageSpawns[i].tileY == tileY)
            return i;
    }
    return -1;
}

bool Editor::loadForageArchetypes()
{
    m_forageArchetypes.clear();

    fs::path p = ForageDataDir() / "forage_archetypes.json";

    if (!fs::exists(p))
    {
        EditorForageArchetype fungi;
        fungi.id = "generic_mushroom_patch";
        fungi.category = "mushroom";
        fungi.displayUnknown = "neznámé houby";
        fungi.displayPartial = "houby";
        fungi.genericMapSprite = "forage_map_mushroom_general_01";
        fungi.mapScale = 0.35f;
        fungi.examinationSlots = { "cap", "stem", "gills", "smell", "habitat" };
        m_forageArchetypes.push_back(std::move(fungi));

        EditorForageArchetype herb;
        herb.id = "generic_herb_patch";
        herb.category = "herb";
        herb.displayUnknown = "neznámé byliny";
        herb.displayPartial = "byliny";
        herb.genericMapSprite = "forage_map_herb_general_01";
        herb.mapScale = 0.28f;
        herb.examinationSlots = { "flower", "leaf", "smell", "root", "habitat" };
        m_forageArchetypes.push_back(std::move(herb));

        m_selectedForageArchetypeIndex = 0;
        m_lastIoStatus = "Default forage archetypes created in memory. Save to create JSON.";
        return true;
    }

    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(p.string(), root, err))
    {
        m_lastIoStatus = "Forage archetypes parse failed: " + err;
        return false;
    }

    if (!root.contains("archetypes") || !root["archetypes"].is_array())
    {
        m_lastIoStatus = "Forage archetypes missing 'archetypes' array";
        return false;
    }

    for (const auto& ja : root["archetypes"])
    {
        EditorForageArchetype a;
        a.id = ja.value("id", "");
        a.category = ja.value("category", "");
        a.displayUnknown = ja.value("display_unknown", "");
        a.displayPartial = ja.value("display_partial", "");
        a.genericMapSprite = ja.value("generic_map_sprite", "");
        a.mapScale = ja.value("map_scale", 0.35f);
        a.weight = ja.value("weight", ja.value("weight_kg", 0.05f));
        a.volume = ja.value("volume", ja.value("volume_l", 0.10f));
        a.maxStack = std::clamp(ja.value("max_stack", ja.value("maxStack", 64)), 1, 64);
        a.detailPlaceholderSprite = ja.value("detail_placeholder_sprite", "");
        a.herbariumPlaceholderSprite = ja.value("herbarium_placeholder_sprite", "");

        if (ja.contains("examination_slots") && ja["examination_slots"].is_array())
        {
            for (const auto& v : ja["examination_slots"])
            {
                if (v.is_string())
                    a.examinationSlots.push_back(v.get<std::string>());
            }
        }

        if (!a.id.empty())
            m_forageArchetypes.push_back(std::move(a));
    }

    m_selectedForageArchetypeIndex = m_forageArchetypes.empty() ? -1 : 0;
    m_lastIoStatus = "Forage archetypes loaded";
    return true;
}

bool Editor::saveForageArchetypes()
{
    fs::path p = ForageDataDir() / "forage_archetypes.json";

    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    json root;
    root["archetypes"] = json::array();

    for (const auto& a : m_forageArchetypes)
    {
        json ja;
        ja["id"] = a.id;
        ja["category"] = a.category;
        ja["display_unknown"] = a.displayUnknown;
        ja["display_partial"] = a.displayPartial;
        ja["generic_map_sprite"] = a.genericMapSprite;
        ja["map_scale"] = a.mapScale;
        ja["weight"] = a.weight;
        ja["volume"] = a.volume;
        ja["max_stack"] = std::clamp(a.maxStack, 1, 64);
        ja["detail_placeholder_sprite"] = a.detailPlaceholderSprite;
        ja["herbarium_placeholder_sprite"] = a.herbariumPlaceholderSprite;
        ja["examination_slots"] = json::array();

        for (const auto& s : a.examinationSlots)
            ja["examination_slots"].push_back(s);

        root["archetypes"].push_back(std::move(ja));
    }

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        m_lastIoStatus = "Forage archetypes save failed: " + p.string();
        return false;
    }

    f << root.dump(2);
    m_lastIoStatus = "Forage archetypes saved";
    return true;
}
