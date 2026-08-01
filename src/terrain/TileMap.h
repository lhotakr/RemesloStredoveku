#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gameobj { class ObjectCatalog; struct ObjectDef; }

class TileMap
{
public:
    TileMap(int width, int height, int tileSize);

    int width() const { return m_width; }
    int height() const { return m_height; }
    int tileSize() const { return m_tileSize; }

    // ===== Base terrain =====
    uint16_t get(int x, int y) const;
    void     set(int x, int y, uint16_t value);

    uint8_t  getVar(int x, int y) const;
    void     setVar(int x, int y, uint8_t v);

    // ===== Override (feature sprite) =====
    uint16_t getOverride(int x, int y) const;
    void     setOverride(int x, int y, uint16_t ov);
    void     clearOverride(int x, int y);

    // ===== Utility =====
    void clear();

    // ===== IO =====
    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

    // ===== Objects layer =====
    const gameobj::ObjectDef* getObjDefAt(const gameobj::ObjectCatalog& cat, int x, int y) const;

    void getObjPivotWorld(int tileX, int tileY, int& outWorldX, int& outWorldY) const;

    const std::string& getObjId(int x, int y) const;
    void setObjId(int x, int y, const std::string& objectId);
    bool hasObj(int x, int y) const;

    uint8_t  getObjVar(int x, int y) const;
    void     setObjVar(int x, int y, uint8_t v);

    uint16_t getObjHP(int x, int y) const;
    void     setObjHP(int x, int y, uint16_t hp);

    float    getObjScale(int x, int y) const;
    void     setObjScale(int x, int y, float scale);

    void     clearObj(int x, int y);


private:
    int m_width = 0;
    int m_height = 0;
    int m_tileSize = 0;

    // Base terrain
    std::vector<uint16_t> m_tiles;
    std::vector<uint8_t>  m_vars;

    // Feature sprite override
    std::vector<uint16_t> m_override;

    // Objects
    std::vector<std::string> m_objIds;
    std::vector<uint8_t>  m_objVars;
    std::vector<uint16_t> m_objHP;
    std::vector<float>    m_objScales;
    std::string m_emptyObjId;



};