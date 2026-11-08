#pragma once
// Логотип ASTRIX: картинка лежит в ресурсах программы.

#include "../ImGui/imgui.h"

#include <string>

namespace Logo {
    void Draw(ImDrawList* dl, ImVec2 min, float size);
    bool LoadCustom(const std::string& path);     // своя картинка в центре экрана
    bool HasCustom();
    void DrawCustom(ImDrawList* dl, ImVec2 center, float size);
    void DrawTinted(ImDrawList* dl, ImVec2 center, float size, ImU32 color); // крупно и в любом цвете
}
