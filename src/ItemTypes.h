#pragma once

#include <cstdint>

enum class ItemCategory : std::uint8_t
{
    Clothing,
    BeltItem,
    Tool,
    WeaponTool,
    Food,
    Drink,
    WaterContainer,
    Container,
    Material,
    Utility,
    Medicine,
    Quest
};

enum class EquipSlot : std::uint8_t
{
    None,

    Head,
    TorsoInner,
    TorsoOuter,
    Legs,
    Feet,
    Cloak,

    Belt,
    Back,

    MainHand,
    OffHand
};

enum class BeltSlot : std::uint8_t
{
    None,
    Knife,
    Pouch,
    Utility1,
    Utility2
};

enum class ContainerType : std::uint8_t
{
    None,
    Pockets,
    Backpack,
    WorldContainer
};