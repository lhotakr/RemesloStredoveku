#include "SunCycle.h"
#include "../JsonUtils.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

bool SunCycle::parseDateDdMm(const std::string& text, int& outDay, int& outMonth)
{
    outDay = 1;
    outMonth = 1;

    int d = 0, m = 0;
    if (std::sscanf(text.c_str(), "%d.%d", &d, &m) != 2)
        return false;

    if (d < 1 || d > 31 || m < 1 || m > 12)
        return false;

    outDay = d;
    outMonth = m;
    return true;
}

bool SunCycle::parseTimeHhMm(const std::string& text, int& outMinutes)
{
    outMinutes = 0;

    int h = 0, m = 0;
    if (std::sscanf(text.c_str(), "%d:%d", &h, &m) != 2)
        return false;

    if (h < 0 || h > 23 || m < 0 || m > 59)
        return false;

    outMinutes = h * 60 + m;
    return true;
}

std::string SunCycle::formatMinutesHhMm(int minutes)
{
    if (minutes < 0) minutes = 0;
    if (minutes > 23 * 60 + 59) minutes = 23 * 60 + 59;

    const int h = minutes / 60;
    const int m = minutes % 60;

    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << h
        << ":"
        << std::setfill('0') << std::setw(2) << m;
    return ss.str();
}

bool SunCycle::loadFromFile(const std::string& path, std::string* outError)
{
    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(path, root, err)) {
        if (outError) *outError = err;
        return false;
    }

    if (!root.is_array()) {
        if (outError) *outError = "SunCycle: root musi byt pole";
        return false;
    }

    m_entries.clear();

    for (const auto& item : root)
    {
        const std::string dateText = item.value("datum", "");
        const std::string sunriseText = item.value("vychod_slunce", "");
        const std::string sunsetText = item.value("zapad_slunce", "");

        int day = 1, month = 1;
        int sunrise = 8 * 60;
        int sunset = 16 * 60;

        if (!parseDateDdMm(dateText, day, month))
            continue;

        if (!parseTimeHhMm(sunriseText, sunrise))
            continue;

        if (!parseTimeHhMm(sunsetText, sunset))
            continue;

        Entry e;
        e.day = day;
        e.month = month;
        e.sunriseMinutes = sunrise;
        e.sunsetMinutes = sunset;

        m_entries.push_back(std::move(e));
    }

    if (m_entries.empty()) {
        if (outError) *outError = "SunCycle: nenacten zadny zaznam";
        return false;
    }

    return true;
}

std::optional<SunCycle::Entry> SunCycle::findEntry(int day, int month) const
{
    for (const auto& e : m_entries) {
        if (e.day == day && e.month == month)
            return e;
    }
    return std::nullopt;
}

SunCycle::DayInfo SunCycle::getDayInfo(int day, int month, int hour, int minute) const
{
    DayInfo info{};

    const auto e = findEntry(day, month);
    if (e.has_value()) {
        info.sunriseMinutes = e->sunriseMinutes;
        info.sunsetMinutes = e->sunsetMinutes;
    }

    const int nowMinutes = hour * 60 + minute;
    info.isDay = (nowMinutes >= info.sunriseMinutes && nowMinutes < info.sunsetMinutes);

    return info;
}