#pragma once
#include <string>

class GameTime
{
public:
    struct DateTime
    {
        int day = 29;
        int month = 3;
        int year = 1400;
        int hour = 8;
        int minute = 0;
    };

    enum class DayPeriod
    {
        Morning,      // 5:00 - 7:00
        Forenoon,     // 7:00 - 11:00
        Noon,         // 11:00 - 13:00
        Afternoon,    // 13:00 - 17:00
        EarlyEvening, // 17:00 - 19:00
        Evening,      // 19:00 - 21:00
        Night         // 21:00 - 5:00
    };

    enum class DayPhase
    {
        Dawn,       // kolem východu slunce
        Morning,    // po východu
        Forenoon,   // dopoledne
        Noon,       // 11:00–13:00 pevnì
        Afternoon,  // odpoledne
        LateDay,    // podveèer
        Evening,    // veèer po západu / kolem západu
        Night       // noc
    };

    enum class WeekDay
    {
        Monday,
        Tuesday,
        Wednesday,
        Thursday,
        Friday,
        Saturday,
        Sunday
    };

    WeekDay currentWeekDay() const;
    int currentWeekDayIndexMondayFirst() const;
	int currentDayPeriodIndex() const;
    std::string formatFullDateCz() const;

public:
    void setStartDateTime(int day, int month, int year, int hour, int minute);
    int update(float dtSeconds);

    bool setTimeFromString(const std::string& hhmm);
    bool setDateFromString(const std::string& ddmmyyyy);

    const DateTime& now() const { return m_now; }

    std::string formatTime() const;
    std::string formatDateCz() const;
    std::string formatDateTimeCz() const;
    std::string dayPeriodTextCz() const;

    DayPeriod currentDayPeriod() const;

	// pause time (e.g. when player opens inventory or console)
	
    void setPaused(bool paused) { m_paused = paused; }
	bool paused() const { return m_paused; }

private:
    static bool isLeapYear(int year);
    static int daysInMonth(int month, int year);
    void advanceMinutes(int minutes);
	bool m_paused = false;

private:
    DateTime m_now{};
    float m_gameMinutesAccumulator = 0.0f;

    // 1 real second = 24 game minutes
    // => 1 whole day (1440 min) = 60 real minutes
    float m_gameMinutesPerRealSecond = 0.4f;
};