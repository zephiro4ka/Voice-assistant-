#pragma once
// Внешний вид: масштаб под DPI, шрифты, цвета, стиль ImGui, палитры настроений.

#include "../ImGui/imgui.h"

#include <cmath>

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

    extern ImU32 kInk, kInkSoft, kInkMuted; // цвет текста зависит от темы: светлый на тёмном, чернила на бумаге
    constexpr ImU32 kOk = IM_COL32(110, 231, 183, 255);
    constexpr ImU32 kBad = IM_COL32(251, 113, 133, 255);

    void Init(float dpiScale);      // шрифты + стиль
    void LoadFonts();               // загрузить шрифты по настройкам
    float Px(float value);          // значение в пикселях с учётом DPI
    // Доля пути за кадр для плавного сближения: одинаково мягко при любой частоте кадров
    inline float Ease(float dt, float speed) { return 1.0f - std::exp(-dt * speed); }

    // Плавно перетекающая палитра из трёх цветов
    void UpdatePalette(Mood mood, float dt);
    const Color& Palette(int index);

    // ---- внешний вид из настроек: тема, любимый цвет, стекло
    void Apply(const Settings& settings);  // каждый кадр: дёшево, меняет только цвета
    Color Accent();                               // любимый цвет — кнопки, переключатели, выбор
    Color Background(int index);                  // 0 — верх фона, 1 — низ
    ImU32 Surface(float alpha = 1.0f);            // цвет панелей (со «стеклом» — прозрачнее)
    ImU32 SurfaceSolid();                          // то же, но всегда плотно — для всплывающих меню (стекло не просвечивает)
    ImU32 Overlay(int alpha);                     // тонкая линия или подсветка: белая на тёмном, карандашная на бумаге

    // Шрифт и его размер (80–120%, 100% — как задумано)
    int FontCount();
    const char* FontName(int index);
    int FontIndex();
    int FontScale();
    void SetFont(int index, int scale);

    // Формат «Домашний уют»: бумага и рисунки от руки
    bool Cozy();
    int CozyPattern();
    void SetCozy(bool on, int pattern);
    bool CozyDark();                              // тёмный вариант: ночная тетрадь
    void SetCozyDark(bool on);
    bool Paper();                                 // светлая бумага

    bool ButtonPlates();                          // подложка под кнопками окна

    struct LookColors { Color top, bottom, surface; };
    LookColors LookPreset(int look, int customRgb);
    constexpr int kAccentPresets = 6;
    Color AccentPreset(int index);
    Color FromRgb(int rgb);
    int ToRgb(const Color& color);
}
