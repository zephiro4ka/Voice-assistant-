#pragma once
// Живая графика: фон с сиянием и звёздами, «жидкая» сфера Ани, градиентные примитивы.

#include "Theme.h"

namespace Visuals {
    void Init();

    // Фон главного экрана: стиль, скорость и цвет — из настроек «Внешний вид»
    struct BackdropOptions {
        int style = 0;              // Backdrop: сияние, нити, волны, звёзды, спокойный
        float speed = 1.0f;         // 1 — обычная скорость
        bool animate = true;        // false — фон замирает (окно не в фокусе)
        bool customColor = false;   // true — красим анимацию в color, иначе — по настроению Ани
        Theme::Color color{};
        float opacity = 1.0f;       // меньше 1 — сквозь фон видно акрил/Mica Windows
        float wallpaperDim = 0.45f; // затемнение своих обоев (если они есть)
    };
    void DrawBackground(ImDrawList* dl, ImVec2 size, float dt, const BackdropOptions& options);
    void DrawOrb(ImDrawList* dl, ImVec2 center, float radius, Theme::Mood mood, float time, float dt);

    // Многоугольник, залитый «веером» от точки focus: цвет inner → outer
    void FanFill(ImDrawList* dl, ImVec2 focus, const ImVec2* points, int count, ImU32 inner, ImU32 outer);
    // Скруглённый прямоугольник с таким же градиентом
    void GradientRoundRect(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, ImVec2 focus, ImU32 inner, ImU32 outer);
    // Радиальный градиент из нескольких колец
    void RadialGradient(ImDrawList* dl, ImVec2 center, const float* radii, const ImU32* colors, int stops, int segments = 64);
}
