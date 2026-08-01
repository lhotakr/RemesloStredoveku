#pragma once
#include <string>
#include <vector>
#include <optional>

class MoonCycle
{
public:
    struct PhaseEntry
    {
        float day = 0.0f;
        std::string phase;
    };

    struct MoonInfo
    {
        float cycleDay = 0.0f;      // 0 .. cycle_length
        std::string phase;
        float brightness = 0.0f;    // 0 .. 1
    };

public:
    bool loadFromFile(const std::string& path, std::string* outError = nullptr);

    MoonInfo getMoonInfo(int day, int month, int year) const;

private:
    static bool isLeapYear(int year);
    static int daysInMonth(int month, int year);
    static int daysSinceEpoch(int day, int month, int year);

private:
    float m_cycleLength = 29.53059f;
    std::vector<PhaseEntry> m_phases;
};