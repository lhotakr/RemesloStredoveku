#include "ImGuiUtils.h"
#include "Utf8.h"

namespace ui
{
    bool LoadDefaultFont(ImGuiIO& io, const char* fontPath, float sizePx)
    {
        static const ImWchar czRanges[] = {
            0x0020, 0x00FF,
            0x0100, 0x017F,
            0x0180, 0x024F,
            0
        };

        ImFontConfig cfg{};
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        cfg.PixelSnapH = true;

        return io.Fonts->AddFontFromFileTTF(fontPath, sizePx, &cfg, czRanges) != nullptr;
    }

    void ProgressStat(const char* label, float value01, const ImVec2& size)
    {
        ImGui::TextUnformatted(label);
        ImGui::ProgressBar(value01, size);
    }

    void ColoredStatus(const ImVec4& color, const char* text)
    {
        ImGui::TextColored(color, "%s", text);
    }

    const char* WeekDayCz(int index)
    {
        switch (index)
        {
        case 0: return U8("pond\u011Bl\u00ED");
        case 1: return U8("\u00FAter\u00FD");
        case 2: return U8("st\u0159eda");
        case 3: return U8("\u010Dtvrtek");
        case 4: return U8("p\u00E1tek");
        case 5: return U8("sobota");
        case 6: return U8("ned\u011Ble");
        default: return U8("pond\u011Bl\u00ED");
        }
    }

    const char* DayPeriodCz(int index)
    {
        switch (index)
        {
        case 0: return U8("r\u00E1no");
        case 1: return U8("dopoledne");
        case 2: return U8("poledne");
        case 3: return U8("odpoledne");
        case 4: return U8("podve\u010Der");
        case 5: return U8("ve\u010Der");
        case 6: return U8("noc");
        default: return U8("noc");
        }
    }

    namespace text
    {
        const char* HudTitle() { return U8("Stav hr\u00E1\u010De"); }
        const char* Hp() { return "HP"; }
        const char* Hunger() { return U8("Hlad"); }
        const char* Thirst() { return U8("\u017D\u00EDze\u0148"); }
        const char* Fatigue() { return U8("\u00DAnava"); }
        const char* Hygiene() { return U8("Hygiena"); }
		const char* Social() { return U8("Spole\u010Den\u00ED"); }
        const char* Carry() { return U8("Zat\u00ED\u017Een\u00ED"); }

        const char* Poisoned() { return U8("Otrava"); }
        const char* Injured() { return U8("Zran\u011Bn\u00ED"); }
        const char* Fracture() { return U8("Zlomenina"); }
        const char* Bleeding() { return U8("Krv\u00E1cen\u00ED"); }
        const char* TreatedWound() { return U8("O\u0161et\u0159en\u00E1 r\u00E1na"); }

        const char* TempFormat() { return U8("Teplota: %.1f \u00B0C"); }
        const char* CarryFormat() { return U8("Zat\u00ED\u017Een\u00ED: %.1f / %.1f kg"); }
        const char* TimeFormat() { return U8("\u010Cas: %s"); }
        const char* DateFormat() { return U8("Datum: %s"); }
        const char* DayPeriodLabel() { return U8("\u010C\u00E1st dne:"); }
        const char* FeastLabel() { return U8("Sv\u00E1tek:"); }
        const char* NoFeast() { return U8("bez sv\u00E1tku"); }

        const char* ConsoleTitle() { return U8("Debug konzole"); }
        const char* ConsoleHint()
        {
            return U8("P\u0159\u00EDklady: set hp 50, set thirst 80, set time 22:00, set date 14.12.1400, IDDQD, help");
        }
    }
}