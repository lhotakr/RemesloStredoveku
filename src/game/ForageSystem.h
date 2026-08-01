#pragma once

#include "ForageTypes.h"
#include "ForageDatabase.h"
#include <string>
#include <vector>
#include <cstdint>

class ForageSystem
{
public:
    bool loadSpawnsForMap(const std::string& path, std::string* outError = nullptr);
    bool saveSpawnsForMap(const std::string& path, std::string* outError = nullptr);

    void clear();

    const std::vector<ForageSpawnDef>& spawns() const { return m_spawns; }
    std::vector<ForageSpawnDef>& spawns() { return m_spawns; }

    const ForageSpawnDef* findSpawnAt(int tileX, int tileY) const;
    ForageSpawnDef* findSpawnAt(int tileX, int tileY);

    const ForageSpeciesDef* pickSpeciesForSpawn(
        const ForageSpawnDef& spawn,
        const ForageDatabase& db,
        uint32_t worldSeed = 1337u) const;

    ForageExaminationResult examine(
        const ForageSpeciesDef& species,
        const std::vector<ForageExaminationAnswer>& answers,
        PlayerForageKnowledgeEntry& ioKnowledge,
        float foragingSkill,
        float observationSkill,
        float focusSkill,
        float memorySkill) const;

    ForageExaminationResult identify(
        const ForageSpeciesDef& species,
        const ForageIdentificationInput& input,
        PlayerForageKnowledgeEntry& ioKnowledge,
        float foragingSkill,
        float observationSkill,
        float focusSkill,
        float memorySkill,
        float natureComfort) const;

private:
    std::vector<ForageSpawnDef> m_spawns;
};