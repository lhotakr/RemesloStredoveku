#pragma once
#include "WeatherTypes.h"

#include <string>
#include <unordered_map>
#include <cstdint>

class WeatherSystem
{
public:
    bool loadClimateProfile(const std::string& path, std::string* outError = nullptr);

    void setBaseSeed(uint32_t seed) { m_baseSeed = seed; }

    WeatherDayProfile getDayProfile(int day, int month, int year) const;

    WeatherRuntimeState getRuntimeState(
        const WeatherDayProfile& dayProfile,
        int hour,
        int minute,
        int sunriseMinutes,
        int sunsetMinutes) const;

    static const char* precipitationTypeToString(PrecipitationType type);
    static PrecipitationType precipitationTypeFromString(const std::string& text);

private:
    WeatherYearModifiers buildYearModifiers(int year) const;
    WeatherDayProfile generateDayProfile(int day, int month, int year) const;

    uint32_t makeYearSeed(int year) const;
    uint32_t makeDaySeed(int day, int month, int year) const;

    static float clamp01(float v);
    static float clamp100(float v);
    static float lerp(float a, float b, float t);

    static float random01(uint32_t seed);
    static float randomRange(uint32_t seed, float minV, float maxV);

    static int dayOfYear(int day, int month, int year);
    static bool isLeapYear(int year);
    static int daysInMonth(int month, int year);

    const MonthlyWeatherProfile* findMonthProfile(int month) const;
    WeatherDayProfile buildFallbackProfile(int day, int month, int year) const;

private:
    uint32_t m_baseSeed = 1400u;
    std::unordered_map<int, MonthlyWeatherProfile> m_months;
    mutable std::unordered_map<int, WeatherDayProfile> m_cache;
    static int makeDateKey(int day, int month, int year);

};