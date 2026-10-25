#include "InventorySpriteCatalog.h"
#include "JsonUtils.h"

#include <algorithm>
#include <unordered_map>

namespace inventorysprites
{
std::vector<Choice> Load(const std::filesystem::path& projectRoot)
{
    namespace fs = std::filesystem;
    // The assets catalog is authoritative in Campaign; the data catalog is a fallback.
    std::unordered_map<std::string, Choice> items;
    for (const auto& file : {projectRoot / "data/items/Items.json", projectRoot / "assets/data/Items.json"})
    {
        nlohmann::json root;
        std::string error;
        if (!jsonutils::LoadJsonFileSafe(file.string(), root, error) ||
            !root.is_object() || !root.contains("items") || !root["items"].is_array()) continue;
        for (const auto& item : root["items"])
        {
            if (!item.is_object() || !item.contains("id") || !item["id"].is_string()) continue;
            Choice choice;
            choice.id = item["id"].get<std::string>();
            if (choice.id.empty()) continue;
            // A later definition replaces the earlier one even if it has no usable icon.
            items.erase(choice.id);
            choice.name = item.value("name", choice.id);
            choice.spriteId = item.value("spriteId", std::string("item_default"));
            if (choice.spriteId.empty() || choice.spriteId.starts_with("forage:")) continue;
            std::string image = choice.spriteId;
            std::replace(image.begin(), image.end(), '\\', '/');
            fs::path path;
            if (image.find('/') != std::string::npos)
            {
                path = image;
                if (!path.has_extension()) path += ".png";
            }
            else path = fs::path("assets/data/items") / (image + ".png");
            const auto absolute = path.is_absolute() ? path : projectRoot / path;
            std::error_code ec;
            if (!fs::is_regular_file(absolute, ec)) continue;
            choice.path = absolute.lexically_normal().lexically_relative(projectRoot).generic_string();
            if (choice.name.empty()) choice.name = choice.id;
            items[choice.id] = std::move(choice);
        }
    }
    std::vector<Choice> choices;
    for (auto& [id, choice] : items) choices.push_back(std::move(choice));
    std::sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    });
    return choices;
}
}
