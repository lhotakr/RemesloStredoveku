#include "TileMap.h"
#include "ObjectCatalog.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>

static inline bool inBounds(int x, int y, int w, int h)
{
    return (x >= 0 && y >= 0 && x < w && y < h);
}

TileMap::TileMap(int width, int height, int tileSize)
    : m_width(width)
    , m_height(height)
    , m_tileSize(tileSize)
    , m_tiles(width * height, 1)
    , m_vars(width * height, 0)
    , m_override(width * height, 0)
    , m_objIds(width * height)
    , m_objVars(width * height, 0)
    , m_objHP(width * height, 0)
    , m_objScales(width * height, 1.0f)
{
}

uint16_t TileMap::get(int x, int y) const
{
    if (!inBounds(x, y, m_width, m_height)) return 0;
    return m_tiles[y * m_width + x];
}

void TileMap::set(int x, int y, uint16_t value)
{
    if (!inBounds(x, y, m_width, m_height)) return;
    m_tiles[y * m_width + x] = value;
}

uint8_t TileMap::getVar(int x, int y) const
{
    if (!inBounds(x, y, m_width, m_height)) return 0;
    return m_vars[y * m_width + x];
}

void TileMap::setVar(int x, int y, uint8_t v)
{
    if (!inBounds(x, y, m_width, m_height)) return;
    m_vars[y * m_width + x] = v;
}

uint16_t TileMap::getOverride(int x, int y) const
{
    if (!inBounds(x, y, m_width, m_height)) return 0;
    return m_override[y * m_width + x];
}

void TileMap::setOverride(int x, int y, uint16_t ov)
{
    if (!inBounds(x, y, m_width, m_height)) return;
    m_override[y * m_width + x] = ov;
}

void TileMap::clearOverride(int x, int y)
{
    setOverride(x, y, 0);
}

// ===== Objects =====

const std::string& TileMap::getObjId(int x, int y) const
{
    if (!inBounds(x, y, m_width, m_height))
        return m_emptyObjId;
    return m_objIds[y * m_width + x];
}

void TileMap::setObjId(int x, int y, const std::string& objectId)
{
    if (!inBounds(x, y, m_width, m_height))
        return;
    m_objIds[y * m_width + x] = objectId;
}

bool TileMap::hasObj(int x, int y) const
{
    return !getObjId(x, y).empty();
}

uint8_t TileMap::getObjVar(int x, int y) const
{
    if (!inBounds(x, y, m_width, m_height)) return 0;
    return m_objVars[y * m_width + x];
}

void TileMap::setObjVar(int x, int y, uint8_t v)
{
    if (!inBounds(x, y, m_width, m_height)) return;
    m_objVars[y * m_width + x] = v;
}

uint16_t TileMap::getObjHP(int x, int y) const
{
    if (!inBounds(x, y, m_width, m_height)) return 0;
    return m_objHP[y * m_width + x];
}

void TileMap::setObjHP(int x, int y, uint16_t hp)
{
    if (!inBounds(x, y, m_width, m_height)) return;
    m_objHP[y * m_width + x] = hp;
}

float TileMap::getObjScale(int x, int y) const
{
    if (!inBounds(x, y, m_width, m_height)) return 1.0f;
    return m_objScales[y * m_width + x];
}

void TileMap::setObjScale(int x, int y, float scale)
{
    if (!inBounds(x, y, m_width, m_height)) return;
    m_objScales[y * m_width + x] = std::max(0.05f, scale);
}

void TileMap::clearObj(int x, int y)
{
    if (!inBounds(x, y, m_width, m_height)) return;
    const int i = y * m_width + x;
    m_objIds[i].clear();
    m_objVars[i] = 0;
    m_objHP[i] = 0;
    m_objScales[i] = 1.0f;
}

// ---------------- IO ----------------

static bool parseCellTokenRvm4(
    const std::string& tok,
    uint16_t& outTile,
    uint8_t& outVar,
    uint16_t& outOverride,
    std::string& outObjId,
    uint8_t& outObjVar,
    uint16_t& outObjHP,
    float& outObjScale)
{
    outTile = 0;
    outVar = 0;
    outOverride = 0;
    outObjId.clear();
    outObjVar = 0;
    outObjHP = 0;
    outObjScale = 1.0f;

    std::vector<std::string> parts;
    parts.reserve(7);

    size_t start = 0;
    while (true)
    {
        size_t pos = tok.find(':', start);
        if (pos == std::string::npos)
        {
            parts.push_back(tok.substr(start));
            break;
        }
        parts.push_back(tok.substr(start, pos - start));
        start = pos + 1;
    }

    if (parts.size() != 6 && parts.size() != 7)
        return false;

    auto toInt = [](const std::string& s, int& out) -> bool {
        try { out = std::stoi(s); return true; }
        catch (...) { return false; }
    };

    auto toFloat = [](const std::string& s, float& out) -> bool {
        try { out = std::stof(s); return true; }
        catch (...) { return false; }
    };

    int t = 0, v = 0, ovw = 0, ov = 0, ohp = 0;
    float osc = 1.0f;

    if (!toInt(parts[0], t)) return false;
    if (!toInt(parts[1], v)) return false;
    if (!toInt(parts[2], ovw)) return false;
    if (!toInt(parts[4], ov)) return false;
    if (!toInt(parts[5], ohp)) return false;

    if (parts.size() == 7)
    {
        if (!toFloat(parts[6], osc))
            return false;
    }

    t = std::clamp(t, 0, 65535);
    v = std::clamp(v, 0, 255);
    ovw = std::clamp(ovw, 0, 65535);
    ov = std::clamp(ov, 0, 255);
    ohp = std::clamp(ohp, 0, 65535);
    osc = std::max(0.05f, osc);

    outTile = (uint16_t)t;
    outVar = (uint8_t)v;
    outOverride = (uint16_t)ovw;

    if (parts[3] != "-" && !parts[3].empty())
        outObjId = parts[3];

    outObjVar = (uint8_t)ov;
    outObjHP = (uint16_t)ohp;
    outObjScale = osc;

    return true;
}

void TileMap::clear()
{
    std::fill(m_tiles.begin(), m_tiles.end(), 1);
    std::fill(m_vars.begin(), m_vars.end(), 0);
    std::fill(m_override.begin(), m_override.end(), 0);
    std::fill(m_objIds.begin(), m_objIds.end(), std::string{});
    std::fill(m_objVars.begin(), m_objVars.end(), 0);
    std::fill(m_objHP.begin(), m_objHP.end(), 0);
    std::fill(m_objScales.begin(), m_objScales.end(), 1.0f);
}

bool TileMap::saveToFile(const std::string& path) const
{
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f) return false;

    f << "RVM4\n";
    f << m_width << " " << m_height << " " << m_tileSize << "\n";

    for (int y = 0; y < m_height; ++y)
    {
        for (int x = 0; x < m_width; ++x)
        {
            const int i = y * m_width + x;

            f << m_tiles[i]
              << ":" << (int)m_vars[i]
              << ":" << m_override[i]
              << ":" << (m_objIds[i].empty() ? "-" : m_objIds[i])
              << ":" << (int)m_objVars[i]
              << ":" << m_objHP[i]
              << ":" << m_objScales[i];

            if (x + 1 < m_width)
                f << " ";
        }
        f << "\n";
    }

    return true;
}

bool TileMap::loadFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return false;

    std::string magic;
    std::getline(f, magic);

    if (magic != "RVM4")
        return false;

    int w = 0, h = 0, ts = 0;
    f >> w >> h >> ts;
    if (w <= 0 || h <= 0 || ts <= 0) return false;

    m_width = w;
    m_height = h;
    m_tileSize = ts;

    m_tiles.assign(m_width * m_height, 1);
    m_vars.assign(m_width * m_height, 0);
    m_override.assign(m_width * m_height, 0);
    m_objIds.assign(m_width * m_height, std::string{});
    m_objVars.assign(m_width * m_height, 0);
    m_objHP.assign(m_width * m_height, 0);
    m_objScales.assign(m_width * m_height, 1.0f);

    std::string line;
    std::getline(f, line);

    for (int y = 0; y < m_height; ++y)
    {
        for (int x = 0; x < m_width; ++x)
        {
            std::string tok;
            if (!(f >> tok)) return false;

            uint16_t t = 0, ovw = 0, ohp = 0;
            uint8_t v = 0, ov = 0;
            float osc = 1.0f;
            std::string objId;

            if (!parseCellTokenRvm4(tok, t, v, ovw, objId, ov, ohp, osc))
                return false;

            const int i = y * m_width + x;
            m_tiles[i] = t;
            m_vars[i] = v;
            m_override[i] = ovw;
            m_objIds[i] = objId;
            m_objVars[i] = ov;
            m_objHP[i] = ohp;
            m_objScales[i] = osc;
        }
    }

    return true;
}

// --- Objects -> definitions ---

const gameobj::ObjectDef* TileMap::getObjDefAt(const gameobj::ObjectCatalog& cat, int x, int y) const
{
    const std::string& objectId = getObjId(x, y);
    if (objectId.empty())
        return nullptr;

    return cat.findById(objectId);
}

void TileMap::getObjPivotWorld(int tileX, int tileY, int& outWorldX, int& outWorldY) const
{
    outWorldX = tileX * m_tileSize + (m_tileSize / 2);
    outWorldY = tileY * m_tileSize + m_tileSize;
}