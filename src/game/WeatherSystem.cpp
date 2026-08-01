#include "WeatherSystem.h"
#include "../JsonUtils.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

static float SmoothStep01(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool WeatherSystem::loadClimateProfile(const std::string& path, std::string* outError)
{
    json root;
    std::string err;
    if (!jsonutils::LoadJsonFileSafe(path, root, err))
    {
        if (outError) *outError = "WeatherSystem: " + err;
        return false;
    }

    if (!root.contains("monthlyProfiles") || !root["monthlyProfiles"].is_array())
    {
        if (outError) *outError = "WeatherSystem: chybi pole 'monthlyProfiles'";
        return false;
    }

    m_months.clear();

    for (const auto& jm : root["monthlyProfiles"])
    {
        MonthlyWeatherProfile p;
        p.month = jm.value("month", 1);
        p.avgMinTemp = jm.value("avgMinTemp", 0.0f);
        p.avgMaxTemp = jm.value("avgMaxTemp", 10.0f);
        p.tempVariance = jm.value("tempVariance", 3.0f);

        p.cloudinessAvg = jm.value("cloudinessAvg", 50.0f);
        p.cloudinessVariance = jm.value("cloudinessVariance", 15.0f);

        p.precipitationChanceAvg = jm.value("precipitationChanceAvg", 20.0f);
        p.precipitationChanceVariance = jm.value("precipitationChanceVariance", 10.0f);

        p.windAvg = jm.value("windAvg", 8.0f);
        p.windVariance = jm.value("windVariance", 4.0f);

        p.fogMorningAvg = jm.value("fogMorningAvg", 20.0f);
        p.fogMorningVariance = jm.value("fogMorningVariance", 10.0f);

        p.groundDrying = jm.value("groundDrying", 5.0f);

        p.snowBias = jm.value("snowBias", 0.0f);
        p.rainBias = jm.value("rainBias", 1.0f);
        p.stormBias = jm.value("stormBias", 0.0f);

        m_months[p.month] = p;
    }

    if (m_months.empty())
    {
        if (outError) *outError = "WeatherSystem: nenacten zadny mesic";
        return false;
    }

    return true;
}

const MonthlyWeatherProfile* WeatherSystem::findMonthProfile(int month) const
{
    auto it = m_months.find(month);
    if (it == m_months.end())
        return nullptr;
    return &it->second;
}

bool WeatherSystem::isLeapYear(int year)
{
    return (year % 4 == 0);
}

int WeatherSystem::daysInMonth(int month, int year)
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

int WeatherSystem::dayOfYear(int day, int month, int year)
{
    int total = 0;
    for (int m = 1; m < month; ++m)
        total += daysInMonth(m, year);
    return total + day;
}

float WeatherSystem::clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

float WeatherSystem::clamp100(float v)
{
    return std::clamp(v, 0.0f, 100.0f);
}

float WeatherSystem::lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float WeatherSystem::random01(uint32_t seed)
{
    uint32_t x = seed;
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;

    return (x & 0x00FFFFFF) / 16777215.0f;
}

float WeatherSystem::randomRange(uint32_t seed, float minV, float maxV)
{
    return lerp(minV, maxV, random01(seed));
}

uint32_t WeatherSystem::makeYearSeed(int year) const
{
    return m_baseSeed ^ (uint32_t)(year * 2654435761u);
}

uint32_t WeatherSystem::makeDaySeed(int day, int month, int year) const
{
    uint32_t y = makeYearSeed(year);
    return y ^ (uint32_t)(month * 73856093u) ^ (uint32_t)(day * 19349663u);
}

WeatherYearModifiers WeatherSystem::buildYearModifiers(int year) const
{
    WeatherYearModifiers mod;
    const uint32_t ys = makeYearSeed(year);

    mod.annualTempOffset = randomRange(ys + 11u, -1.8f, 1.8f);
    mod.annualWetnessOffset = randomRange(ys + 23u, -12.0f, 12.0f);
    mod.annualWindOffset = randomRange(ys + 37u, -3.0f, 3.0f);
    mod.storminessOffset = randomRange(ys + 41u, -8.0f, 8.0f);
    mod.fogOffset = randomRange(ys + 53u, -10.0f, 10.0f);

    return mod;
}

WeatherDayProfile WeatherSystem::buildFallbackProfile(int day, int month, int year) const
{
    WeatherDayProfile p;
    p.day = day;
    p.month = month;
    p.year = year;
    p.minTemp = 0.0f;
    p.maxTemp = 10.0f;
    p.cloudiness = 50.0f;
    p.precipitationChance = 20.0f;
    p.precipitationType = PrecipitationType::Rain;
    p.precipitationIntensity = 0.2f;
    p.wind.avg = 8.0f;
    p.wind.gustChance = 12.0f;
    p.fogMorning = 20.0f;
    p.groundWetness = 20.0f;
    p.frontType = "stable";
    return p;
}

WeatherDayProfile WeatherSystem::generateDayProfile(int day, int month, int year) const
{
    const MonthlyWeatherProfile* mp = findMonthProfile(month);
    if (!mp)
        return buildFallbackProfile(day, month, year);

    WeatherDayProfile out;
    out.day = day;
    out.month = month;
    out.year = year;

    const WeatherYearModifiers yearMod = buildYearModifiers(year);
    const uint32_t ds = makeDaySeed(day, month, year);
    const int doy = dayOfYear(day, month, year);

    const float seasonalWave =
        std::sin((float)doy / (isLeapYear(year) ? 366.0f : 365.0f) * 6.2831853f);

    float minTemp = mp->avgMinTemp
        + yearMod.annualTempOffset
        + randomRange(ds + 1u, -mp->tempVariance, mp->tempVariance);

    float maxTemp = mp->avgMaxTemp
        + yearMod.annualTempOffset
        + randomRange(ds + 2u, -mp->tempVariance, mp->tempVariance);

    float cloudiness =
        mp->cloudinessAvg
        + yearMod.annualWetnessOffset * 0.6f
        + randomRange(ds + 3u, -mp->cloudinessVariance, mp->cloudinessVariance);

    float precipitationChance =
        mp->precipitationChanceAvg
        + yearMod.annualWetnessOffset
        + randomRange(ds + 4u, -mp->precipitationChanceVariance, mp->precipitationChanceVariance);

    float windAvg =
        mp->windAvg
        + yearMod.annualWindOffset
        + randomRange(ds + 5u, -mp->windVariance, mp->windVariance);

    float fogMorning =
        mp->fogMorningAvg
        + yearMod.fogOffset
        + randomRange(ds + 6u, -mp->fogMorningVariance, mp->fogMorningVariance);

    cloudiness += (seasonalWave < 0.0f ? 4.0f : -2.0f);
    fogMorning += (month == 3 || month == 4 || month == 10 || month == 11) ? 8.0f : 0.0f;

    bool hasPrev = !(day == 1 && month == 1);
    WeatherDayProfile prev;

    if (hasPrev)
    {
        int prevDay = day - 1;
        int prevMonth = month;
        int prevYear = year;

        if (prevDay < 1)
        {
            prevMonth--;
            if (prevMonth < 1)
            {
                prevMonth = 12;
                prevYear--;
            }
            prevDay = daysInMonth(prevMonth, prevYear);
        }

        prev = getDayProfile(prevDay, prevMonth, prevYear);
    }

    if (hasPrev)
    {
        cloudiness = prev.cloudiness * 0.55f + cloudiness * 0.45f;
        precipitationChance = prev.precipitationChance * 0.40f + precipitationChance * 0.60f;
        windAvg = prev.wind.avg * 0.35f + windAvg * 0.65f;
        fogMorning = prev.groundWetness * 0.18f + fogMorning * 0.82f;
    }

    cloudiness = clamp100(cloudiness);
    precipitationChance = clamp100(precipitationChance);
    windAvg = std::max(0.0f, windAvg);
    fogMorning = clamp100(fogMorning);

    if (cloudiness > 75.0f)
    {
        maxTemp -= 1.0f;
        minTemp += 0.8f;
    }
    else if (cloudiness < 25.0f)
    {
        maxTemp += 1.0f;
        minTemp -= 1.2f;
    }

    if (maxTemp < minTemp)
        std::swap(maxTemp, minTemp);

    float intensity =
        clamp01((precipitationChance / 100.0f) * 0.65f + randomRange(ds + 7u, 0.0f, 0.35f));

    PrecipitationType ptype = PrecipitationType::None;

    if (precipitationChance < 15.0f)
    {
        ptype = PrecipitationType::None;
        intensity = 0.0f;
    }
    else
    {
        const bool winterLike = (maxTemp <= 1.0f);
        const bool mixedLike = (minTemp < 0.0f && maxTemp > 1.5f);
        const bool summerStorm =
            (month >= 5 && month <= 8 && intensity > 0.65f &&
                random01(ds + 8u) < clamp01((mp->stormBias + yearMod.storminessOffset * 0.01f)));

        if (summerStorm)
            ptype = PrecipitationType::Storm;
        else if (winterLike && random01(ds + 9u) < mp->snowBias)
            ptype = PrecipitationType::Snow;
        else if (mixedLike)
            ptype = PrecipitationType::Sleet;
        else if (intensity < 0.22f)
            ptype = PrecipitationType::Drizzle;
        else
            ptype = PrecipitationType::Rain;
    }

    float groundWetness = cloudiness * 0.22f + precipitationChance * 0.30f + intensity * 35.0f;

    if (hasPrev)
    {
        groundWetness = prev.groundWetness * 0.65f + groundWetness * 0.35f - mp->groundDrying;
    }

    out.minTemp = minTemp;
    out.maxTemp = maxTemp;
    out.cloudiness = cloudiness;
    out.precipitationChance = precipitationChance;
    out.precipitationType = ptype;
    out.precipitationIntensity = clamp01(intensity);
    out.wind.avg = windAvg;
    out.wind.gustChance = clamp100(8.0f + intensity * 40.0f + windAvg * 1.2f);
    out.fogMorning = fogMorning;
    out.groundWetness = clamp100(groundWetness);

    if (ptype == PrecipitationType::Storm)
        out.frontType = "unstable";
    else if (ptype == PrecipitationType::Snow || ptype == PrecipitationType::Sleet)
        out.frontType = "cold_snap";
    else if (out.precipitationChance > 55.0f)
        out.frontType = "wet";
    else if (out.maxTemp > mp->avgMaxTemp + 2.0f)
        out.frontType = "warm_spell";
    else
        out.frontType = "stable";

    return out;
}

WeatherDayProfile WeatherSystem::getDayProfile(int day, int month, int year) const
{
    const int key = makeDateKey(day, month, year);

    auto it = m_cache.find(key);
    if (it != m_cache.end())
        return it->second;

    WeatherDayProfile result = generateDayProfile(day, month, year);
    m_cache[key] = result;
    return result;
}

WeatherRuntimeState WeatherSystem::getRuntimeState(
    const WeatherDayProfile& dayProfile,
    int hour,
    int minute,
    int sunriseMinutes,
    int sunsetMinutes) const
{
    WeatherRuntimeState st{};

    const int nowMin = hour * 60 + minute;
    const int warmPeak = std::max(sunriseMinutes + 60, sunsetMinutes - 180);

    float baseTemp = dayProfile.minTemp;

    if (nowMin <= sunriseMinutes)
    {
        baseTemp = dayProfile.minTemp;
    }
    else if (nowMin < warmPeak)
    {
        float t = (float)(nowMin - sunriseMinutes) / (float)std::max(1, warmPeak - sunriseMinutes);
        t = SmoothStep01(t);
        baseTemp = lerp(dayProfile.minTemp, dayProfile.maxTemp, t);
    }
    else
    {
        float t = (float)(nowMin - warmPeak) / (float)std::max(1, 1440 - warmPeak);
        t = SmoothStep01(t);
        baseTemp = lerp(dayProfile.maxTemp, dayProfile.minTemp + 1.0f, t);
    }

    if (dayProfile.cloudiness > 75.0f && nowMin > sunriseMinutes && nowMin < sunsetMinutes)
        baseTemp -= 0.8f;

    if (dayProfile.cloudiness > 75.0f && (nowMin < sunriseMinutes || nowMin > sunsetMinutes))
        baseTemp += 0.6f;

    const uint32_t runtimeSeed =
        (uint32_t)(dayProfile.year * 10000 + dayProfile.month * 100 + dayProfile.day)
        ^ (uint32_t)(nowMin * 9781);

    // dÈöù po blocÌch bÏhem dne
    float rainWindowChance = dayProfile.precipitationChance / 100.0f;
    rainWindowChance *= (0.45f + dayProfile.precipitationIntensity * 0.8f);

    bool canRain = dayProfile.precipitationType != PrecipitationType::None;
    bool isRaining = false;

    if (canRain)
    {
        const int block = nowMin / 90;
        const float r = random01(runtimeSeed ^ (uint32_t)(block * 11939));
        isRaining = r < rainWindowChance;
    }

    float rainIntensity = 0.0f;
    if (isRaining)
    {
        rainIntensity = clamp01(
            dayProfile.precipitationIntensity * 0.65f +
            randomRange(runtimeSeed + 17u, 0.0f, 0.35f));

        if (dayProfile.precipitationType == PrecipitationType::Storm)
            rainIntensity = std::max(rainIntensity, 0.65f);

        baseTemp -= 0.8f + rainIntensity * 1.2f;
    }

    float windNow =
        dayProfile.wind.avg +
        std::sin((float)nowMin * 0.02f) * 2.0f +
        randomRange(runtimeSeed + 29u, -1.5f, 1.5f);

    windNow = std::max(0.0f, windNow);

    bool isFoggy = false;
    if (nowMin <= sunriseMinutes + 180)
    {
        float morningFactor = 1.0f - clamp01((float)(nowMin - sunriseMinutes + 30) / 210.0f);
        float fogStrength =
            (dayProfile.fogMorning / 100.0f) *
            (dayProfile.groundWetness / 100.0f * 0.6f + 0.4f) *
            (windNow < 10.0f ? 1.0f : 0.55f) *
            morningFactor;

        isFoggy = fogStrength > 0.28f;
    }

    float discomfort = 0.0f;

    if (baseTemp < 5.0f)
        discomfort += (5.0f - baseTemp) * 4.2f;

    discomfort += windNow * 0.55f;

    if (isRaining)
        discomfort += 10.0f + rainIntensity * 25.0f;

    if (isFoggy)
        discomfort += 6.0f;

    discomfort += dayProfile.groundWetness * 0.12f;

    st.currentTemp = baseTemp;
    st.isRaining = isRaining;
    st.rainIntensity = rainIntensity;
    st.isFoggy = isFoggy;
    st.windNow = windNow;
    st.discomfortIndex = std::clamp(discomfort, 0.0f, 100.0f);

    return st;
}

const char* WeatherSystem::precipitationTypeToString(PrecipitationType type)
{
    switch (type)
    {
    case PrecipitationType::None:    return "none";
    case PrecipitationType::Drizzle: return "drizzle";
    case PrecipitationType::Rain:    return "rain";
    case PrecipitationType::Storm:   return "storm";
    case PrecipitationType::Snow:    return "snow";
    case PrecipitationType::Sleet:   return "sleet";
    }
    return "none";
}

PrecipitationType WeatherSystem::precipitationTypeFromString(const std::string& text)
{
    if (text == "drizzle") return PrecipitationType::Drizzle;
    if (text == "rain")    return PrecipitationType::Rain;
    if (text == "storm")   return PrecipitationType::Storm;
    if (text == "snow")    return PrecipitationType::Snow;
    if (text == "sleet")   return PrecipitationType::Sleet;
    return PrecipitationType::None;
}

int WeatherSystem::makeDateKey(int day, int month, int year)
{
    return year * 10000 + month * 100 + day;
}