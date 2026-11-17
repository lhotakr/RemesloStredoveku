#pragma once

#include <string>

struct InteriorInteractionEvent
{
    std::string interiorId;
    std::string sourceId;
    std::string workshopId;
    std::string stationId;
    std::string actionId;
    std::string label;
};
