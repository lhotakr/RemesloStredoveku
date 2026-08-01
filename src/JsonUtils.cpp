#include "JsonUtils.h"

#include <fstream>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
    bool ReadFileRaw(const std::string& path, std::string& outText)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;

        std::ostringstream ss;
        ss << f.rdbuf();
        outText = ss.str();
        return true;
    }

#ifdef _WIN32
    bool ConvertAnsiCp1250ToUtf8(const std::string& input, std::string& output)
    {
        if (input.empty()) {
            output.clear();
            return true;
        }

        const int wideLen = MultiByteToWideChar(
            1250, 0,
            input.data(), (int)input.size(),
            nullptr, 0);

        if (wideLen <= 0)
            return false;

        std::wstring wide((size_t)wideLen, L'\0');
        if (MultiByteToWideChar(
            1250, 0,
            input.data(), (int)input.size(),
            wide.data(), wideLen) <= 0)
        {
            return false;
        }

        const int utf8Len = WideCharToMultiByte(
            CP_UTF8, 0,
            wide.data(), (int)wide.size(),
            nullptr, 0,
            nullptr, nullptr);

        if (utf8Len <= 0)
            return false;

        output.resize((size_t)utf8Len);
        if (WideCharToMultiByte(
            CP_UTF8, 0,
            wide.data(), (int)wide.size(),
            output.data(), utf8Len,
            nullptr, nullptr) <= 0)
        {
            return false;
        }

        return true;
    }
#endif

    bool StripUtf8Bom(std::string& text)
    {
        if (text.size() >= 3 &&
            (unsigned char)text[0] == 0xEF &&
            (unsigned char)text[1] == 0xBB &&
            (unsigned char)text[2] == 0xBF)
        {
            text.erase(0, 3);
            return true;
        }
        return false;
    }
}

namespace jsonutils
{
    bool ReadTextFileUtf8Safe(const std::string& path, std::string& outText, std::string& outError)
    {
        std::string raw;
        if (!ReadFileRaw(path, raw)) {
            outError = "Nepodarilo se otevrit soubor: " + path;
            return false;
        }

        StripUtf8Bom(raw);

        // 1) nejdriv zkusime, ze soubor uz je validni UTF-8
        try {
            // json parser je tady jen jako validator UTF-8 stringu pres parse string literal
            nlohmann::json tmp = raw;
            (void)tmp;
            outText = raw;
            return true;
        }
        catch (...) {
            // pokraèujeme fallbackem
        }

#ifdef _WIN32
        // 2) fallback: ANSI CP1250 -> UTF-8
        std::string converted;
        if (ConvertAnsiCp1250ToUtf8(raw, converted)) {
            outText = converted;
            return true;
        }
#endif

        outError = "Soubor neni validni UTF-8 a fallback konverze selhala: " + path;
        return false;
    }

    bool LoadJsonFileSafe(const std::string& path, nlohmann::json& outJson, std::string& outError)
    {
        std::string raw;
        if (!ReadFileRaw(path, raw)) {
            outError = "Nepodarilo se otevrit JSON soubor: " + path;
            return false;
        }

        StripUtf8Bom(raw);

        // 1) normalni parse
        try {
            outJson = nlohmann::json::parse(raw);
            return true;
        }
        catch (const std::exception& exUtf8) {
#ifdef _WIN32
            // 2) fallback CP1250 -> UTF-8
            std::string converted;
            if (ConvertAnsiCp1250ToUtf8(raw, converted)) {
                try {
                    outJson = nlohmann::json::parse(converted);
                    return true;
                }
                catch (const std::exception& exAnsi) {
                    outError =
                        std::string("JSON parse failed (UTF-8 i CP1250 fallback): ") +
                        exAnsi.what() + " | soubor: " + path;
                    return false;
                }
            }
#endif
            outError =
                std::string("JSON parse failed: ") +
                exUtf8.what() + " | soubor: " + path;
            return false;
        }
    }
}