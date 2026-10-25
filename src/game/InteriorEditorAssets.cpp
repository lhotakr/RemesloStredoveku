#include "BuildInteriorEngine.h"
#include "InventorySpriteCatalog.h"
#include "PathUtils.h"
#include "Utf8.h"
#include "terrain/ObjectCatalog.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace
{
std::string lower(std::string text)
{
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool sameRect(SDL_Rect a, SDL_Rect b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
}

void BuildInteriorEngine::refreshEditorTextureAssets()
{
    m_editorTextureAssets.clear();
    const auto root = pathutils::ProjectRoot();
    const auto objects = root / "assets/Objects";
    std::vector<fs::path> catalogs, images;
    std::error_code error;
    for (fs::recursive_directory_iterator it(root / "assets", fs::directory_options::skip_permission_denied, error), end;
         it != end; it.increment(error))
    {
        if (error) { error.clear(); continue; }
        if (!it->is_regular_file(error)) continue;
        const auto path = it->path();
        const auto extension = lower(path.extension().string());
        if (extension == ".json" && path.lexically_relative(objects).generic_string().rfind("..", 0) != 0)
            catalogs.push_back(path);
        else if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                 extension == ".webp" || extension == ".bmp" || extension == ".tga")
            images.push_back(path);
    }
    std::sort(catalogs.begin(), catalogs.end());
    std::sort(images.begin(), images.end());
    std::unordered_set<std::string> atlases;
    for (const auto& file : catalogs)
    {
        gameobj::ObjectCatalog catalog;
        if (!catalog.LoadFromFile(file.string())) continue;
        for (const auto& object : catalog.Objects())
        {
            if (!object.enabled || !object.has_sprite || object.image.empty() || object.src.w <= 0 || object.src.h <= 0)
                continue;
            const auto image = (objects / object.image).lexically_normal();
            if (!fs::is_regular_file(image, error)) continue;
            EditorTextureAsset asset;
            asset.name = object.name.empty() ? object.id : object.name;
            asset.path = image.lexically_relative(root).generic_string();
            asset.objectId = object.id;
            asset.sourceRect = {object.src.x, object.src.y, object.src.w, object.src.h};
            asset.search = lower(asset.name + " " + asset.path + " " + object.id);
            atlases.insert(asset.path);
            m_editorTextureAssets.push_back(std::move(asset));
        }
    }
    for (const auto& item : inventorysprites::Load(root))
    {
        EditorTextureAsset asset;
        asset.name = item.name;
        asset.path = item.path;
        asset.objectId = item.id;
        asset.inventory = true;
        asset.search = lower(item.name + " " + item.id + " " + item.spriteId + " " + item.path);
        m_editorTextureAssets.push_back(std::move(asset));
    }
    for (const auto& image : images)
    {
        // Animated textures expose their base image; numbered frames are loaded
        // by the existing animation controls after the object is selected.
        if (image.stem().string().find("_anim_") != std::string::npos) continue;
        EditorTextureAsset asset;
        asset.path = image.lexically_relative(root).generic_string();
        asset.name = image.stem().string();
        asset.atlas = atlases.contains(asset.path);
        asset.search = lower(asset.name + " " + asset.path);
        m_editorTextureAssets.push_back(std::move(asset));
    }
    std::sort(m_editorTextureAssets.begin(), m_editorTextureAssets.end(), [](const auto& a, const auto& b)
    {
        if (a.name != b.name) return a.name < b.name;
        return a.path < b.path;
    });
    m_editorTextureAssetsLoaded = true;
}

BuildInteriorEngine::TextureKey BuildInteriorEngine::textureKeyForSource(const std::string& path, SDL_Rect rect, const std::string& name)
{
    const auto normalized = fs::path(path).lexically_normal().generic_string();
    for (auto& [key, texture] : m_textures)
        if (fs::path(texture.path).lexically_normal().generic_string() == normalized && sameRect(texture.sourceRect, rect))
        {
            if (!name.empty()) texture.name = name;
            return key;
        }
    // Do not replace the map's existing textures or another slice of the atlas.
    for (unsigned candidate = 256; candidate <= 65535; ++candidate)
    {
        const auto key = static_cast<TextureKey>(candidate);
        if (!m_textures.contains(key))
            return loadTextureForCell(key, normalized, rect, name) ? key : kNoTexture;
    }
    m_status = U8("Pro další texturu už není volný klíč.");
    return kNoTexture;
}

bool BuildInteriorEngine::renderEditorTexturePicker(const char* label, TextureKey& selected, bool objects)
{
    const auto current = m_textures.find(selected);
    const std::string preview = current != m_textures.end()
        ? (!current->second.name.empty() ? current->second.name : current->second.path)
        : U8("Bez textury");
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 340), ImVec2(800, 460));
    if (!ImGui::BeginCombo(label, preview.c_str())) return false;
    if (!m_editorTextureAssetsLoaded) refreshEditorTextureAssets();

    bool changed = false;
    static char filter[160]{};
    static bool project = true;
    static bool inventoryOnly = false;
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##asset_filter", U8("Hledat název, ID nebo cestu..."), filter, sizeof(filter));
    ImGui::Checkbox(U8("Celý projekt"), &project);
    ImGui::SameLine();
    if (ImGui::SmallButton(U8("Obnovit knihovnu"))) refreshEditorTextureAssets();
    if (objects) ImGui::Checkbox(U8("Jen předměty inventáře"), &inventoryOnly);
    const bool onlyItems = objects && inventoryOnly;
    const auto query = lower(filter);
    ImGui::BeginChild("TextureChoices", ImVec2(0, 270), true);
    std::vector<TextureKey> keys;
    for (const auto& [key, texture] : m_textures)
        if (objects || texture.sourceRect.w == 0) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    ImGui::TextDisabled(U8("V aktuální mapě"));
    for (const auto key : keys)
    {
        if (onlyItems) continue;
        const auto& texture = m_textures.at(key);
        const std::string row = texture.name.empty() ? texture.path : texture.name;
        if (!query.empty() && lower(row + " " + texture.path).find(query) == std::string::npos) continue;
        ImGui::PushID(key);
        if (ImGui::Selectable(row.c_str(), selected == key)) { selected = key; changed = true; }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(texture.path.c_str());
            if (texture.texture)
            {
                const float scale = std::min(160.0f / std::max(1, texture.w), 160.0f / std::max(1, texture.h));
                ImGui::Image((ImTextureID)(intptr_t)texture.texture, ImVec2(texture.w * scale, texture.h * scale));
            }
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    if (project || onlyItems)
    {
        ImGui::Separator();
        ImGui::TextDisabled(U8("Knihovna projektu"));
        for (int index = 0; index < static_cast<int>(m_editorTextureAssets.size()); ++index)
        {
            const auto& asset = m_editorTextureAssets[index];
            if (objects ? asset.atlas : (asset.sourceRect.w > 0 || asset.inventory)) continue;
            if (onlyItems && !asset.inventory) continue;
            if (!query.empty() && asset.search.find(query) == std::string::npos) continue;
            ImGui::PushID(index + 65536);
            const std::string row = (asset.inventory ? U8("Inventář / ") : "") + asset.name + "  |  " + (asset.objectId.empty() ? asset.path : asset.objectId);
            if (ImGui::Selectable(row.c_str()))
            {
                const auto key = textureKeyForSource(asset.path, asset.sourceRect, asset.name);
                if (key != kNoTexture) { selected = key; changed = true; }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(asset.path.c_str());
                if (!asset.objectId.empty()) ImGui::TextUnformatted(asset.objectId.c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (changed) { m_sceneDirty = true; ImGui::CloseCurrentPopup(); }
    ImGui::EndCombo();
    return changed;
}
