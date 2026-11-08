#include "Visuals.h"
#include "CozyArt.h"

#include "Graphics.h"
#include "Wallpaper.h"
#include "State.h"

#include "../ImGui/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <vector>

using Theme::Mood;
using Theme::Palette;
using Theme::Px;

namespace {
    struct Dust { float angle, radius, speed, depth; };
    struct Star { float x, y, depth; };
    struct WarpStar { float x, y, z; }; // звёздный полёт: z — расстояние до зрителя

    std::vector<Dust> g_dust;
    std::vector<Star> g_stars;
    std::vector<WarpStar> g_warp;
    float g_backdropTime = 0.0f; // своё время фона: с учётом скорости и паузы

    float g_energy = 0.0f;     // насколько «возбуждена» сфера, 0..1
    float g_spin = 0.0f;       // общий поворот
    float g_raysAlpha = 0.0f;  // видимость звуковых лучей
    float g_rays[AppState::kLevelHistory] = {};

    float Random01() { return static_cast<float>(std::rand()) / RAND_MAX; }

    ImVec2 OnCircle(ImVec2 c, float r, float angle) { return ImVec2(c.x + std::cos(angle) * r, c.y + std::sin(angle) * r); }

    float TargetEnergy(Mood mood, float t) {
        switch (mood) {
        case Mood::Listening: return 0.18f + std::min(1.0f, g_app.micLevel.load() * 1.3f) * 0.82f;
        case Mood::Thinking:  return 0.32f + std::sin(t * 4.0f) * 0.06f;
        case Mood::Speaking:  return 0.30f + g_app.mouth.load() * 0.55f + 0.08f * std::sin(t * 9.0f);
        default:              return 0.10f + std::sin(t * 0.8f) * 0.03f;
        }
    }

    // Мягкое свечение вокруг: «дышит» и разгорается, когда Аня слушает или говорит
    void DrawHalo(ImDrawList* dl, ImVec2 c, float r, float e, float t) {
        const float breath = 1.0f + 0.04f * std::sin(t * 1.1f);
        const float radii[] = { r * 0.6f, r * 1.25f, r * 2.1f * breath, r * 3.4f * breath };
        const ImU32 colors[] = { Palette(0).WithAlpha(0.22f + e * 0.2f), Palette(0).WithAlpha(0.12f + e * 0.12f),
                                 Palette(2).WithAlpha(0.05f + e * 0.04f), Palette(2).WithAlpha(0.0f) };
        Visuals::RadialGradient(dl, c, radii, colors, 4, 72);
    }

    // Объём шара: свет сверху-слева, к краю — глубокий цвет
    void DrawBody(ImDrawList* dl, ImVec2 c, float r, float e) {
        const ImVec2 light(c.x - r * 0.22f, c.y - r * 0.26f);
        const float radii[] = { 0.0f, r * 0.55f, r * 1.02f };
        const ImU32 colors[] = { Palette(0).Mix({ 255, 255, 255 }, 0.35f).WithAlpha(0.34f + e * 0.12f), Palette(1).WithAlpha(0.20f),
                                 Palette(2).WithAlpha(0.10f) };
        Visuals::RadialGradient(dl, light, radii, colors, 3, 64);
    }

    // «Плазма» внутри: слои, которые медленно перетекают; при голосе — живее
    void DrawPlasma(ImDrawList* dl, ImVec2 c, float r, float e, float t) {
        constexpr int kPoints = 128;
        ImVec2 points[kPoints];
        for (int layer = 0; layer < 5; ++layer) {
            const Theme::Color& color = Palette(layer);
            const float phase = layer * 1.37f;
            const float amplitude = 0.04f + e * 0.13f;
            const float direction = layer % 2 ? 1.0f : -1.0f;
            const float size = 0.92f - layer * 0.11f;
            for (int k = 0; k < kPoints; ++k) {
                const float a = k * IM_PI * 2.0f / kPoints;
                const float wave = std::sin(a * 3 + t * (0.7f + layer * 0.3f) + phase) * 0.5f +
                                   std::sin(a * 5 - t * (1.1f + layer * 0.17f) + phase * 2) * 0.3f +
                                   std::sin(a * 2 + t * 0.5f * (layer + 1)) * 0.35f;
                points[k] = OnCircle(c, r * (size + wave * amplitude), a + g_spin * 0.22f * direction);
            }
            // центр заливки гуляет внутри шара — свет внутри переливается
            const ImVec2 focus(c.x + std::cos(t * 0.6f + phase) * r * 0.25f, c.y + std::sin(t * 0.47f + phase) * r * 0.22f);
            Visuals::FanFill(dl, focus, points, kPoints, color.WithAlpha(0.26f + e * 0.08f), color.WithAlpha(0.0f));
        }
    }

    // Светящийся ободок по краю (эффект стекла) и тонкая кайма
    void DrawRim(ImDrawList* dl, ImVec2 c, float r, float e) {
        dl->AddCircle(c, r * 1.005f, Palette(0).WithAlpha(0.18f + e * 0.2f), 96, Px(5.0f));
        dl->AddCircle(c, r * 1.005f, IM_COL32(255, 255, 255, static_cast<int>(40 + e * 50)), 96, Px(1.2f));
    }

    void DrawHighlight(ImDrawList* dl, ImVec2 c, float r, float e) {
        const float radii[] = { 0.0f, r * 0.18f, r * 0.5f };
        const ImU32 colors[] = { IM_COL32(255, 255, 255, static_cast<int>(70 + e * 50)), IM_COL32(255, 255, 255, 16), IM_COL32(255, 255, 255, 0) };
        Visuals::RadialGradient(dl, ImVec2(c.x - r * 0.32f, c.y - r * 0.38f), radii, colors, 3, 40);
    }

    // Голос — плавная волна вокруг шара (слушает: громкость микрофона, говорит: её речь)
    void DrawVoiceWave(ImDrawList* dl, ImVec2 c, float r, Mood mood, float e, float t, float dt) {
        if (g_raysAlpha < 0.02f) return;
        constexpr int kCount = AppState::kLevelHistory;
        const int head = g_app.levelIndex.load();
        for (int i = 0; i < kCount; ++i) {
            float value;
            if (mood == Mood::Listening) {
                const int age = i < kCount / 2 ? i : kCount - 1 - i; // зеркально: свежий звук сверху
                value = g_app.levelHistory[(head - 1 - age) & (kCount - 1)].load();
            } else {
                value = e * (0.35f + 0.65f * std::fabs(std::sin(i * 0.7f + t * 6.0f) * std::cos(i * 0.31f - t * 3.7f)));
            }
            g_rays[i] += (value - g_rays[i]) * std::min(1.0f, dt * 14.0f);
        }
        constexpr int kPoints = 160;
        for (int ring = 0; ring < 2; ++ring) {
            ImVec2 points[kPoints];
            for (int k = 0; k < kPoints; ++k) {
                const float f = static_cast<float>(k) / kPoints * kCount;
                const int i0 = static_cast<int>(f) % kCount, i1 = (i0 + 1) % kCount;
                const float v = g_rays[i0] + (g_rays[i1] - g_rays[i0]) * (f - std::floor(f)); // сглаживание между отсчётами
                const float a = k * IM_PI * 2.0f / kPoints - IM_PI / 2.0f;
                const float rr = r * (1.14f + ring * 0.06f) + v * r * (0.22f - ring * 0.08f) * (1.0f + 0.25f * std::sin(a * 3 + t * 2.0f));
                points[k] = OnCircle(c, rr, a);
            }
            dl->AddPolyline(points, kPoints, Palette(ring).WithAlpha((0.55f - ring * 0.25f) * g_raysAlpha), ImDrawFlags_Closed, Px(1.6f - ring * 0.5f));
        }
    }

    // Одно тонкое наклонное кольцо — как у маленькой планеты
    void DrawRing(ImDrawList* dl, ImVec2 c, float r, float e) {
        constexpr int kPoints = 120;
        ImVec2 points[kPoints];
        const float rotation = -0.35f + 0.08f * std::sin(g_spin * 0.3f);
        const float rx = r * 1.62f, ry = rx * 0.24f;
        for (int k = 0; k < kPoints; ++k) {
            const float a = k * IM_PI * 2.0f / kPoints + g_spin * 0.2f;
            const float x = std::cos(a) * rx, y = std::sin(a) * ry;
            points[k] = ImVec2(c.x + x * std::cos(rotation) - y * std::sin(rotation), c.y + x * std::sin(rotation) + y * std::cos(rotation));
        }
        dl->AddPolyline(points, kPoints, Palette(1).WithAlpha(0.16f + e * 0.18f), ImDrawFlags_Closed, Px(1.1f));
    }

    void DrawDust(ImDrawList* dl, ImVec2 c, float r, float e, float t, float dt) {
        for (size_t i = 0; i < g_dust.size(); i += 2) { // реже, чем раньше — спокойнее
            auto& p = g_dust[i];
            p.angle += dt * p.speed * (0.1f + e * 0.9f);
            const float rr = r * (p.radius + e * 0.18f * std::sin(t * 2.2f + p.angle * 4.0f));
            const float twinkle = 0.3f + 0.7f * std::fabs(std::sin(t * 1.2f + p.depth * 12.0f));
            const ImVec2 pos(c.x + std::cos(p.angle) * rr, c.y + std::sin(p.angle) * rr * 0.9f);
            dl->AddCircleFilled(pos, Px(0.6f + p.depth * 1.3f), Palette(static_cast<int>(p.depth * 2.99f)).WithAlpha((0.15f + 0.45f * twinkle) * (0.45f + e)), 8);
        }
    }
}

// ---------------------------------------------------------------------
void Visuals::Init() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    for (int i = 0; i < 120; ++i)
        g_dust.push_back({ Random01() * 6.2832f, 1.22f + Random01() * 0.8f, (0.15f + Random01() * 0.7f) * (Random01() < 0.5f ? -1.0f : 1.0f), Random01() });
    for (int i = 0; i < 140; ++i)
        g_stars.push_back({ Random01(), Random01(), Random01() });
    for (int i = 0; i < 260; ++i)
        g_warp.push_back({ Random01() * 2.0f - 1.0f, Random01() * 2.0f - 1.0f, 0.05f + Random01() });
}

namespace {
    using Theme::Color;

    // Три цвета анимации: свой цвет (с оттенками) или палитра настроения
    struct Tints { Color c[3]; };
    Tints MakeTints(const Visuals::BackdropOptions& o) {
        if (!o.customColor) return { { Palette(0), Palette(1), Palette(2) } };
        return { { o.color, o.color.Mix({ 255, 255, 255 }, 0.3f), o.color.Mix({ 40, 20, 80 }, 0.35f) } };
    }

    void Stars(ImDrawList* dl, ImVec2 size, float t, float alpha, bool twinkle) {
        for (const auto& s : g_stars) {
            const float glow = twinkle ? 0.25f + 0.75f * std::fabs(std::sin(t * (0.4f + s.depth) + s.depth * 40.0f)) : 0.6f;
            dl->AddCircleFilled(ImVec2(s.x * size.x, s.y * size.y), Px(0.6f + s.depth * 0.9f),
                IM_COL32(255, 240, 255, static_cast<int>(glow * 70 * s.depth * alpha)), 6);
        }
    }

    // Сияние: медленно плывущие пятна света
    void Aurora(ImDrawList* dl, ImVec2 size, float t, const Tints& tint, float strength = 1.0f) {
        struct Spot { float x, y, radius, speed, phase; int color; float alpha; };
        static const Spot kSpots[] = {
            { 0.12f, 0.10f, 0.55f, 0.07f, 0.0f, 0, 0.16f },
            { 0.92f, 0.95f, 0.60f, 0.05f, 2.0f, 1, 0.14f },
            { 0.60f, 0.55f, 0.40f, 0.09f, 4.0f, 2, 0.08f },
        };
        const float span = std::max(size.x, size.y);
        for (const auto& a : kSpots) {
            const ImVec2 c(size.x * (a.x + 0.06f * std::sin(t * a.speed + a.phase)), size.y * (a.y + 0.07f * std::cos(t * a.speed * 1.3f + a.phase)));
            const float r = span * a.radius * (1.0f + 0.08f * std::sin(t * a.speed * 2.0f + a.phase));
            const float radii[] = { 0.0f, r * 0.45f, r };
            const float alpha = a.alpha * strength;
            const ImU32 colors[] = { tint.c[a.color].WithAlpha(alpha), tint.c[a.color].WithAlpha(alpha * 0.45f), tint.c[a.color].WithAlpha(0.0f) };
            Visuals::RadialGradient(dl, c, radii, colors, 3, 48);
        }
    }

    // Нити: пучок тонких линий, которые плавно изгибаются через весь экран
    void Threads(ImDrawList* dl, ImVec2 size, float t, const Tints& tint) {
        constexpr int kLines = 22;
        constexpr int kMaxPoints = 160;
        const int points = std::clamp(static_cast<int>(size.x / Px(14)), 24, kMaxPoints);
        ImVec2 line[kMaxPoints];
        for (int l = 0; l < kLines; ++l) {
            const float k = static_cast<float>(l) / (kLines - 1); // 0…1 поперёк пучка
            const float spread = (k - 0.5f) * Px(150);
            const Color color = tint.c[0].Mix(tint.c[1], k);
            for (int i = 0; i < points; ++i) {
                const float u = static_cast<float>(i) / (points - 1);
                const float wave = 0.16f * std::sin(u * 3.0f + t * 0.32f + k * 0.9f) + 0.06f * std::sin(u * 7.4f - t * 0.55f + k * 2.1f);
                const float fan = spread * (0.35f + 0.65f * (0.5f + 0.5f * std::sin(u * 2.4f + t * 0.21f)));
                line[i] = ImVec2(u * size.x, size.y * (0.58f + wave) + fan);
            }
            const float middle = 1.0f - std::fabs(k - 0.5f) * 1.6f;
            dl->AddPolyline(line, points, color.WithAlpha(0.05f + 0.22f * middle), 0, Px(1.3f));
        }
    }

    // Волны: несколько полупрозрачных слоёв у нижнего края
    void Waves(ImDrawList* dl, ImVec2 size, float t, const Tints& tint) {
        const ImVec2 uv = dl->_Data->TexUvWhitePixel;
        const int points = std::clamp(static_cast<int>(size.x / Px(12)), 24, 200);
        for (int layer = 0; layer < 4; ++layer) {
            const float base = size.y * (0.60f + layer * 0.09f), amplitude = Px(22.0f + layer * 8.0f);
            const ImU32 top = tint.c[layer % 3].WithAlpha(0.16f - layer * 0.02f), bottom = tint.c[layer % 3].WithAlpha(0.02f);
            dl->PrimReserve((points - 1) * 6, points * 2);
            const auto first = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
            for (int i = 0; i < points; ++i) {
                const float u = static_cast<float>(i) / (points - 1);
                const float y = base + amplitude * (std::sin(u * (2.6f + layer * 0.7f) * 3.1416f + t * (0.45f + layer * 0.12f) + layer * 1.3f) * 0.7f +
                                                    std::sin(u * 9.0f - t * 0.8f + layer) * 0.3f);
                dl->PrimWriteVtx(ImVec2(u * size.x, y), uv, top);
                dl->PrimWriteVtx(ImVec2(u * size.x, size.y), uv, bottom);
            }
            for (int i = 0; i + 1 < points; ++i) {
                const auto a = static_cast<ImDrawIdx>(first + i * 2), b = static_cast<ImDrawIdx>(a + 1);
                const auto c = static_cast<ImDrawIdx>(a + 2), d = static_cast<ImDrawIdx>(a + 3);
                dl->PrimWriteIdx(a); dl->PrimWriteIdx(b); dl->PrimWriteIdx(d);
                dl->PrimWriteIdx(a); dl->PrimWriteIdx(d); dl->PrimWriteIdx(c);
            }
        }
    }

    // Звёзды: медленный полёт сквозь звёздное поле
    void Warp(ImDrawList* dl, ImVec2 size, float step, const Tints& tint) {
        const ImVec2 c(size.x * 0.5f, size.y * 0.42f);
        const float scale = std::max(size.x, size.y) * 0.18f;
        const Color color = tint.c[0].Mix({ 255, 255, 255 }, 0.6f);
        for (auto& s : g_warp) {
            s.z -= step * 0.06f;
            if (s.z <= 0.04f) s = { Random01() * 2.0f - 1.0f, Random01() * 2.0f - 1.0f, 1.0f };
            const ImVec2 p(c.x + s.x / s.z * scale, c.y + s.y / s.z * scale);
            if (p.x < -10 || p.y < -10 || p.x > size.x + 10 || p.y > size.y + 10) continue;
            const float closeness = std::clamp(1.0f - s.z, 0.0f, 1.0f); // («near» — макрос Windows)
            dl->AddCircleFilled(p, Px(0.5f + closeness * 2.0f), color.WithAlpha(0.15f + closeness * 0.7f), 8);
        }
    }
}

void Visuals::DrawBackground(ImDrawList* dl, ImVec2 size, float dt, const BackdropOptions& o) {
    if (Theme::Cozy()) { CozyArt::Background(dl, size, Theme::CozyPattern()); return; }
    const float step = o.animate ? dt * o.speed : 0.0f;
    g_backdropTime += step;
    const float t = g_backdropTime;

    const Theme::Color top = Theme::Background(0), bottom = Theme::Background(1);
    float a = o.opacity;
    if (Wallpaper::Loaded()) { // свои обои, а поверх — затемнение цветом темы, чтобы текст читался
        Wallpaper::Draw(dl, size, o.opacity);
        a = o.opacity * o.wallpaperDim;
    }
    dl->AddRectFilledMultiColor(ImVec2(0, 0), size, top.WithAlpha(a), top.Mix(bottom, 0.4f).WithAlpha(a),
        bottom.WithAlpha(a), top.Mix(bottom, 0.6f).WithAlpha(a));

    const Tints tint = MakeTints(o);
    Graphics::BeginAdditive(dl);
    switch (o.style) {
    case 1: Aurora(dl, size, t, tint, 0.55f); Threads(dl, size, t, tint); break;
    case 2: Aurora(dl, size, t, tint, 0.5f); Waves(dl, size, t, tint); Stars(dl, size, t, 0.6f, true); break;
    case 3: Aurora(dl, size, t, tint, 0.35f); Warp(dl, size, step, tint); break;
    case 4: Stars(dl, size, t, 0.7f, false); break; // спокойный: без движения
    default: Aurora(dl, size, t, tint); Stars(dl, size, t, 1.0f, true); break;
    }
    Graphics::EndAdditive(dl);
}

void Visuals::DrawOrb(ImDrawList* dl, ImVec2 center, float radius, Mood mood, float t, float dt) {
    if (Theme::Cozy()) { CozyArt::Orb(dl, center, radius, t); return; }
    const float target = TargetEnergy(mood, t);
    g_energy += (target - g_energy) * std::min(1.0f, dt * (target > g_energy ? 14.0f : 5.0f));
    g_spin += dt * (mood == Mood::Thinking ? 2.4f : 0.3f + g_energy * 0.6f);
    const float raysTarget = (mood == Mood::Listening || mood == Mood::Speaking) ? 1.0f : 0.0f;
    g_raysAlpha += (raysTarget - g_raysAlpha) * std::min(1.0f, dt * 5.0f);

    const float e = g_energy;
    const float r = radius * (1.0f + e * 0.08f + 0.012f * std::sin(t * 1.3f)); // лёгкое «дыхание»

    Graphics::BeginAdditive(dl);
    DrawHalo(dl, center, r, e, t);
    DrawRing(dl, center, r, e);
    DrawBody(dl, center, r, e);
    DrawPlasma(dl, center, r, e, t);
    DrawRim(dl, center, r, e);
    DrawHighlight(dl, center, r, e);
    DrawVoiceWave(dl, center, r, mood, e, t, dt);
    DrawDust(dl, center, r, e, t, dt);
    Graphics::EndAdditive(dl);
}

// ---------------------------------------------------------------------
void Visuals::FanFill(ImDrawList* dl, ImVec2 focus, const ImVec2* points, int count, ImU32 inner, ImU32 outer) {
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    dl->PrimReserve(count * 3, count + 1);
    const auto base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
    dl->PrimWriteVtx(focus, uv, inner);
    for (int i = 0; i < count; ++i) dl->PrimWriteVtx(points[i], uv, outer);
    for (int i = 0; i < count; ++i) {
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + (i + 1) % count));
    }
}

void Visuals::GradientRoundRect(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, ImVec2 focus, ImU32 inner, ImU32 outer) {
    dl->PathRect(min, max, rounding);
    const std::vector<ImVec2> points(dl->_Path.Data, dl->_Path.Data + dl->_Path.Size);
    dl->PathClear();
    if (points.size() >= 3) FanFill(dl, focus, points.data(), static_cast<int>(points.size()), inner, outer);
}

void Visuals::RadialGradient(ImDrawList* dl, ImVec2 c, const float* radii, const ImU32* colors, int stops, int segments) {
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    auto ring = [&](float r, float a) { return OnCircle(c, r, a); };

    // центральный диск
    dl->PrimReserve(segments * 3, segments + 1);
    auto base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
    dl->PrimWriteVtx(c, uv, colors[0]);
    for (int i = 0; i < segments; ++i) dl->PrimWriteVtx(ring(radii[0], i * IM_PI * 2.0f / segments), uv, colors[0]);
    for (int i = 0; i < segments; ++i) {
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + (i + 1) % segments));
    }

    // кольца между соседними радиусами
    for (int s = 1; s < stops; ++s) {
        dl->PrimReserve(segments * 6, segments * 2);
        base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
        for (int i = 0; i < segments; ++i) {
            const float a = i * IM_PI * 2.0f / segments;
            dl->PrimWriteVtx(ring(radii[s - 1], a), uv, colors[s - 1]);
            dl->PrimWriteVtx(ring(radii[s], a), uv, colors[s]);
        }
        for (int i = 0; i < segments; ++i) {
            const int j = (i + 1) % segments;
            const auto in0 = static_cast<ImDrawIdx>(base + i * 2), out0 = static_cast<ImDrawIdx>(in0 + 1);
            const auto in1 = static_cast<ImDrawIdx>(base + j * 2), out1 = static_cast<ImDrawIdx>(in1 + 1);
            dl->PrimWriteIdx(in0); dl->PrimWriteIdx(out0); dl->PrimWriteIdx(out1);
            dl->PrimWriteIdx(in0); dl->PrimWriteIdx(out1); dl->PrimWriteIdx(in1);
        }
    }
}
