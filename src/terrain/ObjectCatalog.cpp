#include "ObjectCatalog.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace gameobj
{
    using json = nlohmann::json;

    const gameobj::ObjectDef* gameobj::ObjectCatalog::findById(const std::string& id) const
    {
        return FindById(id);
    }

    std::string Trim(const std::string& s)
    {
        size_t b = 0, e = s.size();
        while (b < e && std::isspace((unsigned char)s[b])) ++b;
        while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
        return s.substr(b, e - b);
    }

    std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    static std::string FileStemForObjectIdPrefix(const std::string& path)
    {
        std::string file = path;

        const size_t slash = file.find_last_of("/\\");
        if (slash != std::string::npos)
            file = file.substr(slash + 1);

        const size_t dot = file.find_last_of('.');
        if (dot != std::string::npos)
            file = file.substr(0, dot);

        file = ToLower(Trim(file));

        std::string out;
        out.reserve(file.size());
        for (unsigned char c : file)
        {
            if (std::isalnum(c))
                out.push_back((char)c);
            else if (c == '_' || c == '-')
                out.push_back('_');
        }

        if (out.empty())
            out = "object";

        return out;
    }

    static bool ObjectIdExists(const std::vector<ObjectDef>& objects, const std::string& id)
    {
        for (const auto& o : objects)
        {
            if (o.id == id)
                return true;
        }
        return false;
    }

    static std::string MakeUniqueObjectId(
        const std::vector<ObjectDef>& objects,
        const std::string& requestedId,
        const std::string& sourcePath)
    {
        std::string id = Trim(requestedId);
        if (id.empty())
            id = "obj_auto_" + std::to_string(objects.size() + 1);

        if (!ObjectIdExists(objects, id))
            return id;

        const std::string prefix = FileStemForObjectIdPrefix(sourcePath);
        std::string candidate = prefix + "_" + id;

        if (!ObjectIdExists(objects, candidate))
            return candidate;

        for (int n = 2; ; ++n)
        {
            candidate = prefix + "_" + id + "_" + std::to_string(n);
            if (!ObjectIdExists(objects, candidate))
                return candidate;
        }
    }

    bool ObjectDef::HasTag(const std::string& token) const
    {
        const std::string t = ToLower(Trim(token));
        if (t.empty()) return false;
        return std::find(tags.begin(), tags.end(), t) != tags.end();
    }

    static std::string ReadAllText(const std::string& path, std::string* outError)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            if (outError) *outError = "ObjectCatalog: cannot open file: " + path;
            return {};
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static int GetInt(const json& j, const char* key, int def)
    {
        try {
            if (!j.contains(key)) return def;
            const auto& v = j.at(key);
            if (v.is_number_integer()) return v.get<int>();
            if (v.is_number_float()) return (int)v.get<double>();
            if (v.is_string()) return std::stoi(v.get<std::string>());
            return def;
        }
        catch (...) { return def; }
    }

    static bool GetBool(const json& j, const char* key, bool def)
    {
        try {
            if (!j.contains(key)) return def;
            const auto& v = j.at(key);
            if (v.is_boolean()) return v.get<bool>();
            if (v.is_number_integer()) return v.get<int>() != 0;
            if (v.is_string()) {
                std::string s = ToLower(Trim(v.get<std::string>()));
                if (s == "true" || s == "1" || s == "yes") return true;
                if (s == "false" || s == "0" || s == "no") return false;
            }
            return def;
        }
        catch (...) { return def; }
    }

    static std::string GetString(const json& j, const char* key, const std::string& def)
    {
        try {
            if (!j.contains(key)) return def;
            const auto& v = j.at(key);
            if (v.is_string()) return v.get<std::string>();
            return def;
        }
        catch (...) { return def; }
    }

    static RectI ParseRect(const json& j, const char* key, RectI def)
    {
        try {
            if (!j.contains(key) || !j.at(key).is_object()) return def;
            const auto& r = j.at(key);
            def.x = GetInt(r, "x", def.x);
            def.y = GetInt(r, "y", def.y);
            def.w = GetInt(r, "w", def.w);
            def.h = GetInt(r, "h", def.h);
            return def;
        }
        catch (...) { return def; }
    }

    static PivotI ParsePivot(const json& j, const char* key, PivotI def)
    {
        try {
            if (!j.contains(key) || !j.at(key).is_object()) return def;
            const auto& p = j.at(key);
            def.x = GetInt(p, "x", def.x);
            def.y = GetInt(p, "y", def.y);
            return def;
        }
        catch (...) { return def; }
    }

    static ColliderI ParseCollider(const json& j, const char* key, ColliderI def)
    {
        try {
            if (!j.contains(key) || !j.at(key).is_object()) return def;
            const auto& c = j.at(key);
            def.x = GetInt(c, "x", def.x);
            def.y = GetInt(c, "y", def.y);
            def.w = GetInt(c, "w", def.w);
            def.h = GetInt(c, "h", def.h);
            def.enabled = GetBool(c, "enabled", def.enabled);
            return def;
        }
        catch (...) { return def; }
    }

    static std::vector<RectF> ParseRectFArray(const json& j, const char* key)
    {
        std::vector<RectF> out;

        try
        {
            if (!j.contains(key) || !j.at(key).is_array())
                return out;

            for (const auto& it : j.at(key))
            {
                RectF r{};

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
        catch (...)
        {
        }

        return out;
    }

    static std::vector<SDL_Rect> GetWorldRectsFromNormalized(
        const std::vector<RectF>& rects,
        const ObjectDef& def,
        int wx,
        int wy,
        float scale)
    {
        std::vector<SDL_Rect> out;

        const float srcW = (float)def.src.w * scale;
        const float srcH = (float)def.src.h * scale;

        const float worldLeft = (float)wx - def.pivot.x * scale;
        const float worldTop = (float)wy - def.pivot.y * scale;

        for (const auto& rc : rects)
        {
            SDL_Rect r{};
            r.x = (int)std::lround(worldLeft + rc.x * srcW);
            r.y = (int)std::lround(worldTop + rc.y * srcH);
            r.w = (int)std::lround(rc.w * srcW);
            r.h = (int)std::lround(rc.h * srcH);

            if (r.w > 0 && r.h > 0)
                out.push_back(r);
        }

        return out;
    }

    std::vector<SDL_Rect> GetWorldColliderRects(
    const ObjectDef& def,
    int wx,
    int wy,
    float scale)
    {
        if (!def.collisionRects.empty())
            return GetWorldRectsFromNormalized(def.collisionRects, def, wx, wy, scale);

        std::vector<SDL_Rect> out;

        if (def.collider.enabled && def.collider.w > 0 && def.collider.h > 0)
        {
            SDL_Rect r{};

            const float worldLeft = (float)wx - def.pivot.x * scale;
            const float worldTop  = (float)wy - def.pivot.y * scale;

            r.x = (int)std::lround(worldLeft + def.collider.x * scale);
            r.y = (int)std::lround(worldTop  + def.collider.y * scale);
            r.w = (int)std::lround(def.collider.w * scale);
            r.h = (int)std::lround(def.collider.h * scale);

            if (r.w > 0 && r.h > 0)
                out.push_back(r);
        }

        return out;
    }

    std::vector<SDL_Rect> GetWorldWalkableRects(
    const ObjectDef& def,
    int wx,
    int wy,
    float scale)
    {
        return GetWorldRectsFromNormalized(def.walkableRects, def, wx, wy, scale);
    }

    std::vector<SDL_Rect> GetWorldFadeRects(
        const ObjectDef& def,
        int wx,
        int wy,
        float scale)
    {
        return GetWorldRectsFromNormalized(def.fadeRects, def, wx, wy, scale);
    }

    void ObjectCatalog::TokenizeTags(const std::vector<std::string>& raw, std::vector<std::string>& outTokens)
    {
        outTokens.clear();
        for (const auto& r0 : raw)
        {
            std::string s = ToLower(Trim(r0));
            if (s.empty()) continue;

            for (char& c : s) {
                if (c == '.' || c == ',' || c == ';' || c == '|' || c == '\t' || c == '\n' || c == '\r')
                    c = ' ';
            }

            std::string cur;
            for (char c : s) {
                if (std::isspace((unsigned char)c)) {
                    if (!cur.empty()) { outTokens.push_back(cur); cur.clear(); }
                }
                else cur.push_back(c);
            }
            if (!cur.empty()) outTokens.push_back(cur);
        }

        std::sort(outTokens.begin(), outTokens.end());
        outTokens.erase(std::unique(outTokens.begin(), outTokens.end()), outTokens.end());
    }

    void ObjectCatalog::RebuildIndex()
    {
        m_byId.clear();
        m_byId.reserve(m_objects.size());
        for (size_t i = 0; i < m_objects.size(); ++i) {
            if (!m_objects[i].id.empty())
                m_byId[m_objects[i].id] = i;
        }
    }

    bool ObjectCatalog::LoadFromFile(const std::string& path, std::string* outError)
    {
        m_objects.clear();
        m_byId.clear();
        m_image.clear();

        return AppendFromFile(path, outError);
    }

    bool ObjectCatalog::LoadFromString(const std::string& text, std::string* outError)
    {
        try {
            const json root = json::parse(text);

            const std::string defaultImage = Trim(GetString(root, "image", ""));
            m_image = defaultImage;

            m_objects.clear();
            m_byId.clear();

            if (!root.contains("objects") || !root.at("objects").is_array()) {
                if (outError) *outError = "ObjectCatalog: JSON missing array 'objects'";
                return false;
            }

            const auto& arr = root.at("objects");
            m_objects.reserve(arr.size());

            size_t autoId = 0;
            for (const auto& it : arr)
            {
                if (!it.is_object()) continue;

                ObjectDef o;
                o.id = Trim(GetString(it, "id", ""));
                if (o.id.empty()) {
                    ++autoId;
                    o.id = "obj_auto_" + std::to_string(autoId);
                }

                o.id = MakeUniqueObjectId(m_objects, o.id, "inline_objects");

                o.name = Trim(GetString(it, "name", o.id));
                o.image = Trim(GetString(it, "image", defaultImage));

                o.enabled = GetBool(it, "enabled", true);
                o.solid = GetBool(it, "solid", false);
                o.has_sprite = GetBool(it, "has_sprite", true);
                o.unique = GetBool(it, "unique", false);

                o.src = ParseRect(it, "src", RectI{ 0,0,0,0 });

                PivotI pdef;
                pdef.x = (o.src.w > 0) ? (o.src.w / 2) : 0;
                pdef.y = (o.src.h > 0) ? (o.src.h) : 0;
                o.pivot = ParsePivot(it, "pivot", pdef);

                ColliderI cdef;
                cdef.x = 0; cdef.y = 0;
                cdef.w = (o.src.w > 0) ? o.src.w : 1;
                cdef.h = (o.src.h > 0) ? o.src.h : 1;
                cdef.enabled = true;
                o.collider = ParseCollider(it, "collider", cdef);

                o.tags_raw.clear();
                if (it.contains("tags") && it.at("tags").is_array()) {
                    for (const auto& t : it.at("tags")) {
                        if (t.is_string()) {
                            auto s = Trim(t.get<std::string>());
                            if (!s.empty()) o.tags_raw.push_back(s);
                        }
                    }
                }
                TokenizeTags(o.tags_raw, o.tags);

                o.collisionRects = ParseRectFArray(it, "collision_rects");
                o.fadeRects = ParseRectFArray(it, "fade_rects");
                o.walkableRects = ParseRectFArray(it, "walkable_rects");

                o.scale = 1.0f;
                try {
                    if (it.contains("scale"))
                    {
                        const auto& v = it.at("scale");
                        if (v.is_number_float() || v.is_number_integer())
                            o.scale = (float)v.get<double>();
                        else if (v.is_string())
                            o.scale = std::stof(v.get<std::string>());
                    }
                }
                catch (...) {
                    o.scale = 1.0f;
                }

                if (o.scale <= 0.01f)
                    o.scale = 1.0f;

                m_objects.push_back(std::move(o));
            }

            RebuildIndex();
            return true;
        }
        catch (const std::exception& ex) {
            if (outError) *outError = std::string("ObjectCatalog: JSON error: ") + ex.what();
            return false;
        }
    }

    bool ObjectCatalog::AppendFromFile(const std::string& path, std::string* outError)
    {
        const std::string text = ReadAllText(path, outError);
        if (text.empty()) return false;

        try {
            const json root = json::parse(text);

            const std::string defaultImage = Trim(GetString(root, "image", ""));

            if (!root.contains("objects") || !root.at("objects").is_array()) {
                if (outError) *outError = "ObjectCatalog: JSON missing array 'objects'";
                return false;
            }

            const auto& arr = root.at("objects");

            size_t autoId = 0;
            for (const auto& it : arr)
            {
                if (!it.is_object()) continue;

                ObjectDef o;

                o.id = Trim(GetString(it, "id", ""));
                if (o.id.empty()) {
                    ++autoId;
                    o.id = "obj_auto_" + std::to_string(m_objects.size() + autoId);
                }

                o.id = MakeUniqueObjectId(m_objects, o.id, path);

                o.name = Trim(GetString(it, "name", o.id));
                o.image = Trim(GetString(it, "image", defaultImage));

                o.enabled = GetBool(it, "enabled", true);
                o.solid = GetBool(it, "solid", false);
                o.has_sprite = GetBool(it, "has_sprite", true);
                o.unique = GetBool(it, "unique", false);

                o.src = ParseRect(it, "src", RectI{ 0,0,0,0 });

                PivotI pdef;
                pdef.x = (o.src.w > 0) ? (o.src.w / 2) : 0;
                pdef.y = (o.src.h > 0) ? (o.src.h) : 0;
                o.pivot = ParsePivot(it, "pivot", pdef);

                ColliderI cdef;
                cdef.x = 0; cdef.y = 0;
                cdef.w = (o.src.w > 0) ? o.src.w : 1;
                cdef.h = (o.src.h > 0) ? o.src.h : 1;
                cdef.enabled = true;
                o.collider = ParseCollider(it, "collider", cdef);

                o.tags_raw.clear();
                if (it.contains("tags") && it.at("tags").is_array()) {
                    for (const auto& t : it.at("tags")) {
                        if (t.is_string()) {
                            auto s = Trim(t.get<std::string>());
                            if (!s.empty()) o.tags_raw.push_back(s);
                        }
                    }
                }
                TokenizeTags(o.tags_raw, o.tags);

                o.collisionRects = ParseRectFArray(it, "collision_rects");
                o.fadeRects = ParseRectFArray(it, "fade_rects");
                o.walkableRects = ParseRectFArray(it, "walkable_rects");
                o.sourceFile = path;

                o.scale = 1.0f;
                try {
                    if (it.contains("scale"))
                    {
                        const auto& v = it.at("scale");
                        if (v.is_number_float() || v.is_number_integer())
                            o.scale = (float)v.get<double>();
                        else if (v.is_string())
                            o.scale = std::stof(v.get<std::string>());
                    }
                }
                catch (...) {
                    o.scale = 1.0f;
                }

                if (o.scale <= 0.01f)
                    o.scale = 1.0f;

                m_objects.push_back(std::move(o));
            }

            RebuildIndex();
            return true;
        }
        catch (const std::exception& ex) {
            if (outError) *outError = std::string("ObjectCatalog: JSON error: ") + ex.what();
            return false;
        }
    }

    const ObjectDef* ObjectCatalog::FindById(const std::string& id) const
    {
        auto it = m_byId.find(id);
        if (it == m_byId.end()) return nullptr;
        return &m_objects[it->second];
    }

    const ObjectDef* ObjectCatalog::FindByName(const std::string& name) const
    {
        const std::string n = ToLower(Trim(name));
        for (const auto& o : m_objects) {
            if (ToLower(o.name) == n) return &o;
        }
        return nullptr;
    }

}