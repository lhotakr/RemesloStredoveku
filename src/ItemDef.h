#pragma once

#include "ItemTypes.h"
#include <string>

struct ItemDef
{
    std::string id;
    std::string name;
	bool lockedInInventory = false; // pokud true, item nelze z inventáøe vyhodit (dropnout) ani dát do kontejneru, ale mùže být pøesouván mezi sloty a vybaven

    // texty do UI
    std::string description;
    std::string flavorText;

    // id sprite/ikony, zatim muze byt "item_default"
    std::string spriteId = "item_default";
    std::string audioNoteSfx;

    ItemCategory category = ItemCategory::Utility;

    // fyzicke vlastnosti
    float weight = 0.0f;
    float volume = 0.0f;

    // stackovani
    bool stackable = false;
    int maxStack = 1;

    // equip
    bool equippable = false;
    EquipSlot equipSlot = EquipSlot::None;

    // opasek
    bool beltCompatible = false;
    BeltSlot preferredBeltSlot = BeltSlot::None;

    // container item
    bool isContainer = false;
    float containerWeightCapacity = 0.0f;
    float containerVolumeCapacity = 0.0f;

    // gameplay staty
    float warmth = 0.0f;
    float rainProtection = 0.0f;
    float outfitAuthenticity = 0.0f;
    float socialValue = 0.0f;

    // utility
    float waterCapacity = 0.0f;
    float toolPower = 0.0f;

    bool quickUsable = false;
};

