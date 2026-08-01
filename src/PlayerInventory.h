#pragma once

#include "ItemDef.h"
#include "ItemTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>


struct ItemStack
{
    std::string itemId;
    int count = 0;

    float durability = 100.0f;
    float wetness = 0.0f;

    bool empty() const
    {
        return itemId.empty() || count <= 0;
    }

    void clear()
    {
        itemId.clear();
        count = 0;
        durability = 100.0f;
        wetness = 0.0f;
    }
};

struct ContainerInventory
{
    ContainerType type = ContainerType::None;
    std::vector<ItemStack> items;

    float maxWeight = 0.0f;
    float maxVolume = 0.0f;

    float computeCurrentWeight(const std::unordered_map<std::string, ItemDef>& defs) const
    {
        float total = 0.0f;
        for (const auto& stack : items)
        {
            auto it = defs.find(stack.itemId);
            if (it == defs.end())
                continue;

            total += it->second.weight * static_cast<float>(stack.count);
        }
        return total;
    }

    float computeCurrentVolume(const std::unordered_map<std::string, ItemDef>& defs) const
    {
        float total = 0.0f;
        for (const auto& stack : items)
        {
            auto it = defs.find(stack.itemId);
            if (it == defs.end())
                continue;

            total += it->second.volume * static_cast<float>(stack.count);
        }
        return total;
    }

    bool canAccept(const ItemDef& def, int count,
        const std::unordered_map<std::string, ItemDef>& defs) const
    {
        const float newWeight = computeCurrentWeight(defs) + def.weight * static_cast<float>(count);
        const float newVolume = computeCurrentVolume(defs) + def.volume * static_cast<float>(count);

        return newWeight <= maxWeight && newVolume <= maxVolume;
    }

    bool addItem(const ItemDef& def, int count,
        const std::unordered_map<std::string, ItemDef>& defs)
    {
        if (count <= 0)
            return false;

        if (!canAccept(def, count, defs))
            return false;

        if (def.stackable)
        {
            for (auto& stack : items)
            {
                if (stack.itemId == def.id && stack.count < def.maxStack)
                {
                    const int freeSpace = def.maxStack - stack.count;
                    const int add = std::min(freeSpace, count);
                    stack.count += add;
                    count -= add;

                    if (count <= 0)
                        return true;
                }
            }
        }

        while (count > 0)
        {
            ItemStack s;
            s.itemId = def.id;

            if (def.stackable)
            {
                s.count = std::min(count, def.maxStack);
                count -= s.count;
            }
            else
            {
                s.count = 1;
                count -= 1;
            }

            items.push_back(s);
        }

        return true;
    }

    void compact()
    {
        items.erase(
            std::remove_if(items.begin(), items.end(),
                [](const ItemStack& s)
                {
                    return s.empty();
                }),
            items.end());
    }
};

struct EquippedItems
{
    ItemStack head;
    ItemStack torsoInner;
    ItemStack torsoOuter;
    ItemStack legs;
    ItemStack feet;
    ItemStack cloak;

    ItemStack belt;
    ItemStack back;

    ItemStack mainHand;
    ItemStack offHand;
};

struct BeltLoadout
{
    ItemStack knife;
    ItemStack pouch;
    ItemStack utility1;
    ItemStack utility2;
};

struct PlayerInventory
{
    EquippedItems equipped;
    BeltLoadout beltSlots;

    ContainerInventory pockets;
    ContainerInventory backpack;

    void initDefaults()
    {
        pockets.type = ContainerType::Pockets;
        pockets.maxWeight = 1.5f;
        pockets.maxVolume = 1.5f;

        backpack.type = ContainerType::Backpack;
        backpack.maxWeight = 0.0f;
        backpack.maxVolume = 0.0f;
    }

    float computeEquippedWeight(const std::unordered_map<std::string, ItemDef>& defs) const
    {
        float total = 0.0f;

        auto addStack = [&](const ItemStack& s)
            {
                if (s.empty())
                    return;

                auto it = defs.find(s.itemId);
                if (it == defs.end())
                    return;

                total += it->second.weight * static_cast<float>(s.count);
            };

        addStack(equipped.head);
        addStack(equipped.torsoInner);
        addStack(equipped.torsoOuter);
        addStack(equipped.legs);
        addStack(equipped.feet);
        addStack(equipped.cloak);
        addStack(equipped.belt);
        addStack(equipped.back);
        addStack(equipped.mainHand);
        addStack(equipped.offHand);

        addStack(beltSlots.knife);
        addStack(beltSlots.pouch);
        addStack(beltSlots.utility1);
        addStack(beltSlots.utility2);

        return total;
    }

    float computeEquippedVolume(const std::unordered_map<std::string, ItemDef>& defs) const
    {
        float total = 0.0f;

        auto addStack = [&](const ItemStack& s)
            {
                if (s.empty())
                    return;

                auto it = defs.find(s.itemId);
                if (it == defs.end())
                    return;

                total += it->second.volume * static_cast<float>(s.count);
            };

        addStack(equipped.head);
        addStack(equipped.torsoInner);
        addStack(equipped.torsoOuter);
        addStack(equipped.legs);
        addStack(equipped.feet);
        addStack(equipped.cloak);
        addStack(equipped.belt);
        addStack(equipped.back);
        addStack(equipped.mainHand);
        addStack(equipped.offHand);

        addStack(beltSlots.knife);
        addStack(beltSlots.pouch);
        addStack(beltSlots.utility1);
        addStack(beltSlots.utility2);

        return total;
    }

    float computeTotalWeight(const std::unordered_map<std::string, ItemDef>& defs) const
    {
        return computeEquippedWeight(defs)
            + pockets.computeCurrentWeight(defs)
            + backpack.computeCurrentWeight(defs);
    }

    float computeTotalVolume(const std::unordered_map<std::string, ItemDef>& defs) const
    {
        return computeEquippedVolume(defs)
            + pockets.computeCurrentVolume(defs)
            + backpack.computeCurrentVolume(defs);
    }

    float computeTotalWarmth(const std::unordered_map<std::string, ItemDef>& defs) const
    {
        float total = 0.0f;

        auto addWarmth = [&](const ItemStack& s)
            {
                if (s.empty())
                    return;

                auto it = defs.find(s.itemId);
                if (it == defs.end())
                    return;

                total += it->second.warmth;
            };

        addWarmth(equipped.head);
        addWarmth(equipped.torsoInner);
        addWarmth(equipped.torsoOuter);
        addWarmth(equipped.legs);
        addWarmth(equipped.feet);
        addWarmth(equipped.cloak);

        return total;
    }

    float computeOutfitAuthenticity(const std::unordered_map<std::string, ItemDef>& defs) const
    {
        float total = 0.0f;

        auto addValue = [&](const ItemStack& s)
            {
                if (s.empty())
                    return;

                auto it = defs.find(s.itemId);
                if (it == defs.end())
                    return;

                total += it->second.outfitAuthenticity;
            };

        addValue(equipped.head);
        addValue(equipped.torsoInner);
        addValue(equipped.torsoOuter);
        addValue(equipped.legs);
        addValue(equipped.feet);
        addValue(equipped.cloak);
        addValue(equipped.belt);

        return total;
    }

    void syncBackpackFromEquippedItem(const std::unordered_map<std::string, ItemDef>& defs)
    {
        if (equipped.back.empty())
        {
            backpack.maxWeight = 0.0f;
            backpack.maxVolume = 0.0f;
            return;
        }

        auto it = defs.find(equipped.back.itemId);
        if (it == defs.end() || !it->second.isContainer)
        {
            backpack.maxWeight = 0.0f;
            backpack.maxVolume = 0.0f;
            return;
        }

        backpack.maxWeight = it->second.containerWeightCapacity;
        backpack.maxVolume = it->second.containerVolumeCapacity;
    }

    bool addItem(const ItemDef& def, int count,
        const std::unordered_map<std::string, ItemDef>& defs)
    {
        if (def.category == ItemCategory::Container ||
            def.equippable ||
            def.beltCompatible)
        {
            // pro V1 ukládej bìžné vìci do batohu, když je dostupný,
            // jinak do kapes
            if (backpack.maxWeight > 0.0f || backpack.maxVolume > 0.0f)
            {
                if (backpack.addItem(def, count, defs))
                    return true;
            }

            return pockets.addItem(def, count, defs);
        }

        if (backpack.maxWeight > 0.0f || backpack.maxVolume > 0.0f)
        {
            if (backpack.addItem(def, count, defs))
                return true;
        }

        return pockets.addItem(def, count, defs);
    }

    bool equipItem(
        const ItemDef& def,
        ItemStack& sourceStack,
        ItemStack& targetSlot,
        const std::unordered_map<std::string, ItemDef>& defs)
    {
        if (sourceStack.empty())
            return false;

        if (!def.equippable)
            return false;

        // swap pokud slot obsazený
        if (!targetSlot.empty())
        {
            std::swap(targetSlot, sourceStack);
        }
        else
        {
            targetSlot = sourceStack;
            sourceStack.clear();
        }

        syncBackpackFromEquippedItem(defs);

        return true;
    }

    bool autoEquip(
        const ItemDef& def,
        ItemStack& stack,
        const std::unordered_map<std::string, ItemDef>& defs)
    {
        switch (def.equipSlot)
        {
        case EquipSlot::Head:
            return equipItem(def, stack, equipped.head, defs);

        case EquipSlot::TorsoInner:
            return equipItem(def, stack, equipped.torsoInner, defs);

        case EquipSlot::TorsoOuter:
            return equipItem(def, stack, equipped.torsoOuter, defs);

        case EquipSlot::Legs:
            return equipItem(def, stack, equipped.legs, defs);

        case EquipSlot::Feet:
            return equipItem(def, stack, equipped.feet, defs);

        case EquipSlot::Cloak:
            return equipItem(def, stack, equipped.cloak, defs);

        case EquipSlot::Belt:
            return equipItem(def, stack, equipped.belt, defs);

        case EquipSlot::Back:
            return equipItem(def, stack, equipped.back, defs);

        case EquipSlot::MainHand:
            return equipItem(def, stack, equipped.mainHand, defs);

        case EquipSlot::OffHand:
            return equipItem(def, stack, equipped.offHand, defs);

        default:
            return false;
        }
    }
};