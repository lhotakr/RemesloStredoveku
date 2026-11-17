#pragma once

#include <string>
#include <vector>

namespace sequence
{
    struct ActionTypeInfo
    {
        const char* category;
        const char* type;
    };

    const std::vector<ActionTypeInfo>& AllActionTypes();
    bool IsKnownActionType(const std::string& type);
}
