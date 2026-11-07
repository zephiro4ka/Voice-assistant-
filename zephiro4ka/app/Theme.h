#pragma once
// Внешний вид: масштаб под DPI, шрифты, цвета, стиль ImGui, палитры настроений.

#include "../ImGui/imgui.h"

struct Settings;

namespace Theme {
    // Настроение Ани — от него зависят цвета сферы, фона и акцентов
    enum class Mood { Idle, Listening, Thinking, Speaking };

    struct Color {
        float r, g, b;
        ImU32 WithAlpha(float a) const;
        Color Mix(const Color& to, float t) const;
    };

    extern float dpi;          // 1.0 = 96 DPI
    extern ImFont* body;       // обычный текст
    extern ImFont* semibold;   // подписи, кнопки
    extern ImFont* display;    // заголовки
    extern ImFont* hero;       // крупное приветствие
    extern ImFont* title;      // названия настроек и карточек — жирный

    constexpr ImU32 kInk = IM_COL32(244, 239, 250, 255);
    constexpr ImU32 kInkSoft = IM_COL32(244, 239, 250, 160);
    constexpr ImU32 kInkMuted = IM_COL32(244, 239, 250, 95);
    constexpr ImU32 kOk = IM_COL32(110, 231, 183, 255);
    constexpr ImU32 kBad = IM_COL32(251, 113, 133, 255);

    void Init(float dpiScale);      // шрифты + стиль
    float Px(float value);          // значение в пикселях с учётом DPI

    // Плавно перетекающая палитра из трёх цветов
    void UpdatePalette(Mood mood, float dt);
    const Color& Palette(int index);

    // ---- внешний вид из настроек: тема, любимый цвет, стекло
    void Apply(const Settings& settings);  // каждый кадр: дёшево, меняет только цвета
    Color Accent();                               // любимый цвет — кнопки, переключатели, выбор
    Color Background(int index);                  // 0 — верх фона, 1 — низ
    ImU32 Surface(float alpha = 1.0f);            // цвет панелей (со «стеклом» — прозрачнее)
    ImU32 SurfaceSolid();                          // то же, но всегда плотно — для всплывающих меню (стекло не просвечивает)
    bool ButtonPlates();                          // подложка под кнопками окна

    struct LookColors { Color top, bottom, surface; };
    LookColors LookPreset(int look, int customRgb);
    constexpr int kAccentPresets = 6;
    Color AccentPreset(int index);
    Color FromRgb(int rgb);
    int ToRgb(const Color& color);
}
