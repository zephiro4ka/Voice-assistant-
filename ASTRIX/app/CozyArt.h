#pragma once
// «Домашний уют»: лист тетради с рисунками от руки и нарисованная сфера.

#include "../ImGui/imgui.h"

namespace CozyArt {
    constexpr int kPatterns = 8;
    const char* PatternName(int index);
    void Background(ImDrawList* dl, ImVec2 size, int pattern);
    void Orb(ImDrawList* dl, ImVec2 center, float radius, float time);
}
