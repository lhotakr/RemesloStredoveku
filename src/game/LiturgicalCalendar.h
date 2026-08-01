#pragma once
#include <string>
#include <vector>

class LiturgicalCalendar
{
public:
    struct Entry
    {
        int day = 1;
        int month = 1;

		std::string id;         // napø. "sv_jiri"
        std::string title;      // "svatý Jiøí"
        std::string onForm;     // "svatého Jiøí"
        std::string afterForm;  // "svatém Jiøí"

        bool movable = false;   // true = poèítá se od Velikonoc
        int offsetFromEaster = 0; // napø. 0=Boží hod velikonoèní, -2=Velký pátek, +39=Nanebevstoupení

		std::string importance; // "major", "minor", "commemoration" - pro pøípadné filtrování a/nebo odlišné zobrazení v UI
		std::vector<std::string> tags; // napø. "spring", "harvest", "marian", "saint", "royal", ... - pro pøípadné filtrování a/nebo odlišné zobrazení v UI
    };

    enum class Style
    {
        Documentary,
        Spoken,
        Latin
    };

public:
    bool loadFromFile(const std::string& path, std::string* outError = nullptr);

    std::vector<Entry> entriesForDate(int day, int month, int year) const;
    std::string primaryTitle(int day, int month, int year) const;

    std::string formatMedievalDate(
        int day,
        int month,
        int year,
        const std::string& weekDayCz,
        Style style = Style::Documentary) const;

    std::vector<std::string> tagsForDate(int day, int month, int year) const;
    std::string primaryId(int day, int month, int year) const;
    std::string primaryImportance(int day, int month, int year) const;
    bool hasTag(int day, int month, int year, const std::string& tag) const;

private:
    struct CivilDate
    {
        int day = 1;
        int month = 1;
        int year = 1400;
    };

private:
    static bool isLeapYear(int year);
    static int daysInMonth(int month, int year);
    static int dayOfYear(int day, int month, int year);
    static CivilDate civilFromDayOfYear(int doy, int year);

    static std::string romanYear(int year);
    static std::string weekdayWithPrepositionCz(const std::string& weekDayCz);
    static std::string spokenOrdinalDaysCz(int n);

    static CivilDate julianEaster(int year);

    std::vector<Entry> resolvedEntriesForYear(int year) const;
    const Entry* exactEntryForDate(int day, int month, int year, std::vector<Entry>& resolved) const;
    const Entry* nearestPreviousEntry(int day, int month, int year, std::vector<Entry>& resolved) const;
    int daysSincePreviousFeast(int day, int month, int year, std::vector<Entry>& resolved) const;

    
    

private:
    std::vector<Entry> m_entries;
};