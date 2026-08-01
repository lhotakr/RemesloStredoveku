#define NOMINMAX
#include "ObjectEditor.h"

#include "Utf8.h"
#include "PathUtils.h"

#include <SDL_image.h>
#include <nlohmann/json.hpp>
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <unordered_map>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string trimCopy(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();

    while (b < e && std::isspace((unsigned char)s[b]))
        ++b;

    while (e > b && std::isspace((unsigned char)s[e - 1]))
        --e;

    return s.substr(b, e - b);
}

static std::string fileNameOnlyLower(const std::string& path)
{
    std::string s = path;
    const size_t p = s.find_last_of("/\\");
    if (p != std::string::npos)
        s = s.substr(p + 1);

    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });

    return s;
}



static std::string normalizeObjectImageRef(const std::string& image)
{
    const std::string trimmed = trimCopy(image);
    if (trimmed.empty())
        return {};

    fs::path p(trimmed);

    // Old Python tooling sometimes wrote absolute paths such as
    // D:/Dev/Pajthon/Objects/Decoration.png. Runtime/editor should keep only
    // the atlas filename, because atlases live in assets/Objects.
    if (p.is_absolute() || trimmed.find('/') != std::string::npos || trimmed.find('\\') != std::string::npos)
        return p.filename().string();

    return trimmed;
}

static std::string sourceStemLower(const std::string& sourceFile)
{
    std::string s = fs::path(sourceFile).stem().string();

    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });

    for (char& c : s)
    {
        if (!(std::isalnum((unsigned char)c) || c == '_'))
            c = '_';
    }

    if (s.empty())
        s = "object";

    return s;
}

static ImVec2 clampPointToRect(ImVec2 p, ImVec2 minP, ImVec2 maxP)
{
    p.x = std::clamp(p.x, minP.x, maxP.x);
    p.y = std::clamp(p.y, minP.y, maxP.y);
    return p;
}

static SDL_Rect normalizedRectFromPoints(ImVec2 a, ImVec2 b)
{
    const int x1 = (int)std::floor(std::min(a.x, b.x));
    const int y1 = (int)std::floor(std::min(a.y, b.y));
    const int x2 = (int)std::ceil(std::max(a.x, b.x));
    const int y2 = (int)std::ceil(std::max(a.y, b.y));

    return SDL_Rect{ x1, y1, std::max(1, x2 - x1), std::max(1, y2 - y1) };
}


static SDL_Rect spriteRectFromNormalized(const gameobj::RectF& r, int spriteW, int spriteH)
{
    SDL_Rect out{};
    out.x = (int)std::lround(r.x * (float)spriteW);
    out.y = (int)std::lround(r.y * (float)spriteH);
    out.w = (int)std::lround(r.w * (float)spriteW);
    out.h = (int)std::lround(r.h * (float)spriteH);

    out.x = std::clamp(out.x, 0, std::max(0, spriteW - 1));
    out.y = std::clamp(out.y, 0, std::max(0, spriteH - 1));
    out.w = std::clamp(out.w, 1, std::max(1, spriteW - out.x));
    out.h = std::clamp(out.h, 1, std::max(1, spriteH - out.y));
    return out;
}

static gameobj::RectF normalizedFromSpriteRect(SDL_Rect r, int spriteW, int spriteH)
{
    spriteW = std::max(1, spriteW);
    spriteH = std::max(1, spriteH);

    r.x = std::clamp(r.x, 0, std::max(0, spriteW - 1));
    r.y = std::clamp(r.y, 0, std::max(0, spriteH - 1));
    r.w = std::clamp(r.w, 1, std::max(1, spriteW - r.x));
    r.h = std::clamp(r.h, 1, std::max(1, spriteH - r.y));

    gameobj::RectF out{};
    out.x = (float)r.x / (float)spriteW;
    out.y = (float)r.y / (float)spriteH;
    out.w = (float)r.w / (float)spriteW;
    out.h = (float)r.h / (float)spriteH;
    return out;
}

static const char* zoneLayerJsonName(int layer)
{
    switch (layer)
    {
    case 0: return "collision_rects";
    case 1: return "walkable_rects";
    case 2: return "fade_rects";
    default: return "collision_rects";
    }
}

static const char* zoneLayerNiceName(int layer)
{
    switch (layer)
    {
    case 0: return "Collision";
    case 1: return "Walkable / courtyard";
    case 2: return "Fade / gate opacity";
    default: return "Collision";
    }
}

static std::vector<gameobj::RectF>& zoneLayerRects(gameobj::ObjectDef& def, int layer)
{
    switch (layer)
    {
    case 0: return def.collisionRects;
    case 1: return def.walkableRects;
    case 2: return def.fadeRects;
    default: return def.collisionRects;
    }
}

static bool containsNoCase(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;

    std::string h = haystack;
    std::string n = needle;

    std::transform(h.begin(), h.end(), h.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    std::transform(n.begin(), n.end(), n.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });

    return h.find(n) != std::string::npos;
}

static int getInt(const json& j, const char* key, int def)
{
    try {
        if (!j.contains(key))
            return def;

        const auto& v = j.at(key);
        if (v.is_number_integer())
            return v.get<int>();
        if (v.is_number_float())
            return (int)v.get<double>();
        if (v.is_string())
            return std::stoi(v.get<std::string>());
    }
    catch (...) {}

    return def;
}

static bool getBool(const json& j, const char* key, bool def)
{
    try {
        if (!j.contains(key))
            return def;

        const auto& v = j.at(key);
        if (v.is_boolean())
            return v.get<bool>();
        if (v.is_number_integer())
            return v.get<int>() != 0;
        if (v.is_string())
        {
            std::string s = trimCopy(v.get<std::string>());
            std::transform(s.begin(), s.end(), s.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });

            if (s == "true" || s == "1" || s == "yes")
                return true;
            if (s == "false" || s == "0" || s == "no")
                return false;
        }
    }
    catch (...) {}

    return def;
}

static gameobj::RectI parseRectI(const json& j, const char* key, gameobj::RectI def)
{
    try {
        if (!j.contains(key) || !j.at(key).is_object())
            return def;

        const auto& r = j.at(key);
        def.x = getInt(r, "x", def.x);
        def.y = getInt(r, "y", def.y);
        def.w = getInt(r, "w", def.w);
        def.h = getInt(r, "h", def.h);
    }
    catch (...) {}

    return def;
}

static gameobj::PivotI parsePivotI(const json& j, const char* key, gameobj::PivotI def)
{
    try {
        if (!j.contains(key) || !j.at(key).is_object())
            return def;

        const auto& p = j.at(key);
        def.x = getInt(p, "x", def.x);
        def.y = getInt(p, "y", def.y);
    }
    catch (...) {}

    return def;
}

static gameobj::ColliderI parseColliderI(const json& j, const char* key, gameobj::ColliderI def)
{
    try {
        if (!j.contains(key) || !j.at(key).is_object())
            return def;

        const auto& c = j.at(key);
        def.x = getInt(c, "x", def.x);
        def.y = getInt(c, "y", def.y);
        def.w = getInt(c, "w", def.w);
        def.h = getInt(c, "h", def.h);
        def.enabled = getBool(c, "enabled", def.enabled);
    }
    catch (...) {}

    return def;
}

static std::vector<gameobj::RectF> parseRectFArray(const json& j, const char* key)
{
    std::vector<gameobj::RectF> out;

    try
    {
        if (!j.contains(key) || !j.at(key).is_array())
            return out;

        for (const auto& it : j.at(key))
        {
            gameobj::RectF r{};

            if (it.is_object())
            {
                r.x = it.value("x", 0.0f);
                r.y = it.value("y", 0.0f);
                r.w = it.value("w", 0.0f);
                r.h = it.value("h", 0.0f);
            }
            else if (it.is_array() && it.size() >= 4)
            {
                r.x = it[0].get<float>();
                r.y = it[1].get<float>();
                r.w = it[2].get<float>();
                r.h = it[3].get<float>();
            }

            if (r.w > 0.0f && r.h > 0.0f)
                out.push_back(r);
        }
    }
    catch (...) {}

    return out;
}

static json saveRectFArray(const std::vector<gameobj::RectF>& rects)
{
    json arr = json::array();

    for (const auto& r : rects)
    {
        arr.push_back({
            {"x", r.x},
            {"y", r.y},
            {"w", r.w},
            {"h", r.h}
        });
    }

    return arr;
}

static json saveObjectJson(const gameobj::ObjectDef& o, const std::string& defaultImage)
{
    json j;

    j["id"] = o.id;
    j["name"] = o.name;

    const std::string imageRef = normalizeObjectImageRef(o.image);
    const std::string defaultImageRef = normalizeObjectImageRef(defaultImage);
    if (!imageRef.empty() && imageRef != defaultImageRef)
        j["image"] = imageRef;

    j["src"] = {
        {"x", o.src.x},
        {"y", o.src.y},
        {"w", o.src.w},
        {"h", o.src.h}
    };

    j["pivot"] = {
        {"x", o.pivot.x},
        {"y", o.pivot.y}
    };

    j["collider"] = {
        {"x", o.collider.x},
        {"y", o.collider.y},
        {"w", o.collider.w},
        {"h", o.collider.h},
        {"enabled", o.collider.enabled}
    };

    j["tags"] = o.tags_raw;
    j["solid"] = o.solid;
    j["has_sprite"] = o.has_sprite;
    j["scale"] = o.scale;

    if (!o.enabled)
        j["enabled"] = false;

    if (o.unique)
        j["unique"] = true;

    if (!o.collisionRects.empty())
        j["collision_rects"] = saveRectFArray(o.collisionRects);

    if (!o.fadeRects.empty())
        j["fade_rects"] = saveRectFArray(o.fadeRects);

    if (!o.walkableRects.empty())
        j["walkable_rects"] = saveRectFArray(o.walkableRects);

    return j;
}

bool ObjectEditor::init(SDL_Window* window, SDL_Renderer* renderer)
{
    m_window = window;
    m_renderer = renderer;

    if (!m_window || !m_renderer)
        return false;

    return loadCatalog();
}

void ObjectEditor::shutdown()
{
    destroyTextures();

    m_entries.clear();
    m_defaultImageBySource.clear();
    m_selectedIndex = -1;

    m_window = nullptr;
    m_renderer = nullptr;
}

void ObjectEditor::handleEvent(const SDL_Event&)
{
}

void ObjectEditor::update(float)
{
}

void ObjectEditor::destroyTextures()
{
    for (auto& kv : m_textures)
    {
        if (kv.second)
            SDL_DestroyTexture(kv.second);
    }

    m_textures.clear();
}

std::vector<std::string> ObjectEditor::objectJsonFiles() const
{
    std::vector<std::string> files;

    const fs::path dir = pathutils::ProjectRoot() / "assets" / "Objects";
    if (!fs::exists(dir))
        return files;

    for (const auto& e : fs::directory_iterator(dir))
    {
        if (!e.is_regular_file())
            continue;

        if (e.path().extension() == ".json")
            files.push_back(e.path().string());
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::string ObjectEditor::forageSpritesJsonFile() const
{
    return (pathutils::ProjectRoot() / "data" / "foraging" / "forage_sprites.json").string();
}

bool ObjectEditor::isForageSpriteSource(const std::string& sourceFile) const
{
    const fs::path a = fs::weakly_canonical(fs::path(sourceFile));
    const fs::path b = fs::weakly_canonical(fs::path(forageSpritesJsonFile()));
    return fileNameOnlyLower(a.string()) == "forage_sprites.json" && fileNameOnlyLower(b.string()) == "forage_sprites.json";
}

void ObjectEditor::loadForageSpritesIntoEntries()
{
    const std::string file = forageSpritesJsonFile();
    if (!fs::exists(file))
        return;

    std::ifstream f(file, std::ios::binary);
    if (!f)
        return;

    json root;
    try
    {
        f >> root;
    }
    catch (const std::exception& ex)
    {
        m_lastStatus = std::string("Forage sprite JSON parse failed: ") + ex.what();
        return;
    }

    if (!root.contains("sprites") || !root["sprites"].is_array())
        return;

    m_defaultImageBySource[file] = "";

    for (const auto& js : root["sprites"])
    {
        if (!js.is_object())
            continue;

        gameobj::ObjectDef o;
        o.id = trimCopy(js.value("id", ""));
        if (o.id.empty())
            continue;

        o.name = js.value("name", o.id);
        o.image = normalizeObjectImageRef(js.value("image", ""));
        o.enabled = true;
        o.solid = false;
        o.has_sprite = true;
        o.unique = false;
        o.src = parseRectI(js, "src", gameobj::RectI{ 0, 0, 0, 0 });

        gameobj::PivotI defaultPivot;
        defaultPivot.x = (o.src.w > 0) ? (o.src.w / 2) : 0;
        defaultPivot.y = (o.src.h > 0) ? o.src.h : 0;
        o.pivot = parsePivotI(js, "pivot", defaultPivot);

        o.collider = gameobj::ColliderI{ 0, 0, std::max(1, o.src.w), std::max(1, o.src.h), false };

        const std::string category = trimCopy(js.value("category", "forage"));
        o.tags_raw.clear();
        o.tags_raw.push_back("forage");
        o.tags_raw.push_back("forage_sprite");
        if (!category.empty())
            o.tags_raw.push_back(category);

        o.tags.clear();
        for (const auto& rawTag : o.tags_raw)
        {
            std::string tag = trimCopy(rawTag);
            std::transform(tag.begin(), tag.end(), tag.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });
            if (!tag.empty())
                o.tags.push_back(tag);
        }
        std::sort(o.tags.begin(), o.tags.end());
        o.tags.erase(std::unique(o.tags.begin(), o.tags.end()), o.tags.end());

        o.scale = 1.0f;
        o.sourceFile = file;

        ObjectEditorEntry e;
        e.def = std::move(o);
        e.sourceFile = file;
        m_entries.push_back(std::move(e));
    }
}

bool ObjectEditor::saveForageSpritesFile(const std::string& sourceFile)
{
    json root;
    root["sprites"] = json::array();

    for (const auto& e : m_entries)
    {
        if (e.sourceFile != sourceFile)
            continue;

        const auto& o = e.def;
        std::string category = "forage";
        for (const auto& t : o.tags_raw)
        {
            const std::string lt = fileNameOnlyLower(t);
            if (lt != "forage" && lt != "forage_sprite")
            {
                category = trimCopy(t);
                break;
            }
        }

        json js;
        js["id"] = o.id;
        js["image"] = normalizeObjectImageRef(o.image);
        js["category"] = category;
        js["src"] = { {"x", o.src.x}, {"y", o.src.y}, {"w", o.src.w}, {"h", o.src.h} };
        js["pivot"] = { {"x", o.pivot.x}, {"y", o.pivot.y} };
        root["sprites"].push_back(std::move(js));
    }

    std::error_code ec;
    fs::create_directories(fs::path(sourceFile).parent_path(), ec);

    std::ofstream f(sourceFile, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        m_lastStatus = "Forage sprite save failed: " + sourceFile;
        return false;
    }

    f << root.dump(2);
    return true;
}

std::string ObjectEditor::resolveImagePath(const std::string& image) const
{
    if (image.empty())
        return {};

    fs::path p(image);

    if (p.is_absolute() && fs::exists(p))
        return p.string();

    fs::path direct = pathutils::ProjectRoot() / "assets" / "Objects" / p.filename();
    if (fs::exists(direct))
        return direct.string();

    fs::path relative = pathutils::ProjectRoot() / "assets" / "Objects" / p;
    if (fs::exists(relative))
        return relative.string();

    fs::path forageDirect = pathutils::ProjectRoot() / "assets" / "Foraging" / p.filename();
    if (fs::exists(forageDirect))
        return forageDirect.string();

    fs::path forageRelative = pathutils::ProjectRoot() / "assets" / "Foraging" / p;
    if (fs::exists(forageRelative))
        return forageRelative.string();

    return direct.string();
}

SDL_Texture* ObjectEditor::textureForImage(const std::string& image)
{
    const std::string key = fileNameOnlyLower(image);
    auto it = m_textures.find(key);
    if (it != m_textures.end())
        return it->second;

    const std::string path = resolveImagePath(image);
    SDL_Texture* tex = IMG_LoadTexture(m_renderer, path.c_str());

    if (!tex)
    {
        m_lastStatus = "Sprite atlas load failed: " + path;
        m_textures[key] = nullptr;
        return nullptr;
    }

    m_textures[key] = tex;
    return tex;
}

std::vector<std::string> ObjectEditor::SplitTags(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;

    auto flush = [&]()
    {
        std::string s = trimCopy(cur);
        cur.clear();

        if (!s.empty())
            out.push_back(s);
    };

    for (char c : text)
    {
        if (c == ',' || c == ';' || c == '|')
            flush();
        else
            cur.push_back(c);
    }

    flush();

    return out;
}

std::string ObjectEditor::JoinTags(const std::vector<std::string>& tags)
{
    std::string out;

    for (size_t i = 0; i < tags.size(); ++i)
    {
        if (i > 0)
            out += ", ";

        out += tags[i];
    }

    return out;
}

bool ObjectEditor::loadCatalog()
{
    destroyTextures();
    m_entries.clear();
    m_defaultImageBySource.clear();
    m_selectedIndex = -1;

    const auto files = objectJsonFiles();
    if (files.empty())
    {
        m_lastStatus = "No object json files found in assets/Objects.";
        return true;
    }

    std::string lastError;

    for (const std::string& file : files)
    {
        std::ifstream f(file, std::ios::binary);
        if (!f)
        {
            lastError = "Cannot open: " + file;
            continue;
        }

        json root;
        try
        {
            f >> root;
        }
        catch (const std::exception& ex)
        {
            lastError = "JSON parse failed: " + file + " | " + ex.what();
            continue;
        }

        const std::string defaultImage = normalizeObjectImageRef(root.value("image", ""));
        m_defaultImageBySource[file] = defaultImage;

        if (!root.contains("objects") || !root["objects"].is_array())
            continue;

        size_t autoId = 0;

        for (const auto& jo : root["objects"])
        {
            if (!jo.is_object())
                continue;

            gameobj::ObjectDef o;
            o.id = trimCopy(jo.value("id", ""));
            if (o.id.empty())
            {
                ++autoId;
                o.id = "obj_auto_" + std::to_string(autoId);
            }

            o.name = trimCopy(jo.value("name", o.id));
            o.image = normalizeObjectImageRef(jo.value("image", defaultImage));

            o.enabled = getBool(jo, "enabled", true);
            o.solid = getBool(jo, "solid", false);
            o.has_sprite = getBool(jo, "has_sprite", true);
            o.unique = getBool(jo, "unique", false);

            o.src = parseRectI(jo, "src", gameobj::RectI{ 0, 0, 0, 0 });

            gameobj::PivotI defaultPivot;
            defaultPivot.x = (o.src.w > 0) ? (o.src.w / 2) : 0;
            defaultPivot.y = (o.src.h > 0) ? o.src.h : 0;
            o.pivot = parsePivotI(jo, "pivot", defaultPivot);

            gameobj::ColliderI defaultCollider;
            defaultCollider.x = 0;
            defaultCollider.y = 0;
            defaultCollider.w = (o.src.w > 0) ? o.src.w : 1;
            defaultCollider.h = (o.src.h > 0) ? o.src.h : 1;
            defaultCollider.enabled = true;
            o.collider = parseColliderI(jo, "collider", defaultCollider);

            o.tags_raw.clear();
            if (jo.contains("tags") && jo["tags"].is_array())
            {
                for (const auto& jt : jo["tags"])
                {
                    if (jt.is_string())
                    {
                        std::string tag = trimCopy(jt.get<std::string>());
                        if (!tag.empty())
                            o.tags_raw.push_back(tag);
                    }
                }
            }

            o.tags.clear();
            for (const auto& rawTag : o.tags_raw)
            {
                std::string tag = trimCopy(rawTag);
                std::transform(tag.begin(), tag.end(), tag.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });
                if (!tag.empty())
                    o.tags.push_back(tag);
            }
            std::sort(o.tags.begin(), o.tags.end());
            o.tags.erase(std::unique(o.tags.begin(), o.tags.end()), o.tags.end());

            o.collisionRects = parseRectFArray(jo, "collision_rects");
            o.fadeRects = parseRectFArray(jo, "fade_rects");
            o.walkableRects = parseRectFArray(jo, "walkable_rects");

            o.scale = 1.0f;
            try
            {
                if (jo.contains("scale"))
                {
                    const auto& v = jo["scale"];
                    if (v.is_number())
                        o.scale = (float)v.get<double>();
                    else if (v.is_string())
                        o.scale = std::stof(v.get<std::string>());
                }
            }
            catch (...) {}

            if (o.scale <= 0.01f)
                o.scale = 1.0f;

            o.sourceFile = file;

            ObjectEditorEntry e;
            e.def = std::move(o);
            e.sourceFile = file;
            m_entries.push_back(std::move(e));
        }
    }

    const size_t objectCount = m_entries.size();
    loadForageSpritesIntoEntries();
    const size_t forageCount = m_entries.size() - objectCount;

    if (!m_entries.empty())
    {
        m_selectedIndex = 0;
        loadSelectedToBuffers();
    }

    m_lastStatus = lastError.empty()
        ? ("Object editor loaded: " + std::to_string(objectCount) + " objects + " + std::to_string(forageCount) + " forage sprites.")
        : lastError;

    return true;
}

void ObjectEditor::loadSelectedToBuffers()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_entries.size())
        return;

    const auto& o = m_entries[m_selectedIndex].def;

    snprintf(m_idBuf, sizeof(m_idBuf), "%s", o.id.c_str());
    snprintf(m_nameBuf, sizeof(m_nameBuf), "%s", o.name.c_str());
    snprintf(m_imageBuf, sizeof(m_imageBuf), "%s", o.image.c_str());
    snprintf(m_tagsBuf, sizeof(m_tagsBuf), "%s", JoinTags(o.tags_raw).c_str());

    m_enabledBuf = o.enabled;
    m_solidBuf = o.solid;
    m_hasSpriteBuf = o.has_sprite;
    m_uniqueBuf = o.unique;

    m_srcX = o.src.x;
    m_srcY = o.src.y;
    m_srcW = o.src.w;
    m_srcH = o.src.h;

    m_pivotX = o.pivot.x;
    m_pivotY = o.pivot.y;

    m_colliderEnabled = o.collider.enabled;
    m_colX = o.collider.x;
    m_colY = o.collider.y;
    m_colW = o.collider.w;
    m_colH = o.collider.h;

    m_scale = o.scale;
}

void ObjectEditor::applyBuffersToSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_entries.size())
        return;

    auto& o = m_entries[m_selectedIndex].def;

    o.id = trimCopy(m_idBuf);
    o.name = trimCopy(m_nameBuf);
    o.image = normalizeObjectImageRef(m_imageBuf);
    o.tags_raw = SplitTags(m_tagsBuf);
    o.tags.clear();
            for (const auto& rawTag : o.tags_raw)
            {
                std::string tag = trimCopy(rawTag);
                std::transform(tag.begin(), tag.end(), tag.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });
                if (!tag.empty())
                    o.tags.push_back(tag);
            }
            std::sort(o.tags.begin(), o.tags.end());
            o.tags.erase(std::unique(o.tags.begin(), o.tags.end()), o.tags.end());

    o.enabled = m_enabledBuf;
    o.solid = m_solidBuf;
    o.has_sprite = m_hasSpriteBuf;
    o.unique = m_uniqueBuf;

    o.src.x = std::max(0, m_srcX);
    o.src.y = std::max(0, m_srcY);
    o.src.w = std::max(0, m_srcW);
    o.src.h = std::max(0, m_srcH);

    o.pivot.x = m_pivotX;
    o.pivot.y = m_pivotY;

    o.collider.enabled = m_colliderEnabled;
    o.collider.x = m_colX;
    o.collider.y = m_colY;
    o.collider.w = std::max(0, m_colW);
    o.collider.h = std::max(0, m_colH);

    o.scale = std::max(0.01f, m_scale);
}

bool ObjectEditor::saveSourceFile(const std::string& sourceFile)
{
    if (isForageSpriteSource(sourceFile))
        return saveForageSpritesFile(sourceFile);

    json root;
    root["image"] = normalizeObjectImageRef(m_defaultImageBySource[sourceFile]);
    root["objects"] = json::array();

    for (const auto& e : m_entries)
    {
        if (e.sourceFile != sourceFile)
            continue;

        const std::string defaultImage = m_defaultImageBySource[sourceFile];
        root["objects"].push_back(saveObjectJson(e.def, defaultImage));
    }

    std::ofstream f(sourceFile, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        m_lastStatus = "Save failed: " + sourceFile;
        return false;
    }

    f << root.dump(2);
    return true;
}

bool ObjectEditor::saveAll()
{
    applyBuffersToSelected();

    std::vector<std::string> sources;
    for (const auto& e : m_entries)
    {
        if (std::find(sources.begin(), sources.end(), e.sourceFile) == sources.end())
            sources.push_back(e.sourceFile);
    }

    for (const auto& source : sources)
    {
        if (!saveSourceFile(source))
            return false;
    }

    m_lastStatus = "Object catalog saved.";
    return true;
}


bool ObjectEditor::isDuplicateId(const std::string& id, int ignoreIndex) const
{
    if (id.empty())
        return false;

    for (int i = 0; i < (int)m_entries.size(); ++i)
    {
        if (i == ignoreIndex)
            continue;

        if (m_entries[i].def.id == id)
            return true;
    }

    return false;
}

int ObjectEditor::duplicateCountForId(const std::string& id) const
{
    if (id.empty())
        return 0;

    int count = 0;
    for (const auto& e : m_entries)
    {
        if (e.def.id == id)
            ++count;
    }

    return count;
}

std::string ObjectEditor::makeUniqueIdForEntry(int index, const std::string& baseId) const
{
    if (index < 0 || index >= (int)m_entries.size())
        return baseId.empty() ? "object" : baseId;

    std::string cleanBase = trimCopy(baseId);
    if (cleanBase.empty())
        cleanBase = "object";

    const std::string prefix = sourceStemLower(m_entries[index].sourceFile);

    std::string candidate = cleanBase;
    if (candidate.rfind(prefix + "_", 0) != 0)
        candidate = prefix + "_" + candidate;

    int suffix = 2;
    const std::string root = candidate;
    while (isDuplicateId(candidate, index))
    {
        candidate = root + "_" + std::to_string(suffix);
        ++suffix;
    }

    return candidate;
}

void ObjectEditor::fixDuplicateIdsGlobally()
{
    applyBuffersToSelected();

    std::unordered_map<std::string, int> counts;
    for (const auto& e : m_entries)
        counts[e.def.id]++;

    int changed = 0;
    for (int i = 0; i < (int)m_entries.size(); ++i)
    {
        auto& id = m_entries[i].def.id;
        if (counts[id] <= 1)
            continue;

        const std::string newId = makeUniqueIdForEntry(i, id);
        if (newId != id)
        {
            id = newId;
            ++changed;
        }
    }

    loadSelectedToBuffers();
    m_lastStatus = "Duplicate object IDs fixed: " + std::to_string(changed) + " renamed.";
}

void ObjectEditor::renderObjectList()
{
    ImGui::Text("Objects / forage sprites");
    ImGui::InputText("Filter", m_filter, sizeof(m_filter));

    ImGui::Separator();

    if (ImGui::Button("Reload"))
        loadCatalog();

    ImGui::SameLine();

    if (ImGui::Button("Save all"))
        saveAll();

    ImGui::SameLine();

    if (ImGui::Button("Fix duplicate IDs"))
    {
        fixDuplicateIdsGlobally();
        saveAll();
        loadCatalog();
        m_lastStatus = "Duplicate object IDs fixed and saved to project assets/Objects.";
    }

    ImGui::Separator();

    if (ImGui::BeginChild("ObjectList", ImVec2(330, 0), true))
    {
        for (int i = 0; i < (int)m_entries.size(); ++i)
        {
            const auto& e = m_entries[i];
            const auto& o = e.def;

            if (!containsNoCase(o.id, m_filter) &&
                !containsNoCase(o.name, m_filter) &&
                !containsNoCase(JoinTags(o.tags_raw), m_filter) &&
                !containsNoCase(fileNameOnlyLower(e.sourceFile), m_filter))
            {
                continue;
            }

            std::string label = isForageSpriteSource(e.sourceFile) ? std::string("[forage] ") + o.id : o.id;
            if (!o.name.empty() && o.name != o.id)
                label += " - " + o.name;

            if (duplicateCountForId(o.id) > 1)
                label = "[DUP] " + label;

            label += "##" + std::to_string(i);

            if (ImGui::Selectable(label.c_str(), i == m_selectedIndex))
            {
                applyBuffersToSelected();
                m_selectedIndex = i;
                loadSelectedToBuffers();
            }

            if (i == m_selectedIndex)
                ImGui::SetItemDefaultFocus();
        }
    }

    ImGui::EndChild();
}


void ObjectEditor::renderAtlasSpriteSelector(const gameobj::ObjectDef& def)
{
    ImGui::Text("Spritesheet source rect");

    if (!def.has_sprite)
    {
        ImGui::TextDisabled("Enable 'Has sprite' first.");
        return;
    }

    SDL_Texture* tex = textureForImage(def.image);
    if (!tex)
    {
        ImGui::TextDisabled("Atlas texture not loaded.");
        return;
    }

    int texW = 0;
    int texH = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);

    if (texW <= 0 || texH <= 0)
    {
        ImGui::TextDisabled("Invalid atlas texture.");
        return;
    }

    ImGui::SliderFloat("Atlas zoom", &m_atlasZoom, 0.10f, 4.00f, "%.2fx");
    ImGui::SameLine();
    if (ImGui::Button("1:1##atlaszoom"))
        m_atlasZoom = 1.0f;

    ImGui::TextDisabled("Left mouse drag on atlas = set src rectangle. Current atlas size: %d x %d", texW, texH);

    const float zoom = std::max(0.05f, m_atlasZoom);
    const ImVec2 imageSize((float)texW * zoom, (float)texH * zoom);

    if (ImGui::BeginChild("AtlasSpriteSelector", ImVec2(0.0f, 360.0f), true,
        ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImGui::Image((ImTextureID)tex, imageSize);

        const ImVec2 imgMin = ImGui::GetItemRectMin();
        const ImVec2 imgMax = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        auto screenToAtlas = [&](ImVec2 p) -> ImVec2
        {
            p = clampPointToRect(p, imgMin, imgMax);
            return ImVec2((p.x - imgMin.x) / zoom, (p.y - imgMin.y) / zoom);
        };

        if (m_srcW > 0 && m_srcH > 0)
        {
            const ImVec2 a(imgMin.x + (float)m_srcX * zoom, imgMin.y + (float)m_srcY * zoom);
            const ImVec2 b(imgMin.x + (float)(m_srcX + m_srcW) * zoom, imgMin.y + (float)(m_srcY + m_srcH) * zoom);
            dl->AddRect(a, b, IM_COL32(255, 220, 40, 255), 0.0f, 0, 2.0f);
            dl->AddRectFilled(a, ImVec2(b.x, a.y + 18.0f), IM_COL32(0, 0, 0, 150));
            dl->AddText(ImVec2(a.x + 4.0f, a.y + 2.0f), IM_COL32(255, 255, 255, 255), "src");
        }

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_draggingSrcRect = true;
            m_dragStartLocal = screenToAtlas(ImGui::GetIO().MousePos);
            m_dragEndLocal = m_dragStartLocal;
        }

        if (m_draggingSrcRect)
        {
            m_dragEndLocal = screenToAtlas(ImGui::GetIO().MousePos);

            const SDL_Rect r = normalizedRectFromPoints(m_dragStartLocal, m_dragEndLocal);
            const ImVec2 a(imgMin.x + (float)r.x * zoom, imgMin.y + (float)r.y * zoom);
            const ImVec2 b(imgMin.x + (float)(r.x + r.w) * zoom, imgMin.y + (float)(r.y + r.h) * zoom);
            dl->AddRect(a, b, IM_COL32(80, 255, 120, 255), 0.0f, 0, 2.0f);
            dl->AddRectFilled(a, ImVec2(b.x, a.y + 18.0f), IM_COL32(0, 0, 0, 150));
            dl->AddText(ImVec2(a.x + 4.0f, a.y + 2.0f), IM_COL32(255, 255, 255, 255), "new src");

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                m_draggingSrcRect = false;

                m_srcX = std::clamp(r.x, 0, texW - 1);
                m_srcY = std::clamp(r.y, 0, texH - 1);
                m_srcW = std::clamp(r.w, 1, texW - m_srcX);
                m_srcH = std::clamp(r.h, 1, texH - m_srcY);

                if (m_pivotX <= 0 && m_pivotY <= 0)
                {
                    m_pivotX = m_srcW / 2;
                    m_pivotY = m_srcH;
                }

                if (m_colW <= 0 || m_colH <= 0)
                {
                    m_colX = 0;
                    m_colY = 0;
                    m_colW = m_srcW;
                    m_colH = m_srcH;
                    m_colliderEnabled = true;
                }
            }
        }
    }

    ImGui::EndChild();
}


void ObjectEditor::renderSpriteZonesEditor(gameobj::ObjectDef& def)
{
    ImGui::Text("Multi-zone editor");

    if (!def.has_sprite)
    {
        ImGui::TextDisabled("Object has no sprite.");
        return;
    }

    SDL_Texture* tex = textureForImage(def.image);
    if (!tex)
    {
        ImGui::TextDisabled("Atlas texture not loaded.");
        return;
    }

    int texW = 0;
    int texH = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);

    if (texW <= 0 || texH <= 0 || m_srcW <= 0 || m_srcH <= 0)
    {
        ImGui::TextDisabled("Set valid src rectangle first.");
        return;
    }

    const char* layers[] = { "Collision", "Walkable / courtyard", "Fade / gate opacity" };
    if (ImGui::Combo("Edited layer", &m_zoneEditLayer, layers, 3))
        m_selectedZoneRectIndex = -1;

    ImGui::SameLine();
    ImGui::SliderFloat("Preview zoom##zones", &m_spritePreviewZoom, 0.25f, 6.0f, "%.2fx");

    std::vector<gameobj::RectF>& rects = zoneLayerRects(def, m_zoneEditLayer);

    ImGui::TextWrapped(
        "Left drag over sprite = create/replace rectangle in %s. "
        "collision_rects block movement, walkable_rects create pass-through holes/courtyards, fade_rects trigger semi-transparent overlay when player walks through the area.",
        zoneLayerJsonName(m_zoneEditLayer));

    if (ImGui::Button("Add full sprite rect"))
    {
        rects.push_back(gameobj::RectF{ 0.0f, 0.0f, 1.0f, 1.0f });
        m_selectedZoneRectIndex = (int)rects.size() - 1;
        m_lastStatus = std::string("Added rect to ") + zoneLayerJsonName(m_zoneEditLayer);
    }

    ImGui::SameLine();
    const bool hasSelected = m_selectedZoneRectIndex >= 0 && m_selectedZoneRectIndex < (int)rects.size();
    if (!hasSelected)
        ImGui::BeginDisabled();

    if (ImGui::Button("Delete selected rect"))
    {
        rects.erase(rects.begin() + m_selectedZoneRectIndex);
        m_selectedZoneRectIndex = std::min(m_selectedZoneRectIndex, (int)rects.size() - 1);
        m_lastStatus = std::string("Deleted rect from ") + zoneLayerJsonName(m_zoneEditLayer);
    }

    if (!hasSelected)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Clear layer"))
    {
        rects.clear();
        m_selectedZoneRectIndex = -1;
        m_lastStatus = std::string("Cleared ") + zoneLayerJsonName(m_zoneEditLayer);
    }

    if (ImGui::BeginChild("ZoneRectList", ImVec2(0.0f, 116.0f), true))
    {
        for (int i = 0; i < (int)rects.size(); ++i)
        {
            SDL_Rect px = spriteRectFromNormalized(rects[i], m_srcW, m_srcH);
            char label[160];
            std::snprintf(label, sizeof(label), "%s %02d  px[%d,%d,%d,%d]  norm[%.3f,%.3f,%.3f,%.3f]",
                zoneLayerNiceName(m_zoneEditLayer), i + 1,
                px.x, px.y, px.w, px.h,
                rects[i].x, rects[i].y, rects[i].w, rects[i].h);

            if (ImGui::Selectable(label, i == m_selectedZoneRectIndex))
                m_selectedZoneRectIndex = i;
        }
    }
    ImGui::EndChild();

    const ImVec2 uv0((float)m_srcX / (float)texW, (float)m_srcY / (float)texH);
    const ImVec2 uv1((float)(m_srcX + m_srcW) / (float)texW, (float)(m_srcY + m_srcH) / (float)texH);

    const float fitScale = std::min(700.0f / (float)m_srcW, 430.0f / (float)m_srcH);
    const float scale = std::clamp(fitScale * m_spritePreviewZoom, 0.10f, 14.0f);
    const ImVec2 imageSize((float)m_srcW * scale, (float)m_srcH * scale);

    ImGui::Image((ImTextureID)tex, imageSize, uv0, uv1);

    const ImVec2 imgMin = ImGui::GetItemRectMin();
    const ImVec2 imgMax = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto screenToSprite = [&](ImVec2 p) -> ImVec2
    {
        p = clampPointToRect(p, imgMin, imgMax);
        return ImVec2((p.x - imgMin.x) / scale, (p.y - imgMin.y) / scale);
    };

    auto spriteToScreen = [&](float x, float y) -> ImVec2
    {
        return ImVec2(imgMin.x + x * scale, imgMin.y + y * scale);
    };

    auto drawLayer = [&](const std::vector<gameobj::RectF>& source, ImU32 color, const char* label, bool selectableLayer)
    {
        for (int i = 0; i < (int)source.size(); ++i)
        {
            SDL_Rect r = spriteRectFromNormalized(source[i], m_srcW, m_srcH);
            const ImVec2 a = spriteToScreen((float)r.x, (float)r.y);
            const ImVec2 b = spriteToScreen((float)(r.x + r.w), (float)(r.y + r.h));
            const bool selected = selectableLayer && i == m_selectedZoneRectIndex;

            dl->AddRect(a, b, color, 0.0f, 0, selected ? 4.0f : 2.0f);
            dl->AddRectFilled(a, ImVec2(std::min(b.x, a.x + 170.0f), a.y + 18.0f), IM_COL32(0, 0, 0, 150));

            char text[64];
            std::snprintf(text, sizeof(text), "%s %d", label, i + 1);
            dl->AddText(ImVec2(a.x + 4.0f, a.y + 2.0f), IM_COL32(255, 255, 255, 255), text);
        }
    };

    // Always show all layers as context: collision red, walkable cyan, fade yellow.
    drawLayer(def.collisionRects, IM_COL32(255, 70, 70, 255), "col", m_zoneEditLayer == 0);
    drawLayer(def.walkableRects, IM_COL32(0, 220, 255, 255), "walk", m_zoneEditLayer == 1);
    drawLayer(def.fadeRects, IM_COL32(255, 220, 40, 255), "fade", m_zoneEditLayer == 2);

    const ImVec2 pivot = spriteToScreen((float)m_pivotX, (float)m_pivotY);
    dl->AddLine(ImVec2(pivot.x - 7.0f, pivot.y), ImVec2(pivot.x + 7.0f, pivot.y), IM_COL32(80, 170, 255, 255), 2.0f);
    dl->AddLine(ImVec2(pivot.x, pivot.y - 7.0f), ImVec2(pivot.x, pivot.y + 7.0f), IM_COL32(80, 170, 255, 255), 2.0f);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        m_draggingZoneRect = true;
        m_dragStartLocal = screenToSprite(ImGui::GetIO().MousePos);
        m_dragEndLocal = m_dragStartLocal;
    }

    if (m_draggingZoneRect)
    {
        m_dragEndLocal = screenToSprite(ImGui::GetIO().MousePos);
        const SDL_Rect r = normalizedRectFromPoints(m_dragStartLocal, m_dragEndLocal);

        const ImVec2 a = spriteToScreen((float)r.x, (float)r.y);
        const ImVec2 b = spriteToScreen((float)(r.x + r.w), (float)(r.y + r.h));
        dl->AddRect(a, b, IM_COL32(80, 255, 120, 255), 0.0f, 0, 3.0f);
        dl->AddRectFilled(a, ImVec2(b.x, a.y + 18.0f), IM_COL32(0, 0, 0, 150));
        dl->AddText(ImVec2(a.x + 4.0f, a.y + 2.0f), IM_COL32(255, 255, 255, 255), "new zone");

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_draggingZoneRect = false;
            const gameobj::RectF nr = normalizedFromSpriteRect(r, m_srcW, m_srcH);

            if (m_selectedZoneRectIndex >= 0 && m_selectedZoneRectIndex < (int)rects.size())
                rects[m_selectedZoneRectIndex] = nr;
            else
            {
                rects.push_back(nr);
                m_selectedZoneRectIndex = (int)rects.size() - 1;
            }

            if (m_zoneEditLayer == 0)
                m_solidBuf = true;

            m_lastStatus = std::string("Updated ") + zoneLayerJsonName(m_zoneEditLayer);
        }
    }
}

void ObjectEditor::renderSpriteColliderEditor(const gameobj::ObjectDef& def)
{
    ImGui::Text("Sprite collider / pivot editor");

    if (!def.has_sprite)
    {
        ImGui::TextDisabled("Object has no sprite.");
        return;
    }

    SDL_Texture* tex = textureForImage(def.image);
    if (!tex)
    {
        ImGui::TextDisabled("Atlas texture not loaded.");
        return;
    }

    int texW = 0;
    int texH = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);

    if (texW <= 0 || texH <= 0 || m_srcW <= 0 || m_srcH <= 0)
    {
        ImGui::TextDisabled("Set valid src rectangle first.");
        return;
    }

    ImGui::RadioButton("Drag collider", &m_spriteEditMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Move pivot", &m_spriteEditMode, 1);
    ImGui::SameLine();
    ImGui::SliderFloat("Preview zoom", &m_spritePreviewZoom, 0.25f, 6.0f, "%.2fx");

    ImGui::TextDisabled("Collider mode: left drag rectangle. Pivot mode: click/drag pivot point.");

    const ImVec2 uv0((float)m_srcX / (float)texW, (float)m_srcY / (float)texH);
    const ImVec2 uv1((float)(m_srcX + m_srcW) / (float)texW, (float)(m_srcY + m_srcH) / (float)texH);

    const float fitScale = std::min(520.0f / (float)m_srcW, 360.0f / (float)m_srcH);
    const float scale = std::clamp(fitScale * m_spritePreviewZoom, 0.10f, 12.0f);
    const ImVec2 imageSize((float)m_srcW * scale, (float)m_srcH * scale);

    ImGui::Image((ImTextureID)tex, imageSize, uv0, uv1);

    const ImVec2 imgMin = ImGui::GetItemRectMin();
    const ImVec2 imgMax = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto screenToSprite = [&](ImVec2 p) -> ImVec2
    {
        p = clampPointToRect(p, imgMin, imgMax);
        return ImVec2((p.x - imgMin.x) / scale, (p.y - imgMin.y) / scale);
    };

    auto spriteToScreen = [&](float x, float y) -> ImVec2
    {
        return ImVec2(imgMin.x + x * scale, imgMin.y + y * scale);
    };

    if (m_colliderEnabled && m_colW > 0 && m_colH > 0)
    {
        const ImVec2 a = spriteToScreen((float)m_colX, (float)m_colY);
        const ImVec2 b = spriteToScreen((float)(m_colX + m_colW), (float)(m_colY + m_colH));
        dl->AddRect(a, b, IM_COL32(255, 70, 70, 255), 0.0f, 0, 2.0f);
        dl->AddRectFilled(a, ImVec2(b.x, a.y + 18.0f), IM_COL32(0, 0, 0, 150));
        dl->AddText(ImVec2(a.x + 4.0f, a.y + 2.0f), IM_COL32(255, 255, 255, 255), "collider");
    }

    const ImVec2 pivot = spriteToScreen((float)m_pivotX, (float)m_pivotY);
    dl->AddLine(ImVec2(pivot.x - 8.0f, pivot.y), ImVec2(pivot.x + 8.0f, pivot.y), IM_COL32(80, 170, 255, 255), 2.0f);
    dl->AddLine(ImVec2(pivot.x, pivot.y - 8.0f), ImVec2(pivot.x, pivot.y + 8.0f), IM_COL32(80, 170, 255, 255), 2.0f);
    dl->AddCircle(pivot, 5.0f, IM_COL32(80, 170, 255, 255), 16, 2.0f);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const ImVec2 local = screenToSprite(ImGui::GetIO().MousePos);

        if (m_spriteEditMode == 0)
        {
            m_draggingColliderRect = true;
            m_dragStartLocal = local;
            m_dragEndLocal = local;
        }
        else
        {
            m_draggingPivot = true;
            m_dragStartLocal = local;
            m_dragEndLocal = local;
            m_pivotX = std::clamp((int)std::lround(local.x), 0, std::max(1, m_srcW));
            m_pivotY = std::clamp((int)std::lround(local.y), 0, std::max(1, m_srcH));
        }
    }

    if (m_draggingColliderRect)
    {
        m_dragEndLocal = screenToSprite(ImGui::GetIO().MousePos);
        const SDL_Rect r = normalizedRectFromPoints(m_dragStartLocal, m_dragEndLocal);

        const ImVec2 a = spriteToScreen((float)r.x, (float)r.y);
        const ImVec2 b = spriteToScreen((float)(r.x + r.w), (float)(r.y + r.h));
        dl->AddRect(a, b, IM_COL32(80, 255, 120, 255), 0.0f, 0, 2.0f);

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_draggingColliderRect = false;
            m_colX = std::clamp(r.x, 0, std::max(0, m_srcW - 1));
            m_colY = std::clamp(r.y, 0, std::max(0, m_srcH - 1));
            m_colW = std::clamp(r.w, 1, std::max(1, m_srcW - m_colX));
            m_colH = std::clamp(r.h, 1, std::max(1, m_srcH - m_colY));
            m_colliderEnabled = true;
        }
    }

    if (m_draggingPivot)
    {
        const ImVec2 local = screenToSprite(ImGui::GetIO().MousePos);
        m_pivotX = std::clamp((int)std::lround(local.x), 0, std::max(1, m_srcW));
        m_pivotY = std::clamp((int)std::lround(local.y), 0, std::max(1, m_srcH));

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_draggingPivot = false;
    }
}

void ObjectEditor::renderObjectPreview(const gameobj::ObjectDef& def)
{
    ImGui::Text("Preview");

    if (!def.has_sprite)
    {
        ImGui::TextDisabled("Object has no sprite.");
        return;
    }

    SDL_Texture* tex = textureForImage(def.image);
    if (!tex)
    {
        ImGui::TextDisabled("Atlas texture not loaded.");
        return;
    }

    int texW = 0;
    int texH = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);

    if (texW <= 0 || texH <= 0 || def.src.w <= 0 || def.src.h <= 0)
    {
        ImGui::TextDisabled("Invalid sprite rect.");
        return;
    }

    const ImVec2 uv0(
        (float)def.src.x / (float)texW,
        (float)def.src.y / (float)texH
    );

    const ImVec2 uv1(
        (float)(def.src.x + def.src.w) / (float)texW,
        (float)(def.src.y + def.src.h) / (float)texH
    );

    const float maxW = 360.0f;
    const float maxH = 260.0f;

    float scale = std::min(maxW / (float)def.src.w, maxH / (float)def.src.h);
    scale = std::clamp(scale, 0.25f, 4.0f);

    ImGui::Image(
        (ImTextureID)tex,
        ImVec2((float)def.src.w * scale, (float)def.src.h * scale),
        uv0,
        uv1
    );

    ImGui::TextDisabled("Atlas: %s", def.image.c_str());
}

void ObjectEditor::renderObjectDetails()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_entries.size())
    {
        ImGui::TextDisabled("Select object first.");
        return;
    }

    ObjectEditorEntry& entry = m_entries[m_selectedIndex];

    if (ImGui::Button("Apply"))
    {
        applyBuffersToSelected();
        m_lastStatus = "Object applied in memory.";
    }

    ImGui::SameLine();

    if (ImGui::Button("Save source"))
    {
        applyBuffersToSelected();
        saveSourceFile(entry.sourceFile);
    }

    ImGui::SameLine();

    if (ImGui::Button("Duplicate"))
    {
        applyBuffersToSelected();

        ObjectEditorEntry copy = entry;
        copy.def.id += "_copy";
        copy.def.name += " copy";

        m_entries.insert(m_entries.begin() + m_selectedIndex + 1, copy);
        m_selectedIndex++;
        m_entries[m_selectedIndex].def.id = makeUniqueIdForEntry(m_selectedIndex, m_entries[m_selectedIndex].def.id);
        loadSelectedToBuffers();

        m_lastStatus = "Object duplicated with unique ID.";
    }

    ImGui::Separator();

    ImGui::TextDisabled("Source: %s", entry.sourceFile.c_str());
    if (isForageSpriteSource(entry.sourceFile))
    {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
            "Forage sprite source: saved to data/foraging/forage_sprites.json, image loaded from assets/Foraging.");
    }

    const std::string currentId = trimCopy(m_idBuf);
    if (isDuplicateId(currentId, m_selectedIndex))
    {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
            "Duplicate ID in loaded catalogs: %s", currentId.c_str());

        if (ImGui::Button("Make this ID unique"))
        {
            const std::string newId = makeUniqueIdForEntry(m_selectedIndex, currentId);
            snprintf(m_idBuf, sizeof(m_idBuf), "%s", newId.c_str());
            m_lastStatus = "Object ID changed to: " + newId;
        }
    }

    ImGui::InputText("ID", m_idBuf, sizeof(m_idBuf));
    ImGui::InputText("Name", m_nameBuf, sizeof(m_nameBuf));
    ImGui::InputText("Image", m_imageBuf, sizeof(m_imageBuf));
    {
        const std::string normalizedImage = normalizeObjectImageRef(m_imageBuf);
        if (normalizedImage != trimCopy(m_imageBuf))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                "Will save as: %s", normalizedImage.c_str());
        }
    }
    ImGui::InputText("Tags", m_tagsBuf, sizeof(m_tagsBuf));

    ImGui::Checkbox("Enabled", &m_enabledBuf);
    ImGui::SameLine();
    ImGui::Checkbox("Has sprite", &m_hasSpriteBuf);
    ImGui::SameLine();
    ImGui::Checkbox("Solid", &m_solidBuf);
    ImGui::SameLine();
    ImGui::Checkbox("Unique", &m_uniqueBuf);

    ImGui::DragFloat("Default scale", &m_scale, 0.01f, 0.01f, 10.0f);

    ImGui::Separator();

    ImGui::Text("Sprite rect");
    ImGui::DragInt("src.x", &m_srcX, 1.0f, 0, 20000);
    ImGui::DragInt("src.y", &m_srcY, 1.0f, 0, 20000);
    ImGui::DragInt("src.w", &m_srcW, 1.0f, 0, 20000);
    ImGui::DragInt("src.h", &m_srcH, 1.0f, 0, 20000);

    ImGui::Separator();

    ImGui::Text("Pivot");
    ImGui::DragInt("pivot.x", &m_pivotX, 1.0f, -20000, 20000);
    ImGui::DragInt("pivot.y", &m_pivotY, 1.0f, -20000, 20000);

    if (ImGui::Button("Pivot bottom center"))
    {
        m_pivotX = std::max(0, m_srcW / 2);
        m_pivotY = std::max(0, m_srcH);
    }

    ImGui::Separator();

    ImGui::Text("Collider");
    ImGui::Checkbox("Collider enabled", &m_colliderEnabled);
    ImGui::DragInt("collider.x", &m_colX, 1.0f, -20000, 20000);
    ImGui::DragInt("collider.y", &m_colY, 1.0f, -20000, 20000);
    ImGui::DragInt("collider.w", &m_colW, 1.0f, 0, 20000);
    ImGui::DragInt("collider.h", &m_colH, 1.0f, 0, 20000);

    if (ImGui::Button("Collider from sprite"))
    {
        m_colX = 0;
        m_colY = 0;
        m_colW = std::max(1, m_srcW);
        m_colH = std::max(1, m_srcH);
        m_colliderEnabled = true;
    }

    ImGui::Separator();

    gameobj::ObjectDef preview = entry.def;
    preview.id = m_idBuf;
    preview.name = m_nameBuf;
    preview.image = m_imageBuf;
    preview.enabled = m_enabledBuf;
    preview.solid = m_solidBuf;
    preview.has_sprite = m_hasSpriteBuf;
    preview.unique = m_uniqueBuf;
    preview.src = gameobj::RectI{ m_srcX, m_srcY, m_srcW, m_srcH };
    preview.pivot = gameobj::PivotI{ m_pivotX, m_pivotY };
    preview.collider = gameobj::ColliderI{ m_colX, m_colY, m_colW, m_colH, m_colliderEnabled };
    preview.scale = m_scale;

    if (ImGui::BeginTabBar("ObjectEditorVisualTabs"))
    {
        if (ImGui::BeginTabItem("Preview"))
        {
            renderObjectPreview(preview);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Spritesheet src"))
        {
            renderAtlasSpriteSelector(preview);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Zones / colliders"))
        {
            applyBuffersToSelected();
            renderSpriteZonesEditor(entry.def);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Legacy collider / pivot"))
        {
            renderSpriteColliderEditor(preview);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

void ObjectEditor::render()
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("Object Editor", nullptr, flags))
    {
        ImGui::TextUnformatted(U8("Object editor"));
        ImGui::TextDisabled("ESC = back to main menu");
        ImGui::Separator();

        if (!m_lastStatus.empty())
            ImGui::TextWrapped("%s", m_lastStatus.c_str());

        ImGui::Columns(2, "ObjectEditorColumns", true);
        ImGui::SetColumnWidth(0, 360.0f);

        renderObjectList();

        ImGui::NextColumn();

        if (ImGui::BeginChild("ObjectDetails", ImVec2(0, 0), true))
            renderObjectDetails();

        ImGui::EndChild();

        ImGui::Columns(1);
    }

    ImGui::End();
}
