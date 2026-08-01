#include "GameTime.h"

#include <sstream>
#include <iomanip>
#include <cstdio>

void GameTime::setStartDateTime(int day, int month, int year, int hour, int minute)
{
    m_now.day = day;
    m_now.month = month;
    m_now.year = year;
    m_now.hour = hour;
    m_now.minute = minute;
    m_gameMinutesAccumulator = 0.0f;
}

bool GameTime::isLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int GameTime::daysInMonth(int month, int year)
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

GameTime::WeekDay GameTime::currentWeekDay() const
{
    int d = m_now.day;
    int m = m_now.month;
    int y = m_now.year;

    if (m < 3) {
        m += 12;
        --y;
    }

    const int K = y % 100;
    const int J = y / 100;

    const int h =
        (d + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;

    // Zeller:
    // 0 = Saturday, 1 = Sunday, 2 = Monday, ..., 6 = Friday
    switch (h)
    {
    case 0: return WeekDay::Saturday;
    case 1: return WeekDay::Sunday;
    case 2: return WeekDay::Monday;
    case 3: return WeekDay::Tuesday;
    case 4: return WeekDay::Wednesday;
    case 5: return WeekDay::Thursday;
    case 6: return WeekDay::Friday;
    default: return WeekDay::Monday;
    }
}

int GameTime::currentWeekDayIndexMondayFirst() const
{
    switch (currentWeekDay())
    {
    case WeekDay::Monday:    return 0;
    case WeekDay::Tuesday:   return 1;
    case WeekDay::Wednesday: return 2;
    case WeekDay::Thursday:  return 3;
    case WeekDay::Friday:    return 4;
    case WeekDay::Saturday:  return 5;
    case WeekDay::Sunday:    return 6;
    }
    return 0;
}

int GameTime::currentDayPeriodIndex() const
{
    switch (currentDayPeriod())
    {
    case DayPeriod::Morning:      return 0;
    case DayPeriod::Forenoon:     return 1;
    case DayPeriod::Noon:         return 2;
    case DayPeriod::Afternoon:    return 3;
    case DayPeriod::EarlyEvening: return 4;
    case DayPeriod::Evening:      return 5;
    case DayPeriod::Night:        return 6;
    }
    return 6;
}

std::string GameTime::formatFullDateCz() const
{
    return currentWeekDayIndexMondayFirst() + " " + formatDateCz();
}

void GameTime::advanceMinutes(int minutes)
{
    m_now.minute += minutes;

    while (m_now.minute >= 60) {
        m_now.minute -= 60;
        ++m_now.hour;
    }

    while (m_now.hour >= 24) {
        m_now.hour -= 24;
        ++m_now.day;
    }

    while (m_now.day > daysInMonth(m_now.month, m_now.year)) {
        m_now.day -= daysInMonth(m_now.month, m_now.year);
        ++m_now.month;
        if (m_now.month > 12) {
            m_now.month = 1;
            ++m_now.year;
        }
    }
}

int GameTime::update(float dtSeconds)
{
    if (m_paused)
        return 0;

    m_gameMinutesAccumulator += dtSeconds * m_gameMinutesPerRealSecond;

    const int wholeMinutes = (int)m_gameMinutesAccumulator;

    if (wholeMinutes > 0) {
        advanceMinutes(wholeMinutes);
        m_gameMinutesAccumulator -= (float)wholeMinutes;
    }

    return wholeMinutes;
}

bool GameTime::setTimeFromString(const std::string& hhmm)
{
    int h = 0, m = 0;
    if (std::sscanf(hhmm.c_str(), "%d:%d", &h, &m) != 2)
        return false;

    if (h < 0 || h > 23 || m < 0 || m > 59)
        return false;

    m_now.hour = h;
    m_now.minute = m;
    return true;
}

bool GameTime::setDateFromString(const std::string& ddmmyyyy)
{
    int d = 0, mo = 0, y = 0;
    if (std::sscanf(ddmmyyyy.c_str(), "%d.%d.%d", &d, &mo, &y) != 3)
        return false;

    if (y < 1 || mo < 1 || mo > 12)
        return false;

    const int dim = daysInMonth(mo, y);
    if (d < 1 || d > dim)
        return false;

    m_now.day = d;
    m_now.month = mo;
    m_now.year = y;
    return true;
}

std::string GameTime::formatTime() const
{
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << m_now.hour
        << ":"
        << std::setfill('0') << std::setw(2) << m_now.minute;
    return ss.str();
}

std::string GameTime::formatDateCz() const
{
    std::ostringstream ss;
    ss << m_now.day << "." << m_now.month << "." << m_now.year;
    return ss.str();
}

std::string GameTime::formatDateTimeCz() const
{
    return formatDateCz() + " " + formatTime();
}

GameTime::DayPeriod GameTime::currentDayPeriod() const
{
    const int minutes = m_now.hour * 60 + m_now.minute;

    if (minutes >= 5 * 60 && minutes < 7 * 60)   return DayPeriod::Morning;
    if (minutes >= 7 * 60 && minutes < 11 * 60)  return DayPeriod::Forenoon;
    if (minutes >= 11 * 60 && minutes < 13 * 60) return DayPeriod::Noon;
    if (minutes >= 13 * 60 && minutes < 17 * 60) return DayPeriod::Afternoon;
    if (minutes >= 17 * 60 && minutes < 19 * 60) return DayPeriod::EarlyEvening;
    if (minutes >= 19 * 60 && minutes < 21 * 60) return DayPeriod::Evening;
    return DayPeriod::Night;
}

std::string GameTime::dayPeriodTextCz() const
{
    switch (currentDayPeriod())
    {
    case DayPeriod::Morning:      return "rano";
    case DayPeriod::Forenoon:     return "dopoledne";
    case DayPeriod::Noon:         return "poledne";
    case DayPeriod::Afternoon:    return "odpoledne";
    case DayPeriod::EarlyEvening: return "podvecer";
    case DayPeriod::Evening:      return "vecer";
    case DayPeriod::Night:        return "noc";
    }
    return "noc";
}