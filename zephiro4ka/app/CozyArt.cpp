#include "CozyArt.h"

#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <vector>

using Theme::Px;

namespace {
    constexpr float kPi = 3.14159265f;
    enum Shape { Star, Heart, Spiral, Cube, Flower, Cloud, Sun, Squiggle, Triangle, Parabola, CircleRadius, Atom, Wave, Pendulum, Lens, Benzene, Flask, Staff, Cat, Question, Hatch, Crosses, Mug, Note, Sparkle, Tangle, kShapes };

    float Hash(int a, int b) {
        const float v = std::sin(a * 127.1f + b * 311.7f) * 43758.5453f;
        return v - std::floor(v);
    }

    struct Pen {
        ImDrawList* dl;
        ImVec2 origin;
        float scale, angle;
        ImU32 color;
        int seed;

        ImVec2 At(float x, float y) const { // поворот и лёгкая дрожь руки
            const float wx = x + (Hash(seed, static_cast<int>(x * 7 + y * 13)) - 0.5f) * 0.018f;
            const float wy = y + (Hash(seed + 1, static_cast<int>(y * 7 + x * 13)) - 0.5f) * 0.018f;
            const float c = std::cos(angle), s = std::sin(angle);
            return ImVec2(origin.x + (wx * c - wy * s) * scale, origin.y + (wx * s + wy * c) * scale);
        }
        void Line(std::initializer_list<ImVec2> points) const {
            std::vector<ImVec2> out;
            for (const ImVec2& p : points) out.push_back(At(p.x, p.y));
            dl->AddPolyline(out.data(), static_cast<int>(out.size()), color, 0, Px(1.5f));
        }
        void Curve(float (*fx)(float), float (*fy)(float), int steps, float t0, float t1) const {
            std::vector<ImVec2> out;
            for (int i = 0; i <= steps; ++i) { const float t = t0 + (t1 - t0) * i / steps; out.push_back(At(fx(t), fy(t))); }
            dl->AddPolyline(out.data(), static_cast<int>(out.size()), color, 0, Px(1.5f));
        }
        void Ellipse(float cx, float cy, float rx, float ry, float tilt = 0, bool closed = true) const {
            std::vector<ImVec2> out;
            const float c = std::cos(tilt), s = std::sin(tilt);
            for (int i = 0; i <= 36; ++i) {
                const float t = i * 2 * kPi / 36, x = std::cos(t) * rx, y = std::sin(t) * ry;
                out.push_back(At(cx + x * c - y * s, cy + x * s + y * c));
            }
            (void)closed;
            dl->AddPolyline(out.data(), static_cast<int>(out.size()), color, 0, Px(1.5f));
        }
        void Label(float x, float y, const char* text, float size) const {
            const int first = dl->VtxBuffer.Size;
            const ImVec2 p = At(x, y);
            dl->AddText(Theme::body, size, p, color, text);
            const float c = std::cos(angle), s = std::sin(angle);
            for (int i = first; i < dl->VtxBuffer.Size; ++i) {
                ImVec2& v = dl->VtxBuffer[i].pos;
                const float dx = v.x - p.x, dy = v.y - p.y;
                v = ImVec2(p.x + dx * c - dy * s, p.y + dx * s + dy * c);
            }
        }
    };

    void Draw(const Pen& p, Shape shape) {
        switch (shape) {
        case Star: {
            std::vector<ImVec2> v;
            for (int i = 0; i <= 5; ++i) { const float t = -kPi / 2 + i * 4 * kPi / 5; v.push_back(ImVec2(std::cos(t) * 0.5f, std::sin(t) * 0.5f)); }
            std::vector<ImVec2> out; for (auto& a : v) out.push_back(p.At(a.x, a.y));
            p.dl->AddPolyline(out.data(), 6, p.color, 0, Px(1.5f));
            break;
        }
        case Heart: p.Curve([](float t) { const float s = std::sin(t); return s * s * s * 0.5f; },
                            [](float t) { return -(13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t)) / 32; }, 40, 0, 2 * kPi); break;
        case Spiral: p.Curve([](float t) { return std::cos(t) * t * 0.03f; }, [](float t) { return std::sin(t) * t * 0.03f; }, 80, 0, 18); break;
        case Cube:
            p.Line({ {-.4f,-.15f},{.15f,-.15f},{.15f,.4f},{-.4f,.4f},{-.4f,-.15f},{-.15f,-.4f},{.4f,-.4f},{.4f,.15f},{.15f,.4f} });
            p.Line({ {.15f,-.15f},{.4f,-.4f} });
            break;
        case Flower:
            for (int i = 0; i < 6; ++i) { const float t = i * kPi / 3; p.Ellipse(std::cos(t) * .24f, std::sin(t) * .24f, .15f, .15f); }
            p.Ellipse(0, 0, .1f, .1f);
            break;
        case Cloud:
            p.Ellipse(-.25f, .05f, .2f, .16f); p.Ellipse(.05f, -.08f, .26f, .22f); p.Ellipse(.3f, .08f, .17f, .14f);
            p.Line({ {-.4f,.18f},{.45f,.2f} });
            break;
        case Sun:
            p.Ellipse(0, 0, .2f, .2f);
            for (int i = 0; i < 10; ++i) { const float t = i * kPi / 5; p.Line({ {std::cos(t) * .28f, std::sin(t) * .28f}, {std::cos(t) * .44f, std::sin(t) * .44f} }); }
            break;
        case Squiggle: p.Curve([](float t) { return t; }, [](float t) { return std::sin(t * 18) * .12f; }, 70, -.55f, .55f); break;
        case Triangle:
            p.Line({ {-.45f,.35f},{.45f,.35f},{-.45f,-.35f},{-.45f,.35f} });
            p.Line({ {-.35f,.35f},{-.35f,.25f},{-.45f,.25f} });
            p.Label(-.62f, -.1f, "a", Px(15)); p.Label(0, .4f, "b", Px(15)); p.Label(.05f, -.2f, "c", Px(15));
            break;
        case Parabola:
            p.Line({ {-.5f,.4f},{.5f,.4f} }); p.Line({ {0,.5f},{0,-.5f} });
            p.Curve([](float t) { return t; }, [](float t) { return .4f - t * t * 3.2f; }, 30, -.45f, .45f);
            p.Label(.3f, .42f, "x", Px(14)); p.Label(.05f, -.56f, "y", Px(14));
            break;
        case CircleRadius:
            p.Ellipse(0, 0, .42f, .42f); p.Line({ {0,0},{.3f,-.29f} }); p.Label(.02f, -.26f, "R", Px(14));
            break;
        case Atom:
            for (int i = 0; i < 3; ++i) p.Ellipse(0, 0, .5f, .18f, i * kPi / 3);
            p.Ellipse(0, 0, .05f, .05f);
            break;
        case Wave:
            p.Line({ {-.55f,0},{.55f,0} });
            p.Curve([](float t) { return t; }, [](float t) { return -std::sin(t * 11) * .25f; }, 60, -.55f, .55f);
            p.Label(.3f, -.42f, "λ", Px(16));
            break;
        case Pendulum:
            p.Line({ {-.35f,-.45f},{.35f,-.45f} }); p.Line({ {0,-.45f},{.26f,.28f} }); p.Line({ {0,-.45f},{0,.3f} });
            p.Ellipse(.28f, .36f, .09f, .09f);
            break;
        case Lens:
            p.Ellipse(0, 0, .08f, .45f); p.Line({ {-.55f,-.2f},{0,-.2f},{.5f,.14f} }); p.Line({ {-.55f,.2f},{0,.2f},{.5f,-.14f} });
            break;
        case Benzene: {
            std::vector<ImVec2> out;
            for (int i = 0; i <= 6; ++i) { const float t = kPi / 6 + i * kPi / 3; out.push_back(p.At(std::cos(t) * .42f, std::sin(t) * .42f)); }
            p.dl->AddPolyline(out.data(), 7, p.color, 0, Px(1.5f));
            p.Ellipse(0, 0, .24f, .24f);
            break;
        }
        case Flask:
            p.Line({ {-.1f,-.5f},{-.1f,-.1f},{-.4f,.45f},{.4f,.45f},{.1f,-.1f},{.1f,-.5f} });
            p.Line({ {-.25f,.18f},{.25f,.18f} }); p.Ellipse(-.05f, .32f, .04f, .04f); p.Ellipse(.1f, .28f, .05f, .05f);
            break;
        case Staff:
            for (int i = 0; i < 5; ++i) { const float y = -.24f + i * .12f; p.Line({ {-.55f,y},{.55f,y} }); }
            p.Label(-.35f, -.34f, "♪", Px(24)); p.Label(.15f, -.28f, "♫", Px(24));
            break;
        case Cat:
            p.Ellipse(0, .05f, .36f, .3f);
            p.Line({ {-.32f,-.08f},{-.3f,-.42f},{-.08f,-.25f} }); p.Line({ {.32f,-.08f},{.3f,-.42f},{.08f,-.25f} });
            p.Ellipse(-.13f, 0, .03f, .03f); p.Ellipse(.13f, 0, .03f, .03f);
            p.Line({ {-.08f,.12f},{0,.17f},{.08f,.12f} });
            p.Line({ {-.5f,.04f},{-.22f,.1f} }); p.Line({ {.5f,.04f},{.22f,.1f} });
            break;
        case Question:
            p.Curve([](float t) { return std::cos(t) * .2f; }, [](float t) { return -.2f + std::sin(t) * .22f; }, 24, -kPi * 1.1f, kPi * .45f);
            p.Line({ {.04f,.01f},{.0f,.14f},{.0f,.24f} });
            p.Ellipse(0, .42f, .035f, .035f);
            break;
        case Hatch:
            for (int i = 0; i < 9; ++i) { const float x = -.42f + i * .1f, h = i % 2 ? .3f : .36f; p.Line({ {x - .08f, h}, {x + .08f, -h} }); }
            break;
        case Crosses:
            for (int i = 0; i < 3; ++i) { const float x = -.36f + i * .36f; p.Line({ {x - .13f,-.13f},{x + .13f,.13f} }); p.Line({ {x + .13f,-.13f},{x - .13f,.13f} }); }
            break;
        case Mug:
            p.Line({ {-.28f,-.12f},{-.22f,.36f},{.18f,.36f},{.24f,-.12f},{-.28f,-.12f} });
            p.Ellipse(.3f, .1f, .12f, .14f);
            p.Curve([](float t) { return -.1f + std::sin(t * 18) * .035f; }, [](float t) { return t; }, 16, -.42f, -.2f);
            p.Curve([](float t) { return .06f + std::sin(t * 18) * .035f; }, [](float t) { return t; }, 16, -.42f, -.2f);
            break;
        case Note:
            p.Ellipse(-.24f, .3f, .13f, .1f, -.3f); p.Ellipse(.22f, .22f, .13f, .1f, -.3f);
            p.Line({ {-.12f,.28f},{-.12f,-.36f},{.34f,-.44f},{.34f,.2f} }); p.Line({ {-.12f,-.22f},{.34f,-.3f} });
            break;
        case Sparkle:
            p.Curve([](float t) { const float c = std::cos(t); return c * c * c * .48f; }, [](float t) { const float n = std::sin(t); return n * n * n * .48f; }, 48, 0, 2 * kPi);
            p.Line({ {.34f,-.42f},{.34f,-.26f} }); p.Line({ {.26f,-.34f},{.42f,-.34f} });
            break;
        case Tangle:
            p.Curve([](float t) { return std::sin(t * 3) * .34f + std::cos(t * 7) * .12f; }, [](float t) { return std::cos(t * 2) * .22f + std::sin(t * 5) * .1f; }, 120, 0, 2 * kPi);
            break;
        default: break;
        }
    }

    struct Set { std::vector<const char*> texts; std::vector<Shape> shapes; };

    Set Content(int pattern) {
        switch (pattern) {
        case 1: return { { "a² + b² = c²", "∫ x dx", "π ≈ 3,14", "sin²α + cos²α = 1", "D = b² − 4ac", "y = kx + b", "√(x+1)", "Σ", "2 + 2 = 4", "lim x→0", "f(x)", "∞", "log₂ 8 = 3" }, { Triangle, Parabola, CircleRadius } };
        case 2: return { { "E = mc²", "F = ma", "v = s / t", "P = UI", "I = U / R", "A = F·s", "Q = cmΔt", "p = mv", "λ", "g ≈ 9,8", "ΔU = Q − A" }, { Atom, Wave, Pendulum, Lens } };
        case 3: return { { "H₂O", "NaCl", "CO₂", "2H₂ + O₂ → 2H₂O", "CH₄", "Fe", "Au", "pH = 7", "H₂SO₄", "O₃" }, { Benzene, Flask } };
        case 4: return { { "♪", "♫", "♬", "до ре ми", "ля-ля-ля", "𝄞" }, { Staff, Squiggle, Heart } };
        case 5: return { { "❀", "♥", "дом", "уют", "чай ☕" }, { Flower, Heart, Sun, Cloud, Cat } };
        case 6: return { {}, {} };
        case 7: return { { "E = mc²", "a² + b² = c²", "H₂O", "♪", "π", "уют", "F = ma" }, { Star, Heart, Spiral, Cube, Flower, Triangle, Atom, Benzene, Staff, Cat, Sun } };
        default: return { { "ок", "хм…", "!!!", "?!", "zzz", "ой", "ха-ха" }, { Star, Heart, Spiral, Cube, Squiggle, Cloud, Question, Hatch, Crosses, Mug, Note, Sparkle, Tangle, Spiral, Question } };
        }
    }
}

const char* CozyArt::PatternName(int index) {
    static const char* kNames[kPatterns] = { "Каракули", "Математика", "Физика", "Химия", "Музыка", "Цветы и котики", "Чистая тетрадь", "Всё вместе" };
    return kNames[std::clamp(index, 0, kPatterns - 1)];
}

void CozyArt::Background(ImDrawList* dl, ImVec2 size, int pattern) {
    const Theme::Color top = Theme::Background(0), bottom = Theme::Background(1);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), size, top.WithAlpha(1), top.WithAlpha(1), bottom.WithAlpha(1), bottom.WithAlpha(1));

    const bool paper = Theme::Paper();
    const ImU32 grid = paper ? IM_COL32(95, 135, 190, 42) : IM_COL32(235, 225, 210, 20);
    const float cell = Px(26);
    for (float x = cell; x < size.x; x += cell) dl->AddLine(ImVec2(x, 0), ImVec2(x, size.y), grid, 1.0f);
    for (float y = cell; y < size.y; y += cell) dl->AddLine(ImVec2(0, y), ImVec2(size.x, y), grid, 1.0f);
    dl->AddLine(ImVec2(Px(64), 0), ImVec2(Px(64), size.y), IM_COL32(205, 80, 80, 110), Px(1.3f)); // поля тетради

    const Set set = Content(pattern);
    const int total = static_cast<int>(set.texts.size() + set.shapes.size());
    if (total == 0) return;
    static const ImU32 kPencils[4] = { IM_COL32(52, 82, 150, 105), IM_COL32(72, 62, 58, 95), IM_COL32(185, 65, 60, 95), IM_COL32(50, 125, 88, 100) };
    static const ImU32 kChalk[4] = { IM_COL32(150, 195, 240, 95), IM_COL32(240, 232, 218, 85), IM_COL32(245, 150, 140, 90), IM_COL32(150, 220, 175, 90) };
    const ImU32* colors = paper ? kPencils : kChalk;
    const float stepX = Px(205), stepY = Px(165);
    const int cols = static_cast<int>(size.x / stepX) + 1, rows = static_cast<int>(size.y / stepY) + 1;
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) {
            const ImVec2 origin(Px(110) + x * stepX + Hash(x, y) * Px(70), Px(70) + y * stepY + Hash(y, x + 9) * Px(60));
            Pen pen{ dl, origin, Px(62 + Hash(x + 3, y) * 34), (Hash(x, y + 5) - 0.5f) * 0.55f, colors[static_cast<int>(Hash(x + 7, y + 2) * 4) % 4], x * 31 + y * 17 };
            const int kind = static_cast<int>(Hash(x + 11, y + 13) * total) % total;
            if (kind < static_cast<int>(set.texts.size())) pen.Label(-0.4f, -0.1f, set.texts[kind], Px(19 + Hash(x, y + 21) * 9));
            else Draw(pen, set.shapes[kind - set.texts.size()]);
        }
}

void CozyArt::Orb(ImDrawList* dl, ImVec2 c, float r, float time) {
    const float breathe = 1.0f + std::sin(time * 1.6f) * 0.018f;
    const float R = r * breathe;
    const Theme::Color tone = Theme::Palette(0).Mix({ 255, 214, 160 }, 0.82f);
    dl->AddCircleFilled(ImVec2(c.x + Px(5), c.y + Px(8)), R, IM_COL32(90, 65, 45, 42), 96); // тень на бумаге
    dl->AddCircleFilled(c, R, tone.WithAlpha(1), 96);
    const ImU32 ink = IM_COL32(58, 46, 38, 215);
    for (int pass = 0; pass < 2; ++pass) { // обводка в два штриха, как карандашом
        ImVec2 points[97];
        for (int i = 0; i <= 96; ++i) {
            const float t = i * 2 * kPi / 96;
            const float wobble = 1.0f + (std::sin(t * 5 + pass * 2.0f) + std::sin(t * 11 + pass)) * 0.006f + pass * 0.012f;
            points[i] = ImVec2(c.x + std::cos(t) * R * wobble, c.y + std::sin(t) * R * wobble);
        }
        dl->AddPolyline(points, 97, ink, 0, Px(pass ? 1.2f : 2.2f));
    }
    // штриховка тени справа внизу
    for (int i = 0; i < 9; ++i) {
        const float k = i / 8.0f, x = c.x + R * (0.2f + k * 0.5f), half = std::sqrt(std::max(0.0f, R * R - (x - c.x) * (x - c.x))) * 0.72f;
        dl->AddLine(ImVec2(x, c.y + half), ImVec2(x + R * 0.1f, c.y + half * 0.15f), IM_COL32(58, 46, 38, 55), Px(1.4f));
    }
    // мордочка; глаза иногда моргают
    const float blink = std::fmod(time, 4.3f) < 0.14f ? 0.12f : 1.0f;
    for (float side : { -1.0f, 1.0f })
        dl->AddEllipseFilled(ImVec2(c.x + side * R * 0.3f, c.y - R * 0.12f), ImVec2(R * 0.07f, R * 0.11f * blink), ink, 0.0f, 20);
    ImVec2 smile[21];
    for (int i = 0; i <= 20; ++i) {
        const float t = kPi * (0.18f + 0.64f * i / 20.0f);
        smile[i] = ImVec2(c.x + std::cos(t) * R * 0.34f, c.y + R * 0.05f + std::sin(t) * R * 0.3f);
    }
    dl->AddPolyline(smile, 21, ink, 0, Px(2.4f));
    for (float side : { -1.0f, 1.0f })
        dl->AddCircleFilled(ImVec2(c.x + side * R * 0.5f, c.y + R * 0.16f), R * 0.09f, IM_COL32(235, 120, 120, 90), 24);
}
