#include "WeatherWidget.h"

#include "Theme.h"
#include "Weather.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using Theme::Px;
using Weather::Sky;

namespace {
    constexpr float kPi = 3.14159265f;
    float g_open = 0.0f; // 0 — маленькая, 1 — раскрытая

    // размеры (до DPI)
    constexpr float kPad = 14, kHeader = 42, kLine = 19;
    constexpr float kMiniCell = 54, kMiniCellHeight = 80;
    constexpr int kColumns = 6, kRows = 4;
    constexpr float kCell = 56, kCellHeight = 74;

    ImU32 Fade(ImU32 color, float alpha) {
        const float a = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f);
        return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
    }

    std::string Temp(float t) {
        char text[16];
        sprintf_s(text, "%d°", static_cast<int>(std::lround(t)));
        return text;
    }

    void TextCentered(ImDrawList* dl, ImFont* font, float centerX, float y, ImU32 color, const std::string& text) {
        const float w = Widgets::TextSize(font, text.c_str()).x;
        Widgets::Text(dl, font, ImVec2(centerX - w * 0.5f, y), color, text.c_str());
    }

    // ------------------------------------------------------------ значки погоды (рисуем векторно)
    void Sun(ImDrawList* dl, ImVec2 c, float s, float a) {
        const ImU32 gold = Fade(IM_COL32(255, 204, 102, 255), a);
        const float r = s * 0.2f;
        dl->AddCircleFilled(c, r * 1.9f, Fade(IM_COL32(255, 204, 102, 40), a), 24); // мягкое свечение
        dl->AddCircleFilled(c, r, gold, 24);
        for (int i = 0; i < 8; ++i) {
            const float angle = i * kPi / 4.0f;
            const ImVec2 dir(std::cos(angle), std::sin(angle));
            dl->AddLine(ImVec2(c.x + dir.x * r * 1.45f, c.y + dir.y * r * 1.45f), ImVec2(c.x + dir.x * r * 2.0f, c.y + dir.y * r * 2.0f), gold, s * 0.055f);
        }
    }

    void Moon(ImDrawList* dl, ImVec2 c, float s, float a) {
        // серп = круг минус сдвинутый круг: внешняя дуга между точками пересечения, затем внутренняя обратно
        const float r = s * 0.27f, inner = r * 0.82f;
        const float phi = std::atan2(-0.28f, 0.48f), d = r * std::sqrt(0.48f * 0.48f + 0.28f * 0.28f);
        const ImVec2 c1(c.x + std::cos(phi) * d, c.y + std::sin(phi) * d);
        const float alpha = std::acos((r * r + d * d - inner * inner) / (2 * r * d));         // точки пересечения на внешнем круге
        const float beta = std::acos((inner * inner + d * d - r * r) / (2 * inner * d));      // и на внутреннем
        constexpr int kSteps = 28;
        ImVec2 points[kSteps * 2 + 2];
        int n = 0;
        for (int i = 0; i <= kSteps; ++i) {
            const float t = phi + alpha + (2 * kPi - 2 * alpha) * i / kSteps;
            points[n++] = ImVec2(c.x + std::cos(t) * r, c.y + std::sin(t) * r);
        }
        for (int i = 0; i <= kSteps; ++i) {
            const float t = phi + kPi + beta - 2 * beta * i / kSteps;
            points[n++] = ImVec2(c1.x + std::cos(t) * inner, c1.y + std::sin(t) * inner);
        }
        dl->AddCircleFilled(c, r * 1.6f, Fade(IM_COL32(200, 190, 255, 30), a), 24);
        dl->AddConcavePolyFilled(points, n, Fade(IM_COL32(226, 220, 255, 255), a));
    }

    void Cloud(ImDrawList* dl, ImVec2 c, float s, float a, ImU32 color = IM_COL32(214, 208, 236, 255)) {
        const ImU32 col = Fade(color, a);
        dl->AddCircleFilled(ImVec2(c.x - s * 0.17f, c.y + s * 0.04f), s * 0.16f, col, 24);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.03f, c.y - s * 0.07f), s * 0.21f, col, 24);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.21f, c.y + s * 0.06f), s * 0.14f, col, 24);
        dl->AddRectFilled(ImVec2(c.x - s * 0.3f, c.y + s * 0.04f), ImVec2(c.x + s * 0.33f, c.y + s * 0.2f), col, s * 0.08f);
    }

    void DrawSky(ImDrawList* dl, Sky sky, bool night, ImVec2 c, float s, float a) {
        auto celestial = [&](ImVec2 p, float size) { night ? Moon(dl, p, size, a) : Sun(dl, p, size, a); };
        const ImVec2 up(c.x, c.y - s * 0.1f); // туча повыше, под ней капли
        switch (sky) {
        case Sky::Clear: celestial(c, s); break;
        case Sky::PartlyCloudy:
            celestial(ImVec2(c.x - s * 0.14f, c.y - s * 0.14f), s * 0.8f);
            Cloud(dl, ImVec2(c.x + s * 0.06f, c.y + s * 0.06f), s * 0.82f, a);
            break;
        case Sky::Cloudy:
            Cloud(dl, ImVec2(c.x + s * 0.1f, c.y - s * 0.08f), s * 0.7f, a, IM_COL32(150, 142, 180, 255)); // дальняя, темнее
            Cloud(dl, ImVec2(c.x - s * 0.04f, c.y + s * 0.04f), s * 0.9f, a);
            break;
        case Sky::Fog:
            for (int i = 0; i < 3; ++i) {
                const float y = c.y + (i - 1) * s * 0.2f, half = s * (i == 1 ? 0.34f : 0.26f);
                dl->AddLine(ImVec2(c.x - half, y), ImVec2(c.x + half, y), Fade(IM_COL32(214, 208, 236, 220), a), s * 0.08f);
            }
            break;
        case Sky::Drizzle:
        case Sky::Rain: {
            Cloud(dl, up, s, a);
            const int drops = sky == Sky::Rain ? 3 : 2;
            const ImU32 blue = Fade(IM_COL32(125, 185, 255, 255), a);
            for (int i = 0; i < drops; ++i) {
                const float x = c.x + (i - (drops - 1) * 0.5f) * s * 0.2f, y = c.y + s * 0.22f;
                dl->AddLine(ImVec2(x + s * 0.04f, y), ImVec2(x - s * 0.04f, y + s * (sky == Sky::Rain ? 0.2f : 0.12f)), blue, s * 0.06f);
            }
            break;
        }
        case Sky::Snow:
            Cloud(dl, up, s, a);
            for (int i = 0; i < 3; ++i)
                dl->AddCircleFilled(ImVec2(c.x + (i - 1) * s * 0.2f, c.y + s * (i == 1 ? 0.36f : 0.28f)), s * 0.045f, Fade(IM_COL32(255, 255, 255, 255), a), 12);
            break;
        case Sky::Thunder: {
            Cloud(dl, up, s, a, IM_COL32(170, 160, 200, 255));
            const ImVec2 bolt[] = { { c.x + s * 0.04f, c.y + s * 0.12f }, { c.x - s * 0.08f, c.y + s * 0.3f }, { c.x + s * 0.03f, c.y + s * 0.3f }, { c.x - s * 0.06f, c.y + s * 0.46f } };
            dl->AddPolyline(bolt, 4, Fade(IM_COL32(255, 214, 90, 255), a), 0, s * 0.06f);
            break;
        }
        }
    }

    // ------------------------------------------------------------ части карточки
    // Шапка: значок, температура, город; под ней — описание
    float DrawHeader(ImDrawList* dl, const Weather::Today& w, ImVec2 p, float width, float a) {
        DrawSky(dl, w.sky, w.night, ImVec2(p.x + Px(20), p.y + Px(21)), Px(40), a);
        const std::string temp = Temp(w.temperature);
        Widgets::Text(dl, Theme::hero, ImVec2(p.x + Px(46), p.y + Px(2)), Fade(Theme::kInk, a), temp.c_str());
        const float cityWidth = Widgets::TextSize(Theme::semibold, w.city.c_str()).x;
        Widgets::Text(dl, Theme::semibold, ImVec2(p.x + width - cityWidth, p.y + Px(4)), Fade(Theme::kInkMuted, a), w.city.c_str());
        Widgets::Text(dl, Theme::semibold, ImVec2(p.x, p.y + Px(kHeader)), Fade(Theme::kInkSoft, a), w.description.c_str());
        return Px(kHeader + kLine);
    }

    void DrawHour(ImDrawList* dl, const Weather::HourView& h, ImVec2 p, ImVec2 size, bool current, bool past, bool compact, float a) {
        const float cx = p.x + size.x * 0.5f;
        if (current) {
            dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Fade(IM_COL32(255, 255, 255, 18), a), Px(12));
            dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), Theme::Palette(0).WithAlpha(0.55f * a), Px(12), 0, Px(1.2f));
        }
        const float dim = past ? 0.45f : 1.0f;
        char label[16];
        if (current && compact) strcpy_s(label, "сейчас");
        else sprintf_s(label, compact ? "%02d:00" : "%02d", h.hour);
        TextCentered(dl, Theme::semibold, cx, p.y + Px(6), Fade(current ? Theme::kInk : Theme::kInkMuted, a * dim), label);
        DrawSky(dl, h.sky, h.night, ImVec2(cx, p.y + size.y * 0.45f), Px(compact ? 28.0f : 24.0f), a * dim);
        const float tempY = p.y + size.y - Px(compact ? 26.0f : 32.0f);
        TextCentered(dl, Theme::body, cx, tempY, Fade(Theme::kInk, a * dim), Temp(h.temperature));
        if (!compact && h.rainChance >= 20) {
            char rain[16];
            sprintf_s(rain, "%d%%", h.rainChance);
            TextCentered(dl, Theme::semibold, cx, tempY + Px(17), Fade(IM_COL32(125, 185, 255, 255), a * dim), rain);
        }
    }

    ImVec2 MiniSize() { return ImVec2(Px(kMiniCell * 4 + kPad * 2), Px(kPad * 2 + kHeader + kLine + 8 + kMiniCellHeight)); }
    ImVec2 FullSize() { return ImVec2(Px(kCell * kColumns + kPad * 2), Px(kPad * 2 + kHeader + kLine * 3 + 10 + kCellHeight * kRows)); }

    void DrawMini(ImDrawList* dl, const Weather::Today& w, ImVec2 p, float a) {
        const float width = MiniSize().x - Px(kPad * 2);
        const float y = p.y + DrawHeader(dl, w, p, width, a) + Px(8);
        for (size_t i = 0; i < w.around.size(); ++i) {
            const auto& h = w.around[i];
            DrawHour(dl, h, ImVec2(p.x + i * Px(kMiniCell), y), ImVec2(Px(kMiniCell - 4), Px(kMiniCellHeight)), h.hour == w.currentHour,
                h.hour == (w.currentHour + 23) % 24, true, a);
        }
    }

    void DrawFull(ImDrawList* dl, const Weather::Today& w, ImVec2 p, float a) {
        const float width = FullSize().x - Px(kPad * 2);
        float y = p.y + DrawHeader(dl, w, p, width, a);

        char line[160];
        sprintf_s(line, "ощущается как %s · ветер %d км/ч · влажность %d%%", Temp(w.feelsLike).c_str(), static_cast<int>(std::lround(w.wind)), w.humidity);
        Widgets::Text(dl, Theme::semibold, ImVec2(p.x, y), Fade(Theme::kInkMuted, a), line);
        y += Px(kLine);
        sprintf_s(line, "сегодня от %s до %s", Temp(w.low).c_str(), Temp(w.high).c_str());
        Widgets::Text(dl, Theme::semibold, ImVec2(p.x, y), Fade(Theme::kInkMuted, a), line);
        y += Px(kLine + 10);

        for (size_t i = 0; i < w.day.size() && i < kColumns * kRows; ++i) {
            const auto& h = w.day[i];
            const ImVec2 cell(p.x + (i % kColumns) * Px(kCell), y + (i / kColumns) * Px(kCellHeight));
            DrawHour(dl, h, cell, ImVec2(Px(kCell - 4), Px(kCellHeight - 4)), h.hour == w.currentHour, h.hour < w.currentHour, false, a);
        }
    }
}

float WeatherWidget::MiniWidth() { return MiniSize().x; }

namespace { ImVec2 g_boundsMin, g_boundsMax; }
void WeatherWidget::Bounds(ImVec2& min, ImVec2& max) { min = g_boundsMin, max = g_boundsMax; }

void WeatherWidget::Draw(ImDrawList* dl, float left, float centerY, float top, float bottom, bool hover, float dt) {
    const Weather::Today w = Weather::Snapshot();
    const ImVec2 mini = MiniSize(), full = FullSize();
    const float ease = g_open * g_open * (3.0f - 2.0f * g_open); // плавный старт и финиш
    ImVec2 size(mini.x + (full.x - mini.x) * ease, mini.y + (full.y - mini.y) * ease);
    if (!w.ready) size = ImVec2(mini.x, Px(52));

    // по центру сферы, но не залезая на шапку и поле ввода (раскрытой можно ниже — она поверх)
    float y = centerY - size.y * 0.5f;
    y = std::max(top, std::min(y, (g_open > 0.01f ? ImGui::GetIO().DisplaySize.y - Px(12) : bottom) - size.y));
    const ImVec2 min(left, y), max(left + size.x, y + size.y);
    g_boundsMin = min, g_boundsMax = ImVec2(left + mini.x, y + (w.ready ? mini.y : size.y));

    const bool hovered = hover && w.ready && ImGui::IsMouseHoveringRect(min, max, false);
    g_open += ((hovered ? 1.0f : 0.0f) - g_open) * std::min(1.0f, dt * 10.0f);

    dl->AddRectFilled(min, max, Theme::Surface(0.82f + 0.18f * ease), Px(20));
    dl->AddRect(min, max, IM_COL32(255, 255, 255, static_cast<int>(14 + 14 * ease)), Px(20), 0, Px(1.0f));

    const ImVec2 content(min.x + Px(kPad), min.y + Px(kPad));
    dl->PushClipRect(min, max, true);
    if (!w.ready) {
        Widgets::Text(dl, Theme::semibold, ImVec2(content.x, min.y + (size.y - Theme::semibold->LegacySize) * 0.5f), Theme::kInkMuted, "погода загружается…");
    } else if (ease < 0.5f) {
        DrawMini(dl, w, content, 1.0f - ease * 2.0f);
    } else {
        DrawFull(dl, w, content, ease * 2.0f - 1.0f);
    }
    dl->PopClipRect();
}
