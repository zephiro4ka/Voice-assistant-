#include "HomeWidgets.h"

#include "MediaNow.h"
#include "Rates.h"
#include "SystemMonitor.h"
#include "TextUtils.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using Theme::Px;
using Widgets::Icon;

namespace {
    constexpr float kWidth = 270, kPad = 16, kRadius = 20;

    float Approach(float& value, float target, float dt, float speed = 6.0f) {
        value += (target - value) * std::min(1.0f, dt * speed);
        return value;
    }

    ImU32 Fade(ImU32 color, float alpha) {
        const float a = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f);
        return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
    }

    // Строка, обрезанная по ширине с «…»
    std::string Fit(ImFont* font, const std::string& text, float width) {
        if (Widgets::TextSize(font, text.c_str()).x <= width) return text;
        std::wstring w = Text::ToWide(text);
        while (!w.empty() && Widgets::TextSize(font, (Text::ToUtf8(w) + "…").c_str()).x > width) w.pop_back();
        return Text::ToUtf8(w) + "…";
    }

    ImU32 TempColor(float t) {
        if (t < 60) return IM_COL32(110, 231, 183, 255);
        if (t < 80) return IM_COL32(250, 204, 21, 255);
        return IM_COL32(251, 113, 133, 255);
    }

    // Карточка: фон, рамка, при наведении — чуть приподнимается и светлеет. Возвращает сдвиг (подъём)
    float Card(ImDrawList* dl, ImVec2 min, ImVec2 max, float alpha, float& hover) {
        Approach(hover, ImGui::IsMouseHoveringRect(min, max, false) ? 1.0f : 0.0f, ImGui::GetIO().DeltaTime, 12.0f);
        const float lift = Px(2) * hover;
        min.y -= lift, max.y -= lift;
        dl->AddRectFilled(min, max, Fade(Theme::Surface(0.86f + 0.08f * hover), alpha), Px(kRadius));
        dl->AddRect(min, max, Theme::Overlay(static_cast<int>((14 + 18 * hover) * alpha)), Px(kRadius), 0, Px(1));
        return lift;
    }

    // ------------------------------------------------------------ «Система»
    struct Meter { float value = 0, temp = 0; };
    Meter g_cpu, g_gpu, g_ram, g_vram, g_disk;
    float g_sysHover = 0, g_mediaHover = 0;

    void MeterRow(ImDrawList* dl, ImVec2 pos, float width, const char* label, const std::string& value, float fraction, float temp, float alpha) {
        Widgets::Text(dl, Theme::semibold, pos, Fade(Theme::kInkSoft, alpha), label);
        float right = pos.x + width;
        if (temp > 0) { // температура — цветной «бейдж» справа
            char t[16];
            snprintf(t, sizeof(t), "%d°", static_cast<int>(std::lround(temp)));
            const float tw = Widgets::TextSize(Theme::semibold, t).x;
            Widgets::Text(dl, Theme::semibold, ImVec2(right - tw, pos.y), Fade(TempColor(temp), alpha), t);
            right -= tw + Px(10);
        }
        const float vw = Widgets::TextSize(Theme::semibold, value.c_str()).x;
        Widgets::Text(dl, Theme::semibold, ImVec2(right - vw, pos.y), Fade(Theme::kInk, alpha), value.c_str());

        const float barY = pos.y + Theme::semibold->LegacySize + Px(6), barH = Px(5);
        dl->AddRectFilled(ImVec2(pos.x, barY), ImVec2(pos.x + width, barY + barH), Theme::Overlay(static_cast<int>(16 * alpha)), barH);
        const float fill = std::clamp(fraction, 0.0f, 1.0f) * width;
        if (fill > 1) {
            const Theme::Color a = Theme::Accent(), b = Theme::Accent().Mix({ 244, 114, 182 }, 0.6f);
            dl->AddRectFilledMultiColor(ImVec2(pos.x, barY), ImVec2(pos.x + fill, barY + barH), a.WithAlpha(alpha), b.WithAlpha(alpha), b.WithAlpha(alpha), a.WithAlpha(alpha));
        }
    }

    void Sparkline(ImDrawList* dl, ImVec2 min, ImVec2 max, const std::array<float, SystemMonitor::kHistory>& data, ImU32 color, float alpha) {
        ImVec2 points[SystemMonitor::kHistory];
        for (int i = 0; i < SystemMonitor::kHistory; ++i) {
            const float x = min.x + (max.x - min.x) * i / (SystemMonitor::kHistory - 1);
            points[i] = ImVec2(x, max.y - (max.y - min.y) * std::clamp(data[i] / 100.0f, 0.0f, 1.0f));
        }
        // заливка под линией: столбики сверху вниз — прозрачнее
        for (int i = 0; i + 1 < SystemMonitor::kHistory; ++i)
            dl->AddQuadFilled(points[i], points[i + 1], ImVec2(points[i + 1].x, max.y), ImVec2(points[i].x, max.y), Fade(color, 0.12f * alpha));
        dl->AddPolyline(points, SystemMonitor::kHistory, Fade(color, alpha), 0, Px(1.5f));
    }

    float DrawSystem(ImDrawList* dl, ImVec2 pos, float dt, float alpha) {
        const auto s = SystemMonitor::Snapshot();
        const float width = Px(kWidth), inner = width - Px(kPad) * 2;
        const int rows = s.hasGpu ? 5 : 3;
        const float rowH = Px(34), graphH = Px(46);
        const float height = Px(kPad) * 2 + Px(26) + rows * rowH + graphH + Px(24);
        const float lift = Card(dl, pos, ImVec2(pos.x + width, pos.y + height), alpha, g_sysHover);
        ImVec2 p(pos.x + Px(kPad), pos.y + Px(kPad) - lift);

        // заголовок с «живым» огоньком
        Widgets::DrawIcon(dl, Icon::Grid, ImVec2(p.x + Px(7), p.y + Px(8)), Px(14), Fade(Theme::kInkSoft, alpha));
        Widgets::Text(dl, Theme::semibold, ImVec2(p.x + Px(22), p.y), Fade(Theme::kInk, alpha), "Система");
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f);
        dl->AddCircleFilled(ImVec2(p.x + inner - Px(4), p.y + Px(8)), Px(3.5f), Fade(IM_COL32(110, 231, 183, 255), alpha * (0.5f + 0.5f * pulse)), 12);
        p.y += Px(26);

        if (!s.ready) {
            Widgets::Text(dl, Theme::semibold, p, Fade(Theme::kInkMuted, alpha), "собираю данные…");
            return height;
        }
        char text[64];
        snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::lround(Approach(g_cpu.value, s.cpu, dt))));
        MeterRow(dl, p, inner, "Процессор", text, g_cpu.value / 100.0f, s.cpuTemp, alpha);
        p.y += rowH;
        if (s.hasGpu) {
            snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::lround(Approach(g_gpu.value, s.gpu, dt))));
            MeterRow(dl, p, inner, "Видеокарта", text, g_gpu.value / 100.0f, s.gpuTemp, alpha);
            p.y += rowH;
            snprintf(text, sizeof(text), "%.1f / %.0f ГБ", s.vramUsedGb, s.vramTotalGb);
            MeterRow(dl, p, inner, "Видеопамять", text, Approach(g_vram.value, s.vramTotalGb > 0 ? s.vramUsedGb / s.vramTotalGb : 0, dt), -1, alpha);
            p.y += rowH;
        }
        snprintf(text, sizeof(text), "%.1f / %.0f ГБ", s.ramUsedGb, s.ramTotalGb);
        MeterRow(dl, p, inner, "Память", text, Approach(g_ram.value, s.ram / 100.0f, dt), -1, alpha);
        p.y += rowH;
        snprintf(text, sizeof(text), "свободно %.0f ГБ", s.diskFreeGb);
        MeterRow(dl, p, inner, "Диск C", text, Approach(g_disk.value, s.diskTotalGb > 0 ? 1.0f - s.diskFreeGb / s.diskTotalGb : 0, dt), -1, alpha);
        p.y += rowH;

        // график загрузки за минуту
        const ImVec2 gMin(p.x, p.y + Px(2)), gMax(p.x + inner, p.y + graphH);
        dl->AddRectFilled(gMin, gMax, Theme::Overlay(static_cast<int>(8 * alpha)), Px(8));
        if (s.hasGpu) Sparkline(dl, gMin, gMax, s.gpuHistory, Theme::Accent().Mix({ 244, 114, 182 }, 0.6f).WithAlpha(0.9f), alpha);
        Sparkline(dl, gMin, gMax, s.cpuHistory, Theme::Accent().WithAlpha(1.0f), alpha);
        p.y += graphH + Px(6);
        auto speed = [](float kbps, char* out, size_t size) {
            if (kbps >= 1024) snprintf(out, size, "%.1f МБ/с", kbps / 1024);
            else snprintf(out, size, "%.0f КБ/с", kbps);
        };
        char down[24], up[24];
        speed(s.netDownKbps, down, sizeof(down));
        speed(s.netUpKbps, up, sizeof(up));
        snprintf(text, sizeof(text), "сеть  ↓ %s   ↑ %s", down, up);
        Widgets::Text(dl, Theme::semibold, p, Fade(Theme::kInkMuted, alpha), text);
        return height;
    }

    // ------------------------------------------------------------ «Сейчас играет»
    float g_progress = 0, g_bars[4] = {}, g_mediaShow = 0;

    float DrawMedia(ImDrawList* dl, ImVec2 pos, float dt, float alpha) {
        const auto t = MediaNow::Snapshot();
        Approach(g_mediaShow, t.present ? 1.0f : 0.0f, dt, 8.0f);
        if (g_mediaShow < 0.02f) return 0;
        alpha *= g_mediaShow;
        const float width = Px(kWidth), inner = width - Px(kPad) * 2, height = Px(126);
        const float lift = Card(dl, pos, ImVec2(pos.x + width, pos.y + height), alpha, g_mediaHover);
        ImVec2 p(pos.x + Px(kPad), pos.y + Px(kPad) - lift);

        // эквалайзер: прыгает, когда играет
        const float time = static_cast<float>(ImGui::GetTime());
        for (int i = 0; i < 4; ++i) {
            const float target = t.playing ? 0.35f + 0.65f * std::fabs(std::sin(time * (3.1f + i * 1.3f) + i)) : 0.2f;
            Approach(g_bars[i], target, dt, 10.0f);
            const float h = Px(12) * g_bars[i];
            dl->AddRectFilled(ImVec2(p.x + i * Px(4), p.y + Px(13) - h), ImVec2(p.x + i * Px(4) + Px(2.5f), p.y + Px(13)), Fade(Theme::Accent().WithAlpha(1.0f), alpha), Px(1));
        }
        const std::string source = t.app + (t.playing ? " · играет" : " · пауза");
        Widgets::Text(dl, Theme::semibold, ImVec2(p.x + Px(22), p.y), Fade(Theme::kInkMuted, alpha), Fit(Theme::semibold, source, inner - Px(22)).c_str());
        p.y += Px(22);
        Widgets::Text(dl, Theme::body, p, Fade(Theme::kInk, alpha), Fit(Theme::body, t.title.empty() ? "без названия" : t.title, inner).c_str());
        p.y += Theme::body->LegacySize + Px(2);
        if (!t.artist.empty()) Widgets::Text(dl, Theme::semibold, p, Fade(Theme::kInkSoft, alpha), Fit(Theme::semibold, t.artist, inner).c_str());
        p.y += Theme::semibold->LegacySize + Px(10);

        // полоска прогресса и кнопки
        const float barY = pos.y + height - Px(kPad) - Px(30) - lift;
        if (t.duration > 0) {
            Approach(g_progress, static_cast<float>(t.position / t.duration), dt, 4.0f);
            const float w = inner - Px(110);
            dl->AddRectFilled(ImVec2(p.x, barY + Px(13)), ImVec2(p.x + w, barY + Px(16)), Theme::Overlay(static_cast<int>(20 * alpha)), Px(2));
            dl->AddRectFilled(ImVec2(p.x, barY + Px(13)), ImVec2(p.x + w * std::clamp(g_progress, 0.0f, 1.0f), barY + Px(16)), Fade(Theme::Accent().WithAlpha(1.0f), alpha), Px(2));
        }
        const float size = Px(30);
        float x = pos.x + width - Px(kPad) - size;
        if (Widgets::IconButton("##mediaNext", ImVec2(x, barY), size, Icon::Next)) MediaNow::Control(MediaNow::Action::Next);
        x -= size + Px(6);
        if (Widgets::IconButton("##mediaPlay", ImVec2(x, barY), size, t.playing ? Icon::Pause : Icon::Play)) MediaNow::Control(MediaNow::Action::PlayPause);
        x -= size + Px(6);
        if (Widgets::IconButton("##mediaPrev", ImVec2(x, barY), size, Icon::Previous)) MediaNow::Control(MediaNow::Action::Previous);
        return height;
    }
}

namespace {
    // ------------------------------------------------------------ «Курсы валют»
    float g_ratesHover = 0;

    std::string Money(double value) {
        char text[32];
        if (value < 100) { snprintf(text, sizeof(text), "%.2f", value); return text; }
        snprintf(text, sizeof(text), "%.0f", value);
        std::string out = text;
        for (int at = static_cast<int>(out.size()) - 3; at > 0; at -= 3) out.insert(at, " ");
        return out;
    }

    const char* UnitName(const std::string& base) { return base == "UZS" ? " сум" : base == "RUB" ? " руб" : base == "KZT" ? " тг" : ""; }

    float RatesHeight() { return Px(kPad) * 2 + Px(26) + Px(27) * 3; }

    float DrawRates(ImDrawList* dl, ImVec2 pos, float alpha) {
        const auto data = Rates::Snapshot();
        const float width = Px(kWidth), inner = width - Px(kPad) * 2, height = RatesHeight();
        const float lift = Card(dl, pos, ImVec2(pos.x + width, pos.y + height), alpha, g_ratesHover);
        ImVec2 p(pos.x + Px(kPad), pos.y + Px(kPad) - lift);
        Widgets::Text(dl, Theme::semibold, p, Fade(Theme::kInk, alpha), "Курсы валют");
        if (!data.base.empty()) {
            const float w = Widgets::TextSize(Theme::semibold, data.base.c_str()).x;
            Widgets::Text(dl, Theme::semibold, ImVec2(p.x + inner - w, p.y), Fade(Theme::kInkMuted, alpha), data.base.c_str());
        }
        p.y += Px(26);
        if (!data.ready) {
            Widgets::Text(dl, Theme::semibold, p, Fade(Theme::kInkMuted, alpha), "загружаю…");
            return height;
        }
        for (const auto& item : data.items) {
            Widgets::Text(dl, Theme::semibold, p, Fade(Theme::kInkSoft, alpha), item.code.c_str());
            const std::string value = Money(item.value) + UnitName(data.base);
            const float vw = Widgets::TextSize(Theme::semibold, value.c_str()).x;
            Widgets::Text(dl, Theme::semibold, ImVec2(p.x + inner - vw, p.y), Fade(Theme::kInk, alpha), value.c_str());
            if (item.change != 0) {
                char change[32];
                snprintf(change, sizeof(change), "%+.2f", item.change);
                Widgets::Text(dl, Theme::semibold, ImVec2(p.x + Px(48), p.y), Fade(item.change > 0 ? IM_COL32(110, 231, 183, 255) : IM_COL32(251, 113, 133, 255), alpha), change);
            }
            p.y += Px(27);
        }
        return height;
    }
}

float HomeWidgets::Width() { return Px(kWidth); }

namespace { ImVec2 g_boundsMin, g_boundsMax; }
void HomeWidgets::Bounds(ImVec2& min, ImVec2& max) { min = g_boundsMin, max = g_boundsMax; }

void HomeWidgets::Draw(ImDrawList* dl, float right, float top, float bottom, float dt, float appear) {
    const float ease = 1.0f - std::pow(1.0f - std::clamp(appear, 0.0f, 1.0f), 3.0f);
    const float x = right - Px(kWidth) + (1.0f - ease) * Px(30); // выезжает справа
    float y = top;
    y += DrawSystem(dl, ImVec2(x, y), dt, ease) + Px(14);
    if (y + Px(126) < bottom) { const float media = DrawMedia(dl, ImVec2(x, y), dt, ease); y = media > 0 ? y + media : y - Px(14); }
    else y -= Px(14);
    if (Rates::Enabled() && y + Px(14) + RatesHeight() < bottom) {
        if (y > top) y += Px(14);
        y += DrawRates(dl, ImVec2(x, y), ease);
    }
    g_boundsMin = ImVec2(x, top), g_boundsMax = ImVec2(x + Px(kWidth), y);
}
