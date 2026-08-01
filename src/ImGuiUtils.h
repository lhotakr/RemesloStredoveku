#pragma once
#include "imgui.h"

namespace ui
{
    bool LoadDefaultFont(ImGuiIO& io, const char* fontPath, float sizePx);

    void ProgressStat(const char* label, float value01, const ImVec2& size = ImVec2(220.0f, 0.0f));
    void ColoredStatus(const ImVec4& color, const char* text);

    const char* WeekDayCz(int index);
    const char* DayPeriodCz(int index);

    namespace text
    {
        const char* HudTitle();
        const char* Hp();
        const char* Hunger();
        const char* Thirst();
        const char* Fatigue();
        const char* Hygiene();
		const char* Social();
        const char* Carry();

        const char* Poisoned();
        const char* Injured();
        const char* Fracture();
        const char* Bleeding();
        const char* TreatedWound();
        
        const char* TempFormat();
        const char* CarryFormat();
		const char* TimeFormat();
		const char* DateFormat();
		const char* DayPeriodLabel();
		const char* FeastLabel();
        const char* NoFeast();

        const char* ConsoleTitle();
		const char* ConsoleHint();
    }
}