#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum class PrecipitationType
{
    None,
    Drizzle,
    Rain,
    Storm,
    Snow,
    Sleet
};

struct WeatherWindProfile
{
    float avg = 0.0f;        // km/h
    float gustChance = 0.0f; // 0..100
};

struct WeatherDayProfile
{
    int day = 1;
    int month = 1;
    int year = 1400;

    float minTemp = 0.0f;
    float maxTemp = 0.0f;

    float cloudiness = 0.0f;             // 0..100
    float precipitationChance = 0.0f;    // 0..100
    PrecipitationType precipitationType = PrecipitationType::None;
    float precipitationIntensity = 0.0f; // 0..1

    WeatherWindProfile wind;

    float fogMorning = 0.0f;             // 0..100
    float groundWetness = 0.0f;          // 0..100

    std::string frontType;               // stable / unstable / wet / cold_snap / warm_spell
};

struct WeatherRuntimeState
{
    float currentTemp = 0.0f;
    bool isRaining = false;
    float rainIntensity = 0.0f;
    bool isFoggy = false;
    float windNow = 0.0f;
    float discomfortIndex = 0.0f;
};

struct MonthlyWeatherProfile
{
    int month = 1;

    float avgMinTemp = 0.0f;
    float avgMaxTemp = 0.0f;
    float tempVariance = 0.0f;

    float cloudinessAvg = 0.0f;
    float cloudinessVariance = 0.0f;

    float precipitationChanceAvg = 0.0f;
    float precipitationChanceVariance = 0.0f;

    float windAvg = 0.0f;
    float windVariance = 0.0f;

    float fogMorningAvg = 0.0f;
    float fogMorningVariance = 0.0f;

    float groundDrying = 0.0f;

    float snowBias = 0.0f;
    float rainBias = 0.0f;
    float stormBias = 0.0f;
};

struct WeatherYearModifiers
{
    float annualTempOffset = 0.0f;      // -2 .. +2
    float annualWetnessOffset = 0.0f;   // -15 .. +15
    float annualWindOffset = 0.0f;      // -4 .. +4
    float storminessOffset = 0.0f;      // -10 .. +10
    float fogOffset = 0.0f;             // -15 .. +15
};