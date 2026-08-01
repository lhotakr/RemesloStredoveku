#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace interior
{
    enum class HistoricalCertainty
    {
        Documented,
        Probable,
        Interpretive,
        Legendary,
        Unknown
    };

    inline std::string ToLowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    inline HistoricalCertainty ParseHistoricalCertainty(const std::string& value)
    {
        const std::string lower = ToLowerAscii(value);
        if (lower == "documented")  return HistoricalCertainty::Documented;
        if (lower == "probable")    return HistoricalCertainty::Probable;
        if (lower == "interpretive")return HistoricalCertainty::Interpretive;
        if (lower == "legendary")   return HistoricalCertainty::Legendary;
        return HistoricalCertainty::Unknown;
    }

    inline const char* HistoricalCertaintyToString(HistoricalCertainty value)
    {
        switch (value)
        {
        case HistoricalCertainty::Documented:   return "documented";
        case HistoricalCertainty::Probable:     return "probable";
        case HistoricalCertainty::Interpretive: return "interpretive";
        case HistoricalCertainty::Legendary:    return "legendary";
        default:                                 return "unknown";
        }
    }

    struct HistoricalMetadata
    {
        HistoricalCertainty certainty = HistoricalCertainty::Unknown;
        std::string phase;
        int activeFrom = 0;
        int activeTo = 0;
        std::vector<std::string> sources;
        std::string note;
    };
}
