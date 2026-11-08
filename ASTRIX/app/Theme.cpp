#include "Theme.h"

#include "Platform.h"
#include "Settings.h"

#include <algorithm>
#include <cstdlib>
#include <string>

float Theme::dpi = 1.0f;
ImFont* Theme::body = nullptr;
ImFont* Theme::semibold = nullptr;
ImFont* Theme::display = nullptr;
ImFont* Theme::hero = nullptr;
ImFont* Theme::title = nullptr;
ImU32 Theme::kInk = IM_COL32(244, 239, 250, 255);
ImU32 Theme::kInkSoft = IM_COL32(244, 239, 250, 170);
ImU32 Theme::kInkMuted = IM_COL32(244, 239, 250, 110);

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

    struct FontFace { const char* name; const char* regular; const char* bold; };
    constexpr FontFace kFaces[] = {
        { "Системный (как в Windows)", "segoeui.ttf", "seguisb.ttf" }, { "Segoe UI", "segoeui.ttf", "seguisb.ttf" }, { "Consolas (как раньше)", "consola.ttf", "consolab.ttf" },
        { "Bahnschrift", "bahnschrift.ttf", "bahnschrift.ttf" }, { "Calibri", "calibri.ttf", "calibrib.ttf" }, { "Trebuchet MS", "trebuc.ttf", "trebucbd.ttf" },
        { "Verdana", "verdana.ttf", "verdanab.ttf" }, { "Georgia", "georgia.ttf", "georgiab.ttf" }, { "Times New Roman", "times.ttf", "timesbd.ttf" },
        { "Comic Sans", "comic.ttf", "comicbd.ttf" }, { "От руки (Segoe Print)", "segoepr.ttf", "segoeprb.ttf" } };
    constexpr int kFaceCount = static_cast<int>(sizeof(kFaces) / sizeof(kFaces[0]));
    constexpr int kHandwriting = kFaceCount - 1;
    int g_face = 0, g_fontScale = 100, g_cozyPattern = 0;
    float g_systemDpi = 1.0f; // масштаб Windows; Theme::dpi — он же с учётом «Размера интерфейса»
    bool g_cozyDark = false, g_paper = false;
    bool g_cozy = false, g_optionsRead = false, g_fontsDirty = false;

    std::string FontFileOf(const std::string& registryName) { // файл шрифта по его имени в Windows
        char file[260] = {};
        DWORD size = sizeof(file);
        if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", registryName.c_str(), RRF_RT_REG_SZ, nullptr, file, &size) != ERROR_SUCCESS) return {};
        const std::string name = file;
        return name.find('\\') == std::string::npos ? name : std::string();
    }

    // Шрифт сообщений из настроек Windows
    void SystemFont(std::string& regular, std::string& bold) {
        NONCLIENTMETRICSA metrics{ sizeof(metrics) };
        if (!SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) return;
        const std::string face = metrics.lfMessageFont.lfFaceName;
        const std::string plain = FontFileOf(face + " (TrueType)");
        if (plain.empty()) return;
        regular = plain;
        std::string strong = FontFileOf(face + " Semibold (TrueType)");
        if (strong.empty()) strong = FontFileOf(face + " Bold (TrueType)");
        bold = strong.empty() ? plain : strong;
    }

    // «Размер текста» из специальных возможностей Windows
    float SystemTextScale() {
        DWORD value = 100, size = sizeof(value);
        RegGetValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Accessibility", "TextScaleFactor", RRF_RT_REG_DWORD, nullptr, &value, &size);
        return std::clamp(value / 100.0f, 1.0f, 1.6f);
    }

    void ReadOptions() {
        if (g_optionsRead) return;
        g_optionsRead = true;
        g_face = std::clamp(std::atoi(Config::Value("look", "font", "0").c_str()), 0, kFaceCount - 1);
        g_fontScale = std::clamp(std::atoi(Config::Value("look", "font_scale", "100").c_str()), 80, 120);
        g_cozy = Config::Value("cozy", "enabled", "0") == "1";
        g_cozyPattern = std::atoi(Config::Value("cozy", "pattern", "0").c_str());
        g_cozyDark = Config::Value("cozy", "dark", "0") == "1";
        g_fontsDirty = true;
    }

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
        ImGui::GetStyle() = ImGuiStyle(); // масштаб не должен накапливаться при смене размера
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
    g_systemDpi = dpiScale;
    ReadOptions();
    g_fontsDirty = false;
    LoadFonts();
}

void Theme::LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    dpi = g_systemDpi * SystemTextScale() * g_fontScale / 100.0f; // весь интерфейс растёт вместе с текстом: блоки, отступы, значки
    const FontFace& chosen = kFaces[g_face == 0 && g_cozy ? kHandwriting : g_face];
    std::string regular = chosen.regular, bold = chosen.bold;
    if (g_face == 0 && !g_cozy) SystemFont(regular, bold);
    struct { const char* regular; const char* bold; } face{ regular.c_str(), bold.c_str() };
    const float k = 1.14f; // текст крупнее, а блоки того же размера
    body = LoadFont(face.regular, Px(15.5f * k));
    semibold = LoadFont(face.bold, Px(13.5f * k));
    title = LoadFont(face.bold, Px(16.0f * k));
    display = LoadFont(face.bold, Px(22.0f * k));
    hero = LoadFont(face.bold, Px(32.0f * k));
    if (!body) body = LoadFont("segoeui.ttf", Px(15.5f * k)); // выбранного шрифта нет — Segoe UI
    if (!semibold) semibold = LoadFont("seguisb.ttf", Px(13.5f * k));
    if (!title) title = semibold;
    if (!display) display = LoadFont("seguisb.ttf", Px(22.0f * k));
    if (!hero) hero = LoadFont("seguisb.ttf", Px(32.0f * k));
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
    ReadOptions();
    if (g_fontsDirty) { g_fontsDirty = false; LoadFonts(); }
    g_accent = s.accent >= kAccentPresets ? FromRgb(s.accentColor) : AccentPreset(s.accent);
    g_look = LookPreset(static_cast<int>(s.look), s.lookColor);
    if (s.backgroundColor >= 0) { // свой цвет фона поверх темы
        g_look.top = FromRgb(s.backgroundColor);
        g_look.bottom = g_look.top.Mix({ 0, 0, 0 }, 0.6f);
    }
    g_glass = s.glass;
    g_plates = s.buttonPlates;
    g_paper = g_cozy && !g_cozyDark;
    if (g_paper) {
        g_look = { { 251, 245, 230 }, { 243, 232, 209 }, { 255, 251, 241 } };
        g_accent = g_accent.Mix({ 120, 70, 45 }, 0.45f); // на бумаге нужен цвет чернил поглубже
        g_glass = false;
    } else if (g_cozy) { // ночная тетрадь: тёплый тёмный лист и мел
        g_look = { { 42, 37, 46 }, { 28, 25, 33 }, { 54, 48, 58 } };
        g_accent = g_accent.Mix({ 250, 205, 150 }, 0.45f);
        g_glass = false;
    }
    kInk = g_paper ? IM_COL32(58, 46, 38, 255) : IM_COL32(244, 239, 250, 255);
    kInkSoft = g_paper ? IM_COL32(58, 46, 38, 195) : IM_COL32(244, 239, 250, 170);
    kInkMuted = g_paper ? IM_COL32(58, 46, 38, 145) : IM_COL32(244, 239, 250, 110);

    ImVec4* c = ImGui::GetStyle().Colors;
    auto accent = [&](float a) { return ImGui::ColorConvertU32ToFloat4(g_accent.WithAlpha(a)); };
    c[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(g_look.surface.WithAlpha(g_glass ? 0.94f : 1.0f)); // панель разговора
    c[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(g_look.surface.Mix({ 0, 0, 0 }, 0.25f).WithAlpha(0.99f));
    const ImVec4 ink = ImGui::ColorConvertU32ToFloat4(kInk);
    c[ImGuiCol_Text] = ink;
    c[ImGuiCol_TextDisabled] = ImVec4(ink.x, ink.y, ink.z, g_paper ? 0.58f : 0.40f);
    c[ImGuiCol_Border] = g_paper ? ImVec4(0.3f, 0.2f, 0.12f, 0.16f) : ImVec4(1, 1, 1, 0.05f);
    const float frame = g_paper ? 0.09f : 0.27f;
    c[ImGuiCol_FrameBg] = c[ImGuiCol_Button] = ImVec4(0.25f * g_paper, 0.15f * g_paper, 0.05f * g_paper, frame);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.25f * g_paper, 0.15f * g_paper, 0.05f * g_paper, frame + 0.06f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.25f * g_paper, 0.15f * g_paper, 0.05f * g_paper, frame + 0.1f);
    c[ImGuiCol_ButtonHovered] = g_paper ? ImVec4(0.25f, 0.15f, 0.05f, 0.16f) : ImVec4(1, 1, 1, 0.09f);
    c[ImGuiCol_ButtonActive] = g_paper ? ImVec4(0.25f, 0.15f, 0.05f, 0.24f) : ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_ScrollbarGrab] = g_paper ? ImVec4(0.3f, 0.2f, 0.12f, 0.28f) : ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_ScrollbarGrabHovered] = g_paper ? ImVec4(0.3f, 0.2f, 0.12f, 0.4f) : ImVec4(1, 1, 1, 0.20f);
    c[ImGuiCol_ScrollbarGrabActive] = g_paper ? ImVec4(0.3f, 0.2f, 0.12f, 0.5f) : ImVec4(1, 1, 1, 0.28f);
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
ImU32 Theme::SurfaceSolid() { return g_look.surface.WithAlpha(1.0f); }
ImU32 Theme::Surface(float alpha) { return g_look.surface.WithAlpha(g_cozy ? 1.0f : alpha * (g_glass ? 0.82f : 1.0f)); }
ImU32 Theme::Overlay(int alpha) {
    if (!g_paper || alpha >= 90) return IM_COL32(255, 255, 255, std::clamp(alpha, 0, 255)); // яркое — ручки, текст на кнопках — остаётся белым
    return IM_COL32(70, 50, 30, std::clamp(alpha * 2, 0, 255));
}

int Theme::FontCount() { return kFaceCount; }
const char* Theme::FontName(int index) { return kFaces[std::clamp(index, 0, kFaceCount - 1)].name; }
int Theme::FontIndex() { ReadOptions(); return g_face; }
int Theme::FontScale() { ReadOptions(); return g_fontScale; }

void Theme::SetFont(int index, int scale) {
    ReadOptions();
    g_face = std::clamp(index, 0, kFaceCount - 1);
    g_fontScale = std::clamp(scale, 80, 120);
    Config::SetValue("look", "font", std::to_string(g_face));
    Config::SetValue("look", "font_scale", std::to_string(g_fontScale));
    g_fontsDirty = true;
}

bool Theme::Cozy() { ReadOptions(); return g_cozy; }
bool Theme::CozyDark() { ReadOptions(); return g_cozyDark; }
bool Theme::Paper() { return Cozy() && !g_cozyDark; }
void Theme::SetCozyDark(bool on) {
    ReadOptions();
    g_cozyDark = on;
    Config::SetValue("cozy", "dark", on ? "1" : "0");
}
int Theme::CozyPattern() { ReadOptions(); return g_cozyPattern; }

void Theme::SetCozy(bool on, int pattern) {
    ReadOptions();
    if (on != g_cozy && g_face == 0) g_fontsDirty = true; // «автоматически» на бумаге — почерк
    g_cozy = on;
    g_cozyPattern = pattern;
    Config::SetValue("cozy", "enabled", on ? "1" : "0");
    Config::SetValue("cozy", "pattern", std::to_string(pattern));
}

bool Theme::ButtonPlates() { return g_plates; }
