#pragma once
// Погода слева от сферы.
//   Маленькая карточка: сейчас + 4 часа (час назад, сейчас, через час, через два).
//   Навёл мышь — плавно раскрывается: подробности и все 24 часа сегодняшнего дня.

#include "../ImGui/imgui.h"

namespace WeatherWidget {
    // left/centerY — куда прижать карточку; top/bottom — свободная полоса по высоте; hover — можно ли раскрываться
    void Draw(ImDrawList* dl, float left, float centerY, float top, float bottom, bool hover, float dt);
    float MiniWidth();
    void Bounds(ImVec2& min, ImVec2& max); // где карточка нарисована в последнем кадре
}
