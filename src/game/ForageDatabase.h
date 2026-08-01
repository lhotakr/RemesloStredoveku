#pragma once

#include "ForageTypes.h"
#include <string>
#include <unordered_map>
#include <vector>

class ForageDatabase
{
public:
    bool loadAll(
        const std::string& archetypesPath,
        const std::string& speciesPath,
        const std::string& traitsPath,
        std::string* outError = nullptr);

    void clear();

    const ForageArchetypeDef* findArchetype(const std::string& id) const;
    const ForageSpeciesDef* findSpecies(const std::string& id) const;
    const ForageTraitGroup* findTraitGroup(const std::string& id) const;

    const std::unordered_map<std::string, ForageArchetypeDef>& archetypes() const { return m_archetypes; }
    const std::unordered_map<std::string, ForageSpeciesDef>& species() const { return m_species; }
    const std::unordered_map<std::string, ForageTraitGroup>& traitGroups() const { return m_traitGroups; }

private:
    bool loadArchetypes(const std::string& path, std::string* outError);
    bool loadSpecies(const std::string& path, std::string* outError);
    bool loadTraits(const std::string& path, std::string* outError);

private:
    std::unordered_map<std::string, ForageArchetypeDef> m_archetypes;
    std::unordered_map<std::string, ForageSpeciesDef> m_species;
    std::unordered_map<std::string, ForageTraitGroup> m_traitGroups;
};