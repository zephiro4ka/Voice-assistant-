#include "CozyArt.h"

#include "Logo.h"
#include "Settings.h"
#include "Wallpaper.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <vector>

using Theme::Px;

namespace {
    constexpr float kPi = 3.14159265f;
    enum Shape { Star, Heart, Spiral, Cube, Flower, Cloud, Sun, Squiggle, Triangle, Parabola, CircleRadius, Atom, Wave, Pendulum, Lens, Benzene, Flask, Staff, Cat, Question, Hatch, Crosses, Mug, Note, Sparkle, Tangle, Rocket, Planet, Moon, Pizza, Donut, Cupcake, Dice, Gamepad, Book, Pencil, Fish, Paw, Ghost, Crown, Bolt, Smile, Tree, House, Arrow, kShapes };

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
        case Rocket:
            p.Line({ {0,-.5f},{.16f,-.18f},{.16f,.25f},{-.16f,.25f},{-.16f,-.18f},{0,-.5f} });
            p.Line({ {-.16f,.05f},{-.32f,.32f},{-.16f,.25f} }); p.Line({ {.16f,.05f},{.32f,.32f},{.16f,.25f} });
            p.Ellipse(0, -.1f, .07f, .07f); p.Line({ {-.07f,.3f},{0,.5f},{.07f,.3f} });
            break;
        case Planet: p.Ellipse(0, 0, .26f, .26f); p.Ellipse(0, 0, .5f, .12f, -.35f); break;
        case Moon:
            p.Curve([](float t) { return std::cos(t) * .38f; }, [](float t) { return std::sin(t) * .38f; }, 30, .9f, 5.4f);
            p.Curve([](float t) { return .24f + std::cos(t) * .28f; }, [](float t) { return std::sin(t) * .3f; }, 18, 1.1f, 5.2f);
            break;
        case Pizza:
            p.Line({ {0,.48f},{-.38f,-.3f},{.38f,-.3f},{0,.48f} }); p.Curve([](float t) { return t; }, [](float t) { return -.3f - (.16f - t * t) * .5f; }, 18, -.38f, .38f);
            p.Ellipse(-.08f, -.1f, .06f, .06f); p.Ellipse(.12f, -.12f, .05f, .05f); p.Ellipse(.01f, .14f, .05f, .05f);
            break;
        case Donut:
            p.Ellipse(0, 0, .42f, .4f); p.Ellipse(0, 0, .13f, .12f);
            p.Line({ {-.25f,-.15f},{-.2f,-.08f} }); p.Line({ {.2f,-.22f},{.27f,-.17f} }); p.Line({ {.22f,.15f},{.17f,.22f} }); p.Line({ {-.2f,.2f},{-.26f,.15f} });
            break;
        case Cupcake:
            p.Line({ {-.3f,.02f},{-.2f,.45f},{.2f,.45f},{.3f,.02f} });
            p.Curve([](float t) { return std::cos(t) * .36f; }, [](float t) { return .02f - std::abs(std::sin(t)) * .32f + std::sin(t * 7) * .03f; }, 40, 0, kPi);
            p.Ellipse(0, -.38f, .07f, .07f);
            break;
        case Dice:
            p.Line({ {-.34f,-.34f},{.34f,-.34f},{.34f,.34f},{-.34f,.34f},{-.34f,-.34f} });
            p.Ellipse(-.17f, -.17f, .05f, .05f); p.Ellipse(.17f, -.17f, .05f, .05f); p.Ellipse(0, 0, .05f, .05f); p.Ellipse(-.17f, .17f, .05f, .05f); p.Ellipse(.17f, .17f, .05f, .05f);
            break;
        case Gamepad:
            p.Curve([](float t) { return std::cos(t) * .5f; }, [](float t) { return std::sin(t) * .26f; }, 36, 0, 2 * kPi);
            p.Line({ {-.3f,0},{-.14f,0} }); p.Line({ {-.22f,-.08f},{-.22f,.08f} }); p.Ellipse(.17f, -.05f, .045f, .045f); p.Ellipse(.29f, .05f, .045f, .045f);
            break;
        case Book:
            p.Line({ {0,-.28f},{-.46f,-.36f},{-.46f,.28f},{0,.38f},{.46f,.28f},{.46f,-.36f},{0,-.28f},{0,.38f} });
            p.Line({ {-.36f,-.18f},{-.1f,-.13f} }); p.Line({ {-.36f,-.02f},{-.1f,.03f} }); p.Line({ {.36f,-.18f},{.1f,-.13f} });
            break;
        case Pencil:
            p.Line({ {-.42f,.42f},{-.38f,.2f},{.26f,-.44f},{.44f,-.26f},{-.2f,.38f},{-.42f,.42f} }); p.Line({ {-.38f,.2f},{-.2f,.38f} }); p.Line({ {.18f,-.36f},{.36f,-.18f} });
            break;
        case Fish:
            p.Curve([](float t) { return std::cos(t) * .36f; }, [](float t) { return std::sin(t) * .22f; }, 36, 0, 2 * kPi);
            p.Line({ {-.34f,0},{-.56f,-.2f},{-.56f,.2f},{-.34f,0} }); p.Ellipse(.2f, -.05f, .03f, .03f);
            break;
        case Paw:
            p.Ellipse(0, .15f, .22f, .18f);
            for (float x : { -.3f, -.11f, .11f, .3f }) p.Ellipse(x, std::abs(x) > .2f ? -.08f : -.22f, .08f, .11f);
            break;
        case Ghost:
            p.Curve([](float t) { return std::cos(t) * .3f; }, [](float t) { return -.1f + std::sin(t) * .34f; }, 24, kPi, 2 * kPi);
            p.Line({ {.3f,-.1f},{.3f,.4f},{.15f,.28f},{0,.4f},{-.15f,.28f},{-.3f,.4f},{-.3f,-.1f} });
            p.Ellipse(-.11f, -.12f, .04f, .06f); p.Ellipse(.11f, -.12f, .04f, .06f);
            break;
        case Crown: p.Line({ {-.42f,.28f},{-.46f,-.26f},{-.2f,.02f},{0,-.36f},{.2f,.02f},{.46f,-.26f},{.42f,.28f},{-.42f,.28f} }); break;
        case Bolt: p.Line({ {.12f,-.5f},{-.26f,.06f},{0,.06f},{-.12f,.5f},{.28f,-.1f},{.02f,-.1f},{.12f,-.5f} }); break;
        case Smile:
            p.Ellipse(0, 0, .4f, .4f); p.Ellipse(-.14f, -.1f, .04f, .06f); p.Ellipse(.14f, -.1f, .04f, .06f);
            p.Curve([](float t) { return std::cos(t) * .22f; }, [](float t) { return .02f + std::sin(t) * .2f; }, 16, .3f, kPi - .3f);
            break;
        case Tree:
            p.Line({ {0,-.5f},{-.28f,-.12f},{-.13f,-.12f},{-.36f,.22f},{.36f,.22f},{.13f,-.12f},{.28f,-.12f},{0,-.5f} }); p.Line({ {-.07f,.22f},{-.07f,.46f},{.07f,.46f},{.07f,.22f} });
            break;
        case House:
            p.Line({ {-.44f,-.02f},{0,-.44f},{.44f,-.02f} }); p.Line({ {-.32f,-.1f},{-.32f,.42f},{.32f,.42f},{.32f,-.1f} }); p.Line({ {-.1f,.42f},{-.1f,.12f},{.1f,.12f},{.1f,.42f} });
            break;
        case Arrow:
            p.Curve([](float t) { return t; }, [](float t) { return std::sin(t * 4) * .18f; }, 30, -.5f, .38f);
            p.Line({ {.2f,-.14f},{.4f,.08f},{.14f,.2f} });
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
        case 8: return { { "ой, космос", "SOS", "★", "до Марса 3 дня" }, { Rocket, Planet, Moon, Star, Sparkle, Atom } };
        case 9: return { { "ням", "вкусно", "ещё кусочек" }, { Pizza, Donut, Cupcake, Mug, Heart } };
        case 10: return { { "GG", "win", "1 UP", "lvl 99", "ещё катку" }, { Gamepad, Dice, Crown, Bolt, Star, Ghost } };
        case 11: return { { "дз", "пять!", "2 + 2 = ?", "не забыть", "звонок" }, { Book, Pencil, Triangle, Question, Hatch, Sparkle } };
        case 12: return { { "мяу", "гав", "буль" }, { Cat, Fish, Paw, Heart, Cloud } };
        case 13: return { { "весна", "ля-ля", "тепло" }, { Tree, House, Sun, Cloud, Flower, Squiggle } };
        case 14: return { { ":)", ":D", "хи-хи", "ура", "o_O" }, { Smile, Ghost, Heart, Star, Arrow, Crown, Bolt } };
        case 7: return { { "E = mc²", "a² + b² = c²", "H₂O", "♪", "π", "уют", "F = ma" }, { Star, Heart, Spiral, Cube, Flower, Triangle, Atom, Benzene, Staff, Cat, Sun } };
        default: return { { "ок", "хм…", "!!!", "?!", "zzz", "ой", "ха-ха" }, { Star, Heart, Spiral, Cube, Squiggle, Cloud, Question, Hatch, Crosses, Mug, Note, Sparkle, Tangle, Spiral, Question, Arrow, Bolt, Smile, Crown } };
        }
    }
}

const char* CozyArt::PatternName(int index) {
    static const char* kNames[kPatterns] = { "Каракули", "Математика", "Физика", "Химия", "Музыка", "Цветы и котики", "Чистая тетрадь", "Всё вместе", "Космос", "Вкусняшки", "Игры", "Школа", "Зверята", "Природа", "Рожицы" };
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
    const ImU32 own = Theme::DoodleColor() >= 0 ? Theme::FromRgb(Theme::DoodleColor()).WithAlpha(0.5f) : 0;
    const ImU32 ownSet[4] = { own, own, own, own };
    const ImU32* colors = own ? ownSet : paper ? kPencils : kChalk;
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

namespace {
    int g_mascot = -1;
    bool g_pickRequested = false, g_customTried = false;

    void EnsureMascot() {
        if (g_mascot >= 0) return;
        g_mascot = std::clamp(std::atoi(Config::Value("cozy", "mascot", "0").c_str()), 0, CozyArt::kMascots - 1);
    }

    struct Draw2 {
        ImDrawList* dl; ImVec2 c; float r; ImU32 ink, fill;
        ImVec2 P(float x, float y) const { return ImVec2(c.x + x * r, c.y + y * r); }
        void Circle(float x, float y, float R, ImU32 color) const { dl->AddCircleFilled(P(x, y), R * r, color, 48); }
        void Ring(float x, float y, float R, float w = 2.4f) const { dl->AddCircle(P(x, y), R * r, ink, 64, Px(w)); }
        void Oval(float x, float y, float rx, float ry, ImU32 color) const { dl->AddEllipseFilled(P(x, y), ImVec2(rx * r, ry * r), color, 0.0f, 32); }
        void Poly(std::initializer_list<ImVec2> pts, ImU32 color, bool closed, float w = 2.4f) const {
            std::vector<ImVec2> out;
            for (const auto& p : pts) out.push_back(P(p.x, p.y));
            if (color) dl->AddConvexPolyFilled(out.data(), static_cast<int>(out.size()), color);
            dl->AddPolyline(out.data(), static_cast<int>(out.size()), ink, closed ? ImDrawFlags_Closed : 0, Px(w));
        }
        void Arc(float x, float y, float R, float from, float to, float w = 2.4f) const {
            dl->PathArcTo(P(x, y), R * r, from, to, 28);
            dl->PathStroke(ink, 0, Px(w));
        }
        void Eyes(float x, float y, float blink, float size = 0.085f) const {
            for (float side : { -1.0f, 1.0f }) {
                Oval(side * x, y, size, size * 1.35f * blink, ink);
                if (blink > 0.5f) Circle(side * x + size * 0.3f, y - size * 0.45f, size * 0.32f, IM_COL32(255, 255, 255, 235));
            }
        }
        void Cheeks(float x, float y) const { for (float side : { -1.0f, 1.0f }) Oval(side * x, y, 0.1f, 0.065f, IM_COL32(240, 120, 130, 120)); }
    };

    void DrawCat(const Draw2& d, float time, float blink) {
        const float tail = std::sin(time * 2.2f) * 0.18f; // хвост машет
        std::vector<ImVec2> line;
        for (int i = 0; i <= 24; ++i) { const float k = i / 24.0f; line.push_back(d.P(0.6f + k * 0.42f + tail * k, 0.62f - k * 0.95f + std::sin(k * 5.0f + time * 2.2f) * 0.05f)); }
        d.dl->AddPolyline(line.data(), 25, d.ink, 0, Px(9.0f));
        d.dl->AddPolyline(line.data(), 25, d.fill, 0, Px(5.0f));
        const float twitch = std::fmod(time, 5.1f) < 0.18f ? 0.05f : 0.0f; // ухо дёргается
        d.Poly({ {-0.74f,-0.28f},{-0.68f - twitch,-0.92f},{-0.2f,-0.66f} }, d.fill, true);
        d.Poly({ {0.74f,-0.28f},{0.68f,-0.92f},{0.2f,-0.66f} }, d.fill, true);
        d.Poly({ {-0.62f,-0.42f},{-0.6f,-0.74f},{-0.36f,-0.6f} }, IM_COL32(240, 140, 150, 170), true, 1.0f);
        d.Poly({ {0.62f,-0.42f},{0.6f,-0.74f},{0.36f,-0.6f} }, IM_COL32(240, 140, 150, 170), true, 1.0f);
        d.Circle(0, 0, 0.8f, d.fill);
        d.Ring(0, 0, 0.8f, 3.0f);
        d.Eyes(0.3f, -0.1f, blink, 0.1f);
        d.Poly({ {-0.05f,0.08f},{0.05f,0.08f},{0,0.15f} }, IM_COL32(230, 110, 130, 255), true, 1.2f);
        d.Arc(-0.1f, 0.17f, 0.1f, 0.0f, kPi, 2.6f);
        d.Arc(0.1f, 0.17f, 0.1f, 0.0f, kPi, 2.6f);
        for (float side : { -1.0f, 1.0f }) for (float dy : { -0.06f, 0.08f })
            d.Poly({ {side * 0.42f, 0.12f + dy * 0.3f},{side * 0.98f, 0.06f + dy * 1.6f} }, 0, false, 1.8f);
        d.Cheeks(0.48f, 0.22f);
    }

    void DrawGhost(const Draw2& d, float time, float blink) {
        std::vector<ImVec2> body;
        for (int i = 0; i <= 28; ++i) { const float t = kPi + kPi * i / 28.0f; body.push_back(d.P(std::cos(t) * 0.68f, -0.1f + std::sin(t) * 0.78f)); }
        for (int i = 0; i <= 44; ++i) { const float k = i / 44.0f, x = 0.68f - k * 1.36f; body.push_back(d.P(x, 0.72f + std::sin(k * 18.85f + time * 3.0f) * 0.08f)); }
        d.dl->AddConcavePolyFilled(body.data(), static_cast<int>(body.size()), d.fill);
        d.dl->AddPolyline(body.data(), static_cast<int>(body.size()), d.ink, ImDrawFlags_Closed, Px(3.0f));
        d.Eyes(0.25f, -0.2f, blink, 0.11f);
        d.Oval(0, 0.12f, 0.09f, 0.12f + std::sin(time * 2.0f) * 0.03f, d.ink);
        d.Cheeks(0.43f, 0.04f);
    }

    void Robot(const Draw2& d, float time, float blink) {
        d.Poly({ {0,-0.72f},{0,-0.98f} }, 0, false, 3.0f);
        d.Circle(0, -1.02f, 0.09f, std::fmod(time, 1.4f) < 0.7f ? IM_COL32(255, 90, 90, 255) : IM_COL32(120, 40, 40, 255));
        d.Ring(0, -1.02f, 0.09f);
        for (float side : { -1.0f, 1.0f }) d.dl->AddRectFilled(d.P(side > 0 ? 0.8f : -0.94f, -0.26f), d.P(side > 0 ? 0.94f : -0.8f, 0.26f), d.ink, 0.05f * d.r);
        d.dl->AddRectFilled(d.P(-0.82f, -0.72f), d.P(0.82f, 0.72f), d.fill, 0.24f * d.r);
        d.dl->AddRect(d.P(-0.82f, -0.72f), d.P(0.82f, 0.72f), d.ink, 0.24f * d.r, 0, Px(3.0f));
        for (float side : { -1.0f, 1.0f }) {
            d.Circle(side * 0.34f, -0.2f, 0.19f, IM_COL32(255, 255, 255, 240));
            d.Ring(side * 0.34f, -0.2f, 0.19f);
            d.Oval(side * 0.34f + std::sin(time * 0.8f) * 0.06f, -0.2f, 0.08f, 0.1f * blink, d.ink);
        }
        d.dl->AddRectFilled(d.P(-0.46f, 0.24f), d.P(0.46f, 0.52f), d.ink, 0.08f * d.r);
        for (int i = 0; i < 7; ++i) { // рот — голосовая волна
            const float x = -0.36f + i * 0.12f, h = 0.03f + 0.08f * std::fabs(std::sin(time * 5.0f + i));
            d.dl->AddRectFilled(d.P(x - 0.03f, 0.38f - h), d.P(x + 0.03f, 0.38f + h), d.fill, 0.02f * d.r);
        }
    }

    void Cocoa(const Draw2& d, float time, float blink) {
        for (int s = -1; s <= 1; ++s) { // пар
            std::vector<ImVec2> steam;
            for (int i = 0; i <= 18; ++i) { const float k = i / 18.0f; steam.push_back(d.P(s * 0.3f + std::sin(k * 7.0f + time * 2.0f + s) * 0.07f, -0.56f - k * 0.46f)); }
            d.dl->AddPolyline(steam.data(), 19, d.ink, 0, Px(2.2f));
        }
        d.dl->AddCircle(d.P(0.76f, 0.1f), 0.28f * d.r, d.ink, 40, Px(9.0f));
        d.dl->AddCircle(d.P(0.76f, 0.1f), 0.28f * d.r, d.fill, 40, Px(5.0f));
        d.dl->AddRectFilled(d.P(-0.72f, -0.5f), d.P(0.72f, 0.8f), d.fill, 0.2f * d.r);
        d.dl->AddRect(d.P(-0.72f, -0.5f), d.P(0.72f, 0.8f), d.ink, 0.2f * d.r, 0, Px(3.0f));
        d.Oval(0, -0.5f, 0.72f, 0.13f, IM_COL32(110, 70, 50, 255));
        d.Eyes(0.26f, 0.08f, blink, 0.095f);
        d.Arc(0, 0.26f, 0.14f, 0.2f, kPi - 0.2f, 2.8f);
        d.Cheeks(0.46f, 0.32f);
    }

    void PlanetBuddy(const Draw2& d, float time, float blink) {
        const float co = std::cos(-0.32f), si = std::sin(-0.32f);
        auto ring = [&](float from, float to) {
            std::vector<ImVec2> pts;
            for (int i = 0; i <= 36; ++i) { const float t = from + (to - from) * i / 36.0f, x = std::cos(t) * 1.14f, y = std::sin(t) * 0.3f; pts.push_back(d.P(x * co - y * si, x * si + y * co)); }
            d.dl->AddPolyline(pts.data(), 37, d.ink, 0, Px(3.2f));
        };
        ring(kPi, 2 * kPi);
        d.Circle(0, 0, 0.72f, d.fill);
        d.Ring(0, 0, 0.72f, 3.0f);
        ring(0, kPi);
        d.Eyes(0.27f, -0.12f, blink, 0.095f);
        d.Arc(0, 0.06f, 0.16f, 0.2f, kPi - 0.2f, 2.8f);
        d.Cheeks(0.44f, 0.1f);
        const float a = time * 0.9f, x = std::cos(a) * 1.14f, y = std::sin(a) * 0.3f; // спутник
        d.Circle(x * co - y * si, x * si + y * co, 0.07f, d.ink);
    }

    void DrawSmile(const Draw2& d, float blink) {
        d.Circle(0, 0, 0.84f, d.fill);
        d.Ring(0, 0, 0.84f, 3.0f);
        d.Eyes(0.3f, -0.14f, blink, 0.1f);
        d.Arc(0, 0.02f, 0.36f, 0.3f, kPi - 0.3f, 3.0f);
        d.Cheeks(0.52f, 0.2f);
    }
}

const char* CozyArt::MascotName(int index) {
    static const char* kNames[kMascots] = { "Котик", "Призрак", "Робот", "Кружка какао", "Планетка", "Смайлик", "Логотип ASTRIX", "Своя картинка" };
    return kNames[std::clamp(index, 0, kMascots - 1)];
}

int CozyArt::Mascot() { EnsureMascot(); return g_mascot; }

void CozyArt::SetMascot(int index) {
    g_mascot = std::clamp(index, 0, kMascots - 1);
    Config::SetValue("cozy", "mascot", std::to_string(g_mascot));
}

void CozyArt::PickCustom() {
    g_pickRequested = true;
    Wallpaper::PickAsync();
}

bool CozyArt::TakePicked(const std::string& path) {
    if (!g_pickRequested) return false;
    g_pickRequested = false;
    if (Logo::LoadCustom(path)) {
        Config::SetValue("cozy", "mascot_file", path);
        SetMascot(kMascotCustom);
    }
    return true;
}

void CozyArt::Orb(ImDrawList* dl, ImVec2 c, float r, float time) {
    EnsureMascot();
    if (g_mascot == kMascotCustom && !g_customTried) {
        g_customTried = true;
        const std::string file = Config::Value("cozy", "mascot_file", "");
        if (!file.empty()) Logo::LoadCustom(file);
    }
    const bool paper = Theme::Paper();
    const float breathe = 1.0f + std::sin(time * 1.5f) * 0.018f;
    const ImVec2 at(c.x, c.y + std::sin(time * 1.1f) * r * 0.03f); // персонаж чуть покачивается
    const float R = r * 0.98f * breathe;
    const float blink = std::fmod(time, 4.3f) < 0.14f ? 0.12f : 1.0f;

    if (g_mascot == kMascotLogo || (g_mascot == kMascotCustom && !Logo::HasCustom())) {
        const Theme::Color tone = Theme::Palette(0).Mix(paper ? Theme::Color{ 40, 25, 45 } : Theme::Color{ 255, 255, 255 }, paper ? 0.2f : 0.28f);
        Logo::DrawTinted(dl, at, r * 2.75f * breathe, tone.WithAlpha(1.0f));
        return;
    }
    if (g_mascot == kMascotCustom) {
        Logo::DrawCustom(dl, at, r * 2.1f * breathe);
        dl->AddCircle(at, r * 1.05f * breathe, Theme::Palette(0).WithAlpha(0.9f), 96, Px(3.0f));
        return;
    }
    const Draw2 d{ dl, at, R, IM_COL32(52, 40, 36, 240), Theme::Palette(0).Mix({ 255, 236, 214 }, 0.62f).WithAlpha(1.0f) };
    dl->AddEllipseFilled(ImVec2(at.x, c.y + R * 1.02f), ImVec2(R * 0.72f, R * 0.09f), IM_COL32(0, 0, 0, paper ? 45 : 110), 0.0f, 40); // тень
    switch (g_mascot) {
    case 1: DrawGhost(d, time, blink); break;
    case 2: Robot(d, time, blink); break;
    case 3: Cocoa(d, time, blink); break;
    case 4: PlanetBuddy(d, time, blink); break;
    case 5: DrawSmile(d, blink); break;
    default: DrawCat(d, time, blink); break;
    }
}
