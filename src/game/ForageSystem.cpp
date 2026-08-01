#include "ForageSystem.h"
#include "../JsonUtils.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <unordered_set>
#include <sstream>
#include <cctype>
#include <limits>

using json = nlohmann::json;

static float JsonFloatAny(const json& j, float def, std::initializer_list<const char*> keys)
{
    try
    {
        for (const char* key : keys)
        {
            if (!j.contains(key))
                continue;

            const auto& v = j.at(key);
            if (v.is_number_float() || v.is_number_integer())
                return v.get<float>();

            if (v.is_string())
                return std::stof(v.get<std::string>());
        }
    }
    catch (...)
    {
    }

    return def;
}

static std::vector<std::string> ReadStringArray(const json& j)
{
    std::vector<std::string> out;
    if (!j.is_array())
        return out;

    for (const auto& v : j)
    {
        if (v.is_string())
            out.push_back(v.get<std::string>());
    }

    return out;
}


static std::string NormalizeForageText(std::string s)
{
    std::string out;
    out.reserve(s.size());

    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c >= 128)
            out.push_back((char)std::tolower(c));
        else
            out.push_back(' ');
    }

    std::string compact;
    bool lastSpace = true;
    for (char c : out)
    {
        if (std::isspace((unsigned char)c))
        {
            if (!lastSpace)
                compact.push_back(' ');
            lastSpace = true;
        }
        else
        {
            compact.push_back(c);
            lastSpace = false;
        }
    }

    if (!compact.empty() && compact.back() == ' ')
        compact.pop_back();

    return compact;
}

static std::vector<std::string> TokenizeForageText(const std::string& text)
{
    std::vector<std::string> tokens;
    std::istringstream ss(NormalizeForageText(text));
    std::string t;
    while (ss >> t)
    {
        if (t.size() >= 2)
            tokens.push_back(t);
    }
    return tokens;
}


static std::string TrimForageText(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

static std::vector<std::string> SplitForageTextParts(const std::string& text)
{
    std::vector<std::string> out;
    std::string current;

    for (char c : text)
    {
        if (c == ';')
        {
            std::string part = TrimForageText(current);
            if (!part.empty())
                out.push_back(part);
            current.clear();
        }
        else
        {
            current.push_back(c);
        }
    }

    std::string part = TrimForageText(current);
    if (!part.empty())
        out.push_back(part);

    return out;
}

static int LevenshteinDistance(const std::string& a, const std::string& b)
{
    if (a.empty()) return (int)b.size();
    if (b.empty()) return (int)a.size();

    std::vector<int> prev(b.size() + 1);
    std::vector<int> cur(b.size() + 1);

    for (size_t j = 0; j <= b.size(); ++j)
        prev[j] = (int)j;

    for (size_t i = 1; i <= a.size(); ++i)
    {
        cur[0] = (int)i;
        for (size_t j = 1; j <= b.size(); ++j)
        {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({
                prev[j] + 1,
                cur[j - 1] + 1,
                prev[j - 1] + cost
            });
        }
        prev.swap(cur);
    }

    return prev[b.size()];
}

static float FuzzyTextRatio(const std::string& userRaw, const std::string& expectedRaw)
{
    const std::string user = NormalizeForageText(userRaw);
    const std::string expected = NormalizeForageText(expectedRaw);

    if (user.empty() || expected.empty())
        return 0.0f;

    if (user == expected)
        return 1.0f;

    if (user.find(expected) != std::string::npos || expected.find(user) != std::string::npos)
        return 0.92f;

    const int maxLen = std::max((int)user.size(), (int)expected.size());
    const int dist = LevenshteinDistance(user, expected);
    const float charScore = maxLen > 0 ? 1.0f - ((float)dist / (float)maxLen) : 0.0f;

    const auto userTokens = TokenizeForageText(user);
    const auto expectedTokens = TokenizeForageText(expected);
    float tokenScore = 0.0f;

    if (!userTokens.empty() && !expectedTokens.empty())
    {
        int matched = 0;
        for (const auto& u : userTokens)
        {
            for (const auto& e : expectedTokens)
            {
                if (u == e || u.find(e) != std::string::npos || e.find(u) != std::string::npos)
                {
                    ++matched;
                    break;
                }
            }
        }

        tokenScore = (float)matched / (float)std::max(userTokens.size(), expectedTokens.size());
    }

    return std::clamp(std::max(charScore, tokenScore), 0.0f, 1.0f);
}

static std::vector<std::string> ExpandExpectedParts(const std::vector<std::string>& expectedTexts)
{
    std::vector<std::string> out;
    for (const auto& expected : expectedTexts)
    {
        auto parts = SplitForageTextParts(expected);
        if (parts.empty() && !TrimForageText(expected).empty())
            out.push_back(expected);
        else
            out.insert(out.end(), parts.begin(), parts.end());
    }
    return out;
}

static float BestSemicolonFuzzyScore(const std::string& userText, const std::vector<std::string>& expectedTexts)
{
    const auto userParts = SplitForageTextParts(userText);
    const auto expectedParts = ExpandExpectedParts(expectedTexts);

    if (userParts.empty() || expectedParts.empty())
        return 0.0f;

    float best = 0.0f;
    for (const auto& user : userParts)
    {
        for (const auto& expected : expectedParts)
            best = std::max(best, FuzzyTextRatio(user, expected));
    }

    // 75% textual similarity is accepted as a correct observation.
    if (best >= 0.75f)
        return 1.0f;

    return best;
}

static float CoverageSemicolonFuzzyScore(const std::string& userText, const std::vector<std::string>& expectedTexts)
{
    const auto userParts = SplitForageTextParts(userText);
    const auto expectedParts = ExpandExpectedParts(expectedTexts);

    if (userParts.empty() || expectedParts.empty())
        return 0.0f;

    float sum = 0.0f;
    int count = 0;

    for (const auto& expected : expectedParts)
    {
        if (TrimForageText(expected).empty())
            continue;

        float best = 0.0f;
        for (const auto& user : userParts)
            best = std::max(best, FuzzyTextRatio(user, expected));

        sum += (best >= 0.75f) ? 1.0f : best;
        ++count;
    }

    if (count <= 0)
        return 0.0f;

    return std::clamp(sum / (float)count, 0.0f, 1.0f);
}

static bool TextContainsPhraseOrToken(const std::string& haystackRaw, const std::string& needleRaw)
{
    const std::string haystack = NormalizeForageText(haystackRaw);
    const std::string needle = NormalizeForageText(needleRaw);

    if (haystack.empty() || needle.empty())
        return false;

    if (haystack.find(needle) != std::string::npos)
        return true;

    const auto needleTokens = TokenizeForageText(needle);
    if (needleTokens.empty())
        return false;

    int matched = 0;
    for (const auto& token : needleTokens)
    {
        if (haystack.find(token) != std::string::npos)
            ++matched;
    }

    return matched >= std::max(1, (int)std::ceil((float)needleTokens.size() * 0.75f));
}

static float TokenOverlapScore(const std::string& userText, const std::vector<std::string>& expectedTexts)
{
    std::unordered_set<std::string> expected;
    for (const auto& e : expectedTexts)
    {
        for (const auto& token : TokenizeForageText(e))
            expected.insert(token);
    }

    if (expected.empty())
        return 0.0f;

    std::unordered_set<std::string> user;
    for (const auto& token : TokenizeForageText(userText))
        user.insert(token);

    if (user.empty())
        return 0.0f;

    int matched = 0;
    for (const auto& token : user)
    {
        if (expected.contains(token))
            ++matched;
    }

    return std::clamp((float)matched / (float)std::max(1, (int)expected.size()), 0.0f, 1.0f);
}

static std::vector<std::string> BuildSpeciesExpectedTexts(const ForageSpeciesDef& species)
{
    std::vector<std::string> expected;
    expected.push_back(species.id);
    expected.push_back(species.trueName);
    expected.push_back(species.description);
    expected.insert(expected.end(), species.folkNames.begin(), species.folkNames.end());

    for (const auto& kv : species.traits)
        expected.insert(expected.end(), kv.second.begin(), kv.second.end());

    return expected;
}

static float NameGuessScore(const ForageSpeciesDef& species, const std::string& guess)
{
    const std::string g = NormalizeForageText(guess);
    if (g.empty())
        return 0.0f;

    std::vector<std::string> names;
    names.push_back(species.trueName);
    names.push_back(species.id);
    names.insert(names.end(), species.folkNames.begin(), species.folkNames.end());

    float best = BestSemicolonFuzzyScore(guess, names);
    return std::clamp(best, 0.0f, 1.0f);
}

static ForageExaminationResult BuildIdentificationResult(
    const ForageSpeciesDef& species,
    const ForageIdentificationInput& input,
    PlayerForageKnowledgeEntry& ioKnowledge,
    float foragingSkill,
    float observationSkill,
    float focusSkill,
    float memorySkill,
    float natureComfort)
{
    ForageExaminationResult r{};

    int traitTotal = 0;
    int traitMatched = 0;
    float traitAccum = 0.0f;

    for (const auto& kv : species.traits)
    {
        const auto& expected = kv.second;
        if (expected.empty())
            continue;

        ++traitTotal;
        auto itInput = input.traitTexts.find(kv.first);
        const std::string user = (itInput != input.traitTexts.end()) ? itInput->second : std::string{};

        const float localRaw = CoverageSemicolonFuzzyScore(user, expected);
        const float local = localRaw >= 0.75f ? 1.0f : localRaw;

        if (local >= 0.75f)
        {
            ++traitMatched;
            for (const auto& e : expected)
            {
                if (!e.empty())
                {
                    ioKnowledge.knownTraits.insert(e);
                    r.newlyKnownTraits.push_back(e);
                }
            }
        }

        traitAccum += local;
    }

    const float traitRatio = traitTotal > 0 ? traitAccum / (float)traitTotal : 0.0f;
    r.traitScore = traitRatio * 42.0f;

    const auto expectedTexts = BuildSpeciesExpectedTexts(species);
    const float descRatio = CoverageSemicolonFuzzyScore(input.descriptionText, expectedTexts);
    r.descriptionScore = (descRatio >= 0.75f ? 1.0f : descRatio) * 18.0f;

    // If the player knows the name very accurately, the identification should be valid
    // even without filling every observed trait. This keeps the minigame from feeling
    // punitive when the player already recognizes a common plant/fungus.
    const float nameRatio = NameGuessScore(species, input.nameGuess);
    r.nameScore = nameRatio * 24.0f;
    const bool strongNameIdentification = nameRatio >= 0.90f;

    r.skillBonus = std::clamp(
        (foragingSkill * 0.35f) +
        (observationSkill * 0.25f) +
        (focusSkill * 0.15f) +
        (memorySkill * 0.10f),
        0.0f, 100.0f) * 0.12f;

    r.natureBonus = std::clamp(natureComfort, 0.0f, 100.0f) * 0.08f;

    // difficulty is now authored as commonness / recognition modifier in range -100..100.
    // -100 = rare/hard, 0 = normal, +100 = common/easier.
    const float commonness = std::clamp((float)species.difficulty, -100.0f, 100.0f);
    r.difficultyModifier = commonness * 0.16f;
    r.difficultyPenalty = commonness < 0.0f ? -r.difficultyModifier : 0.0f;

    const float rawScore =
        r.traitScore +
        r.descriptionScore +
        r.nameScore +
        r.skillBonus +
        r.natureBonus +
        r.difficultyModifier;

    float finalScore = rawScore;
    if (strongNameIdentification)
        finalScore = std::max(finalScore, 90.0f);

    r.scorePercent = (int)std::lround(std::clamp(finalScore, 0.0f, 100.0f));
    r.correctTraits = traitMatched;
    r.totalTraits = traitTotal;
    r.success = r.scorePercent >= 55;
    r.verifiedSpecies = r.scorePercent >= 75;

    ioKnowledge.timesSeen = std::max(ioKnowledge.timesSeen, 1);
    ioKnowledge.timesExamined++;

    if (traitMatched > 0 && ioKnowledge.knowledgeLevel < KnowledgeLevel::Seen)
    {
        ioKnowledge.knowledgeLevel = KnowledgeLevel::Seen;
        r.revealedNewKnowledge = true;
    }

    if (r.success && ioKnowledge.knowledgeLevel < KnowledgeLevel::Examined)
    {
        ioKnowledge.knowledgeLevel = KnowledgeLevel::Examined;
        r.revealedNewKnowledge = true;
    }

    if (r.verifiedSpecies)
    {
        ioKnowledge.knowledgeLevel = KnowledgeLevel::Verified;
        ioKnowledge.timesVerified++;
        r.revealedNewKnowledge = true;
    }

    if (r.verifiedSpecies)
        r.feedbackText = "Určení je přesné. Tuhle přírodninu už poznáš.";
    else if (r.success)
        r.feedbackText = "Určení je slušné, ale ještě si necháváš rezervu.";
    else if (r.scorePercent >= 35)
        r.feedbackText = "Některé znaky sedí, ale druh si zatím neověřil.";
    else
        r.feedbackText = "Popis je příliš nejistý. Zkus si lépe všímat znaků.";

    r.feedbackLines.push_back("Znaky: " + std::to_string((int)std::lround(r.traitScore)) + " bodů");
    r.feedbackLines.push_back("Volný popis: " + std::to_string((int)std::lround(r.descriptionScore)) + " bodů");
    r.feedbackLines.push_back("Odhad názvu: " + std::to_string((int)std::lround(r.nameScore)) + " bodů");
    if (strongNameIdentification)
        r.feedbackLines.push_back("Název sedí alespoň na 90 %, druh je ověřen i bez dalších znaků.");
    r.feedbackLines.push_back("Dovednosti a vztah k přírodě: +" + std::to_string((int)std::lround(r.skillBonus + r.natureBonus)) + " bodů");

    const int diffPoints = (int)std::lround(r.difficultyModifier);
    r.feedbackLines.push_back(std::string("Výskyt / obtížnost: ") + (diffPoints >= 0 ? "+" : "") + std::to_string(diffPoints) + " bodů");
    r.feedbackLines.push_back("Shoda 75 % a více se počítá jako správný znak / popis.");

    return r;
}

void ForageSystem::clear()
{
    m_spawns.clear();
}

bool ForageSystem::loadSpawnsForMap(const std::string& path, std::string* outError)
{
    m_spawns.clear();

    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(path, root, err))
    {
        if (outError) *outError = "Forage spawns: " + err;
        return false;
    }

    if (!root.contains("forage_spawns") || !root["forage_spawns"].is_array())
    {
        if (outError) *outError = "Forage spawns: missing 'forage_spawns' array";
        return false;
    }

    std::unordered_set<std::string> usedIds;
    int generatedIndex = 1;

    auto makeUniqueId = [&]()
    {
        for (;;)
        {
            std::string candidate = "forage_" + std::to_string(generatedIndex++);
            if (!usedIds.contains(candidate))
                return candidate;
        }
    };

    for (const auto& js : root["forage_spawns"])
    {
        ForageSpawnDef s;
        s.id = js.value("id", "");
        s.tileX = js.value("tile_x", 0);
        s.tileY = js.value("tile_y", 0);
        s.archetypeId = js.value("archetype_id", "");
        s.speciesPool = ReadStringArray(js.value("species_pool", json::array()));
        s.genericMapSpriteOverride = js.value("generic_map_sprite_override", "");
        s.mapScaleOverride = JsonFloatAny(js, 0.0f, { "map_scale_override", "scale_override", "mapScaleOverride" });
        s.seasonMask = ReadStringArray(js.value("season_mask", json::array()));
        s.respawnDays = js.value("respawn_days", 0);
        s.gatherOnce = js.value("gather_once", false);
        s.quantityMin = js.value("quantity_min", 1);
        s.quantityMax = js.value("quantity_max", 1);
        s.rarity = js.value("rarity", 50);
        s.requiresExamination = js.value("requires_examination", true);

        if (s.archetypeId.empty())
            continue;

        if (s.id.empty())
            s.id = makeUniqueId();

        if (usedIds.contains(s.id))
        {
            const std::string base = s.id;
            int suffix = 2;
            std::string candidate;
            do
            {
                candidate = base + "_" + std::to_string(suffix++);
            }
            while (usedIds.contains(candidate));
            s.id = candidate;
        }

        usedIds.insert(s.id);
        m_spawns.push_back(std::move(s));
    }

    return true;
}

bool ForageSystem::saveSpawnsForMap(const std::string& path, std::string* outError)
{
    json root;
    root["forage_spawns"] = json::array();

    for (const auto& s : m_spawns)
    {
        json js;
        js["id"] = s.id;
        js["tile_x"] = s.tileX;
        js["tile_y"] = s.tileY;
        js["archetype_id"] = s.archetypeId;
        js["species_pool"] = s.speciesPool;
        js["generic_map_sprite_override"] = s.genericMapSpriteOverride;
        if (s.mapScaleOverride > 0.0f)
            js["map_scale_override"] = s.mapScaleOverride;
        js["season_mask"] = s.seasonMask;
        js["respawn_days"] = s.respawnDays;
        js["gather_once"] = s.gatherOnce;
        js["quantity_min"] = s.quantityMin;
        js["quantity_max"] = s.quantityMax;
        js["rarity"] = s.rarity;
        js["requires_examination"] = s.requiresExamination;

        root["forage_spawns"].push_back(std::move(js));
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        if (outError) *outError = "Forage spawns: cannot write file: " + path;
        return false;
    }

    f << root.dump(2);
    return true;
}

const ForageSpawnDef* ForageSystem::findSpawnAt(int tileX, int tileY) const
{
    for (const auto& s : m_spawns)
    {
        if (s.tileX == tileX && s.tileY == tileY)
            return &s;
    }
    return nullptr;
}

ForageSpawnDef* ForageSystem::findSpawnAt(int tileX, int tileY)
{
    for (auto& s : m_spawns)
    {
        if (s.tileX == tileX && s.tileY == tileY)
            return &s;
    }
    return nullptr;
}

const ForageSpeciesDef* ForageSystem::pickSpeciesForSpawn(
    const ForageSpawnDef& spawn,
    const ForageDatabase& db,
    uint32_t worldSeed) const
{
    if (spawn.speciesPool.empty())
        return nullptr;

    const uint32_t h =
        worldSeed ^
        (uint32_t)(spawn.tileX * 73856093) ^
        (uint32_t)(spawn.tileY * 19349663);

    const size_t idx = (size_t)(h % spawn.speciesPool.size());
    return db.findSpecies(spawn.speciesPool[idx]);
}

ForageExaminationResult ForageSystem::examine(
    const ForageSpeciesDef& species,
    const std::vector<ForageExaminationAnswer>& answers,
    PlayerForageKnowledgeEntry& ioKnowledge,
    float foragingSkill,
    float observationSkill,
    float focusSkill,
    float memorySkill) const
{
    ForageExaminationResult r{};

    int correct = 0;
    int total = 0;

    for (const auto& a : answers)
    {
        ++total;

        auto it = species.traits.find(a.traitGroupId);
        if (it == species.traits.end())
            continue;

        const auto& valid = it->second;
        if (std::find(valid.begin(), valid.end(), a.optionId) != valid.end())
        {
            ++correct;
            ioKnowledge.knownTraits.insert(a.optionId);
            r.newlyKnownTraits.push_back(a.optionId);
        }
    }

    const float skillBonus =
        (foragingSkill * 0.35f) +
        (observationSkill * 0.30f) +
        (focusSkill * 0.20f) +
        (memorySkill * 0.15f);

    const float difficultyModifier = std::clamp((float)species.difficulty, -100.0f, 100.0f) * 0.16f;
    const float score = (float)correct * 20.0f + skillBonus + difficultyModifier;

    r.correctTraits = correct;
    r.totalTraits = total;
    r.success = score >= 20.0f;

    ioKnowledge.timesSeen = std::max(ioKnowledge.timesSeen, 1);
    ioKnowledge.timesExamined++;

    if (correct > 0 && ioKnowledge.knowledgeLevel < KnowledgeLevel::Seen)
    {
        ioKnowledge.knowledgeLevel = KnowledgeLevel::Seen;
        r.revealedNewKnowledge = true;
    }

    if (score >= 20.0f && ioKnowledge.knowledgeLevel < KnowledgeLevel::Examined)
    {
        ioKnowledge.knowledgeLevel = KnowledgeLevel::Examined;
        r.revealedNewKnowledge = true;
    }

    if (score >= 50.0f)
    {
        ioKnowledge.knowledgeLevel = KnowledgeLevel::Verified;
        ioKnowledge.timesVerified++;
        r.verifiedSpecies = true;
    }

    if (r.verifiedSpecies)
        r.feedbackText = "Druh se ti podarilo urcit velmi presne.";
    else if (r.success)
        r.feedbackText = "Rozpoznal jsi nekolik dulezitych znaku.";
    else
        r.feedbackText = "Stale si nejsi zcela jist.";

    return r;
}

ForageExaminationResult ForageSystem::identify(
    const ForageSpeciesDef& species,
    const ForageIdentificationInput& input,
    PlayerForageKnowledgeEntry& ioKnowledge,
    float foragingSkill,
    float observationSkill,
    float focusSkill,
    float memorySkill,
    float natureComfort) const
{
    return BuildIdentificationResult(
        species,
        input,
        ioKnowledge,
        foragingSkill,
        observationSkill,
        focusSkill,
        memorySkill,
        natureComfort);
}
