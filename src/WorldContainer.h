#pragma once

#include "PlayerInventory.h"
#include <string>

struct WorldContainer
{
    std::string id;
    std::string displayName;

    ContainerInventory inventory;

    bool isPlayerOwned = false;
    bool hidden = false;
    bool locked = false;
};