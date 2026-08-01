#include "LiturgicalCalendar.h"
#include "../JsonUtils.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>
#include <cstdio>

using json = nlohmann::json;

bool LiturgicalCalendar::loadFromFile(const std::string& path, std::string* outError)
{
    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(path, root, err)) {
        if (outError) *outError = err;
        return false;
    }

    if (!root.contains("days") || !root["days"].is_array()) {
        if (outError) *outError = "LiturgicalCalendar: chybi pole 'days'";
        return false;
    }

    m_entries.clear();

    for (const auto& jd : root["days"])
    {
        Entry e;
		e.id = jd.value("id", "");
        e.day = jd.value("day", 1);
        e.month = jd.value("month", 1);
        e.title = jd.value("title", "");
        e.onForm = jd.value("on_form", "");
        e.afterForm = jd.value("after_form", "");
        e.movable = jd.value("movable", false);
        e.offsetFromEaster = jd.value("offset_from_easter", 0);
        e.importance = jd.value("importance", "minor");

        if (jd.contains("tags") && jd["tags"].is_array())
        {
            for (const auto& tag : jd["tags"])
            {
                if (tag.is_string())
                    e.tags.push_back(tag.get<std::string>());
            }
        }

        if (e.title.empty())
            continue;

        if (e.onForm.empty()) e.onForm = e.title;
        if (e.afterForm.empty()) e.afterForm = e.title;

        m_entries.push_back(std::move(e));
    }

    return true;
}

bool LiturgicalCalendar::isLeapYear(int year)
{
    // pro tvoji hru kolem roku 1400 je vhodné držet juliánské pravidlo
    return (year % 4 == 0);
}

int LiturgicalCalendar::daysInMonth(int month, int year)
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

int LiturgicalCalendar::dayOfYear(int day, int month, int year)
{
    int total = 0;
    for (int m = 1; m < month; ++m)
        total += daysInMonth(m, year);
    return total + day;
}

LiturgicalCalendar::CivilDate LiturgicalCalendar::civilFromDayOfYear(int doy, int year)
{
    CivilDate out;
    out.year = year;

    int month = 1;
    while (doy > daysInMonth(month, year)) {
        doy -= daysInMonth(month, year);
        ++month;
    }

    out.month = month;
    out.day = doy;
    return out;
}

std::string LiturgicalCalendar::romanYear(int year)
{
    struct RomanPart { int value; const char* numeral; };
    static const RomanPart parts[] = {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
        {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
        {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"},
        {1, "I"}
    };

    std::string out;
    int n = year;
    for (const auto& p : parts) {
        while (n >= p.value) {
            out += p.numeral;
            n -= p.value;
        }
    }
    return out;
}

std::string LiturgicalCalendar::weekdayWithPrepositionCz(const std::string& weekDayCz)
{
    if (weekDayCz == "støeda")  return "ve st\u0159edu";
    if (weekDayCz == "ètvrtek") return "ve \u010Dtvrtek";
    if (weekDayCz == "úterý")   return "v \u00FAter\u00FD";
    if (weekDayCz == "pondìlí") return "v pond\u011Bl\u00ED";
    if (weekDayCz == "pátek")   return "v p\u00E1tek";
    if (weekDayCz == "sobota")  return "v sobotu";
    if (weekDayCz == "nedìle")  return "v ned\u011Bli";
    return "v " + weekDayCz;
}

std::string LiturgicalCalendar::spokenOrdinalDaysCz(int n)
{
    switch (n)
    {
    case 1: return "den po";
    case 2: return "druh\u00E9ho dne po";
    case 3: return "t\u0159et\u00EDho dne po";
    case 4: return "\u010Dtvrt\u00E9ho dne po";
    case 5: return "p\u00E1t\u00E9ho dne po";
    case 6: return "\u0161est\u00E9ho dne po";
    case 7: return "sedm\u00E9ho dne po";
    case 8: return "osm\u00E9ho dne po";
    case 9: return "dev\u00E1t\u00E9ho dne po";
    case 10: return "des\u00E1t\u00E9ho dne po";
    case 11: return "jeden\u00E1ct\u00E9ho dne po";
    case 12: return "dvan\u00E1ct\u00E9ho dne po";
    default: return std::to_string(n) + ". dne po";
    }
}

// Meeusùv juliánský výpoèet Velikonoc
LiturgicalCalendar::CivilDate LiturgicalCalendar::julianEaster(int year)
{
    int a = year % 4;
    int b = year % 7;
    int c = year % 19;
    int d = (19 * c + 15) % 30;
    int e = (2 * a + 4 * b - d + 34) % 7;
    int month = (d + e + 114) / 31;
    int day = ((d + e + 114) % 31) + 1;

    CivilDate out;
    out.day = day;
    out.month = month;
    out.year = year;
    return out;
}

std::vector<LiturgicalCalendar::Entry> LiturgicalCalendar::resolvedEntriesForYear(int year) const
{
    std::vector<Entry> out;
    out.reserve(m_entries.size());

    const CivilDate easter = julianEaster(year);
    const int easterDoy = dayOfYear(easter.day, easter.month, year);

    for (auto e : m_entries)
    {
        if (e.movable) {
            int targetDoy = easterDoy + e.offsetFromEaster;

            if (targetDoy < 1) {
                int prevYearDays = isLeapYear(year - 1) ? 366 : 365;
                targetDoy += prevYearDays;
                CivilDate d = civilFromDayOfYear(targetDoy, year - 1);
                e.day = d.day;
                e.month = d.month;
            }
            else {
                int daysThisYear = isLeapYear(year) ? 366 : 365;
                if (targetDoy > daysThisYear) {
                    targetDoy -= daysThisYear;
                    CivilDate d = civilFromDayOfYear(targetDoy, year + 1);
                    e.day = d.day;
                    e.month = d.month;
                }
                else {
                    CivilDate d = civilFromDayOfYear(targetDoy, year);
                    e.day = d.day;
                    e.month = d.month;
                }
            }
        }

        out.push_back(std::move(e));
    }

    std::sort(out.begin(), out.end(),
        [](const Entry& a, const Entry& b)
        {
            if (a.month != b.month) return a.month < b.month;
            return a.day < b.day;
        });

    return out;
}

const LiturgicalCalendar::Entry* LiturgicalCalendar::exactEntryForDate(
    int day, int month, int year, std::vector<Entry>& resolved) const
{
    (void)year;
    for (auto& e : resolved) {
        if (e.day == day && e.month == month)
            return &e;
    }
    return nullptr;
}

const LiturgicalCalendar::Entry* LiturgicalCalendar::nearestPreviousEntry(
    int day, int month, int year, std::vector<Entry>& resolved) const
{
    (void)year;
    const Entry* best = nullptr;

    for (auto& e : resolved)
    {
        if (e.month < month || (e.month == month && e.day <= day))
            best = &e;
        else
            break;
    }

    if (best)
        return best;

    if (!resolved.empty())
        return &resolved.back();

    return nullptr;
}

int LiturgicalCalendar::daysSincePreviousFeast(
    int day, int month, int year, std::vector<Entry>& resolved) const
{
    const Entry* prev = nearestPreviousEntry(day, month, year, resolved);
    if (!prev)
        return 0;

    const int todayDoy = dayOfYear(day, month, year);
    const int feastDoy = dayOfYear(prev->day, prev->month, year);

    if (feastDoy <= todayDoy)
        return todayDoy - feastDoy;

    int prevYearDays = isLeapYear(year - 1) ? 366 : 365;
    int feastPrevYearDoy = dayOfYear(prev->day, prev->month, year - 1);
    return todayDoy + (prevYearDays - feastPrevYearDoy);
}

std::vector<LiturgicalCalendar::Entry> LiturgicalCalendar::entriesForDate(int day, int month, int year) const
{
    std::vector<Entry> resolved = resolvedEntriesForYear(year);
    std::vector<Entry> out;

    for (const auto& e : resolved) {
        if (e.day == day && e.month == month)
            out.push_back(e);
    }

    return out;
}

std::string LiturgicalCalendar::primaryTitle(int day, int month, int year) const
{
    auto entries = entriesForDate(day, month, year);
    if (!entries.empty())
        return entries.front().title;
    return "";
}

std::string LiturgicalCalendar::formatMedievalDate(
    int day,
    int month,
    int year,
    const std::string& weekDayCz,
    Style style) const
{
    std::vector<Entry> resolved = resolvedEntriesForYear(year);

    const Entry* exact = exactEntryForDate(day, month, year, resolved);
    if (exact)
    {
        switch (style)
        {
        case Style::Documentary:
            return "na " + exact->onForm + " l\u00E9ta P\u00E1n\u011B " + romanYear(year);
        case Style::Spoken:
            return "na " + exact->onForm;
        case Style::Latin:
            return "in festo " + exact->title + ", anno Domini " + romanYear(year);
        }
    }

    const Entry* prev = nearestPreviousEntry(day, month, year, resolved);
    if (!prev)
        return weekDayCz + " " + std::to_string(day) + "." + std::to_string(month) + "." + std::to_string(year);

    const int delta = daysSincePreviousFeast(day, month, year, resolved);
    const std::string wd = weekdayWithPrepositionCz(weekDayCz);

    switch (style)
    {
    case Style::Documentary:
        if (delta == 1)
            return wd + " po " + prev->afterForm + " l\u00E9ta P\u00E1n\u011B " + romanYear(year);
        return wd + " " + std::to_string(delta) + " dn\u00ED po " + prev->afterForm + " l\u00E9ta P\u00E1n\u011B " + romanYear(year);

    case Style::Spoken:
        if (delta == 1)
            return wd + " po " + prev->afterForm;
        return spokenOrdinalDaysCz(delta) + " " + prev->afterForm;

    case Style::Latin:
        if (delta == 1)
            return "feria post festum " + prev->title + ", anno Domini " + romanYear(year);
        return std::to_string(delta) + " diebus post festum " + prev->title + ", anno Domini " + romanYear(year);
    }

    return "";
}

std::string LiturgicalCalendar::primaryId(int day, int month, int year) const
{
    auto entries = entriesForDate(day, month, year);
    if (!entries.empty())
        return entries.front().id;
    return "";
}

std::string LiturgicalCalendar::primaryImportance(int day, int month, int year) const
{
    auto entries = entriesForDate(day, month, year);
    if (!entries.empty())
        return entries.front().importance;
    return "";
}

std::vector<std::string> LiturgicalCalendar::tagsForDate(int day, int month, int year) const
{
    auto entries = entriesForDate(day, month, year);
    if (!entries.empty())
        return entries.front().tags;
    return {};
}

bool LiturgicalCalendar::hasTag(int day, int month, int year, const std::string& tag) const
{
    auto entries = entriesForDate(day, month, year);
    if (entries.empty())
        return false;

    for (const auto& t : entries.front().tags)
    {
        if (t == tag)
            return true;
    }
    return false;
}