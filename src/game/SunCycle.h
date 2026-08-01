#pragma once
#include <string>
#include <vector>
#include <optional>

class SunCycle
{
public:
    struct Entry
    {
        int day = 1;
        int month = 1;
        int sunriseMinutes = 8 * 60;
        int sunsetMinutes = 16 * 60;
    };

    struct DayInfo
    {
        int sunriseMinutes = 8 * 60;
        int sunsetMinutes = 16 * 60;
        bool isDay = true;
    };

public:
    bool loadFromFile(const std::string& path, std::string* outError = nullptr);

    std::optional<Entry> findEntry(int day, int month) const;
    DayInfo getDayInfo(int day, int month, int hour, int minute) const;

    static bool parseDateDdMm(const std::string& text, int& outDay, int& outMonth);
    static bool parseTimeHhMm(const std::string& text, int& outMinutes);
    static std::string formatMinutesHhMm(int minutes);

private:
    std::vector<Entry> m_entries;
};