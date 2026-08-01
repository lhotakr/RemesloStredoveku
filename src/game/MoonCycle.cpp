#include "MoonCycle.h"
#include "../JsonUtils.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

bool MoonCycle::loadFromFile(const std::string& path, std::string* outError)
{
    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(path, root, err)) {
        if (outError) *outError = err;
        return false;
    }

    m_cycleLength = root.value("cycle_length", 29.53059f);

    if (!root.contains("phases") || !root["phases"].is_array()) {
        if (outError) *outError = "MoonCycle: chybi pole 'phases'";
        return false;
    }

    m_phases.clear();

    for (const auto& jp : root["phases"])
    {
        PhaseEntry e;
        e.day = jp.value("day", 0.0f);
        e.phase = jp.value("phase", "");

        if (!e.phase.empty())
            m_phases.push_back(std::move(e));
    }

    std::sort(m_phases.begin(), m_phases.end(),
        [](const PhaseEntry& a, const PhaseEntry& b)
        {
            return a.day < b.day;
        });

    return !m_phases.empty();
}

bool MoonCycle::isLeapYear(int year)
{
    return (year % 4 == 0);
}

int MoonCycle::daysInMonth(int month, int year)
{
    switch (month)
    {
    case 1: return 31;
    case 2: return isLeapYear(year) ? 29 : 28;
    case 3: return 31;
    case 4: return 30;
    case 5: return 31;
    case 6: return 30;
    case 7: return 31;
    case 8: return 31;
    case 9: return 30;
    case 10: return 31;
    case 11: return 30;
    case 12: return 31;
    default: return 30;
    }
}

int MoonCycle::daysSinceEpoch(int day, int month, int year)
{
    // jednoduchá interní epocha pro herní výpoèet
    int total = 0;
    for (int y = 1; y < year; ++y)
        total += isLeapYear(y) ? 366 : 365;

    for (int m = 1; m < month; ++m)
        total += daysInMonth(m, year);

    total += day - 1;
    return total;
}

MoonCycle::MoonInfo MoonCycle::getMoonInfo(int day, int month, int year) const
{
    MoonInfo info{};

    if (m_phases.empty() || m_cycleLength <= 0.0f) {
        info.phase = "unknown";
        info.brightness = 0.0f;
        return info;
    }

    const int totalDays = daysSinceEpoch(day, month, year);
    float cycleDay = std::fmod((float)totalDays, m_cycleLength);
    if (cycleDay < 0.0f)
        cycleDay += m_cycleLength;

    info.cycleDay = cycleDay;

    // najdi nejbližší pøedchozí fázi
    info.phase = m_phases.front().phase;
    for (const auto& p : m_phases) {
        if (cycleDay >= p.day)
            info.phase = p.phase;
        else
            break;
    }

    // jednoduchý jas dle sinusového prùbìhu:
    // nov ~ 0, úplnìk ~ 1
    const float x = cycleDay / m_cycleLength; // 0..1
    info.brightness = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * x));

    return info;
}