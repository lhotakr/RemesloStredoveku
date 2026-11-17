#include "Campaign.h"
#include "ui/MedievalUI.h"
#include "JsonUtils.h"
#include "PathUtils.h"
#include "Utf8.h"
#include <algorithm>
#include <vector>
#include <cmath>
#include <SDL_image.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <unordered_map>

using json = nlohmann::json;

static bool AABB_Intersect(const SDL_Rect& a, const SDL_Rect& b)
{
    return (a.x < b.x + b.w) &&
        (a.x + a.w > b.x) &&
        (a.y < b.y + b.h) &&
        (a.y + a.h > b.y);
}

static std::string LowerAsciiForId(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static bool ContainsIdToken(const std::string& id, const char* token)
{
    return LowerAsciiForId(id).find(token) != std::string::npos;
}

static bool HasForageEffect(const ForageSpeciesDef& species, const std::string& effect)
{
    for (const auto& e : species.effectsOnEat)
    {
        if (e == effect)
            return true;
    }
    return false;
}

static SDL_Rect GetObjectWorldRect(const gameobj::ObjectDef& def, int wx, int wy, float scale)
{
    SDL_Rect r{};
    r.w = (int)std::lround(def.src.w * scale);
    r.h = (int)std::lround(def.src.h * scale);
    r.x = (int)std::lround(wx - def.pivot.x * scale);
    r.y = (int)std::lround(wy - def.pivot.y * scale);
    return r;
}

void Campaign::ensureCameraOnPlayer(int screenW, int screenH)
{
    const int mapPixelW = m_map.width() * m_tileSize;
    const int mapPixelH = m_map.height() * m_tileSize;

    int targetCamX = (int)(m_player.x - screenW * 0.5f);
    int targetCamY = (int)(m_player.y - screenH * 0.5f);

    const int maxCamX = std::max(0, mapPixelW - screenW);
    const int maxCamY = std::max(0, mapPixelH - screenH);

    m_camX = std::clamp(targetCamX, 0, maxCamX);
    m_camY = std::clamp(targetCamY, 0, maxCamY);
}

static bool IntersectRects(const SDL_Rect& a, const SDL_Rect& b, SDL_Rect& out)
{
    const int x1 = std::max(a.x, b.x);
    const int y1 = std::max(a.y, b.y);
    const int x2 = std::min(a.x + a.w, b.x + b.w);
    const int y2 = std::min(a.y + a.h, b.y + b.h);

    if (x2 <= x1 || y2 <= y1)
        return false;

    out.x = x1;
    out.y = y1;
    out.w = x2 - x1;
    out.h = y2 - y1;
    return true;
}

static void RenderObjectWorldSubRectAlpha(
    SDL_Renderer* renderer,
    SDL_Texture* tex,
    const gameobj::ObjectDef& def,
    int wx,
    int wy,
    float scale,
    const SDL_Rect& worldSubRect,
    int camX,
    int camY,
    Uint8 alpha)
{
    SDL_Rect objWorld{};
    objWorld.x = (int)std::lround(wx - def.pivot.x * scale);
    objWorld.y = (int)std::lround(wy - def.pivot.y * scale);
    objWorld.w = (int)std::lround(def.src.w * scale);
    objWorld.h = (int)std::lround(def.src.h * scale);

    const float invScale = 1.0f / scale;

    SDL_Rect clipped{};
    if (!IntersectRects(objWorld, worldSubRect, clipped))
        return;

    const int relXSrc = (int)std::lround((clipped.x - objWorld.x) * invScale);
    const int relYSrc = (int)std::lround((clipped.y - objWorld.y) * invScale);
    const int srcW = std::max(1, (int)std::lround(clipped.w * invScale));
    const int srcH = std::max(1, (int)std::lround(clipped.h * invScale));

    SDL_Rect src{};
    src.x = def.src.x + relXSrc;
    src.y = def.src.y + relYSrc;
    src.w = std::min(srcW, def.src.w - relXSrc);
    src.h = std::min(srcH, def.src.h - relYSrc);

    SDL_Rect dst{};
    dst.x = clipped.x - camX;
    dst.y = clipped.y - camY;
    dst.w = clipped.w;
    dst.h = clipped.h;

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex, alpha);
    SDL_RenderCopy(renderer, tex, &src, &dst);
    SDL_SetTextureAlphaMod(tex, 255);
}

struct FadeOverlay
{
    SDL_Texture* tex = nullptr;
    const gameobj::ObjectDef* def = nullptr;
    int wx = 0;
    int wy = 0;
    SDL_Rect fadeRectWorld{};
    Uint8 alpha = 140;
    float scale = 1.0f;
};

static const char* DebugBackgroundName(PlayerStats::Background background)
{
    switch (background)
    {
    case PlayerStats::Background::Survivalist:    return "Zalesak";
    case PlayerStats::Background::ScholarAthlete: return "Student historie";
    case PlayerStats::Background::SocialAdaptable:return "Mestan";
    }
    return "Neznamy";
}

static void DebugProgress(const char* label, float value01)
{
    ImGui::TextUnformatted(label);
    ImGui::ProgressBar(std::clamp(value01, 0.0f, 1.0f), ImVec2(220.0f, 0.0f));
}

static void DebugBulletValue(const char* label, float value)
{
    ImGui::BulletText("%s: %.1f", label, value);
}

static const char* PlayerOverviewTier(float value)
{
    if (value >= 80.0f) return "vyborne";
    if (value >= 60.0f) return "dobre";
    if (value >= 40.0f) return "prumer";
    if (value >= 20.0f) return "slabe";
    return "kriticke";
}

static void PlayerOverviewHeader(const char* title)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.88f, 0.72f, 0.42f, 1.0f), "%s", title);
}

static void PlayerOverviewBar(const char* label, float value, bool highIsBad = false)
{
    const float clamped = PlayerStats::Clamp01To100(value);
    const float t = clamped / 100.0f;
    const ImVec4 okColor = ImVec4(0.34f, 0.72f, 0.38f, 1.0f);
    const ImVec4 warnColor = ImVec4(0.88f, 0.66f, 0.24f, 1.0f);
    const ImVec4 badColor = ImVec4(0.84f, 0.26f, 0.22f, 1.0f);

    ImVec4 color = okColor;
    if (highIsBad)
        color = clamped >= 70.0f ? badColor : (clamped >= 40.0f ? warnColor : okColor);
    else
        color = clamped <= 30.0f ? badColor : (clamped <= 55.0f ? warnColor : okColor);

    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.0f / 100", clamped);

    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(170.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(t, ImVec2(std::max(120.0f, ImGui::GetContentRegionAvail().x), 0.0f), overlay);
    ImGui::PopStyleColor();
    ImGui::PopID();
}

static void PlayerOverviewSkill(const char* label, float value)
{
    const float clamped = PlayerStats::Clamp01To100(value);
    char overlay[48];
    std::snprintf(overlay, sizeof(overlay), "%.0f - %s", clamped, PlayerOverviewTier(clamped));

    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(185.0f);
    ImGui::ProgressBar(clamped / 100.0f, ImVec2(std::max(120.0f, ImGui::GetContentRegionAvail().x), 0.0f), overlay);
    ImGui::PopID();
}

static void PlayerOverviewValue(const char* label, float value, const char* suffix = "")
{
    ImGui::Text("%s: %.1f%s", label, value, suffix);
}

static void PlayerOverviewFlag(const char* label, bool active)
{
    ImGui::TextColored(
        active ? ImVec4(0.95f, 0.38f, 0.28f, 1.0f) : ImVec4(0.48f, 0.68f, 0.42f, 1.0f),
        "%s: %s",
        label,
        active ? "ano" : "ne");
}

struct CraftTreeUiCache
{
    bool attempted = false;
    bool loaded = false;
    json root;
    std::string error;
    std::string sourceName;
};

static CraftTreeUiCache& PlayerOverviewCraftTreeCache()
{
    static CraftTreeUiCache cache;
    if (cache.attempted)
        return cache;

    cache.attempted = true;
    const auto masterPath = pathutils::DataDir() / "crafting" / "crafts_master.json";
    std::string masterError;
    if (jsonutils::LoadJsonFileSafe(masterPath.string(), cache.root, masterError))
    {
        if (cache.root.is_object() && cache.root.value("crafts", json::array()).is_array())
        {
            cache.loaded = true;
            cache.sourceName = "crafts_master.json";
            return cache;
        }

        masterError = "crafts_master.json nema pole crafts.";
        cache.root = json{};
    }

    const auto legacyPath = pathutils::DataDir() / "crafting" / "craft_tree.json";
    std::string legacyError;
    if (jsonutils::LoadJsonFileSafe(legacyPath.string(), cache.root, legacyError))
    {
        if (cache.root.is_object() && cache.root.value("domains", json::array()).is_array())
        {
            cache.loaded = true;
            cache.sourceName = "craft_tree.json";
            return cache;
        }

        legacyError = "craft_tree.json nema pole domains.";
        cache.root = json{};
    }

    cache.error = "crafts_master.json: " + masterError + "\ncraft_tree.json: " + legacyError;
    return cache;
}

static const json& PlayerOverviewEmptyJsonArray()
{
    static const json empty = json::array();
    return empty;
}

static const json& PlayerOverviewEmptyJsonObject()
{
    static const json empty = json::object();
    return empty;
}

static const json& PlayerOverviewJsonArray(const json& obj, const char* key)
{
    if (!obj.is_object())
        return PlayerOverviewEmptyJsonArray();

    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_array())
        return PlayerOverviewEmptyJsonArray();

    return *it;
}

static const json& PlayerOverviewJsonObject(const json& obj, const char* key)
{
    if (!obj.is_object())
        return PlayerOverviewEmptyJsonObject();

    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_object())
        return PlayerOverviewEmptyJsonObject();

    return *it;
}

static const json& PlayerOverviewCraftDomains(const json& root)
{
    const auto crafts = root.find("crafts");
    if (crafts != root.end() && crafts->is_array())
        return *crafts;

    return PlayerOverviewJsonArray(root, "domains");
}

static const json& PlayerOverviewWorkTracks(const json& root)
{
    return PlayerOverviewJsonArray(root, "work_tracks");
}

static std::string PlayerOverviewJsonString(const json& obj, const char* key, const char* fallback = "")
{
    if (!obj.is_object())
        return fallback;

    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string())
        return fallback;

    return it->get<std::string>();
}

static std::string PlayerOverviewJoinStringArray(const json& values, const char* fallback = "-")
{
    if (!values.is_array() || values.empty())
        return fallback;

    std::string out;
    for (const auto& value : values)
    {
        if (!value.is_string())
            continue;

        if (!out.empty())
            out += ", ";
        out += value.get<std::string>();
    }

    return out.empty() ? fallback : out;
}

static std::string PlayerOverviewArrayOrText(
    const json& obj,
    const char* arrayKey,
    const char* textKey,
    const char* fallback = "-")
{
    const std::string joined = PlayerOverviewJoinStringArray(PlayerOverviewJsonArray(obj, arrayKey), "");
    if (!joined.empty())
        return joined;

    const std::string text = PlayerOverviewJsonString(obj, textKey);
    return text.empty() ? fallback : text;
}

static void PlayerOverviewWrappedText(const std::string& text)
{
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
}

static void PlayerOverviewBulletWrapped(const std::string& text)
{
    ImGui::Bullet();
    ImGui::SameLine();
    PlayerOverviewWrappedText(text);
}

static float PlayerOverviewAverageStats(std::initializer_list<float> values)
{
    if (values.size() == 0)
        return 0.0f;

    float sum = 0.0f;
    for (float value : values)
        sum += PlayerStats::Clamp01To100(value);
    return PlayerStats::Clamp01To100(sum / static_cast<float>(values.size()));
}

static float PlayerOverviewCraftReadiness(const std::string& domainId, const PlayerStats& s)
{
    if (domainId == "smithing")
        return PlayerOverviewAverageStats({ s.craft.toolRepair, s.attributes.strength, s.attributes.dexterity, s.mental.focus });
    if (domainId == "woodworking")
        return PlayerOverviewAverageStats({ s.craft.woodcraft, s.survival.woodProcessing, s.craft.toolRepair, s.attributes.dexterity });
    if (domainId == "wheelwrighting")
        return PlayerOverviewAverageStats({ s.craft.woodcraft, s.craft.toolRepair, s.attributes.dexterity, s.mental.focus });
    if (domainId == "pottery")
        return PlayerOverviewAverageStats({ s.attributes.dexterity, s.mental.focus, s.mental.observation, s.mental.memory });
    if (domainId == "textiles")
        return PlayerOverviewAverageStats({ s.craft.clothingRepair, s.craft.ropeWork, s.attributes.dexterity, s.mental.focus });
    if (domainId == "leatherworking")
        return PlayerOverviewAverageStats({ s.craft.clothingRepair, s.attributes.dexterity, s.attributes.strength, s.mental.focus });
    if (domainId == "masonry")
        return PlayerOverviewAverageStats({ s.attributes.strength, s.attributes.endurance, s.attributes.dexterity, s.mental.observation });
    if (domainId == "bowyery")
        return PlayerOverviewAverageStats({ s.craft.woodcraft, s.attributes.dexterity, s.attributes.perception, s.mental.focus });
    if (domainId == "brewing")
        return PlayerOverviewAverageStats({ s.survival.cooking, s.mental.memory, s.mental.focus, s.mental.observation });
    if (domainId == "scribal")
        return PlayerOverviewAverageStats({ s.knowledge.literacy, s.knowledge.history, s.mental.memory, s.mental.focus });
    if (domainId == "herbalism")
        return PlayerOverviewAverageStats({ s.survival.foraging, s.mental.observation, s.mental.memory, s.attributes.perception });
    if (domainId == "healing")
        return PlayerOverviewAverageStats({ s.mental.observation, s.mental.focus, s.social.empathy, s.survival.diseaseResistance });
    if (domainId == "cooking")
        return PlayerOverviewAverageStats({ s.survival.cooking, s.survival.fireMaking, s.mental.focus, s.craft.improvisation });
    if (domainId == "artifact")
        return PlayerOverviewAverageStats({ s.knowledge.history, s.knowledge.symbolRecognition, s.mental.memory, s.social.empathy });

    return 0.0f;
}

static float PlayerOverviewCraftProgress(const std::string& domainId, const PlayerStats& s)
{
    (void)domainId;
    (void)s;
    // Craft progression is separate from attributes and starts at zero until gameplay records it.
    return 0.0f;
}

static std::string PlayerOverviewPercentLabel(float value)
{
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.0f %%", PlayerStats::Clamp01To100(value));
    return overlay;
}

static std::string PlayerOverviewDomainName(const json& root, const std::string& domainId)
{
    for (const auto& domain : PlayerOverviewCraftDomains(root))
    {
        if (PlayerOverviewJsonString(domain, "id") == domainId)
            return PlayerOverviewJsonString(domain, "name", domainId.c_str());
    }

    for (const auto& track : PlayerOverviewWorkTracks(root))
    {
        if (PlayerOverviewJsonString(track, "id") == domainId)
            return PlayerOverviewJsonString(track, "name", domainId.c_str());
    }

    return domainId;
}

static std::string PlayerOverviewRankName(const json& root, const std::string& rankId)
{
    for (const auto& rank : PlayerOverviewJsonArray(root, "ranks"))
    {
        if (rank.is_string())
        {
            const std::string rankName = rank.get<std::string>();
            if (rankName == rankId)
                return rankName;
            continue;
        }

        if (PlayerOverviewJsonString(rank, "id") == rankId)
            return PlayerOverviewJsonString(rank, "name", rankId.c_str());
    }

    const json& progression = PlayerOverviewJsonObject(root, "progression");
    for (const auto& rank : PlayerOverviewJsonArray(progression, "ranks"))
    {
        if (rank.is_string())
        {
            const std::string rankName = rank.get<std::string>();
            if (rankName == rankId)
                return rankName;
        }
    }

    return rankId;
}

static std::string PlayerOverviewRequirementText(const json& root, const json& requirement)
{
    const std::string type = PlayerOverviewJsonString(requirement, "type");
    if (type == "domains_at_progress")
    {
        return std::to_string(requirement.value("count", 0)) +
            " domen alespon na " + std::to_string(requirement.value("min_percent", 0)) + " %";
    }
    if (type == "domains_at_rank")
    {
        return std::to_string(requirement.value("count", 0)) +
            " domeny na hodnosti " + PlayerOverviewRankName(root, requirement.value("rank", std::string{}));
    }
    if (type == "all_worldly_domains_at_progress")
    {
        const int worldlyCount = PlayerOverviewJsonObject(root, "scope").value("full_worldly_crafts", 9);
        return "Vsech " + std::to_string(worldlyCount) + " svetskych domen alespon na " +
            std::to_string(requirement.value("min_percent", 0)) + " %";
    }
    if (type == "all_other_worldly_domains_progress")
    {
        return "Ostatni svetske domeny alespon na " +
            std::to_string(requirement.value("min_percent", 0)) + " %";
    }
    if (type == "cross_domain_master_project")
        return std::string(U8("Mezioborový mistrovský projekt: ")) + requirement.value("id", std::string{});
    if (type == "story_node_completed")
        return "Pribehovy uzel: " + requirement.value("id", std::string{});

    const std::string domain = PlayerOverviewJsonString(requirement, "domain");
    if (!domain.empty())
    {
        const std::string rank = PlayerOverviewJsonString(requirement, "rank");
        return PlayerOverviewDomainName(root, domain) + ": " + PlayerOverviewRankName(root, rank);
    }

    return requirement.dump();
}

static void PlayerOverviewCraftProgressBar(const std::string& domainId, const PlayerStats& s)
{
    const float value = PlayerOverviewCraftProgress(domainId, s);
    const std::string overlay = PlayerOverviewPercentLabel(value);

    ImGui::TextUnformatted(U8("Současný postup"));
    ImGui::SameLine(190.0f);
    ImGui::ProgressBar(value / 100.0f, ImVec2(std::max(120.0f, ImGui::GetContentRegionAvail().x), 0.0f), overlay.c_str());
}

static void PlayerOverviewRenderCraftDomainDetail(const json& root, const json& domain, const PlayerStats& s)
{
    const std::string domainId = PlayerOverviewJsonString(domain, "id");
    const std::string name = PlayerOverviewJsonString(domain, "name", domainId.c_str());

    ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.48f, 1.0f), "%s", name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", domainId.c_str());
    PlayerOverviewCraftProgressBar(domainId, s);

    const std::string role = PlayerOverviewJsonString(domain, "role");
    if (!role.empty())
        PlayerOverviewWrappedText(role);

    PlayerOverviewHeader(U8("Přístup"));
    PlayerOverviewWrappedText(std::string(U8("Mentor: ")) + PlayerOverviewArrayOrText(domain, "first_teachers", "mentor"));

    const std::string laterTeachers = PlayerOverviewJoinStringArray(PlayerOverviewJsonArray(domain, "later_teachers"), "");
    if (!laterTeachers.empty())
        PlayerOverviewWrappedText(std::string(U8("Další učitelé: ")) + laterTeachers);

    PlayerOverviewWrappedText(std::string(U8("Pracoviště: ")) + PlayerOverviewArrayOrText(domain, "workplaces", "place"));
    PlayerOverviewWrappedText(std::string(U8("Specializace: ")) + PlayerOverviewArrayOrText(domain, "specializations", "spec"));

    const std::string links = PlayerOverviewJoinStringArray(PlayerOverviewJsonArray(domain, "cross_domain_links"), "");
    if (!links.empty())
        PlayerOverviewWrappedText(std::string(U8("Vazby: ")) + links);

    const std::string mods = PlayerOverviewJsonString(domain, "mods");
    if (!mods.empty())
        PlayerOverviewWrappedText(std::string(U8("Modifikátory: ")) + mods);

    const std::string masterpiece = PlayerOverviewJsonString(domain, "masterpiece");
    if (!masterpiece.empty())
        PlayerOverviewWrappedText(std::string(U8("Mistrovský projekt: ")) + masterpiece);

    const std::string assetStatus = PlayerOverviewJsonString(domain, "runtime_asset_status");
    const std::string assetRoot = PlayerOverviewJsonString(domain, "runtime_asset_root");
    if (!assetStatus.empty())
    {
        PlayerOverviewWrappedText(
            "Assety: " + assetStatus +
            (assetRoot.empty() ? std::string{} : (" (" + assetRoot + ")")));
    }

    const json& finalDirections = PlayerOverviewJsonArray(domain, "final_directions");
    if (!finalDirections.empty())
    {
        PlayerOverviewHeader(U8("Závěrečné směry"));
        for (const auto& direction : finalDirections)
        {
            PlayerOverviewBulletWrapped(
                PlayerOverviewJsonString(direction, "name") + ": " +
                PlayerOverviewJsonString(direction, "focus"));
        }
    }

    PlayerOverviewHeader(U8("Hodnosti a důkazy"));
    int nodeIndex = 0;
    const json& rankNodes = PlayerOverviewJsonArray(domain, "rank_nodes");
    if (!rankNodes.empty())
    {
        for (const auto& node : rankNodes)
        {
            const std::string nodeId = PlayerOverviewJsonString(node, "id");
            const std::string rankId = PlayerOverviewJsonString(node, "rank");
            const std::string nodeName = PlayerOverviewJsonString(node, "name");
            const std::string rankName = PlayerOverviewRankName(root, rankId);
            const std::string label = nodeName.empty() ? rankName : (rankName + " - " + nodeName);

            ImGui::SetNextItemOpen(nodeIndex < 2, ImGuiCond_Once);
            if (ImGui::TreeNodeEx(nodeId.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s", label.c_str()))
            {
                const std::string evidenceGate = PlayerOverviewJsonString(node, "evidence_gate");
                if (!evidenceGate.empty())
                    PlayerOverviewBulletWrapped(std::string(U8("Důkaz: ")) + evidenceGate);

                const std::string content = PlayerOverviewJsonString(node, "content");
                if (!content.empty())
                    PlayerOverviewBulletWrapped(content);

                const json& requirements = PlayerOverviewJsonArray(node, "requirements");
                if (!requirements.empty())
                {
                    ImGui::TextUnformatted(U8("Podmínky:"));
                    for (const auto& requirement : requirements)
                        PlayerOverviewBulletWrapped(PlayerOverviewRequirementText(root, requirement));
                }

                const std::string skills = PlayerOverviewJoinStringArray(PlayerOverviewJsonArray(node, "skills"), "");
                if (!skills.empty())
                    PlayerOverviewWrappedText(std::string(U8("Dovednosti: ")) + skills);

                const std::string unlocks = PlayerOverviewJoinStringArray(PlayerOverviewJsonArray(node, "unlocks"), "");
                if (!unlocks.empty())
                    PlayerOverviewWrappedText(std::string(U8("Otevře: ")) + unlocks);

                ImGui::TreePop();
            }
            ++nodeIndex;
        }
    }
    else
    {
        for (const auto& row : PlayerOverviewJsonArray(domain, "rows"))
        {
            const std::string rankName = PlayerOverviewJsonString(row, "rank", U8("Hodnost"));
            const std::string nodeId = domainId + ".row." + std::to_string(nodeIndex);

            ImGui::SetNextItemOpen(nodeIndex < 2, ImGuiCond_Once);
            if (ImGui::TreeNodeEx(nodeId.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s", rankName.c_str()))
            {
                const std::string skills = PlayerOverviewJsonString(row, "nodes_and_skills");
                if (!skills.empty())
                    PlayerOverviewWrappedText(std::string(U8("Dovednosti: ")) + skills);

                const std::string evidence = PlayerOverviewJsonString(row, "mastery_evidence");
                if (!evidence.empty())
                    PlayerOverviewBulletWrapped(std::string(U8("Důkaz: ")) + evidence);

                ImGui::TreePop();
            }
            ++nodeIndex;
        }
    }
}

static void PlayerOverviewRenderCraftDomains(const json& root, const PlayerStats& s)
{
    const json& domains = PlayerOverviewCraftDomains(root);
    if (domains.empty())
    {
        ImGui::TextDisabled(U8("Žádné obory."));
        return;
    }

    static int selectedDomain = 0;
    selectedDomain = std::clamp(selectedDomain, 0, static_cast<int>(domains.size()) - 1);

    const float listWidth = std::min(260.0f, std::max(210.0f, ImGui::GetContentRegionAvail().x * 0.34f));
    ImGui::BeginChild("##craft_domain_list", ImVec2(listWidth, 0.0f), true);
    for (int i = 0; i < static_cast<int>(domains.size()); ++i)
    {
        const json& domain = domains[static_cast<size_t>(i)];
        const std::string domainId = PlayerOverviewJsonString(domain, "id");
        const std::string label = PlayerOverviewJsonString(domain, "name", domainId.c_str());
        if (ImGui::Selectable(label.c_str(), selectedDomain == i))
            selectedDomain = i;

        const float progress = PlayerOverviewCraftProgress(domainId, s);
        const std::string overlay = PlayerOverviewPercentLabel(progress);
        ImGui::ProgressBar(progress / 100.0f, ImVec2(-1.0f, 3.0f), overlay.c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##craft_domain_detail", ImVec2(0.0f, 0.0f), true);
    PlayerOverviewRenderCraftDomainDetail(root, domains[static_cast<size_t>(selectedDomain)], s);
    ImGui::EndChild();
}

static void PlayerOverviewRenderCraftProjects(const json& root)
{
    const json& projects = PlayerOverviewJsonArray(root, "cross_domain_projects");
    if (!projects.empty())
    {
        if (ImGui::BeginTable(
            "##craft_projects",
            4,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Projekt", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn(U8("Podmínky"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn(U8("Zkoušky"), ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn(U8("Svědci"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();

            for (const auto& project : projects)
            {
                std::vector<std::string> requirementTexts;
                for (const auto& requirement : PlayerOverviewJsonArray(project, "requirements"))
                    requirementTexts.push_back(PlayerOverviewRequirementText(root, requirement));

                std::string requirements;
                for (const auto& text : requirementTexts)
                {
                    if (!requirements.empty())
                        requirements += ", ";
                    requirements += text;
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                PlayerOverviewWrappedText(PlayerOverviewJsonString(project, "name", PlayerOverviewJsonString(project, "id").c_str()));
                ImGui::TableNextColumn();
                PlayerOverviewWrappedText(requirements.empty() ? "-" : requirements);
                ImGui::TableNextColumn();
                PlayerOverviewWrappedText(PlayerOverviewJoinStringArray(PlayerOverviewJsonArray(project, "tests")));
                ImGui::TableNextColumn();
                PlayerOverviewWrappedText(PlayerOverviewJoinStringArray(PlayerOverviewJsonArray(project, "judges")));
            }

            ImGui::EndTable();
        }
        return;
    }

    const json& tracks = PlayerOverviewWorkTracks(root);
    if (!tracks.empty() && ImGui::CollapsingHeader("Pracovni drahy", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginTable(
            "##craft_work_tracks",
            4,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Draha", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Cinnosti", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Vazby", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("Rozsah", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableHeadersRow();

            for (const auto& track : tracks)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                PlayerOverviewWrappedText(PlayerOverviewJsonString(track, "name", PlayerOverviewJsonString(track, "id").c_str()));
                ImGui::TableNextColumn();
                PlayerOverviewWrappedText(PlayerOverviewJsonString(track, "activities"));
                ImGui::TableNextColumn();
                PlayerOverviewWrappedText(PlayerOverviewJsonString(track, "links"));
                ImGui::TableNextColumn();
                PlayerOverviewWrappedText(PlayerOverviewJsonString(track, "scope"));
            }

            ImGui::EndTable();
        }
    }

    const json& artifact = PlayerOverviewJsonObject(root, "artifact_branch");
    const json& milestones = PlayerOverviewJsonArray(artifact, "milestones");
    if (!milestones.empty() && ImGui::CollapsingHeader("Skryta vetev", ImGuiTreeNodeFlags_DefaultOpen))
    {
        PlayerOverviewWrappedText("Nazvy v UI: " + PlayerOverviewJoinStringArray(PlayerOverviewJsonArray(artifact, "ui_names")));
        const std::string rule = PlayerOverviewJsonString(artifact, "rule");
        if (!rule.empty())
            PlayerOverviewWrappedText(rule);

        for (const auto& milestone : milestones)
        {
            PlayerOverviewBulletWrapped(
                PlayerOverviewJsonString(milestone, "name") + ": " +
                PlayerOverviewJsonString(milestone, "condition"));
        }
    }

    if (tracks.empty() && milestones.empty())
        ImGui::TextDisabled(U8("Žádné mezioborové projekty."));
}

static void PlayerOverviewRenderCraftModel(const json& root)
{
    const json& progression = PlayerOverviewJsonObject(root, "progression");
    if (!progression.empty() && ImGui::CollapsingHeader(U8("Pravidla postupu"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        const char* ruleKeys[] = { "unlock_rule", "books_rule", "failure_rule" };
        for (const char* key : ruleKeys)
        {
            const std::string rule = PlayerOverviewJsonString(progression, key);
            if (!rule.empty())
                PlayerOverviewBulletWrapped(rule);
        }

        const std::string ranks = PlayerOverviewJoinStringArray(PlayerOverviewJsonArray(progression, "ranks"), "");
        if (!ranks.empty())
            PlayerOverviewWrappedText(std::string(U8("Hodnosti: ")) + ranks);

        const std::string knowledgeStates = PlayerOverviewJoinStringArray(PlayerOverviewJsonArray(progression, "knowledge_states"), "");
        if (!knowledgeStates.empty())
            PlayerOverviewWrappedText(std::string(U8("Stavy znalosti: ")) + knowledgeStates);
    }

    const json& designRules = PlayerOverviewJsonArray(root, "design_rules");
    if (!designRules.empty() && ImGui::CollapsingHeader("Pravidla", progression.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0))
    {
        for (const auto& rule : designRules)
        {
            if (rule.is_string())
                PlayerOverviewBulletWrapped(rule.get<std::string>());
        }
    }

    const json& unlockRequirements = PlayerOverviewJsonArray(root, "unlock_requirements");
    if (!unlockRequirements.empty() && ImGui::CollapsingHeader(U8("Podmínky odemykání"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const auto& requirement : unlockRequirements)
        {
            PlayerOverviewBulletWrapped(
                PlayerOverviewJsonString(requirement, "name") + ": " +
                PlayerOverviewJsonString(requirement, "description"));
        }
    }

    if (ImGui::CollapsingHeader("Jakost", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const json& qualityGrades = PlayerOverviewJsonArray(root, "quality_grades");
        if (!qualityGrades.empty() && ImGui::BeginTable("##craft_quality", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn(U8("Stupeň"));
            ImGui::TableSetupColumn("Vliv");
            ImGui::TableSetupColumn(U8("Následek"));
            ImGui::TableHeadersRow();

            for (const auto& grade : qualityGrades)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(PlayerOverviewJsonString(grade, "name").c_str());
                ImGui::TableNextColumn();
                PlayerOverviewWrappedText(PlayerOverviewJsonString(grade, "effect"));
                ImGui::TableNextColumn();
                PlayerOverviewWrappedText(PlayerOverviewJsonString(grade, "consequence"));
            }

            ImGui::EndTable();
        }

        const json& qualityTiers = PlayerOverviewJsonArray(root, "quality_tiers");
        if (!qualityTiers.empty())
        {
            for (const auto& tier : qualityTiers)
            {
                if (tier.is_string())
                    PlayerOverviewBulletWrapped(tier.get<std::string>());
            }
        }
    }

    const json& visibility = PlayerOverviewJsonObject(root, "ui_visibility");
    if (!visibility.empty() && ImGui::CollapsingHeader("Viditelnost v UI", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto it = visibility.begin(); it != visibility.end(); ++it)
        {
            if (it.value().is_string())
                PlayerOverviewBulletWrapped(it.key() + ": " + it.value().get<std::string>());
        }
    }

    if (ImGui::CollapsingHeader(U8("Implementační pořadí"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        const json& implementationOrder = PlayerOverviewJsonArray(root, "implementation_order");
        for (const auto& stage : implementationOrder)
        {
            ImGui::BulletText(
                "%d. %s: %s",
                stage.value("stage", 0),
                PlayerOverviewJsonString(stage, "name").c_str(),
                PlayerOverviewJsonString(stage, "scope").c_str());
            const std::string reason = PlayerOverviewJsonString(stage, "reason");
            if (!reason.empty())
            {
                ImGui::Indent();
                PlayerOverviewWrappedText(reason);
                ImGui::Unindent();
            }
        }

        const json& implementationStatus = PlayerOverviewJsonObject(root, "implementation_status");
        if (!implementationStatus.empty())
        {
            for (auto it = implementationStatus.begin(); it != implementationStatus.end(); ++it)
            {
                if (it.value().is_string())
                    PlayerOverviewBulletWrapped(it.key() + ": " + it.value().get<std::string>());
            }
        }
    }
}

static void PlayerOverviewRenderCraftTree(const PlayerStats& s)
{
    CraftTreeUiCache& cache = PlayerOverviewCraftTreeCache();
    if (!cache.loaded)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), U8("Strom řemesel se nepodařilo načíst."));
        PlayerOverviewWrappedText(cache.error);
        return;
    }

    const json& root = cache.root;
    const std::string title = root.value(
        "title",
        root.value("name", std::string(U8("Plánovaný strom řemesel"))));
    std::string schemaLabel = root.value("schema", std::string{});
    if (schemaLabel.empty() && root.contains("schema_version"))
        schemaLabel = "schema " + std::to_string(root.value("schema_version", 0));
    if (!cache.sourceName.empty())
        schemaLabel += (schemaLabel.empty() ? "" : " | ") + cache.sourceName;

    ImGui::TextColored(
        ImVec4(0.92f, 0.78f, 0.48f, 1.0f),
        "%s",
        title.c_str());
    if (!schemaLabel.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", schemaLabel.c_str());
    }

    const std::string tagline = root.value(
        "tagline",
        PlayerOverviewJsonString(PlayerOverviewJsonObject(root, "progression"), "unlock_rule"));
    if (!tagline.empty())
        PlayerOverviewWrappedText(tagline);

    if (ImGui::BeginTabBar("##craft_tree_tabs"))
    {
        if (ImGui::BeginTabItem("Obory"))
        {
            PlayerOverviewRenderCraftDomains(root, s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Projekty"))
        {
            PlayerOverviewRenderCraftProjects(root);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Model"))
        {
            PlayerOverviewRenderCraftModel(root);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

static CraftTreeUiCache& PlayerOverviewLegacyCraftTreeCache()
{
    static CraftTreeUiCache cache;
    if (cache.attempted)
        return cache;

    cache.attempted = true;
    const auto legacyPath = pathutils::DataDir() / "crafting" / "craft_tree.json";
    std::string error;
    if (jsonutils::LoadJsonFileSafe(legacyPath.string(), cache.root, error))
    {
        if (cache.root.is_object() && cache.root.value("domains", json::array()).is_array())
        {
            cache.loaded = true;
            cache.sourceName = "craft_tree.json";
            return cache;
        }

        error = "craft_tree.json nema pole domains.";
        cache.root = json{};
    }

    cache.error = error;
    return cache;
}

struct CraftDomainTreeSource
{
    const json* root = nullptr;
    const json* domain = nullptr;
};

static CraftDomainTreeSource PlayerOverviewFindCraftDomainTreeSource(const json& fallbackRoot, const json& fallbackDomain)
{
    const bool fallbackHasTree =
        !PlayerOverviewJsonArray(fallbackDomain, "rows").empty() ||
        !PlayerOverviewJsonArray(fallbackDomain, "rank_nodes").empty();
    if (fallbackHasTree)
        return CraftDomainTreeSource{ &fallbackRoot, &fallbackDomain };

    const std::string domainId = PlayerOverviewJsonString(fallbackDomain, "id");
    if (!domainId.empty())
    {
        CraftTreeUiCache& legacy = PlayerOverviewLegacyCraftTreeCache();
        if (legacy.loaded)
        {
            for (const auto& domain : PlayerOverviewJsonArray(legacy.root, "domains"))
            {
                if (PlayerOverviewJsonString(domain, "id") == domainId &&
                    !PlayerOverviewJsonArray(domain, "rank_nodes").empty())
                {
                    return CraftDomainTreeSource{ &legacy.root, &domain };
                }
            }
        }
    }

    return CraftDomainTreeSource{ &fallbackRoot, &fallbackDomain };
}

struct CraftNodeView
{
    std::string id;
    std::string key;
    std::string label;
    std::string subtitle;
    std::string rank;
    std::string state;
    std::string iconPath;
    std::string proofText;
    std::vector<std::pair<std::string, std::string>> lessons;
    ImVec2 pos{ 0.0f, 0.0f };
    float progress = -1.0f;
    int inputPin = 0;
    int outputPin = 0;
    bool secret = false;
};

struct CraftLinkView
{
    int fromIndex = -1;
    int toIndex = -1;
    std::string state;
    ImU32 color = 0;
    float thickness = 2.0f;
};

struct CraftSkillTreeView
{
    std::string domainId;
    std::string title;
    std::string progressLabel;
    float currentProgress = 0.0f;
    ImVec2 referenceSize{ 1280.0f, 720.0f };
    ImVec2 nodeSize{ 250.0f, 112.0f };
    std::vector<CraftNodeView> nodes;
    std::vector<CraftLinkView> links;
    int activeNodeIndex = 0;
    bool rich = false;
};

static bool CraftPointInRect(const ImVec2& point, const ImVec2& min, const ImVec2& max)
{
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
}

static std::string CraftTrim(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    return text;
}

static std::vector<std::string> CraftSplitSemicolonList(const std::string& text)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t end = text.find(';', start);
        std::string part = CraftTrim(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!part.empty())
            out.push_back(part);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return out;
}

static std::string CraftHumanizeToken(std::string token)
{
    for (char& c : token)
    {
        if (c == '_' || c == '.')
            c = ' ';
    }
    if (!token.empty())
        token[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(token[0])));
    return token;
}

static std::string CraftJsonStringKey(const json& obj, const std::string& key, const char* fallback = "")
{
    if (!obj.is_object())
        return fallback;

    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string())
        return fallback;

    return it->get<std::string>();
}

static float CraftJsonArrayFloatAt(const json& obj, const char* key, size_t index, float fallback)
{
    const json& values = PlayerOverviewJsonArray(obj, key);
    if (index >= values.size() || !values[index].is_number())
        return fallback;
    return values[index].get<float>();
}

static ImU32 CraftColorFromHex(const std::string& hex, const ImVec4& fallback)
{
    unsigned int r = 0;
    unsigned int g = 0;
    unsigned int b = 0;
    if (hex.size() == 7 && hex[0] == '#' &&
        std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3)
    {
        return ImGui::GetColorU32(ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f));
    }
    return ImGui::GetColorU32(fallback);
}

static std::string CraftSkillTreeAssetPath(std::string ref)
{
    if (ref.empty())
        return {};

    std::replace(ref.begin(), ref.end(), '\\', '/');

    std::filesystem::path path(ref);
    if (path.has_extension())
        return ref;

    return (std::filesystem::path("assets") /
        "smithing" /
        "skill_tree_node_editor" /
        "assets" /
        (ref + ".png")).generic_string();
}

static SDL_Texture* CraftLoadCachedTexture(
    SDL_Renderer* renderer,
    std::unordered_map<std::string, SDL_Texture*>& cache,
    const std::string& sourcePath)
{
    if (!renderer || sourcePath.empty())
        return nullptr;

    std::filesystem::path path(sourcePath);
    if (path.is_relative())
        path = pathutils::ProjectRoot() / path;

    const std::string key = path.string();
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;

    SDL_Texture* tex = IMG_LoadTexture(renderer, key.c_str());
    cache[key] = tex;
    return tex;
}

static CraftTreeUiCache& PlayerOverviewSmithingSkillTreeCache()
{
    static CraftTreeUiCache cache;
    if (cache.attempted)
        return cache;

    cache.attempted = true;
    const auto path = pathutils::ProjectRoot() /
        "assets" /
        "smithing" /
        "skill_tree_node_editor" /
        "integration" /
        "smithing_skill_tree.json";

    std::string error;
    if (jsonutils::LoadJsonFileSafe(path.string(), cache.root, error))
    {
        if (cache.root.is_object() &&
            cache.root.value("nodes", json::array()).is_array() &&
            cache.root.value("links", json::array()).is_array())
        {
            cache.loaded = true;
            cache.sourceName = "smithing_skill_tree.json";
            return cache;
        }

        error = "smithing_skill_tree.json nema pole nodes/links.";
        cache.root = json{};
    }

    cache.error = error;
    return cache;
}

static ImU32 CraftNodeBorderColor(const std::string& state)
{
    if (state == "completed")
        return ImGui::GetColorU32(ImVec4(0.56f, 0.65f, 0.47f, 1.0f));
    if (state == "active")
        return ImGui::GetColorU32(ImVec4(0.61f, 0.46f, 0.25f, 1.0f));
    if (state == "locked_near")
        return ImGui::GetColorU32(ImVec4(0.52f, 0.47f, 0.37f, 1.0f));
    if (state == "secret")
        return ImGui::GetColorU32(ImVec4(0.75f, 0.55f, 0.22f, 1.0f));
    return ImGui::GetColorU32(ImVec4(0.45f, 0.39f, 0.30f, 1.0f));
}

static std::string CraftRankNameCs(const std::string& rank)
{
    if (rank == "observer") return U8("Pozorovatel");
    if (rank == "apprentice") return U8("Učeň");
    if (rank == "journeyman") return U8("Tovaryš");
    if (rank == "specialist") return U8("Specialista");
    if (rank == "master") return U8("Mistr");
    if (rank == "innovator") return U8("Inovátor");
    if (rank == "workshop_master") return U8("Mistr dílny");
    if (rank == "final") return U8("Závěrečná větev");
    return rank;
}

static std::string CraftRankSubtitle(const std::string& rank)
{
    if (rank == "observer") return U8("Základy výhně");
    if (rank == "apprentice") return U8("První výrobky");
    if (rank == "journeyman") return U8("Samostatná práce");
    if (rank == "specialist") return U8("Volba specializace");
    if (rank == "master") return U8("Vedení dílny");
    if (rank == "final") return U8("Skrytý směr");
    return U8("Postup učení");
}

static std::string CraftEvidenceForRank(const json& domain, const std::string& rank)
{
    for (const auto& node : PlayerOverviewJsonArray(domain, "rank_nodes"))
    {
        if (PlayerOverviewJsonString(node, "rank") == rank)
        {
            const std::string evidence = PlayerOverviewJsonString(node, "evidence_gate");
            if (!evidence.empty())
                return evidence;
        }
    }

    const std::string rankName = CraftRankNameCs(rank);
    for (const auto& row : PlayerOverviewJsonArray(domain, "rows"))
    {
        const std::string rowRank = PlayerOverviewJsonString(row, "rank");
        if (rowRank == rankName || (rank == "final" && rowRank.find("Inovátor") != std::string::npos))
        {
            const std::string evidence = PlayerOverviewJsonString(row, "mastery_evidence");
            if (!evidence.empty())
                return evidence;
        }
    }

    return {};
}

static float CraftNodeProgressFromDomainProgress(int index, int count, float progress)
{
    if (count <= 0)
        return 0.0f;

    const float step = 100.0f / static_cast<float>(count);
    const float start = step * static_cast<float>(index);
    return std::clamp((PlayerStats::Clamp01To100(progress) - start) / step, 0.0f, 1.0f);
}

static std::string CraftNodeStateFromReadiness(int index, int count, float readiness)
{
    if (count <= 0)
        return "available";

    const float step = 100.0f / static_cast<float>(count);
    const float completedAt = step * static_cast<float>(index + 1);
    const float activeAt = step * static_cast<float>(index);
    if (readiness >= completedAt)
        return "completed";
    if (readiness >= activeAt)
        return "active";
    return index <= 1 ? "locked_near" : "locked_far";
}

static int CraftRankColumn(const std::string& rank, int fallback)
{
    if (rank == "observer") return 0;
    if (rank == "apprentice") return 1;
    if (rank == "journeyman") return 2;
    if (rank == "specialist") return 3;
    if (rank == "master") return 4;
    if (rank == "innovator" || rank == "workshop_master" || rank == "final") return 5;
    return fallback;
}

static int CraftNodeIndexById(const CraftSkillTreeView& tree, const std::string& id)
{
    for (int i = 0; i < static_cast<int>(tree.nodes.size()); ++i)
    {
        if (tree.nodes[static_cast<size_t>(i)].id == id)
            return i;
    }
    return -1;
}

static const CraftNodeView* CraftSelectedNode(const CraftSkillTreeView& tree, const std::string& selectedNodeId)
{
    const int index = CraftNodeIndexById(tree, selectedNodeId);
    if (index >= 0)
        return &tree.nodes[static_cast<size_t>(index)];

    if (tree.nodes.empty())
        return nullptr;
    return &tree.nodes[static_cast<size_t>(std::clamp(tree.activeNodeIndex, 0, static_cast<int>(tree.nodes.size()) - 1))];
}

static CraftSkillTreeView CraftBuildSkillTreeView(
    const json& catalogRoot,
    const json& catalogDomain,
    const json& treeDomain,
    const PlayerStats& s)
{
    CraftSkillTreeView view;
    view.domainId = PlayerOverviewJsonString(catalogDomain, "id");
    const std::string domainName = PlayerOverviewJsonString(catalogDomain, "name", view.domainId.c_str());
    view.title = domainName + std::string(U8(" - strom učení"));

    const float progress = PlayerOverviewCraftProgress(view.domainId, s);
    view.currentProgress = progress;
    view.progressLabel = PlayerOverviewPercentLabel(progress);

    CraftTreeUiCache& smithingTree = PlayerOverviewSmithingSkillTreeCache();
    if (view.domainId == "smithing" && smithingTree.loaded)
    {
        view.rich = true;
        const json& tree = smithingTree.root;
        const json& layout = PlayerOverviewJsonObject(tree, "layout");
        view.referenceSize = ImVec2(
            CraftJsonArrayFloatAt(layout, "reference_canvas", 0, 2100.0f),
            CraftJsonArrayFloatAt(layout, "reference_canvas", 1, 950.0f));
        view.nodeSize = ImVec2(
            CraftJsonArrayFloatAt(layout, "node_size", 0, 270.0f),
            CraftJsonArrayFloatAt(layout, "node_size", 1, 132.0f));

        const json& lessonLabels = PlayerOverviewJsonObject(tree, "lesson_labels_cs");
        std::unordered_map<int, int> pinToNode;

        const json& treeNodes = PlayerOverviewJsonArray(tree, "nodes");
        const int nodeCount = static_cast<int>(treeNodes.size());

        int index = 0;
        for (const auto& node : treeNodes)
        {
            CraftNodeView out;
            out.id = std::to_string(node.value("id", index));
            out.key = PlayerOverviewJsonString(node, "key", out.id.c_str());
            out.label = PlayerOverviewJsonString(node, "title_cs", out.key.c_str());
            out.rank = PlayerOverviewJsonString(node, "rank");
            out.subtitle = CraftRankSubtitle(out.rank);
            out.state = CraftNodeStateFromReadiness(index, nodeCount, progress);
            out.progress = CraftNodeProgressFromDomainProgress(index, nodeCount, progress);
            out.inputPin = node.value("input_pin", 0);
            out.outputPin = node.value("output_pin", 0);
            out.secret = node.value("hidden_until_revealed", false) || out.state == "secret";
            out.iconPath = CraftSkillTreeAssetPath(PlayerOverviewJsonString(node, "icon"));
            out.proofText = CraftEvidenceForRank(treeDomain, out.rank);

            out.pos = ImVec2(
                CraftJsonArrayFloatAt(node, "position", 0, 0.0f),
                CraftJsonArrayFloatAt(node, "position", 1, 0.0f));

            for (const auto& lesson : PlayerOverviewJsonArray(node, "lessons"))
            {
                if (!lesson.is_string())
                    continue;

                const std::string key = lesson.get<std::string>();
                const std::string label = CraftJsonStringKey(lessonLabels, key, CraftHumanizeToken(key).c_str());
                out.lessons.push_back({ label, CraftSkillTreeAssetPath("icons/" + key) });
            }

            if (out.state == "active")
                view.activeNodeIndex = index;

            if (out.inputPin != 0)
                pinToNode[out.inputPin] = index;
            if (out.outputPin != 0)
                pinToNode[out.outputPin] = index;

            view.nodes.push_back(std::move(out));
            ++index;
        }

        for (const auto& link : PlayerOverviewJsonArray(tree, "links"))
        {
            const int startPin = link.value("start_pin", 0);
            const int endPin = link.value("end_pin", 0);
            auto fromIt = pinToNode.find(startPin);
            auto toIt = pinToNode.find(endPin);
            if (fromIt == pinToNode.end() || toIt == pinToNode.end())
                continue;

            CraftLinkView out;
            out.fromIndex = fromIt->second;
            out.toIndex = toIt->second;
            out.state = view.nodes[static_cast<size_t>(out.fromIndex)].state;
            out.color = CraftNodeBorderColor(out.state);
            out.thickness = out.state == "active" ? 4.0f : 3.0f;
            view.links.push_back(out);
        }

        return view;
    }

    const json& rankNodes = PlayerOverviewJsonArray(treeDomain, "rank_nodes");
    if (!rankNodes.empty())
    {
        const int nodeCount = static_cast<int>(rankNodes.size());
        int index = 0;
        for (const auto& node : rankNodes)
        {
            const std::string rank = PlayerOverviewJsonString(node, "rank");

            CraftNodeView out;
            out.id = PlayerOverviewJsonString(node, "id", ("node." + std::to_string(index)).c_str());
            out.key = out.id;
            out.rank = rank;
            out.label = PlayerOverviewRankName(catalogRoot, rank);
            out.subtitle = CraftRankSubtitle(rank);
            out.state = CraftNodeStateFromReadiness(index, nodeCount, progress);
            out.progress = CraftNodeProgressFromDomainProgress(index, nodeCount, progress);
            out.proofText = PlayerOverviewJsonString(node, "evidence_gate");
            out.iconPath = CraftSkillTreeAssetPath("icons/" + (rank.empty() ? std::string("tools") : rank));
            out.pos.x = 40.0f + static_cast<float>(CraftRankColumn(rank, index)) * 310.0f;
            out.pos.y = 290.0f;
            if (rank == "innovator")
                out.pos.y = 210.0f;
            else if (rank == "workshop_master")
                out.pos.y = 390.0f;

            for (const auto& skill : PlayerOverviewJsonArray(node, "skills"))
            {
                if (!skill.is_string())
                    continue;
                const std::string key = skill.get<std::string>();
                out.lessons.push_back({ CraftHumanizeToken(key), {} });
            }

            if (out.state == "active")
                view.activeNodeIndex = index;
            view.nodes.push_back(std::move(out));
            ++index;
        }

        std::unordered_map<std::string, int> byId;
        for (int i = 0; i < static_cast<int>(view.nodes.size()); ++i)
            byId[view.nodes[static_cast<size_t>(i)].id] = i;

        for (int from = 0; from < static_cast<int>(rankNodes.size()); ++from)
        {
            for (const auto& unlock : PlayerOverviewJsonArray(rankNodes[static_cast<size_t>(from)], "unlocks"))
            {
                if (!unlock.is_string())
                    continue;

                auto it = byId.find(unlock.get<std::string>());
                if (it == byId.end())
                    continue;

                CraftLinkView link;
                link.fromIndex = from;
                link.toIndex = it->second;
                link.state = view.nodes[static_cast<size_t>(from)].state;
                link.color = CraftNodeBorderColor(link.state);
                link.thickness = link.state == "active" ? 4.0f : 3.0f;
                view.links.push_back(link);
            }
        }

        return view;
    }

    const json& rows = PlayerOverviewJsonArray(catalogDomain, "rows");
    const int rowCount = static_cast<int>(rows.size());
    int index = 0;
    for (const auto& row : rows)
    {
        const std::string rankName = PlayerOverviewJsonString(row, "rank", U8("Hodnost"));

        CraftNodeView out;
        out.id = view.domainId + ".row." + std::to_string(index);
        out.key = out.id;
        out.rank = rankName;
        out.label = rankName;
        out.subtitle = U8("Hodnostní uzel");
        out.state = CraftNodeStateFromReadiness(index, rowCount, progress);
        out.progress = CraftNodeProgressFromDomainProgress(index, rowCount, progress);
        out.proofText = PlayerOverviewJsonString(row, "mastery_evidence");
        out.pos.x = 40.0f + static_cast<float>(index) * 300.0f;
        out.pos.y = 290.0f;

        for (const std::string& lesson : CraftSplitSemicolonList(PlayerOverviewJsonString(row, "nodes_and_skills")))
            out.lessons.push_back({ lesson, {} });

        if (out.state == "active")
            view.activeNodeIndex = index;
        view.nodes.push_back(std::move(out));
        ++index;
    }

    for (int i = 1; i < static_cast<int>(view.nodes.size()); ++i)
    {
        CraftLinkView link;
        link.fromIndex = i - 1;
        link.toIndex = i;
        link.state = view.nodes[static_cast<size_t>(i - 1)].state;
        link.color = CraftNodeBorderColor(link.state);
        link.thickness = link.state == "active" ? 4.0f : 3.0f;
        view.links.push_back(link);
    }

    return view;
}

static void CraftDrawNode(
    ImDrawList* drawList,
    SDL_Renderer* renderer,
    std::unordered_map<std::string, SDL_Texture*>& textureCache,
    const ImVec2& origin,
    float zoom,
    const ImVec2& nodeSize,
    const CraftNodeView& node,
    bool selected)
{
    const ImVec2 size(nodeSize.x * zoom, nodeSize.y * zoom);
    const ImVec2 p0(origin.x + node.pos.x * zoom, origin.y + node.pos.y * zoom);
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    auto& ui = medieval::UI::Get();
    const float rounding = 2 * ui.scale();

    if (node.secret)
    {
        const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
        const float radius = std::max(12.0f, 24.0f * zoom);
        drawList->AddCircleFilled(center, radius, ImGui::GetColorU32(ImVec4(0.08f, 0.07f, 0.06f, 1.0f)), 32);
        drawList->AddCircle(center, radius, CraftNodeBorderColor(node.state), 32, selected ? 3.0f : 2.0f);
        SDL_Texture* question = CraftLoadCachedTexture(renderer, textureCache, CraftSkillTreeAssetPath("badges/question"));
        if (question)
            drawList->AddImage(question, ImVec2(center.x - radius * 0.55f, center.y - radius * 0.55f), ImVec2(center.x + radius * 0.55f, center.y + radius * 0.55f));
        else
            drawList->AddText(ImVec2(center.x - 4.0f, center.y - 8.0f), CraftNodeBorderColor(node.state), "?");
        return;
    }

    ui.panel(drawList, p0, size, medieval::Surface::Parchment);
    if (node.state.find("locked") == 0)
        drawList->AddRectFilled(p0, p1, IM_COL32(77, 62, 42, 42));
    drawList->AddRect(p0, p1, selected ? ImGui::GetColorU32(medieval::Palette::Brass) : CraftNodeBorderColor(node.state), rounding, 0, selected ? 3.0f : 1.0f);

    const float iconSize = std::max(18.0f, 46.0f * zoom);
    const ImVec2 iconMin(p0.x + 12.0f * zoom, p0.y + 42.0f * zoom);
    const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
    SDL_Texture* icon = CraftLoadCachedTexture(renderer, textureCache, node.iconPath);
    if (icon)
        drawList->AddImage(icon, iconMin, iconMax);
    else
        drawList->AddCircleFilled(ImVec2((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f), iconSize * 0.5f, ImGui::GetColorU32(ImVec4(0.10f, 0.09f, 0.08f, 1.0f)));

    const float titleX = p0.x + 12 * zoom;
    const float textSize = std::max(11 * ui.scale(), 21 * zoom);
    const ImVec4 titleClip(titleX, p0.y, p1.x - 12 * zoom, p0.y + 38 * zoom);
    drawList->AddText(ui.font(medieval::FontRole::Heading), textSize,
        {titleX, p0.y + 8 * zoom}, ImGui::GetColorU32(medieval::Palette::Ink),
        node.label.c_str(), nullptr, 0, &titleClip);
    const ImVec4 subtitleClip(p0.x + 70 * zoom, p0.y + 40 * zoom, p1.x - 12 * zoom, p1.y - 27 * zoom);
    drawList->AddText(ui.font(medieval::FontRole::Body), std::max(10 * ui.scale(), 18 * zoom),
        {p0.x + 70 * zoom, p0.y + 48 * zoom}, ImGui::GetColorU32(medieval::Palette::MutedInk),
        node.subtitle.c_str(), nullptr, 0, &subtitleClip);

    if (node.progress >= 0.0f)
    {
        const float clamped = std::clamp(node.progress, 0.0f, 1.0f);
        const ImVec2 bar0(p0.x + 72.0f * zoom, p1.y - 23.0f * zoom);
        const ImVec2 bar1(p1.x - 16.0f * zoom, p1.y - 15.0f * zoom);
        drawList->AddRectFilled(bar0, bar1, ImGui::GetColorU32(ImVec4(0.12f, 0.11f, 0.10f, 1.0f)), 3.0f);
        drawList->AddRectFilled(
            bar0,
            ImVec2(bar0.x + (bar1.x - bar0.x) * clamped, bar1.y),
            node.state == "completed"
                ? ImGui::GetColorU32(ImVec4(0.62f, 0.74f, 0.45f, 1.0f))
                : ImGui::GetColorU32(ImVec4(0.55f, 0.42f, 0.23f, 1.0f)),
            3.0f);
    }

    const char* badgeRef = nullptr;
    if (node.state == "completed")
        badgeRef = "badges/complete";
    else if (node.state == "locked_near" || node.state == "locked_far" || node.state == "locked")
        badgeRef = "badges/lock";

    if (badgeRef)
    {
        SDL_Texture* badge = CraftLoadCachedTexture(renderer, textureCache, CraftSkillTreeAssetPath(badgeRef));
        const float badgeSize = std::max(14.0f, 24.0f * zoom);
        const ImVec2 badgeMin(p1.x - badgeSize - 8.0f * zoom, p0.y + 7.0f * zoom);
        const ImVec2 badgeMax(badgeMin.x + badgeSize, badgeMin.y + badgeSize);
        if (badge)
            drawList->AddImage(badge, badgeMin, badgeMax);
    }
}

static ImVec2 CraftNodeInputPoint(const ImVec2& origin, float zoom, const ImVec2& nodeSize, const CraftNodeView& node)
{
    return ImVec2(origin.x + node.pos.x * zoom, origin.y + (node.pos.y + nodeSize.y * 0.5f) * zoom);
}

static ImVec2 CraftNodeOutputPoint(const ImVec2& origin, float zoom, const ImVec2& nodeSize, const CraftNodeView& node)
{
    return ImVec2(origin.x + (node.pos.x + nodeSize.x) * zoom, origin.y + (node.pos.y + nodeSize.y * 0.5f) * zoom);
}

static void CraftDrawLink(
    ImDrawList* drawList,
    const ImVec2& origin,
    float zoom,
    const ImVec2& nodeSize,
    const CraftNodeView& from,
    const CraftNodeView& to,
    const CraftLinkView& link)
{
    const ImVec2 a = CraftNodeOutputPoint(origin, zoom, nodeSize, from);
    const ImVec2 b = CraftNodeInputPoint(origin, zoom, nodeSize, to);
    const float mid = std::max(28.0f, (b.x - a.x) * 0.45f);
    drawList->AddBezierCubic(
        a,
        ImVec2(a.x + mid, a.y),
        ImVec2(b.x - mid, b.y),
        b,
        link.color ? link.color : CraftNodeBorderColor(link.state),
        std::max(1.5f, link.thickness * zoom));
    drawList->AddCircleFilled(a, std::max(2.5f, 5.0f * zoom), link.color ? link.color : CraftNodeBorderColor(link.state));
    drawList->AddCircleFilled(b, std::max(2.5f, 5.0f * zoom), link.color ? link.color : CraftNodeBorderColor(link.state));
}

static void CraftDrawGrid(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const ImVec2& pan, float zoom)
{
    medieval::UI::Get().panel(drawList, min, {max.x - min.x, max.y - min.y}, medieval::Surface::Parchment);
    const float grid = std::max(12.0f, 72.0f * zoom);
    const ImU32 minor = IM_COL32(88, 66, 39, 15);
    const ImU32 major = IM_COL32(88, 66, 39, 32);

    float xStart = min.x + std::fmod(pan.x, grid);
    if (xStart > min.x)
        xStart -= grid;
    int xIndex = static_cast<int>(std::floor((xStart - min.x - pan.x) / grid));
    for (float x = xStart; x < max.x; x += grid, ++xIndex)
        drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), (xIndex % 5 == 0) ? major : minor, (xIndex % 5 == 0) ? 1.2f : 1.0f);

    float yStart = min.y + std::fmod(pan.y, grid);
    if (yStart > min.y)
        yStart -= grid;
    int yIndex = static_cast<int>(std::floor((yStart - min.y - pan.y) / grid));
    for (float y = yStart; y < max.y; y += grid, ++yIndex)
        drawList->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), (yIndex % 5 == 0) ? major : minor, (yIndex % 5 == 0) ? 1.2f : 1.0f);

    drawList->AddRect(min, max, ImGui::GetColorU32(ImVec4(0.55f, 0.35f, 0.16f, 0.85f)), 6.0f, 0, 2.0f);
}

static void CraftRenderSkillTreeCanvas(
    SDL_Renderer* renderer,
    std::unordered_map<std::string, SDL_Texture*>& textureCache,
    const CraftSkillTreeView& tree,
    std::string& selectedNodeId,
    ImVec2& pan,
    float& zoom,
    bool resetView)
{
    const auto available = ImGui::GetContentRegionAvail();
    const ImVec2 canvasSize{std::max(1.0f, available.x), std::max(1.0f, available.y)};
    const std::string canvasId = "##skill_tree_canvas_" + tree.domainId;
    // Keep an actual node visible when the detail pane moves below a short canvas.
    auto* storage = ImGui::GetStateStorage();
    const auto widthKey = ImGui::GetID((canvasId + "-width").c_str());
    const auto heightKey = ImGui::GetID((canvasId + "-height").c_str());
    const bool resized = storage->GetFloat(widthKey) != canvasSize.x || storage->GetFloat(heightKey) != canvasSize.y;
    storage->SetFloat(widthKey, canvasSize.x);
    storage->SetFloat(heightKey, canvasSize.y);
    if (resetView || resized || zoom <= 0)
    {
        if (resetView || zoom <= 0) zoom = std::clamp((tree.rich ? 0.50f : 0.62f) * medieval::UI::Get().scale(), 0.34f, 1.25f);
        const auto* node = CraftSelectedNode(tree, selectedNodeId);
        pan = {24 - (node ? node->pos.x * zoom : 0),
               (canvasSize.y - tree.nodeSize.y * zoom) / 2 - (node ? node->pos.y * zoom : 0)};
    }
    ImGui::InvisibleButton(canvasId.c_str(), canvasSize);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);

    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 canvasMin = ImGui::GetItemRectMin();
    const ImVec2 canvasMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();

    if (hovered && io.MouseWheel != 0.0f)
    {
        const float oldZoom = zoom;
        zoom = std::clamp(zoom * (io.MouseWheel > 0.0f ? 1.10f : 0.90f), 0.34f, 1.25f);
        if (oldZoom != zoom)
        {
            const ImVec2 mouseLocal(io.MousePos.x - canvasMin.x, io.MousePos.y - canvasMin.y);
            pan.x = mouseLocal.x - (mouseLocal.x - pan.x) * (zoom / oldZoom);
            pan.y = mouseLocal.y - (mouseLocal.y - pan.y) * (zoom / oldZoom);
        }
    }

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
    {
        pan.x += io.MouseDelta.x;
        pan.y += io.MouseDelta.y;
    }

    const ImVec2 worldOrigin(canvasMin.x + pan.x, canvasMin.y + pan.y);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        for (int i = static_cast<int>(tree.nodes.size()) - 1; i >= 0; --i)
        {
            const CraftNodeView& node = tree.nodes[static_cast<size_t>(i)];
            const ImVec2 nodeMin(worldOrigin.x + node.pos.x * zoom, worldOrigin.y + node.pos.y * zoom);
            const ImVec2 nodeMax(nodeMin.x + tree.nodeSize.x * zoom, nodeMin.y + tree.nodeSize.y * zoom);
            if (CraftPointInRect(io.MousePos, nodeMin, nodeMax))
            {
                selectedNodeId = node.id;
                break;
            }
        }
    }

    drawList->PushClipRect(canvasMin, canvasMax, true);
    CraftDrawGrid(drawList, canvasMin, canvasMax, pan, zoom);

    for (const CraftLinkView& link : tree.links)
    {
        if (link.fromIndex < 0 || link.toIndex < 0 ||
            link.fromIndex >= static_cast<int>(tree.nodes.size()) ||
            link.toIndex >= static_cast<int>(tree.nodes.size()))
        {
            continue;
        }

        CraftDrawLink(
            drawList,
            worldOrigin,
            zoom,
            tree.nodeSize,
            tree.nodes[static_cast<size_t>(link.fromIndex)],
            tree.nodes[static_cast<size_t>(link.toIndex)],
            link);
    }

    for (const CraftNodeView& node : tree.nodes)
    {
        CraftDrawNode(
            drawList,
            renderer,
            textureCache,
            worldOrigin,
            zoom,
            tree.nodeSize,
            node,
            node.id == selectedNodeId);
    }

    drawList->PopClipRect();


}

static void CraftRenderLessonLine(
    SDL_Renderer* renderer,
    std::unordered_map<std::string, SDL_Texture*>& textureCache,
    const std::pair<std::string, std::string>& lesson)
{
    const float iconSize = 26.0f;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    SDL_Texture* icon = CraftLoadCachedTexture(renderer, textureCache, lesson.second);
    if (icon)
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)icon, start, ImVec2(start.x + iconSize, start.y + iconSize));
    else
    {
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(start.x + iconSize * 0.5f, start.y + iconSize * 0.5f), iconSize * 0.42f, ImGui::GetColorU32(ImVec4(0.18f, 0.13f, 0.09f, 1.0f)));
        ImGui::GetWindowDrawList()->AddCircle(ImVec2(start.x + iconSize * 0.5f, start.y + iconSize * 0.5f), iconSize * 0.42f, ImGui::GetColorU32(ImVec4(0.75f, 0.52f, 0.22f, 1.0f)), 24, 1.5f);
    }
    ImGui::Dummy(ImVec2(iconSize, iconSize));
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    PlayerOverviewWrappedText(lesson.first);
}

static void CraftRenderSkillDetail(
    SDL_Renderer* renderer,
    std::unordered_map<std::string, SDL_Texture*>& textureCache,
    const CraftSkillTreeView& tree,
    const CraftNodeView* selectedNode)
{
    ImGui::TextColored(medieval::Palette::MutedInk, U8("DETAIL DOVEDNOSTI"));
    ImGui::Separator();

    if (!selectedNode)
    {
        ImGui::TextDisabled(U8("Vyber uzel ve stromu."));
        return;
    }

    const CraftNodeView& node = *selectedNode;
    medieval::UI::Get().heading(node.secret ? U8("Neznámý uzel") : node.label);
    ImGui::TextWrapped("%s", node.subtitle.c_str());
    ImGui::Spacing();

    const ImVec2 iconStart = ImGui::GetCursorScreenPos();
    const float iconSize = 60.0f;
    SDL_Texture* icon = CraftLoadCachedTexture(renderer, textureCache, node.iconPath);
    if (icon && !node.secret)
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)icon, iconStart, ImVec2(iconStart.x + iconSize, iconStart.y + iconSize));
    else
        ImGui::GetWindowDrawList()->AddCircle(
            ImVec2(iconStart.x + iconSize * 0.5f, iconStart.y + iconSize * 0.5f),
            iconSize * 0.42f,
            CraftNodeBorderColor(node.state),
            32,
            2.0f);

    ImGui::Dummy(ImVec2(iconSize, iconSize));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(U8("Současný postup"));
    const float progress = std::clamp(tree.currentProgress / 100.0f, 0.0f, 1.0f);
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.0f %%", tree.currentProgress);
    ImGui::ProgressBar(progress, ImVec2(std::max(100.0f, ImGui::GetContentRegionAvail().x), 0.0f), overlay);
    ImGui::TextWrapped(U8("Účinnost učení řemesla"));
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(medieval::Palette::MutedInk, U8("CO SE UČÍŠ"));

    if (node.lessons.empty())
    {
        ImGui::TextDisabled(U8("Tato větev je zatím skrytá."));
    }
    else
    {
        const int maxLessons = std::min(8, static_cast<int>(node.lessons.size()));
        for (int i = 0; i < maxLessons; ++i)
            CraftRenderLessonLine(renderer, textureCache, node.lessons[static_cast<size_t>(i)]);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(medieval::Palette::MutedInk, U8("PODMÍNKA POSTUPU"));
    if (!node.proofText.empty())
        PlayerOverviewWrappedText(node.proofText);
    else if (node.secret)
        PlayerOverviewWrappedText(U8("Podmínka se odhalí až po mistrovském důkazu v oboru."));
    else
        PlayerOverviewWrappedText(U8("Prokaž znalost, praxi, jakost a přijetí práce mistrem nebo komunitou."));

    ImGui::Spacing();
    ImGui::Spacing();
    if (node.state == "active")
        medieval::UI::Get().button("##specialization", U8("DALŠÍ: VYBER SPECIALIZACI"));
}

void Campaign::renderCraftsUI()
{
    if (!m_craftsOpen)
        return;

    if (m_craftsFocus)
    {
        ImGui::SetNextWindowFocus();
        m_craftsFocus = false;
    }

    auto& ui = medieval::UI::Get();
    const auto* viewport = ImGui::GetMainViewport();
    medieval::PlaceWindow(viewport->WorkSize, ImGuiCond_Always);
    medieval::Window window(ui, U8("Řemesla"), medieval::Surface::Wood,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    if (!window || !medieval::WindowHeader(ui, U8("Mapa řemesel"), m_craftsOpen)) return;

    CraftTreeUiCache& cache = PlayerOverviewCraftTreeCache();
    if (!cache.loaded)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), U8("Strom řemesel se nepodařilo načíst."));
        PlayerOverviewWrappedText(cache.error);
        return;
    }

    const json& root = cache.root;
    const json& domains = PlayerOverviewCraftDomains(root);
    if (domains.empty())
    {
        ImGui::TextDisabled(U8("Žádné obory."));
        return;
    }

    static int selectedDomain = 0;
    static std::unordered_map<std::string, std::string> selectedNodeByDomain;
    static std::unordered_map<std::string, ImVec2> panByDomain;
    static std::unordered_map<std::string, float> zoomByDomain;
    selectedDomain = std::clamp(selectedDomain, 0, static_cast<int>(domains.size()) - 1);

    const float railWidth = 100 * ui.scale();
    {
        medieval::Child rail(ui, "##craft_icon_rail", {railWidth, 0}, medieval::Surface::Wood);
        if (rail) for (int i = 0; i < static_cast<int>(domains.size()); ++i)
        {
            const json& domain = domains[static_cast<size_t>(i)];
            const std::string domainId = PlayerOverviewJsonString(domain, "id");
            const std::string label = PlayerOverviewJsonString(domain, "name", domainId.c_str());
            const std::string iconPath = PlayerOverviewJsonString(domain, "icon");
            ImGui::PushID(i);
            const ImVec2 buttonSize(48 * ui.scale(), 48 * ui.scale());
            if (ImGui::InvisibleButton("craft_icon", buttonSize, ImGuiButtonFlags_EnableNav)) selectedDomain = i;
            const auto min = ImGui::GetItemRectMin();
            const auto max = ImGui::GetItemRectMax();
            auto* draw = ImGui::GetWindowDrawList();
            ui.buttonFrame(draw, min, buttonSize, ImGui::IsItemHovered(), ImGui::IsItemActive(),
                ImGui::IsItemFocused(), selectedDomain == i);
            if (auto* icon = CraftLoadCachedTexture(m_renderer, m_craftIconCache, iconPath))
                draw->AddImage((ImTextureID)icon, {min.x + 5, min.y + 5}, {max.x - 5, max.y - 5});
            else
                draw->AddText({min.x + 12 * ui.scale(), min.y + 10 * ui.scale()},
                    ImGui::GetColorU32(medieval::Palette::Ink), "?");
            if (ImGui::IsItemHovered() || ImGui::IsItemFocused()) ImGui::SetTooltip("%s", label.c_str());
            ImGui::PopID();
        }
    }
    ImGui::SameLine();

    const json& selected = domains[static_cast<size_t>(selectedDomain)];
    const CraftDomainTreeSource treeSource = PlayerOverviewFindCraftDomainTreeSource(root, selected);
    const json& treeRoot = treeSource.root ? *treeSource.root : root;
    const json& treeDomain = treeSource.domain ? *treeSource.domain : selected;
    CraftSkillTreeView tree = CraftBuildSkillTreeView(treeRoot, selected, treeDomain, m_player.stats);

    std::string& selectedNodeId = selectedNodeByDomain[tree.domainId];
    if (CraftNodeIndexById(tree, selectedNodeId) < 0 && !tree.nodes.empty())
        selectedNodeId = tree.nodes[static_cast<size_t>(std::clamp(tree.activeNodeIndex, 0, static_cast<int>(tree.nodes.size()) - 1))].id;

    ImVec2& pan = panByDomain[tree.domainId];
    float& zoom = zoomByDomain[tree.domainId];

    medieval::Child workspace(ui, "##craft_workspace", {0, 0}, medieval::Surface::Wood);
    if (!workspace) return;
    const auto available = ImGui::GetContentRegionAvail();
    const bool stacked = available.x < 850 * ui.scale();
    const float detailWidth = 310 * ui.scale();
    const float mainWidth = stacked ? 0 : std::max(1.0f, available.x - detailWidth - ImGui::GetStyle().ItemSpacing.x);
    const float mainHeight = stacked ? available.y * 0.57f : 0;
    {
        medieval::Child main(ui, "##craft_tree_main", {mainWidth, mainHeight}, medieval::Surface::Wood);
        if (main)
        {
            ui.heading(tree.title);
            const CraftNodeView* activeNode = CraftSelectedNode(tree, selectedNodeId);
            ImGui::TextWrapped("%s%s", tree.progressLabel.c_str(),
                activeNode ? (std::string(U8("  ·  ")) + activeNode->label).c_str() : "");
            const bool resetView = ui.button("##reset-tree", U8("Přehled stromu"), std::min(190 * ui.scale(), ImGui::GetContentRegionAvail().x));
            ImGui::TextWrapped(U8("Kolečko: přiblížení  ·  LMB: výběr a posun"));
            CraftRenderSkillTreeCanvas(m_renderer, m_craftIconCache, tree, selectedNodeId, pan, zoom, resetView);
        }
    }
    if (!stacked) ImGui::SameLine();
    {
        medieval::Child detail(ui, "##craft_skill_detail", {stacked ? 0 : detailWidth, 0}, medieval::Surface::Parchment);
        if (detail) CraftRenderSkillDetail(m_renderer, m_craftIconCache, tree, CraftSelectedNode(tree, selectedNodeId));
    }
}


static SDL_Texture* loadTexture(SDL_Renderer* r, const char* path, int& outW, int& outH)
{
    outW = outH = 0;

    SDL_Surface* surf = IMG_Load(path);
    if (!surf)
        return nullptr;

    outW = surf->w;
    outH = surf->h;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);

    return tex;
}

void Campaign::renderDebugHud()
{
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.78f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("DEBUG HUD", nullptr, flags))
    {
        const auto& now = m_gameTime.now();
        const auto moon = m_moonCycle.getMoonInfo(now.day, now.month, now.year);
        const auto sun = m_sunCycle.getDayInfo(now.day, now.month, now.hour, now.minute);
        const auto& s = m_player.stats;

        const int weekDayIndex = m_gameTime.currentWeekDayIndexMondayFirst();
        const std::string weekDay = ui::WeekDayCz(weekDayIndex);

        const DayPhase dynamicPhase = getDynamicDayPhase();
        const std::string dynamicPhaseText = dayPhaseToString(dynamicPhase);

        const std::string feastTitle = m_liturgicalCalendar.primaryTitle(now.day, now.month, now.year);
        const std::string feastId = m_liturgicalCalendar.primaryId(now.day, now.month, now.year);
        const std::string feastImportance = m_liturgicalCalendar.primaryImportance(now.day, now.month, now.year);
        const std::vector<std::string> feastTags = m_liturgicalCalendar.tagsForDate(now.day, now.month, now.year);

        const float hp01 = m_player.stats.condition.health / 100.0f;
        const float stamina01 = m_player.stats.condition.stamina / 100.0f;
        const float hunger01 = m_player.stats.legacyHunger() / 100.0f;
        const float thirst01 = m_player.stats.legacyThirst() / 100.0f;
        const float fatigue01 = m_player.stats.condition.fatigue / 100.0f;
        const float dirtiness01 = m_player.stats.legacyHygiene() / 100.0f;
        const float stress01 = m_player.stats.condition.stress / 100.0f;
        const float morale01 = m_player.stats.condition.morale / 100.0f;
        const float medievalFeel01 = m_player.stats.legacyMedievalFeel() / 100.0f;

        const std::string docDate = m_liturgicalCalendar.formatMedievalDate(
            now.day, now.month, now.year, weekDay,
            LiturgicalCalendar::Style::Documentary);

        const std::string spokenDate = m_liturgicalCalendar.formatMedievalDate(
            now.day, now.month, now.year, weekDay,
            LiturgicalCalendar::Style::Spoken);

        const std::string latinDate = m_liturgicalCalendar.formatMedievalDate(
            now.day, now.month, now.year, weekDay,
            LiturgicalCalendar::Style::Latin);

        std::string tagsJoined;
        for (size_t i = 0; i < feastTags.size(); ++i)
        {
            if (i > 0)
                tagsJoined += ", ";
            tagsJoined += feastTags[i];
        }

        ImGui::Text("=== CAS ===");
        ImGui::Text("Civilni datum: %s %s", weekDay.c_str(), m_gameTime.formatDateCz().c_str());
        ImGui::Text("Cas: %s", m_gameTime.formatTime().c_str());
        ImGui::Text("Dynamicka cast dne: %s", dynamicPhaseText.c_str());
        ImGui::Text("Je nedele: %s", weekDayIndex == 6 ? "ano" : "ne");

        ImGui::Separator();

        ImGui::Text("=== LITURGICKY KALENDAR ===");
        ImGui::Text("Hlavni svatek: %s", feastTitle.empty() ? "-" : feastTitle.c_str());
        ImGui::Text("Feast ID: %s", feastId.empty() ? "-" : feastId.c_str());
        ImGui::Text("Importance: %s", feastImportance.empty() ? "-" : feastImportance.c_str());
        ImGui::TextWrapped("Tagy: %s", tagsJoined.empty() ? "-" : tagsJoined.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("Dokumentarni: %s", docDate.c_str());
        ImGui::TextWrapped("Mluvena forma: %s", spokenDate.c_str());
        ImGui::TextWrapped("Latinsky: %s", latinDate.c_str());

        ImGui::Separator();

        ImGui::Text("=== SLUNCE A MESIC ===");
        ImGui::Text("Vychod slunce: %s", SunCycle::formatMinutesHhMm(sun.sunriseMinutes).c_str());
        ImGui::Text("Zapad slunce: %s", SunCycle::formatMinutesHhMm(sun.sunsetMinutes).c_str());
        ImGui::Text("Je den: %s", sun.isDay ? "ano" : "ne");
        ImGui::Text("Faze mesice: %s", moon.phase.c_str());
        ImGui::Text("Jas mesice: %.1f%%", moon.brightness * 100.0f);

        ImGui::Separator();

        ImGui::Text("=== PLAYER ===");
        ImGui::Text("Jmeno: %s", m_player.fullName().c_str());
        ImGui::Text("Profil: %s", DebugBackgroundName(s.background));
        ImGui::Text("Move speed: %.1f", m_player.currentMoveSpeed());
        ImGui::Text("Base / mult: %.1f / %.2f", s.moveSpeedBase, s.getMoveSpeedMultiplier());

        ui::ProgressStat("HP", hp01);
        ui::ProgressStat("Stamina", stamina01);
        ui::ProgressStat(ui::text::Hunger(), hunger01);
        ui::ProgressStat(ui::text::Thirst(), thirst01);
        ui::ProgressStat(ui::text::Fatigue(), fatigue01);
        ui::ProgressStat(ui::text::Hygiene(), dirtiness01);
        ui::ProgressStat("Stres", stress01);
        ui::ProgressStat("Moralka", morale01);
        ui::ProgressStat(ui::text::Social(), medievalFeel01);

        ImGui::Spacing();
        ImGui::Text("Telesna tepelna rovnovaha: %.1f / 100", m_player.stats.condition.bodyTemperature);
        ImGui::Text("Nosnost: %.1f / %.1f", m_player.stats.carryWeight, m_player.stats.carryCapacity);
        ImGui::Text("Objem: %.1f / %.1f", m_player.stats.carryVolume, m_player.stats.carryVolumeCapacity);
        ImGui::Text("Rychlost pohybu: %.1f", m_player.stats.getMoveSpeed());

        ImGui::Separator();
        ImGui::Text("ATRIBUTY");
        DebugBulletValue("Sila", s.attributes.strength);
        DebugBulletValue("Vydrz", s.attributes.endurance);
        DebugBulletValue("Obratnost", s.attributes.dexterity);
        DebugBulletValue("Vnimani", s.attributes.perception);
        DebugBulletValue("Inteligence", s.attributes.intelligence);
        DebugBulletValue("Charisma", s.attributes.charisma);
        DebugBulletValue("Vule", s.attributes.willpower);

        ImGui::Separator();
        ImGui::Text("SKILLY");
        DebugBulletValue("Ohen", s.survival.fireMaking);
        DebugBulletValue("Drevo", s.survival.woodProcessing);
        DebugBulletValue("Pristresek", s.survival.shelterBuilding);
        DebugBulletValue("Voda", s.survival.waterPurification);
        DebugBulletValue("Opravy", s.craft.toolRepair);
        DebugBulletValue("Improvizace", s.craft.improvisation);
        DebugBulletValue("Jednani", s.social.negotiation);
        DebugBulletValue("Empatie", s.social.empathy);
        DebugBulletValue("Historie", s.knowledge.history);
        DebugBulletValue("Latina", s.knowledge.latin);
        DebugBulletValue("Nemcina", s.knowledge.german);
        DebugBulletValue("Stredoveka cestina", s.knowledge.medievalCzech);
        DebugBulletValue("Beh", s.physical.running);
        DebugBulletValue("Noseni bremen", s.physical.loadHandling);
        DebugBulletValue("Soustredeni", s.mental.focus);
        DebugBulletValue("Pozorovani", s.mental.observation);

        ImGui::Separator();
        ImGui::Text("KOMFORT A LOADOUT");
        DebugBulletValue("Priroda", s.comfort.natureComfort);
        DebugBulletValue("Mesto", s.comfort.urbanComfort);
        DebugBulletValue("Spolecnost", s.comfort.socialComfort);
        DebugBulletValue("Citlivost hygiena", s.comfort.hygieneSensitivity);
        DebugBulletValue("Adaptace na stredovek", s.comfort.medievalAdaptation);
        ImGui::Text("Hamaka / plachta / filtr: %s / %s / %s", s.loadout.hammock ? "ano" : "ne", s.loadout.tarp ? "ano" : "ne", s.loadout.waterFilter ? "ano" : "ne");
        ImGui::Text("Drevak / plyn / chemicky ohrivac: %s / %s / %s", s.loadout.woodStove ? "ano" : "ne", s.loadout.gasStove ? "ano" : "ne", s.loadout.rationHeater ? "ano" : "ne");
        ImGui::Text("Powerbanka / solar / mobil: %s / %s / %s", s.loadout.powerBank ? "ano" : "ne", s.loadout.solarPanel ? "ano" : "ne", s.loadout.smartphoneOffline ? "ano" : "ne");
        ImGui::Text("Cestovni sprcha / hygiena kit: %s / %s", s.loadout.travelShower ? "ano" : "ne", s.loadout.hygieneKit ? "ano" : "ne");
        ImGui::Text("Nadoby na vodu: %d", s.loadout.waterContainers);
        ImGui::Text("Zatepleni / plachta kvalita: %.1f / %.1f", s.loadout.beddingWarmth, s.loadout.tarpQuality);

        ImGui::Separator();
        ImGui::Text("INVENTAR");

        auto drawContainer = [&](const char* title, const ContainerInventory& inv)
        {
            ImGui::Text("%s", title);

            if (inv.items.empty())
            {
                ImGui::BulletText("-");
                return;
            }

            for (const auto& stack : inv.items)
            {
                if (stack.empty())
                    continue;

                auto it = m_itemDefs.find(stack.itemId);
                const char* itemName = stack.itemId.c_str();
                if (it != m_itemDefs.end())
                    itemName = it->second.name.c_str();

                ImGui::BulletText("%s x%d", itemName, stack.count);
            }
        };

        drawContainer("Kapsy", m_player.inventory.pockets);
        drawContainer("Batoh", m_player.inventory.backpack);

        if (m_activeFoodPoisoning.active)
        {
            const std::string poisonStatus = activeFoodPoisoningStatusText();
            ui::ColoredStatus(
                m_activeFoodPoisoning.fatal
                    ? ImVec4(1.0f, 0.25f, 0.25f, 1.0f)
                    : ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                poisonStatus.c_str());
        }

        if (m_player.stats.condition.poisoned)
            ui::ColoredStatus(ImVec4(0.7f, 1.0f, 0.3f, 1.0f), ui::text::Poisoned());

        if (m_player.stats.condition.injured)
            ui::ColoredStatus(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), ui::text::Injured());

        if (m_player.stats.condition.fracture)
            ui::ColoredStatus(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), ui::text::Fracture());

        if (m_player.stats.condition.bleeding)
            ui::ColoredStatus(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), ui::text::Bleeding());

        if (m_player.stats.condition.treatedWound)
            ui::ColoredStatus(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), ui::text::TreatedWound());

        ImGui::Separator();
        ImGui::Text("=== POCASI ===");
        ImGui::Text("Min / Max: %.1f / %.1f C", m_todayWeather.minTemp, m_todayWeather.maxTemp);
        ImGui::Text("Aktualni teplota: %.1f C", m_runtimeWeather.currentTemp);
        ImGui::Text("Oblacnost: %.0f %%", m_todayWeather.cloudiness);
        ImGui::Text("Sance srazek: %.0f %%", m_todayWeather.precipitationChance);
        ImGui::Text("Typ srazek: %s", WeatherSystem::precipitationTypeToString(m_todayWeather.precipitationType));
        ImGui::Text("Intenzita srazek: %.2f", m_todayWeather.precipitationIntensity);
        ImGui::Text("Prsi: %s", m_runtimeWeather.isRaining ? "ano" : "ne");
        ImGui::Text("Intenzita deste: %.2f", m_runtimeWeather.rainIntensity);
        ImGui::Text("Mlha: %s", m_runtimeWeather.isFoggy ? "ano" : "ne");
        ImGui::Text("Vitr dnes / ted: %.1f / %.1f km/h", m_todayWeather.wind.avg, m_runtimeWeather.windNow);
        ImGui::Text("Ranni mlha: %.0f %%", m_todayWeather.fogMorning);
        ImGui::Text("Vlhkost zeme: %.0f %%", m_todayWeather.groundWetness);
        ImGui::Text("Nepohoda: %.1f", m_runtimeWeather.discomfortIndex);
        ImGui::Text("Fronta: %s", m_todayWeather.frontType.c_str());

        if (m_godMode)
            ui::ColoredStatus(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "GOD MODE ON");
    }

    ImGui::End();
}

// Vykresl� item slot pro invent�?, v?etn? drag&drop logiky a tooltipu. Vrac� true pokud byl slot kliknut (pro p?�padn� dal?� akce, nap?. otev?en� detailn�ho pohledu na item).
void Campaign::drawLabeledSlot(
    const char* label,
    const char* id,
    ItemStack& slotRef,
    float slotSize)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    const float s = medieval::UI::Get().scale();
    const float labelWidth = std::min(170 * s, std::max(40 * s, ImGui::GetContentRegionAvail().x - slotSize - 12 * s));
    ImGui::SameLine(labelWidth);
    drawItemSlot(id, slotRef, m_itemDefs, slotSize);
}

void Campaign::renderDraggedItemIcon()
{
    if (!m_dragItem.active || m_dragItem.stack.empty())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        return;
    }

    ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    if (!m_defaultItemIcon)
        return;

    ImGuiIO& io = ImGui::GetIO();
    auto drawList = ImGui::GetForegroundDrawList();

    const float size = 32.0f;
    ImVec2 pos = io.MousePos;

    ImVec2 topLeft(pos.x - size * 0.5f, pos.y - size * 0.5f);
    ImVec2 bottomRight(topLeft.x + size, topLeft.y + size);

    // ghost background
    drawList->AddRectFilled(
        topLeft,
        bottomRight,
        IM_COL32(20, 20, 20, 120));

    // gold frame
    drawList->AddRect(
        topLeft,
        bottomRight,
        IM_COL32(255, 215, 80, 255),
        0.0f,
        0,
        2.0f);

    // icon
    auto it = m_itemDefs.find(m_dragItem.stack.itemId);

    SDL_Texture* tex = m_defaultItemIcon;

    if (it != m_itemDefs.end())
        tex = getItemIconTexture(it->second);

    drawList->AddImage(
        (ImTextureID)tex,
        topLeft,
        bottomRight);

    // stack count
    if (m_dragItem.stack.count > 1)
    {
        std::string txt = std::to_string(m_dragItem.stack.count);
        drawList->AddText(
            ImVec2(bottomRight.x - 14.0f, bottomRight.y - 16.0f),
            IM_COL32(255, 255, 0, 255),
            txt.c_str());
    }
}

bool Campaign::drawContainerSlot(
    const char* id,
    ContainerInventory& container,
    int index,
    const std::unordered_map<std::string, ItemDef>& defs,
    float size)
{
    container.compact();

    if (index < (int)container.items.size())
    {
        bool result = drawItemSlot(id, container.items[index], defs, size);
        container.compact();
        return result;
    }

    ItemStack virtualEmpty;

    ImGui::PushID(id);

    bool leftClicked = false;
    bool rightClicked = false;

    ImGui::Button("##slot", ImVec2(size, size));

    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    medieval::UI::Get().panel(ImGui::GetWindowDrawList(), min, {size, size}, medieval::Surface::Stone);

    bool canDropHere = true;
    if (m_dragItem.active && !m_dragItem.stack.empty())
    {
        auto it = defs.find(m_dragItem.stack.itemId);
        if (it != defs.end())
        {
            const ItemDef& def = it->second;
            canDropHere = container.canAccept(def, 1, defs);
        }
    }

    if (m_dragItem.active && ImGui::IsItemHovered())
    {
        ImU32 fillColor = canDropHere
            ? IM_COL32(80, 180, 90, 50)
            : IM_COL32(200, 70, 70, 50);

        ImGui::GetWindowDrawList()->AddRectFilled(min, max, fillColor);
    }

    ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(medieval::Palette::Brass);
    if (ImGui::IsItemHovered())
        borderColor = canDropHere
        ? IM_COL32(203, 178, 126, 255)
        : IM_COL32(220, 80, 80, 255);

    ImGui::GetWindowDrawList()->AddRect(min, max, borderColor, 0.0f, 0, 2.0f);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        leftClicked = true;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        rightClicked = true;

    if (leftClicked && m_dragItem.active && !m_dragItem.stack.empty())
    {
        auto it = defs.find(m_dragItem.stack.itemId);
        if (it != defs.end())
        {
            const ItemDef& def = it->second;
            if (container.canAccept(def, m_dragItem.stack.count, defs))
            {
                container.items.push_back(m_dragItem.stack);
                m_dragItem.stack.clear();
                m_dragItem.active = false;
                m_dragItem.source = nullptr;
                container.compact();
            }
        }
    }

    if (rightClicked && m_dragItem.active && !m_dragItem.stack.empty())
    {
        auto it = defs.find(m_dragItem.stack.itemId);
        if (it != defs.end())
        {
            const ItemDef& def = it->second;
            if (container.canAccept(def, 1, defs))
            {
                ItemStack s = m_dragItem.stack;
                s.count = 1;
                container.items.push_back(s);

                m_dragItem.stack.count -= 1;
                if (m_dragItem.stack.count <= 0)
                {
                    m_dragItem.stack.clear();
                    m_dragItem.active = false;
                    m_dragItem.source = nullptr;
                }

                container.compact();
            }
        }
    }

    ImGui::PopID();
    return leftClicked || rightClicked;
}

bool Campaign::isEquipmentSlotId(const std::string& slotId) const
{
    return
        slotId == "head" ||
        slotId == "torsoInner" ||
        slotId == "torsoOuter" ||
        slotId == "legs" ||
        slotId == "feet" ||
        slotId == "cloak" ||
        slotId == "beltEquip" ||
        slotId == "back" ||
        slotId == "leftHand" ||
        slotId == "rightHand" ||
        slotId == "qa_leftHand" ||
        slotId == "qa_rightHand";
}

void Campaign::refreshInventoryDerivedStats()
{
    m_player.stats.carryWeight = m_player.inventory.computeTotalWeight(m_itemDefs);
    m_player.stats.carryVolume = m_player.inventory.computeTotalVolume(m_itemDefs);
}

bool Campaign::isInventoryItemUsable(const ItemDef& def, const ItemStack& stack) const
{
    if (stack.empty())
        return false;

    const std::string& id = stack.itemId;

    if (id.rfind("forage_", 0) == 0 || id.rfind("forage:", 0) == 0)
        return true;

    if (id == "MRE" || id == "food_ration_basic")
        return true;

    if (ContainsIdToken(id, "water_bottle") ||
        ContainsIdToken(id, "canteen") ||
        ContainsIdToken(id, "cutora") ||
        ContainsIdToken(id, "cútora") ||
        ContainsIdToken(id, "čutora"))
    {
        return true;
    }

    return def.quickUsable;
}

bool Campaign::applyInventoryItemUseEffects(const std::string& itemId, const ItemDef& def, bool& outConsumeOne)
{
    outConsumeOne = false;

    auto clampNeeds = [&]()
    {
        m_player.stats.condition.health = PlayerStats::Clamp01To100(m_player.stats.condition.health);
        m_player.stats.condition.nutrition = PlayerStats::Clamp01To100(m_player.stats.condition.nutrition);
        m_player.stats.condition.hydration = PlayerStats::Clamp01To100(m_player.stats.condition.hydration);
        m_player.stats.condition.morale = PlayerStats::Clamp01To100(m_player.stats.condition.morale);
        m_player.stats.condition.stress = PlayerStats::Clamp01To100(m_player.stats.condition.stress);
    };

    if (ContainsIdToken(itemId, "water_bottle") ||
        ContainsIdToken(itemId, "canteen") ||
        ContainsIdToken(itemId, "cutora") ||
        ContainsIdToken(itemId, "cútora") ||
        ContainsIdToken(itemId, "čutora"))
    {
        const float before = m_player.stats.condition.hydration;
        m_player.stats.condition.hydration += 45.0f;
        m_player.stats.condition.morale += 1.0f;
        clampNeeds();

        if (m_player.stats.condition.hydration > before + 0.1f)
            consoleLog("Napil ses z nádoby. Žízeň polevila.");
        else
            consoleLog("Napil ses, ale už jsi byl dobře hydratovaný.");

        // TODO: later replace this with container charges / water quality.
        outConsumeOne = false;
        return true;
    }

    if (itemId == "MRE")
    {
        m_player.stats.condition.nutrition = 100.0f;
        m_player.stats.condition.morale += 3.0f;
        clampNeeds();
        consoleLog("Snědl jsi MRE. Vlastní ohřev udělal svoje — hlad je pryč.");
        outConsumeOne = true;
        return true;
    }

    if (itemId == "food_ration_basic")
    {
        m_player.stats.condition.nutrition += 38.0f;
        m_player.stats.condition.hydration -= 2.0f;
        clampNeeds();
        consoleLog("Snědl jsi základní dávku jídla.");
        outConsumeOne = true;
        return true;
    }

    if (itemId.rfind("forage_", 0) == 0)
    {
        const std::string speciesId = itemId.substr(7);
        const ForageSpeciesDef* species = m_forageDb.findSpecies(speciesId);

        if (!species)
        {
            m_player.stats.condition.nutrition += 2.0f;

            // Safety fallback for older saves/items where the dynamic forage item id no longer
            // maps exactly back to species.json. It lets Amanita test items still trigger poisoning.
            const std::string probe = itemId + " " + def.name + " " + def.description;
            if (ContainsIdToken(probe, "zelen") || ContainsIdToken(probe, "phalloides") || ContainsIdToken(probe, "kalich"))
            {
                startFoodPoisoning(def.name.empty() ? itemId : def.name, true);
                consoleLog("Snědl jsi neznámý, ale velmi nebezpečný vzorek. Otrava byla spuštěna podle názvu předmětu.");
            }
            else if (ContainsIdToken(probe, "červen") || ContainsIdToken(probe, "cerven") || ContainsIdToken(probe, "muscaria"))
            {
                startFoodPoisoning(def.name.empty() ? itemId : def.name, false);
                consoleLog("Snědl jsi podezřelý vzorek. Spouštím lehkou otravu podle názvu předmětu.");
            }
            else
            {
                consoleLog("Ochutnal jsi neznámý přírodní vzorek. Moc tě to nezasytilo.");
            }

            clampNeeds();
            outConsumeOne = true;
            return true;
        }

        const bool knownSpecies = isForageSpeciesKnown(species->id);
        const std::string trueName = species->trueName.empty() ? species->id : species->trueName;
        const std::string eatenLabel = knownSpecies
            ? trueName
            : (def.name.empty() ? std::string("neznámý přírodní vzorek") : def.name);

        // Raw mushrooms are intentionally weak as food until cooking/crafting exists.
        if (species->archetypeId == "generic_mushroom_patch")
        {
            m_player.stats.condition.nutrition += 6.0f;
            m_player.stats.condition.morale -= 1.0f;
        }
        else if (species->edibility == "edible")
        {
            m_player.stats.condition.nutrition += 8.0f;
        }
        else
        {
            m_player.stats.condition.nutrition += 2.0f;
        }

        if (HasForageEffect(*species, "poison_deadly_48h") || species->toxicityLevel >= 90 ||
            species->id.find("zelen") != std::string::npos || species->trueName.find("zelen") != std::string::npos)
        {
            startFoodPoisoning(eatenLabel, true);
            consoleLog("Snědl jsi " + eatenLabel + ". Zpočátku se nemusí stát nic, ale tohle byla velmi špatná volba.");
        }
        else if (HasForageEffect(*species, "poison_mild") || species->toxicityLevel >= 25 ||
            species->id.find("červen") != std::string::npos || species->trueName.find("červen") != std::string::npos)
        {
            startFoodPoisoning(eatenLabel, false);
            consoleLog("Snědl jsi " + eatenLabel + ". Začíná ti být divně od žaludku.");
        }
        else
        {
            consoleLog("Snědl jsi " + eatenLabel + ". Syrové tě to zasytilo jen trochu.");
        }

        clampNeeds();
        outConsumeOne = true;
        return true;
    }

    if (def.quickUsable)
    {
        consoleLog("Tenhle předmět zatím nemá přiřazený konkrétní účinek použití.");
        return false;
    }

    return false;
}

bool Campaign::useInventoryItem(ItemStack& stack)
{
    if (stack.empty())
        return false;

    auto it = m_itemDefs.find(stack.itemId);
    if (it == m_itemDefs.end())
        return false;

    bool consumeOne = false;
    if (!applyInventoryItemUseEffects(stack.itemId, it->second, consumeOne))
        return false;

    if (consumeOne)
    {
        stack.count -= 1;
        if (stack.count <= 0)
            stack.clear();
    }

    refreshInventoryDerivedStats();
    return true;
}

bool Campaign::canPlaceItemIntoSlot(
    const ItemStack& movingStack,
    const ItemStack& targetSlot,
    const std::string& slotId,
    const std::unordered_map<std::string, ItemDef>& defs) const
{
    if (movingStack.empty())
        return false;

    auto it = defs.find(movingStack.itemId);
    if (it == defs.end())
        return false;

    const ItemDef& def = it->second;

    // container sloty (batoh/kapsy) ? tam m??e skoro v?echno
    if (slotId.rfind("bp_", 0) == 0 || slotId.rfind("pocket_", 0) == 0)
        return true;

    // belt sloty
    if (slotId == "qa_knife")
        return def.beltCompatible && def.preferredBeltSlot == BeltSlot::Knife;

    if (slotId == "qa_pouch")
        return def.beltCompatible && def.preferredBeltSlot == BeltSlot::Pouch;

    if (slotId == "qa_util1" || slotId == "qa_util2")
        return def.beltCompatible &&
        (def.preferredBeltSlot == BeltSlot::Utility1 ||
            def.preferredBeltSlot == BeltSlot::Utility2 ||
            def.preferredBeltSlot == BeltSlot::None);

    // ruce
    if (slotId == "leftHand" || slotId == "rightHand" ||
        slotId == "qa_leftHand" || slotId == "qa_rightHand")
    {
        return def.equipSlot == EquipSlot::MainHand ||
            def.equipSlot == EquipSlot::OffHand ||
            def.category == ItemCategory::Tool ||
            def.category == ItemCategory::WeaponTool;
    }

    // equipment sloty
    if (slotId == "head")       return def.equippable && def.equipSlot == EquipSlot::Head;
    if (slotId == "torsoInner") return def.equippable && def.equipSlot == EquipSlot::TorsoInner;
    if (slotId == "torsoOuter") return def.equippable && def.equipSlot == EquipSlot::TorsoOuter;
    if (slotId == "legs")       return def.equippable && def.equipSlot == EquipSlot::Legs;
    if (slotId == "feet")       return def.equippable && def.equipSlot == EquipSlot::Feet;
    if (slotId == "cloak")      return def.equippable && def.equipSlot == EquipSlot::Cloak;
    if (slotId == "beltEquip")  return def.equippable && def.equipSlot == EquipSlot::Belt;
    if (slotId == "back")       return def.equippable && def.equipSlot == EquipSlot::Back;

    return false;
}

bool Campaign::drawItemSlot(
    const char* id,
    ItemStack& slot,
    const std::unordered_map<std::string, ItemDef>& defs,
    float size)
{
    ImGui::PushID(id);

    const std::string slotKey = id;

    bool leftClicked = false;
    bool rightClicked = false;

    auto itDef = defs.find(slot.itemId);
    const ItemDef* slotDef = (!slot.empty() && itDef != defs.end()) ? &itDef->second : nullptr;
    const bool itemLocked = (slotDef != nullptr) ? slotDef->lockedInInventory : false;

    ImGuiIO& io = ImGui::GetIO();
    const bool shiftHeld = io.KeyShift;

    ImGui::Button("##slot", ImVec2(size, size));

    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    medieval::UI::Get().panel(ImGui::GetWindowDrawList(), min, {size, size}, medieval::Surface::Stone);

    bool canDropHere = true;
    if (m_dragItem.active && !m_dragItem.stack.empty())
        canDropHere = canPlaceItemIntoSlot(m_dragItem.stack, slot, slotKey, defs);

    ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(medieval::Palette::Brass);

    if (ImGui::IsItemHovered())
        borderColor = canDropHere
        ? IM_COL32(203, 178, 126, 255)
        : IM_COL32(220, 80, 80, 255);

    if (m_dragItem.active && m_dragItem.source == &slot)
        borderColor = IM_COL32(255, 215, 80, 255);

    const bool activeSmithingWorkpiece =
        (slotKey == "qa_util1" || slotKey == "qa_util2") &&
        (slot.itemId == "smithing_iron_rod_piece" || slot.itemId == "smithing_nail_workpiece");

    if (m_dragItem.active && ImGui::IsItemHovered())
    {
        ImU32 fillColor = canDropHere
            ? IM_COL32(80, 180, 90, 50)
            : IM_COL32(200, 70, 70, 50);

        ImGui::GetWindowDrawList()->AddRectFilled(min, max, fillColor);
    }

    if (activeSmithingWorkpiece)
    {
        ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(255, 140, 55, 34));
        borderColor = IM_COL32(255, 164, 70, 255);
    }

    ImGui::GetWindowDrawList()->AddRect(min, max, borderColor, 0.0f, 0, 2.0f);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        leftClicked = true;

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        rightClicked = true;

    const bool inspectClicked =
        ImGui::IsItemHovered() &&
        io.KeyAlt &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    bool drewAtlasIcon = false;
    if (!slot.empty() && slotDef && slotDef->spriteId.rfind("forage:", 0) == 0)
    {
        const std::string forageSpriteId = slotDef->spriteId.substr(7);
        auto itSprite = m_forageSprites.find(forageSpriteId);
        if (itSprite != m_forageSprites.end())
        {
            SDL_Texture* tex = textureForForageSprite(itSprite->second);
            if (tex)
            {
                int texW = 1, texH = 1;
                SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);

                const float iconSize = 32.0f;
                const float iconX = min.x + (size - iconSize) * 0.5f;
                const float iconY = min.y + (size - iconSize) * 0.5f;

                const SDL_Rect& src = itSprite->second.src;
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)tex,
                    ImVec2(iconX, iconY),
                    ImVec2(iconX + iconSize, iconY + iconSize),
                    ImVec2((float)src.x / (float)texW, (float)src.y / (float)texH),
                    ImVec2((float)(src.x + src.w) / (float)texW, (float)(src.y + src.h) / (float)texH));

                drewAtlasIcon = true;
            }
        }
    }

    SDL_Texture* iconTex = m_defaultItemIcon;
    if (!drewAtlasIcon && slotDef)
        iconTex = getItemIconTexture(*slotDef);

    if (!slot.empty() && !drewAtlasIcon && iconTex)
    {
        const float iconSize = 32.0f;
        const float iconX = min.x + (size - iconSize) * 0.5f;
        const float iconY = min.y + (size - iconSize) * 0.5f;

        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)iconTex,
            ImVec2(iconX, iconY),
            ImVec2(iconX + iconSize, iconY + iconSize));
    }

    if (!slot.empty() && slot.count > 1)
    {
        std::string countText = std::to_string(slot.count);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(min.x + size - 16.0f, min.y + size - 18.0f),
            IM_COL32(255, 220, 80, 255),
            countText.c_str());
    }

    if (!slot.empty() && ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 18.0f);
        ImGui::SetWindowFontScale(1.0f);

        if (slotDef)
        {
            ImGui::TextUnformatted(slotDef->name.c_str());

            if (!slotDef->description.empty())
            {
                ImGui::Separator();
                ImGui::TextWrapped("%s", slotDef->description.c_str());
            }

            if (!slotDef->flavorText.empty())
            {
                ImGui::Separator();
                ImGui::TextWrapped("\"%s\"", slotDef->flavorText.c_str());
            }

            ImGui::Separator();
            ImGui::Text("Vaha: %.2f", slotDef->weight);
            ImGui::Text("Objem: %.2f", slotDef->volume);
            ImGui::Text("Odolnost: %.0f %%", slot.durability);

            if (slotDef->lockedInInventory)
            {
                ImGui::Separator();
                ImGui::TextDisabled("Tento predmet nelze odlozit.");
            }

            if (!slotDef->audioNoteSfx.empty())
            {
                ImGui::Separator();
                ImGui::TextDisabled("Alt + LMB = přehrát poznámku");
            }

            if (isInventoryItemUsable(*slotDef, slot))
            {
                ImGui::Separator();
                ImGui::TextDisabled("Ctrl + LMB / dvojklik = použít");
            }
        }

        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    const bool useClicked =
        !m_dragItem.active &&
        !slot.empty() &&
        slotDef &&
        isInventoryItemUsable(*slotDef, slot) &&
        ((leftClicked && io.KeyCtrl) ||
         (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)));

    if (useClicked)
    {
        const bool used = useInventoryItem(slot);
        ImGui::PopID();
        return used;
    }

	// ALT + LEVY KLIK = prehrat zvukovou poznamku
    if (inspectClicked)
    {
        if (slotDef && !slotDef->audioNoteSfx.empty())
            m_audioManager.playSfx(slotDef->audioNoteSfx);

        ImGui::PopID();
        return true;
    }

    // LEVY KLIK = cely stack / merge / swap
    if (leftClicked)
    {
        if (!m_dragItem.active && !slot.empty() && !itemLocked)
        {
            if (shiftHeld && slot.count > 1)
            {
                m_dragItem.active = true;
                m_dragItem.source = &slot;
                m_dragItem.stack = slot;

                const int take = (slot.count + 1) / 2;
                m_dragItem.stack.count = take;
                slot.count -= take;

                if (slot.count <= 0)
                    slot.clear();
            }
            else
            {
                m_dragItem.active = true;
                m_dragItem.stack = slot;
                m_dragItem.source = &slot;
                slot.clear();
            }
        }
        else if (m_dragItem.active)
        {
            if (canPlaceItemIntoSlot(m_dragItem.stack, slot, slotKey, defs))
            {
                if (!slot.empty() && slot.itemId == m_dragItem.stack.itemId)
                {
                    auto it = defs.find(slot.itemId);
                    if (it != defs.end() && it->second.stackable)
                    {
                        const int maxStack = std::max(1, it->second.maxStack);
                        const int freeSpace = maxStack - slot.count;

                        if (freeSpace > 0)
                        {
                            const int moveCount = std::min(freeSpace, m_dragItem.stack.count);
                            slot.count += moveCount;
                            m_dragItem.stack.count -= moveCount;

                            if (m_dragItem.stack.count <= 0)
                            {
                                m_dragItem.stack.clear();
                                m_dragItem.active = false;
                                m_dragItem.source = nullptr;
                            }
                        }
                        else
                        {
                            ItemStack temp = slot;
                            slot = m_dragItem.stack;
                            m_dragItem.stack = temp;
                        }
                    }
                    else
                    {
                        ItemStack temp = slot;
                        slot = m_dragItem.stack;
                        m_dragItem.stack = temp;
                    }
                }
                else
                {
                    ItemStack temp = slot;
                    slot = m_dragItem.stack;
                    m_dragItem.stack = temp;

                    if (m_dragItem.stack.empty())
                    {
                        m_dragItem.active = false;
                        m_dragItem.source = nullptr;
                    }
                }
            }
        }
    }

    // PRAVY KLIK = split stack / poloz 1 kus
    if (rightClicked)
    {
        if (!m_dragItem.active && !slot.empty() && slot.count > 1 && !itemLocked)
        {
            m_dragItem.active = true;
            m_dragItem.source = &slot;
            m_dragItem.stack = slot;

            const int take = (slot.count + 1) / 2;
            m_dragItem.stack.count = take;
            slot.count -= take;

            if (slot.count <= 0)
                slot.clear();
        }
        else if (!m_dragItem.active && !slot.empty() && slot.count == 1 && !itemLocked)
        {
            m_dragItem.active = true;
            m_dragItem.source = &slot;
            m_dragItem.stack = slot;
            slot.clear();
        }
        else if (m_dragItem.active)
        {
            if (!canPlaceItemIntoSlot(m_dragItem.stack, slot, slotKey, defs))
            {
                // nic
            }
            else if (slot.empty())
            {
                slot = m_dragItem.stack;
                slot.count = 1;
                m_dragItem.stack.count -= 1;

                if (m_dragItem.stack.count <= 0)
                {
                    m_dragItem.stack.clear();
                    m_dragItem.active = false;
                    m_dragItem.source = nullptr;
                }
            }
            else if (slot.itemId == m_dragItem.stack.itemId)
            {
                auto it = defs.find(slot.itemId);
                if (it != defs.end())
                {
                    const int maxStack = std::max(1, it->second.maxStack);
                    if (slot.count < maxStack)
                    {
                        slot.count += 1;
                        m_dragItem.stack.count -= 1;

                        if (m_dragItem.stack.count <= 0)
                        {
                            m_dragItem.stack.clear();
                            m_dragItem.active = false;
                            m_dragItem.source = nullptr;
                        }
                    }
                }
            }
        }
    }

    ImGui::PopID();
    return leftClicked || rightClicked || inspectClicked;
}

void Campaign::renderPlayerOverviewUI()
{
    if (!m_playerOverviewOpen)
        return;

    if (m_playerOverviewFocus)
    {
        ImGui::SetNextWindowFocus();
        m_playerOverviewFocus = false;
    }

    ImGui::SetNextWindowPos(ImVec2(90, 90), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(760, 620), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(560, 430), ImVec2(1200, 900));

    if (!ImGui::Begin("Hrac", &m_playerOverviewOpen))
    {
        ImGui::End();
        return;
    }

    const PlayerStats& s = m_player.stats;
    const PlayerStats::ConditionState& c = s.condition;

    ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.48f, 1.0f), "%s", m_player.fullName().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.66f, 0.62f, 0.52f, 1.0f), "%s", DebugBackgroundName(s.background));
    ImGui::Separator();

    if (ImGui::BeginTabBar("##player_overview_tabs"))
    {
        if (ImGui::BeginTabItem("Stav"))
        {
            PlayerOverviewHeader("Kondice");
            PlayerOverviewBar("Zivot", c.health);
            PlayerOverviewBar("Stamina", c.stamina);
            PlayerOverviewBar("Vyziva", c.nutrition);
            PlayerOverviewBar("Hydratace", c.hydration);
            PlayerOverviewBar("Moralka", c.morale);
            PlayerOverviewBar("Hygiena", c.hygiene);

            PlayerOverviewHeader("Zatizeni tela");
            PlayerOverviewBar("Unava", c.fatigue, true);
            PlayerOverviewBar("Stres", c.stress, true);
            PlayerOverviewBar("Promoceni", c.wetness, true);
            PlayerOverviewBar("Nemoc", c.diseaseLoad, true);
            PlayerOverviewBar("Bolest", c.pain, true);
            PlayerOverviewBar("Telesna teplota", c.bodyTemperature);

            PlayerOverviewHeader("Stavy");
            if (ImGui::BeginTable("##player_state_flags", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                PlayerOverviewFlag("Otrava", c.poisoned);
                PlayerOverviewFlag("Zraneni", c.injured);
                PlayerOverviewFlag("Zlomenina", c.fracture);
                ImGui::TableNextColumn();
                PlayerOverviewFlag("Krvaceni", c.bleeding);
                PlayerOverviewFlag("Osetrena rana", c.treatedWound);
                if (m_activeFoodPoisoning.active)
                    ImGui::TextColored(ImVec4(0.95f, 0.58f, 0.22f, 1.0f), "%s", activeFoodPoisoningStatusText().c_str());
                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Dovednosti"))
        {
            PlayerOverviewHeader("Preziti");
            PlayerOverviewSkill("Ohen", s.survival.fireMaking);
            PlayerOverviewSkill("Drevo", s.survival.woodProcessing);
            PlayerOverviewSkill("Pristresek", s.survival.shelterBuilding);
            PlayerOverviewSkill("Cisteni vody", s.survival.waterPurification);
            PlayerOverviewSkill("Stopovani", s.survival.tracking);
            PlayerOverviewSkill("Orientace", s.survival.navigation);
            PlayerOverviewSkill("Sber", s.survival.foraging);
            PlayerOverviewSkill("Vareni", s.survival.cooking);
            PlayerOverviewSkill("Odolnost chladu", s.survival.coldResistance);
            PlayerOverviewSkill("Odolnost horku", s.survival.heatResistance);
            PlayerOverviewSkill("Snasenlivost jidla", s.survival.foodTolerance);
            PlayerOverviewSkill("Odolnost nemoci", s.survival.diseaseResistance);

            PlayerOverviewHeader(U8("Řemeslo"));
            PlayerOverviewSkill("Opravy nastroju", s.craft.toolRepair);
            PlayerOverviewSkill("Opravy odevu", s.craft.clothingRepair);
            PlayerOverviewSkill("Provaznictvi", s.craft.ropeWork);
            PlayerOverviewSkill("Prace se drevem", s.craft.woodcraft);
            PlayerOverviewSkill("Improvizace", s.craft.improvisation);
            PlayerOverviewSkill("Kovarstvi", s.craft.smithing);

            PlayerOverviewHeader("Socialni");
            PlayerOverviewSkill("Zahajeni rozhovoru", s.social.conversationInitiation);
            PlayerOverviewSkill("Presvedcovani", s.social.persuasion);
            PlayerOverviewSkill("Smlouvani", s.social.negotiation);
            PlayerOverviewSkill("Empatie", s.social.empathy);
            PlayerOverviewSkill("Etiketa", s.social.etiquette);
            PlayerOverviewSkill("Dav", s.social.crowdComfort);
            PlayerOverviewSkill("Socialni pravidla", s.social.socialProtocol);

            PlayerOverviewHeader("Znalosti");
            PlayerOverviewSkill("Historie", s.knowledge.history);
            PlayerOverviewSkill("Nabozenstvi", s.knowledge.religionKnowledge);
            PlayerOverviewSkill("Symboly", s.knowledge.symbolRecognition);
            PlayerOverviewSkill("Gramotnost", s.knowledge.literacy);
            PlayerOverviewSkill("Latina", s.knowledge.latin);
            PlayerOverviewSkill("Nemcina", s.knowledge.german);
            PlayerOverviewSkill("Stredoveka cestina", s.knowledge.medievalCzech);

            PlayerOverviewHeader("Telo a mysl");
            PlayerOverviewSkill("Beh", s.physical.running);
            PlayerOverviewSkill("Noseni bremen", s.physical.loadHandling);
            PlayerOverviewSkill("Lezeni", s.physical.climbing);
            PlayerOverviewSkill("Plavani", s.physical.swimming);
            PlayerOverviewSkill("Regenerace spankem", s.physical.sleepRecovery);
            PlayerOverviewSkill("Efektivita staminy", s.physical.staminaEfficiency);
            PlayerOverviewSkill("Soustredeni", s.mental.focus);
            PlayerOverviewSkill("Pamet", s.mental.memory);
            PlayerOverviewSkill("Odolnost stresu", s.mental.stressResistance);
            PlayerOverviewSkill("Adaptace", s.mental.adaptation);
            PlayerOverviewSkill("Pozorovani", s.mental.observation);
            PlayerOverviewSkill("Samota", s.mental.lonelinessTolerance);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Atributy"))
        {
            PlayerOverviewHeader("Atributy");
            PlayerOverviewSkill("Sila", s.attributes.strength);
            PlayerOverviewSkill("Vydrz", s.attributes.endurance);
            PlayerOverviewSkill("Obratnost", s.attributes.dexterity);
            PlayerOverviewSkill("Vnimani", s.attributes.perception);
            PlayerOverviewSkill("Inteligence", s.attributes.intelligence);
            PlayerOverviewSkill("Charisma", s.attributes.charisma);
            PlayerOverviewSkill("Vule", s.attributes.willpower);

            PlayerOverviewHeader("Komfort");
            PlayerOverviewSkill("Priroda", s.comfort.natureComfort);
            PlayerOverviewSkill("Mesto", s.comfort.urbanComfort);
            PlayerOverviewSkill("Spolecnost", s.comfort.socialComfort);
            PlayerOverviewSkill("Citlivost hygiena", s.comfort.hygieneSensitivity);
            PlayerOverviewSkill("Adaptace na stredovek", s.comfort.medievalAdaptation);

            PlayerOverviewHeader("Odvozene hodnoty");
            PlayerOverviewValue("Aktualni rychlost", m_player.currentMoveSpeed(), " px/s");
            PlayerOverviewValue("Chuze", s.getLimitedMoveSpeed(false), " px/s");
            PlayerOverviewValue("Beh", s.getLimitedMoveSpeed(true), " px/s");
            PlayerOverviewValue("Zaklad rychlosti", s.moveSpeedBase, " px/s");
            PlayerOverviewValue("Nasobitel rychlosti", s.getMoveSpeedMultiplier());
            PlayerOverviewValue("Nesena vaha", s.carryWeight, " kg");
            PlayerOverviewValue("Nosnost", s.carryCapacity, " kg");
            PlayerOverviewValue("Neseny objem", s.carryVolume);
            PlayerOverviewValue("Objemova kapacita", s.carryVolumeCapacity);
            PlayerOverviewValue("Autenticita odevu", s.outfitAuthenticity);
            PlayerOverviewValue("Prijeti vesnici", s.villageAcceptance);
            PlayerOverviewValue("Duvera cirkve", s.churchTrust);
            PlayerOverviewValue("Duvera rychtare", s.reeveTrust);
            PlayerOverviewValue("Strach", s.fear);

            PlayerOverviewHeader("Vybaveni na startu");
            PlayerOverviewFlag("Hamaka", s.loadout.hammock);
            PlayerOverviewFlag("Celta", s.loadout.tarp);
            PlayerOverviewFlag("Filtr na vodu", s.loadout.waterFilter);
            PlayerOverviewFlag("Drevak", s.loadout.woodStove);
            PlayerOverviewFlag("Plynovy varic", s.loadout.gasStove);
            PlayerOverviewFlag("Mobil offline", s.loadout.smartphoneOffline);
            PlayerOverviewFlag("Hygiena", s.loadout.hygieneKit);
            ImGui::Text("Nadoby na vodu: %d", s.loadout.waterContainers);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void Campaign::renderQuickAccessBar()
{
    const float slot = 40.0f;
    const float spacing = 6.0f;
    const float totalWidth = slot * 6.0f + spacing * 5.0f;

    int screenW = 0, screenH = 0;
    SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);

    ImGui::SetNextWindowPos(
        ImVec2((screenW - totalWidth) * 0.5f, screenH - 90.0f),
        ImGuiCond_Always);

    ImGui::SetNextWindowBgAlpha(0.35f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("QuickAccessBar", nullptr, flags))
    {
        auto& inv = m_player.inventory;

		inv.backpack.compact();
		inv.pockets.compact();

        drawItemSlot("qa_leftHand", inv.equipped.offHand, m_itemDefs, slot);
        ImGui::SameLine();

        drawItemSlot("qa_knife", inv.beltSlots.knife, m_itemDefs, slot);
        ImGui::SameLine();

        drawItemSlot("qa_pouch", inv.beltSlots.pouch, m_itemDefs, slot);
        ImGui::SameLine();

        drawItemSlot("qa_util1", inv.beltSlots.utility1, m_itemDefs, slot);
        ImGui::SameLine();

        drawItemSlot("qa_util2", inv.beltSlots.utility2, m_itemDefs, slot);
        ImGui::SameLine();

        drawItemSlot("qa_rightHand", inv.equipped.mainHand, m_itemDefs, slot);
    }

    ImGui::End();
}

SDL_Texture* Campaign::getItemIconTexture(const ItemDef& def)
{
    if (def.spriteId.empty())
        return m_defaultItemIcon;

    auto it = m_itemIconCache.find(def.spriteId);
    if (it != m_itemIconCache.end())
        return it->second ? it->second : m_defaultItemIcon;

    std::filesystem::path path;
    if (def.spriteId.find('/') != std::string::npos || def.spriteId.find('\\') != std::string::npos)
    {
        path = def.spriteId;
        if (!path.has_extension())
            path += ".png";
    }
    else
    {
        path = std::filesystem::path("assets") / "data" / "items" / (def.spriteId + ".png");
    }

    int w = 0, h = 0;
    SDL_Texture* tex = loadTexture(m_renderer, path.string().c_str(), w, h);

    m_itemIconCache[def.spriteId] = tex;
    return tex ? tex : m_defaultItemIcon;
}

SDL_Texture* Campaign::getCraftIconTexture(const std::string& iconPath)
{
    if (iconPath.empty())
        return nullptr;

    auto it = m_craftIconCache.find(iconPath);
    if (it != m_craftIconCache.end())
        return it->second;

    std::filesystem::path path(iconPath);
    if (path.is_relative())
        path = pathutils::ProjectRoot() / path;

    int w = 0;
    int h = 0;
    SDL_Texture* tex = loadTexture(m_renderer, path.string().c_str(), w, h);
    m_craftIconCache[iconPath] = tex;
    return tex;
}

void Campaign::unloadItemIcons()
{
    for (auto& [id, tex] : m_itemIconCache)
    {
        if (tex)
            SDL_DestroyTexture(tex);
    }
    m_itemIconCache.clear();
}

void Campaign::unloadCraftIcons()
{
    for (auto& [id, tex] : m_craftIconCache)
    {
        if (tex)
            SDL_DestroyTexture(tex);
    }
    m_craftIconCache.clear();
}

static SDL_Rect GetObjectWorldRectScaled(const gameobj::ObjectDef& def, int wx, int wy, float scale)
{
    SDL_Rect r{};
    r.w = (int)std::lround(def.src.w * scale);
    r.h = (int)std::lround(def.src.h * scale);
    r.x = (int)std::lround(wx - def.pivot.x * scale);
    r.y = (int)std::lround(wy - def.pivot.y * scale);
    return r;
}



bool Campaign::isForageSpeciesKnown(const std::string& speciesId) const
{
    if (speciesId.empty())
        return false;

    auto it = m_forageKnowledge.entries.find(speciesId);
    if (it == m_forageKnowledge.entries.end())
        return false;

    return it->second.knowledgeLevel >= KnowledgeLevel::Verified;
}

const ForageSpeciesDef* Campaign::resolveForageSpeciesForSpawn(const ForageSpawnDef& spawn) const
{
    if (const ForageSpeciesDef* picked = m_forageSystem.pickSpeciesForSpawn(spawn, m_forageDb, 1337u))
        return picked;

    // Authoring safety net: when a direct-species spawn was saved with a concrete
    // sprite but without species_pool, recover the species from that sprite.
    if (!spawn.genericMapSpriteOverride.empty())
    {
        for (const auto& kv : m_forageDb.species())
        {
            const ForageSpeciesDef& sp = kv.second;
            if (sp.archetypeId != spawn.archetypeId)
                continue;

            if (sp.detailSprite == spawn.genericMapSpriteOverride ||
                sp.inventorySprite == spawn.genericMapSpriteOverride ||
                sp.herbariumSprite == spawn.genericMapSpriteOverride)
            {
                return &sp;
            }
        }
    }

    // Demo-data fallback: if an archetype has exactly one species, it is still
    // safe to examine that species. Once one archetype contains multiple herbs
    // or mushrooms, the editor should save species_pool explicitly.
    const ForageSpeciesDef* only = nullptr;
    for (const auto& kv : m_forageDb.species())
    {
        const ForageSpeciesDef& sp = kv.second;
        if (sp.archetypeId != spawn.archetypeId)
            continue;

        if (only)
            return nullptr;

        only = &sp;
    }

    return only;
}

std::string Campaign::resolveForageSpriteId(const ForageSpawnDef& spawn) const
{
    const ForageArchetypeDef* archetype = m_forageDb.findArchetype(spawn.archetypeId);
    if (archetype && !archetype->genericMapSprite.empty())
        return archetype->genericMapSprite;

    if (!spawn.genericMapSpriteOverride.empty())
        return spawn.genericMapSpriteOverride;

    return {};
}

std::string Campaign::resolveForageDisplaySpriteId(const ForageSpawnDef& spawn) const
{
    const ForageSpeciesDef* species =
        resolveForageSpeciesForSpawn(spawn);

    if (species && isForageSpeciesKnown(species->id))
    {
        if (!species->detailSprite.empty())
            return species->detailSprite;
        if (!species->inventorySprite.empty())
            return species->inventorySprite;
        if (!species->herbariumSprite.empty())
            return species->herbariumSprite;
    }

    // Unknown spawns stay visually generic. This is important for gameplay: the
    // player should not learn the exact species from the map before identification.
    return resolveForageSpriteId(spawn);
}

float Campaign::resolveForageMapScale(const ForageSpawnDef& spawn) const
{
    // Prefer the value parsed directly into runtime data. The editor saves these fields
    // into forage_archetypes.json / *.forage.json.
    if (spawn.mapScaleOverride > 0.0f)
        return std::clamp(spawn.mapScaleOverride, 0.03f, 4.0f);

    const ForageArchetypeDef* archetype = m_forageDb.findArchetype(spawn.archetypeId);
    if (archetype && archetype->mapScale > 0.0f)
        return std::clamp(archetype->mapScale, 0.03f, 4.0f);

    // Backward compatibility with older cache-based runtime patches.
    auto itSpawn = m_forageSpawnScaleOverride.find(spawn.id);
    if (itSpawn != m_forageSpawnScaleOverride.end() && itSpawn->second > 0.0f)
        return std::clamp(itSpawn->second, 0.03f, 4.0f);

    auto itArch = m_forageArchetypeMapScale.find(spawn.archetypeId);
    if (itArch != m_forageArchetypeMapScale.end() && itArch->second > 0.0f)
        return std::clamp(itArch->second, 0.03f, 4.0f);

    return 0.20f;
}

int Campaign::currentForageDaySerial() const
{
    const auto& now = m_gameTime.now();
    return now.year * 372 + now.month * 31 + now.day;
}

std::string Campaign::forageSpawnRuntimeKey(const ForageSpawnDef& spawn) const
{
    return spawn.id + "@" + std::to_string(spawn.tileX) + "," + std::to_string(spawn.tileY);
}

bool Campaign::isForageSpawnAvailable(const ForageSpawnDef& spawn) const
{
    const std::string key = forageSpawnRuntimeKey(spawn);
    if (m_depletedForageSpawnIds.contains(key))
        return false;

    auto it = m_forageRespawnAvailableDay.find(key);
    if (it == m_forageRespawnAvailableDay.end())
        return true;

    return currentForageDaySerial() >= it->second;
}

void Campaign::markForageSpawnGathered(const ForageSpawnDef& spawn)
{
    const std::string key = forageSpawnRuntimeKey(spawn);

    if (spawn.gatherOnce || spawn.respawnDays <= 0)
    {
        m_depletedForageSpawnIds.insert(key);
        return;
    }

    m_forageRespawnAvailableDay[key] = currentForageDaySerial() + std::max(1, spawn.respawnDays);
}

bool Campaign::addForageToInventory(const ForageSpeciesDef* species, const ForageArchetypeDef& archetype, int count)
{
    count = std::clamp(count, 1, kForageStackMax);

    // IMPORTANT: even an unidentified plant/mushroom keeps its hidden species id in the item id.
    // The UI may call it "neznámá bylina", but eating it still uses the real species effects.
    const bool hasSpecies = species != nullptr;
    const bool knownSpecies = hasSpecies && isForageSpeciesKnown(species->id);
    const std::string logicalId = hasSpecies ? species->id : archetype.id;
    const std::string itemId = std::string("forage_") + logicalId;

    const std::string unknownName = archetype.displayPartial.empty()
        ? (archetype.displayUnknown.empty() ? archetype.id : archetype.displayUnknown)
        : archetype.displayPartial;
    const std::string displayName =
        knownSpecies && !species->trueName.empty()
        ? species->trueName
        : unknownName;

    auto applyForageDefData = [&](ItemDef& target)
    {
        target.id = itemId;
        target.name = displayName.empty() ? logicalId : displayName;
        target.description = hasSpecies
            ? (knownSpecies
                ? "Sebraná přírodnina. Vlastnosti určuje znalost hráče a pozdější zpracování."
                : "Neurčený vzorek z přírody. Skutečný druh je skrytý, ale účinky při pozření zůstávají reálné.")
            : "Neurčený vzorek z přírody.";
        target.flavorText = "Nasbíráno v krajině kolem Blatců.";

        std::string invSpriteId;
        if (hasSpecies)
        {
            if (!species->inventorySprite.empty())
                invSpriteId = species->inventorySprite;
            else if (!species->detailSprite.empty())
                invSpriteId = species->detailSprite;
            else if (!species->herbariumSprite.empty())
                invSpriteId = species->herbariumSprite;
        }

        if (invSpriteId.empty() && !archetype.detailPlaceholderSprite.empty())
            invSpriteId = archetype.detailPlaceholderSprite;
        if (invSpriteId.empty() && !archetype.genericMapSprite.empty())
            invSpriteId = archetype.genericMapSprite;

        // forage:<id> means the inventory slot renders an atlas sub-rect from assets/Foraging.
        // Normal items keep the existing assets/data/items/<spriteId>.png path.
        target.spriteId = invSpriteId.empty() ? "item_default" : ("forage:" + invSpriteId);

        if (knownSpecies && species->edibility == "edible")
            target.category = ItemCategory::Food;
        else if (knownSpecies && species->medicinalValue != "none")
            target.category = ItemCategory::Medicine;
        else
            target.category = ItemCategory::Material;

        const float inheritedWeight = archetype.weight > 0.0f ? archetype.weight : 0.03f;
        const float inheritedVolume = archetype.volume > 0.0f ? archetype.volume : 0.05f;

        target.weight = hasSpecies && species->weight > 0.0f ? species->weight : inheritedWeight;
        target.volume = hasSpecies && species->volume > 0.0f ? species->volume : inheritedVolume;
        target.stackable = true;
        target.maxStack = std::clamp(hasSpecies ? species->maxStack : archetype.maxStack, 1, kForageStackMax);
        target.quickUsable = true;
    };

    ItemDef def;
    auto itExisting = m_itemDefs.find(itemId);
    if (itExisting != m_itemDefs.end())
    {
        def = itExisting->second;
        // When the player learns a species later, refresh item display/category for future UI draws.
        // The hidden item id stays the same, so existing stacks do not break.
        applyForageDefData(def);
        m_itemDefs[itemId] = def;
    }
    else
    {
        applyForageDefData(def);
        m_itemDefs[itemId] = def;
    }

    // Default target for gathered nature items is the backpack. If it is full,
    // fall back to the general inventory helper so the player does not lose the item.
    bool added = false;
    if (m_player.inventory.backpack.canAccept(def, count, m_itemDefs))
        added = m_player.inventory.backpack.addItem(def, count, m_itemDefs);

    if (!added)
        added = m_player.inventory.addItem(def, count, m_itemDefs);

    if (added)
    {
        m_player.stats.carryWeight = m_player.inventory.computeTotalWeight(m_itemDefs);
        m_player.stats.carryVolume = m_player.inventory.computeTotalVolume(m_itemDefs);
        return true;
    }

    consoleLog("Nemáš místo v batohu pro: " + def.name);
    return false;
}

const ForageSpawnDef* Campaign::findNearbyForageSpawn(int radiusPx) const
{
    const ForageSpawnDef* best = nullptr;
    float bestDistSq = (float)(radiusPx * radiusPx);

    for (const auto& spawn : m_forageSystem.spawns())
    {
        if (!isForageSpawnAvailable(spawn))
            continue;

        const std::string spriteId = resolveForageDisplaySpriteId(spawn);
        auto itSprite = m_forageSprites.find(spriteId);

        float sx = spawn.tileX * (float)m_tileSize + m_tileSize * 0.5f;
        float sy = spawn.tileY * (float)m_tileSize + m_tileSize * 0.5f;

        // If we know the actual rendered sprite rectangle, use its visual center for
        // interaction. This keeps the prompt aligned with scaled sprites, not with
        // a hardcoded tile center.
        if (itSprite != m_forageSprites.end())
        {
            const auto& sp = itSprite->second;
            const float scale = resolveForageMapScale(spawn);
            const float pivotWorldX = spawn.tileX * (float)m_tileSize + m_tileSize * 0.5f;
            const float pivotWorldY = spawn.tileY * (float)m_tileSize + m_tileSize;

            const float left = pivotWorldX - sp.pivotX * scale;
            const float top = pivotWorldY - sp.pivotY * scale;
            const float w = sp.src.w * scale;
            const float h = sp.src.h * scale;

            sx = left + w * 0.5f;
            sy = top + h * 0.5f;
        }

        const float dx = sx - m_player.x;
        const float dy = sy - m_player.y;
        const float d2 = dx * dx + dy * dy;

        if (d2 <= bestDistSq)
        {
            bestDistSq = d2;
            best = &spawn;
        }
    }

    return best;
}

void Campaign::renderForageSpawns(const TerrainRenderer::View& view)
{
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    const ForageSpawnDef* nearby = findNearbyForageSpawn(42);

    for (const auto& spawn : m_forageSystem.spawns())
    {
        if (!isForageSpawnAvailable(spawn))
            continue;

        const std::string spriteId = resolveForageDisplaySpriteId(spawn);
        auto itSprite = m_forageSprites.find(spriteId);
        const bool selected = (&spawn == nearby);

        if (itSprite == m_forageSprites.end())
        {
            SDL_Rect tileRect{ spawn.tileX * m_tileSize - view.camX, spawn.tileY * m_tileSize - view.camY, m_tileSize, m_tileSize };
            SDL_SetRenderDrawColor(m_renderer, 40, 180, 80, selected ? 170 : 110);
            SDL_RenderFillRect(m_renderer, &tileRect);
            SDL_SetRenderDrawColor(m_renderer, 120, 255, 150, 230);
            SDL_RenderDrawRect(m_renderer, &tileRect);
            continue;
        }

        const auto& sp = itSprite->second;
        SDL_Texture* tex = textureForForageSprite(sp);
        if (!tex)
            continue;

        const float scale = resolveForageMapScale(spawn);
        const int pivotWorldX = spawn.tileX * m_tileSize + m_tileSize / 2;
        const int pivotWorldY = spawn.tileY * m_tileSize + m_tileSize;

        SDL_Rect dst{};
        dst.w = std::max(1, (int)std::lround(sp.src.w * scale));
        dst.h = std::max(1, (int)std::lround(sp.src.h * scale));
        dst.x = (int)std::lround(pivotWorldX - sp.pivotX * scale) - view.camX;
        dst.y = (int)std::lround(pivotWorldY - sp.pivotY * scale) - view.camY;

        SDL_RenderCopy(m_renderer, tex, &sp.src, &dst);

        if (selected)
        {
            SDL_SetRenderDrawColor(m_renderer, 120, 255, 150, 210);
            SDL_RenderDrawRect(m_renderer, &dst);
        }
    }
}

void Campaign::renderForagePrompt()
{
    if (m_forageWindowOpen || m_npcDialogOpen || m_nearNpcIndex >= 0)
        return;

    const ForageSpawnDef* spawn = findNearbyForageSpawn(42);
    if (!spawn)
        return;

    const ForageArchetypeDef* archetype = m_forageDb.findArchetype(spawn->archetypeId);
    const ForageSpeciesDef* species = resolveForageSpeciesForSpawn(*spawn);
    const bool known = species && isForageSpeciesKnown(species->id);

    std::string label;
    if (known)
        label = species->trueName.empty() ? species->id : species->trueName;
    else
        label = archetype && !archetype->displayUnknown.empty() ? archetype->displayUnknown : std::string("přírodnina");

    int screenW = 0, screenH = 0;
    SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const std::string text = known ? ("E - sebrat: " + label) : ("E - prozkoumat: " + label);
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const ImVec2 pos((screenW - textSize.x) * 0.5f, screenH - 96.0f);
    const ImVec2 pad(14.0f, 8.0f);

    dl->AddRectFilled(ImVec2(pos.x - pad.x, pos.y - pad.y), ImVec2(pos.x + textSize.x + pad.x, pos.y + textSize.y + pad.y), IM_COL32(8, 12, 8, 190), 8.0f);
    dl->AddRect(ImVec2(pos.x - pad.x, pos.y - pad.y), ImVec2(pos.x + textSize.x + pad.x, pos.y + textSize.y + pad.y), IM_COL32(120, 230, 140, 220), 8.0f);
    dl->AddText(pos, IM_COL32(210, 255, 210, 255), text.c_str());
}

void Campaign::renderSharedTimeOverlay()
{
    if (!m_renderer)
        return;

    int screenW = 0;
    int screenH = 0;
    SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);
    if (screenW <= 0 || screenH <= 0)
        return;

    const float darkness = computeSkyDarkness();
    if (darkness <= 0.01f)
        return;

    Uint8 r = 5;
    Uint8 g = 8;
    Uint8 b = 18;
    float alphaScale = 145.0f;

    const DayPhase phase = getDynamicDayPhase();
    if (phase == DayPhase::Dawn || phase == DayPhase::Evening)
    {
        r = 52;
        g = 31;
        b = 20;
        alphaScale = 92.0f;
    }

    const Uint8 alpha = static_cast<Uint8>(
        std::clamp(static_cast<int>(darkness * alphaScale), 0, 185));

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, r, g, b, alpha);
    SDL_Rect full{ 0, 0, screenW, screenH };
    SDL_RenderFillRect(m_renderer, &full);
}

void Campaign::renderSharedHudOverlay()
{
    renderSharedTimeOverlay();

    if (m_showDebugHud)
        renderDebugHud();
    if (m_questJournalOpen)
        renderQuestJournal();
    renderPlayerOverviewUI();
    renderCraftsUI();
    renderHud();
    renderConsole();
    renderInventoryUI();
    renderQuickAccessBar();
    renderNailTrainingUi();
}

void Campaign::render()
{
    int screenW = 0, screenH = 0;
    SDL_GetRendererOutputSize(m_renderer, &screenW, &screenH);

    if (m_renderMode == GameRenderMode::Cinematic25D)
    {
        m_cinematicScene.render();
        renderSequenceOverlay();
        if (m_sequencePreviewMode)
            return;
        renderConsole();
        updateInspectCursor();
        return;
    }

    ensureCameraOnPlayer(screenW, screenH);
    if (m_sequenceCameraOverride)
    {
        const int mapPixelW = m_map.width() * m_tileSize;
        const int mapPixelH = m_map.height() * m_tileSize;
        m_camX = (int)std::lround(m_sequenceCameraPos.x - screenW * 0.5f);
        m_camY = (int)std::lround(m_sequenceCameraPos.y - screenH * 0.5f);
        m_camX = std::clamp(m_camX, 0, std::max(0, mapPixelW - screenW));
        m_camY = std::clamp(m_camY, 0, std::max(0, mapPixelH - screenH));
    }
    if (m_sequenceCameraShakeRemaining > 0.0f && m_sequenceCameraShakeIntensity > 0.0f)
    {
        const float phase = (float)SDL_GetTicks() * 0.05f;
        m_camX += (int)std::lround(std::sin(phase) * m_sequenceCameraShakeIntensity * 6.0f);
        m_camY += (int)std::lround(std::cos(phase * 1.37f) * m_sequenceCameraShakeIntensity * 4.0f);
    }

    TerrainRenderer::View view;
    view.camX = m_camX;
    view.camY = m_camY;
    view.tileSize = m_tileSize;
    view.screenW = screenW;
    view.screenH = screenH;

    m_terrainRenderer.renderTerrain(m_renderer, m_tileset, m_map, view);

    renderForageSpawns(view);

    const SDL_Rect playerWorld = m_player.worldAABB();
    std::vector<FadeOverlay> fadeOverlays;

    {
        ImDrawList* fg = ImGui::GetForegroundDrawList();

        SDL_Rect screenWorldRect{
            view.camX,
            view.camY,
            view.screenW,
            view.screenH
        };

        auto intersectsScreen = [&](const SDL_Rect& r) -> bool
        {
            return (r.x < screenWorldRect.x + screenWorldRect.w) &&
                   (r.x + r.w > screenWorldRect.x) &&
                   (r.y < screenWorldRect.y + screenWorldRect.h) &&
                   (r.y + r.h > screenWorldRect.y);
        };

        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

        for (int mapY = 0; mapY < m_map.height(); ++mapY)
        {
            for (int mapX = 0; mapX < m_map.width(); ++mapX)
            {
                const auto* def = m_map.getObjDefAt(m_objCatalog, mapX, mapY);
                if (!def)
                    continue;
                if (def->HasTag(("spawn"))|| def->id == "spawn_player")
                    continue;

                int wx = 0, wy = 0;
                m_map.getObjPivotWorld(mapX, mapY, wx, wy);

                const float objScale = m_map.getObjScale(mapX, mapY) * def->scale;
                SDL_Rect objWorldRect = GetObjectWorldRectScaled(*def, wx, wy, objScale);

                if (!intersectsScreen(objWorldRect))
                    continue;

                const int sx = wx - view.camX;
                const int sy = wy - view.camY;

                if (!def->has_sprite)
                {
                    SDL_Rect tileRect{
                        mapX * m_tileSize - view.camX,
                        mapY * m_tileSize - view.camY,
                        m_tileSize,
                        m_tileSize
                    };

                    SDL_SetRenderDrawColor(m_renderer, 0, 180, 0, 80);
                    SDL_RenderFillRect(m_renderer, &tileRect);

                    SDL_SetRenderDrawColor(m_renderer, 0, 255, 0, 255);
                    SDL_RenderDrawRect(m_renderer, &tileRect);

                    fg->AddText(
                        ImVec2((float)tileRect.x + 3.0f, (float)tileRect.y + 3.0f),
                        IM_COL32(0, 255, 0, 255),
                        def->name.c_str()
                    );
                    continue;
                }

                SDL_Texture* tex = textureForObject(*def);
                if (tex)
                {
                    // 1) cel� objekt norm�ln?
                    gameobj::RenderObjectAtPivot(m_renderer, tex, *def, sx, sy, objScale);

                    // 2) aktivn� fade z�ny si ulo?�me pro overlay nad hr�?em
                    std::vector<SDL_Rect> fadeRects = gameobj::GetWorldFadeRects(*def, wx, wy, objScale);

                    // fallback: pokud objekt nema definovane fade_rects,
                    if (fadeRects.empty() && def->has_sprite)
                    {
                        SDL_Rect full = GetObjectWorldRectScaled(*def, wx, wy, objScale);

                        // 1) Zkusime fallback podle horni hrany collideru
                        const auto colliderRects = gameobj::GetWorldColliderRects(*def, wx, wy, objScale);

                        if (!colliderRects.empty())
                        {
                            int colliderTop = colliderRects[0].y;

                            for (const SDL_Rect& r : colliderRects)
                                colliderTop = std::min(colliderTop, r.y);

                            // Fade oblast = cast objektu NAD horn� hranou collideru
                            SDL_Rect autoFade{};
                            autoFade.x = full.x;
                            autoFade.y = full.y;
                            autoFade.w = full.w;
                            autoFade.h = std::max(0, colliderTop - full.y);

                            if (autoFade.w > 0 && autoFade.h > 0)
                                fadeRects.push_back(autoFade);
                        }
                        else
                        {
                            // 2) Kdyz neni collider, nech fallback na horni cast sprite
                            SDL_Rect autoFade{};
                            autoFade.x = full.x;
                            autoFade.y = full.y;
                            autoFade.w = full.w;
                            autoFade.h = (int)std::lround(full.h * 0.55f);

                            if (autoFade.w > 0 && autoFade.h > 0)
                                fadeRects.push_back(autoFade);
                        }
                    }

                    for (const SDL_Rect& fr : fadeRects)
                    {
                        if (AABB_Intersect(playerWorld, fr))
                        {
                            FadeOverlay ov;
                            ov.tex = tex;
                            ov.def = def;
                            ov.wx = wx;
                            ov.wy = wy;
                            ov.fadeRectWorld = fr;
                            ov.alpha = 140;
                            ov.scale = objScale;
                            fadeOverlays.push_back(ov);
                        }
                    }
                }

                if (m_drawObjColliders)
                {
                    const auto rects = gameobj::GetWorldColliderRects(*def, wx, wy, objScale);
                    SDL_SetRenderDrawColor(m_renderer, 0, 255, 0, 255);

                    for (SDL_Rect r : rects)
                    {
                        r.x -= view.camX;
                        r.y -= view.camY;
                        SDL_RenderDrawRect(m_renderer, &r);
                    }

                    const auto walkRects = gameobj::GetWorldWalkableRects(*def, wx, wy, objScale);
                    SDL_SetRenderDrawColor(m_renderer, 0, 220, 255, 255);

                    for (SDL_Rect r : walkRects)
                    {
                        r.x -= view.camX;
                        r.y -= view.camY;
                        SDL_RenderDrawRect(m_renderer, &r);
                    }
                }
            }
        }
    }

    m_npcManager.render(m_renderer, m_camX, m_camY, m_characterManager);
    renderSequenceActors();
    m_player.render(m_renderer, m_camX, m_camY, m_characterManager);

    // overlay pr?chod? / bran nad hr�?em
    for (const auto& ov : fadeOverlays)
    {
        RenderObjectWorldSubRectAlpha(
    m_renderer,
    ov.tex,
    *ov.def,
    ov.wx,
    ov.wy,
    ov.scale,
    ov.fadeRectWorld,
    view.camX,
    view.camY,
    ov.alpha
);
    }

    if (m_nearNpcIndex >= 0 && !m_npcDialogOpen)
    {
        const auto& npcs = m_npcManager.npcs();
        const auto& npc = npcs[m_nearNpcIndex];

        const int sx = (int)std::lround(npc.x) - m_camX;
        const int sy = (int)std::lround(npc.y) - m_camY - m_tileSize;

        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 255, 230, 80, 220);

        SDL_Rect r{ sx - 5, sy - 10, 10, 10 };
        SDL_RenderFillRect(m_renderer, &r);
    }

    if (m_nearNpcIndex >= 0)
    {
        auto& npc = m_npcManager.npcs()[m_nearNpcIndex];

        const float dx = m_player.x - npc.x;

        if (fabs(dx) > 4.0f)
        {
            npc.facing = dx > 0 ? 1.0f : 3.0f;
        }
    }

    const float darkness = computeSkyDarkness();

    ensureLightMask(screenW, screenH);
    renderSkyOverlay();
    renderLightMask(screenW, screenH, darkness);
    ensureFowMask(screenW, screenH);
    renderFogOfWar(screenW, screenH);
    renderSequenceOverlay();
    if (m_sequencePreviewMode)
        return;
    if (m_showDebugHud)
        renderDebugHud();
    if (m_questJournalOpen)
        renderQuestJournal();
    renderPlayerOverviewUI();
    renderCraftsUI();
    renderHud();
    renderConsole();
    renderForagePrompt();
    renderNpcDialog();
    renderForageWindow();
    renderInventoryUI();
	renderQuickAccessBar();
    updateInspectCursor();
}
