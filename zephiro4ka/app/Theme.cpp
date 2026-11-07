#include "Theme.h"

#include "Platform.h"
#include "Settings.h"

#include <algorithm>
#include <string>

float Theme::dpi = 1.0f;
ImFont* Theme::body = nullptr;
ImFont* Theme::semibold = nullptr;
ImFont* Theme::display = nullptr;
ImFont* Theme::hero = nullptr;
ImFont* Theme::title = nullptr;

namespace {
    using Theme::Color;

    constexpr Color kPalettes[4][3] = {
        { { 167, 139, 250 }, { 244, 114, 182 }, { 99, 102, 241 } },  // отдыхаю — сирень и розовый
        { { 34, 211, 238 }, { 110, 231, 183 }, { 129, 140, 248 } },  // слушаю — бирюза и мята
        { { 251, 191, 36 }, { 192, 132, 252 }, { 244, 114, 182 } },  // думаю — золото
        { { 251, 113, 133 }, { 253, 186, 116 }, { 216, 180, 254 } }, // говорю — коралл и персик
    };
    Color g_palette[3] = { kPalettes[0][0], kPalettes[0][1], kPalettes[0][2] };
    Color g_accent = { 180, 160, 250 };
    Theme::LookColors g_look = { { 40, 9, 38 }, { 16, 3, 18 }, { 48, 15, 46 } };
    bool g_glass = true;
    bool g_plates = true;

    bool FileExists(const std::string& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

    // Шрифт Windows + Segoe UI Symbol для значков (✦ ♀ …)
    ImFont* LoadFont(const char* file, float size) {
        ImGuiIO& io = ImGui::GetIO();
        const std::string path = std::string("C:\\Windows\\Fonts\\") + file;
        if (!FileExists(path)) return nullptr;
        ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size);
        const char* symbols = "C:\\Windows\\Fonts\\seguisym.ttf";
        if (font && FileExists(symbols)) {
            ImFontConfig merge;
            merge.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(symbols, size, &merge);
        }
        return font;
    }

    void ApplyStyle() {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding = 22.0f;
        s.ChildRounding = 14.0f;
        s.FrameRounding = 10.0f;
        s.PopupRounding = 14.0f;
        s.GrabRounding = 10.0f;
        s.ScrollbarRounding = 10.0f;
        s.ScrollbarSize = 8.0f;
        s.WindowPadding = ImVec2(18, 16);
        s.FramePadding = ImVec2(12, 7);   // компактные поля и кнопки
        s.ItemSpacing = ImVec2(8, 8);
        s.WindowBorderSize = 1.0f;
        s.FrameBorderSize = 1.0f;
        s.PopupBorderSize = 1.0f;
        s.GrabMinSize = 14.0f;
        s.ScaleAllSizes(Theme::dpi);

        ImVec4* c = s.Colors;
        c[ImGuiCol_Text] = ImVec4(0.96f, 0.94f, 0.98f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.96f, 0.94f, 0.98f, 0.40f);
        c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.045f, 0.12f, 0.94f);
        c[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.055f, 0.13f, 0.98f);
        c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.05f);
        c[ImGuiCol_FrameBg] = ImVec4(0, 0, 0, 0.27f);        // поля — тёмные «таблетки»
        c[ImGuiCol_FrameBgHovered] = ImVec4(0, 0, 0, 0.36f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0, 0, 0, 0.42f);
        c[ImGuiCol_Button] = ImVec4(0, 0, 0, 0.27f);
        c[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 1, 0.09f);
        c[ImGuiCol_ButtonActive] = ImVec4(1, 1, 1, 0.14f);
        c[ImGuiCol_Header] = ImVec4(0.65f, 0.55f, 0.98f, 0.25f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.65f, 0.55f, 0.98f, 0.35f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.65f, 0.55f, 0.98f, 0.45f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.96f, 0.45f, 0.71f, 1.0f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.6f, 0.8f, 1.0f);
        c[ImGuiCol_CheckMark] = ImVec4(0.96f, 0.45f, 0.71f, 1.0f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.12f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.20f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(1, 1, 1, 0.28f);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.01f, 0.05f, 0.60f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(0.96f, 0.45f, 0.71f, 0.35f);
        c[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0);
    }
}

ImU32 Theme::Color::WithAlpha(float a) const {
    auto channel = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 255.0f)); };
    return IM_COL32(channel(r), channel(g), channel(b), channel(a * 255.0f));
}

Theme::Color Theme::Color::Mix(const Color& to, float t) const {
    return { r + (to.r - r) * t, g + (to.g - g) * t, b + (to.b - b) * t };
}

float Theme::Px(float value) { return value * dpi; }

void Theme::Init(float dpiScale) {
    dpi = dpiScale;
    ImGuiIO& io = ImGui::GetIO();
    // моноширинный стиль: Consolas — обычный для текста, жирный для подписей и заголовков (есть в любой Windows, с кириллицей)
    body = LoadFont("consola.ttf", Px(15.5f));
    semibold = LoadFont("consolab.ttf", Px(13.5f));
    title = LoadFont("consolab.ttf", Px(16.0f));
    display = LoadFont("consolab.ttf", Px(22.0f));
    hero = LoadFont("consolab.ttf", Px(32.0f));
    if (!body) body = LoadFont("segoeui.ttf", Px(16.5f)); // запасной вариант — Segoe UI
    if (!semibold) semibold = LoadFont("seguisb.ttf", Px(13.5f));
    if (!title) title = semibold;
    if (!display) display = LoadFont("bahnschrift.ttf", Px(22.0f));
    if (!hero) hero = LoadFont("segoeuil.ttf", Px(30.0f));
    if (!body) body = io.Fonts->AddFontDefault();
    if (!semibold) semibold = body;
    if (!title) title = semibold;
    if (!display) display = semibold;
    if (!hero) hero = body;
    io.FontDefault = body;
    ApplyStyle();
}

void Theme::UpdatePalette(Mood mood, float dt) {
    Color target[3] = { kPalettes[static_cast<int>(mood)][0], kPalettes[static_cast<int>(mood)][1], kPalettes[static_cast<int>(mood)][2] };
    if (mood == Mood::Idle) { // в покое сфера — любимого цвета
        target[0] = g_accent;
        target[1] = g_accent.Mix({ 244, 114, 182 }, 0.55f);
        target[2] = g_accent.Mix({ 99, 102, 241 }, 0.5f);
    }
    const float t = std::min(1.0f, dt * 2.5f);
    for (int i = 0; i < 3; ++i) g_palette[i] = g_palette[i].Mix(target[i], t);
}

const Theme::Color& Theme::Palette(int index) { return g_palette[index % 3]; }

// ---------------------------------------------------------------------- внешний вид
Theme::Color Theme::FromRgb(int rgb) {
    return { static_cast<float>((rgb >> 16) & 0xFF), static_cast<float>((rgb >> 8) & 0xFF), static_cast<float>(rgb & 0xFF) };
}

int Theme::ToRgb(const Color& c) {
    auto channel = [](float v) { return std::clamp(static_cast<int>(v + 0.5f), 0, 255); };
    return (channel(c.r) << 16) | (channel(c.g) << 8) | channel(c.b);
}

Theme::LookColors Theme::LookPreset(int look, int customRgb) {
    switch (look) {
    case 1: return { { 26, 26, 31 }, { 12, 12, 15 }, { 36, 36, 43 } };    // графит
    case 2: return { { 14, 14, 14 }, { 4, 4, 4 }, { 26, 26, 26 } };       // уголь
    case 3: return { { 10, 16, 38 }, { 3, 6, 16 }, { 18, 26, 54 } };      // полночь
    case 4: {                                                             // своя: из одного цвета
        const Color base = FromRgb(customRgb);
        return { base, base.Mix({ 0, 0, 0 }, 0.6f), base.Mix({ 255, 255, 255 }, 0.08f) };
    }
    default: return { { 40, 9, 38 }, { 16, 3, 18 }, { 48, 15, 46 } };     // ночь — сливовая
    }
}

Theme::Color Theme::AccentPreset(int index) {
    static const Color kAccents[kAccentPresets] = {
        { 180, 160, 250 }, { 125, 175, 245 }, { 110, 210, 180 }, { 250, 165, 125 }, { 245, 205, 95 }, { 192, 196, 206 } };
    return kAccents[std::clamp(index, 0, kAccentPresets - 1)];
}

void Theme::Apply(const Settings& s) {
    g_accent = s.accent >= kAccentPresets ? FromRgb(s.accentColor) : AccentPreset(s.accent);
    g_look = LookPreset(static_cast<int>(s.look), s.lookColor);
    if (s.backgroundColor >= 0) { // свой цвет фона поверх темы
        g_look.top = FromRgb(s.backgroundColor);
        g_look.bottom = g_look.top.Mix({ 0, 0, 0 }, 0.6f);
    }
    g_glass = s.glass;
    g_plates = s.buttonPlates;

    ImVec4* c = ImGui::GetStyle().Colors;
    auto accent = [&](float a) { return ImGui::ColorConvertU32ToFloat4(g_accent.WithAlpha(a)); };
    c[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(g_look.surface.WithAlpha(g_glass ? 0.94f : 1.0f)); // панель разговора
    c[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(g_look.surface.Mix({ 0, 0, 0 }, 0.25f).WithAlpha(0.99f));
    c[ImGuiCol_Header] = accent(0.25f);
    c[ImGuiCol_HeaderHovered] = accent(0.35f);
    c[ImGuiCol_HeaderActive] = accent(0.45f);
    c[ImGuiCol_SliderGrab] = accent(1.0f);
    c[ImGuiCol_SliderGrabActive] = ImGui::ColorConvertU32ToFloat4(g_accent.Mix({ 255, 255, 255 }, 0.3f).WithAlpha(1.0f));
    c[ImGuiCol_CheckMark] = accent(1.0f);
    c[ImGuiCol_TextSelectedBg] = accent(0.35f);
}

Theme::Color Theme::Accent() { return g_accent; }
Theme::Color Theme::Background(int index) { return index == 0 ? g_look.top : g_look.bottom; }
ImU32 Theme::Surface(float alpha) { return g_look.surface.WithAlpha(alpha * (g_glass ? 0.82f : 1.0f)); }
bool Theme::ButtonPlates() { return g_plates; }
