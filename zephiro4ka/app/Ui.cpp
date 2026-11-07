#include "Ui.h"

#include "Brain.h"
#include "LocalAi.h"
#include "Settings.h"
#include "State.h"
#include "TextUtils.h"
#include "Theme.h"
#include "HomeLayout.h"
#include "SetupWizard.h"
#include "Visuals.h"
#include "Voice.h"
#include "Graphics.h"
#include "HomeWidgets.h"
#include "ProcessesPage.h"
#include "ScenariosPage.h"
#include "SettingsPage.h"
#include "Toast.h"
#include "Wallpaper.h"
#include "WeatherWidget.h"
#include "Widgets.h"

#include <algorithm>
#include <cfloat>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

using Theme::Mood;
using Theme::Palette;
using Theme::Px;
using Widgets::Icon;

namespace {
    HWND g_window = nullptr;
    float g_titleHeight = 56.0f;
    float g_titleButtonsLeft = 1e9f;
    ImVec2 g_navMin(0, 0), g_navMax(0, 0); // меню по центру шапки — за него окно не тянем
    float g_navX = -1.0f, g_navW = 0.0f;   // подсветка выбранной вкладки (едет плавно)

    bool g_historyOpen = false;
    float g_historyAnim = 0.0f;
    bool g_focusInput = false;
    char g_input[1024] = "";
    float g_time = 0.0f;
    float g_intro = 0.0f; // появление главного экрана при запуске: 0 → 1

    // Плавное «появление» элемента с задержкой (секунды от запуска)
    float Intro(float delay) {
        const float t = std::clamp((g_intro * 1.1f - delay) / 0.55f, 0.0f, 1.0f);
        return 1.0f - std::pow(1.0f - t, 3.0f);
    }

    struct Layout {
        ImVec2 screen;
        ImVec2 orbCenter;
        float orbRadius;
        float dockTop;
        float dockHeight;
        float dockCenterX = 0; // середина поля ввода (его можно сдвинуть)
        float dockScale = 1;
    };

    Mood CurrentMood() {
        if (g_app.speaking) return Mood::Speaking;
        if (g_app.thinking || g_app.transcribing) return Mood::Thinking;
        if (g_app.capturing || g_app.pttArmed) return Mood::Listening;
        return Mood::Idle;
    }

    std::string IdleHint(bool wakeWord) {
        return wakeWord ? "скажи «" + Config::Get().assistant + "» и вопрос — или нажми на сферу" : std::string("нажми на сферу или пробел — и говори");
    }

    // «Поверх всех окон» из настроек → окну (меняем, только когда переключили)
    void ApplyWindowOptions(const Settings& s) {
        static int s_onTop = -1, s_effect = -1;
        if (!g_window) return;
        const int onTop = s.alwaysOnTop ? 1 : 0;
        if (onTop != s_onTop) {
            s_onTop = onTop;
            SetWindowPos(g_window, onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        if (s.windowEffect != s_effect) { // акрил / Mica Windows 11 под прозрачными местами окна
            s_effect = s.windowEffect;
            const int kBackdrop[] = { 1 /* нет */, 2 /* Mica */, 3 /* акрил */ };
            DwmSetWindowAttribute(g_window, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &kBackdrop[std::clamp(s_effect, 0, 2)], sizeof(int));
        }
    }

    // ---------------------------------------------------------------- свои обои
    void SyncWallpaper(const Settings& s) {
        static std::string s_loaded;
        static bool s_first = true;
        if (s_first || s.wallpaper != s_loaded) {
            s_first = false;
            s_loaded = s.wallpaper;
            if (s.wallpaper.empty()) Wallpaper::Unload();
            else Wallpaper::Load(s.wallpaper);
        }
        std::string picked;
        if (!Wallpaper::Picked(picked)) return;
        if (!Wallpaper::Load(picked)) { Caption::Set("не получилось открыть эту картинку"); return; }
        s_loaded = picked;
        Config::Update([&](Settings& x) { x.wallpaper = picked; });
        if (s.autoColors) Wallpaper::ApplyAutoColors(true); // уже включено — просто подстраиваемся под новую картинку
        else Toast::Ask("Поставить автоматическое подстраивание под цвет обоев? (РАБОТАЕТ ТОЛЬКО В АНЕ!)", [] { Wallpaper::ApplyAutoColors(true); });
    }

    // ---------------------------------------------------------------- шапка
    // Логотип и настроение слева
    void DrawBrand(ImDrawList* dl, Mood mood) {
        const float pad = Px(22);
        const ImVec2 brand(pad, Px(14));
        const std::string title = Text::ToUtf8(Text::LowerRu(Text::ToWide(Config::Get().assistant)));
        Widgets::Text(dl, Theme::display, brand, Theme::kInk, title.c_str());
        const float brandWidth = Widgets::TextSize(Theme::display, title.c_str()).x;
        Widgets::Text(dl, Theme::semibold, ImVec2(brand.x + brandWidth + Px(4), brand.y - Px(2)),
            Palette(1).WithAlpha(0.75f + 0.25f * std::sin(g_time * 2.0f)), "✦");

        static const char* kMoodText[] = { "отдыхаю", "слушаю", "думаю", "говорю" };
        const char* label = kMoodText[static_cast<int>(mood)];
        if (mood == Mood::Idle && !g_app.micEnabled) label = "микрофон выключен";
        if (mood == Mood::Thinking && g_app.transcribing) label = "разбираю речь";
        const float pulse = mood == Mood::Idle ? 1.0f : 1.0f + 0.5f * std::fabs(std::sin(g_time * (mood == Mood::Thinking ? 7.0f : 3.5f)));
        const ImVec2 led(brand.x + brandWidth + Px(30), brand.y + Theme::display->LegacySize * 0.55f);
        dl->AddCircleFilled(led, Px(6) * pulse, Palette(0).WithAlpha(0.18f), 16);
        dl->AddCircleFilled(led, Px(3.5f), Palette(0).WithAlpha(1.0f), 16);
        Widgets::Text(dl, Theme::semibold, ImVec2(led.x + Px(10), led.y - Theme::semibold->LegacySize * 0.55f), Theme::kInkMuted, label);
    }

    // ---------------------------------------------------------------- меню в шапке: порядок, названия, что показывать
    // [layout] nav_order=0,1,2,3 · nav_label_N · nav_hide_N · nav_style (0 — значок и текст, 1 — только значки, 2 — только текст)
    struct NavTab { Icon icon; const char* label; };
    const NavTab kNavTabs[] = { { Icon::Home, "Главная" }, { Icon::Flow, "Сценарии" }, { Icon::Grid, "Процессы" }, { Icon::Gear, "Настройки" } };
    constexpr int kNavCount = 4;
    struct NavConfig { int order[kNavCount] = { 0, 1, 2, 3 }; bool hidden[kNavCount] = {}; std::string labels[kNavCount]; int style = 0; char edit[64] = ""; bool loaded = false; };
    NavConfig g_nav;

    void LoadNav() {
        if (g_nav.loaded) return;
        g_nav.loaded = true;
        const std::string order = Config::Value("layout", "nav_order", "0,1,2,3");
        int used[kNavCount] = {}, n = 0;
        for (size_t i = 0; i < order.size() && n < kNavCount; ++i)
            if (order[i] >= '0' && order[i] < '0' + kNavCount && !used[order[i] - '0']) { used[order[i] - '0'] = 1; g_nav.order[n++] = order[i] - '0'; }
        for (int t = 0; t < kNavCount && n < kNavCount; ++t) if (!used[t]) g_nav.order[n++] = t; // забытые — в конец
        for (int t = 0; t < kNavCount; ++t) {
            g_nav.labels[t] = Config::Value("layout", ("nav_label_" + std::to_string(t)).c_str(), "");
            g_nav.hidden[t] = t != 3 && Config::Value("layout", ("nav_hide_" + std::to_string(t)).c_str(), "0") == "1"; // настройки не прячем
        }
        g_nav.style = std::clamp(std::atoi(Config::Value("layout", "nav_style", "0").c_str()), 0, 2);
    }

    void SaveNav() {
        std::string order;
        for (int i = 0; i < kNavCount; ++i) order += (i ? "," : "") + std::to_string(g_nav.order[i]);
        Config::SetValue("layout", "nav_order", order);
        for (int t = 0; t < kNavCount; ++t) {
            Config::SetValue("layout", ("nav_label_" + std::to_string(t)).c_str(), g_nav.labels[t]);
            Config::SetValue("layout", ("nav_hide_" + std::to_string(t)).c_str(), g_nav.hidden[t] ? "1" : "0");
        }
        Config::SetValue("layout", "nav_style", std::to_string(g_nav.style));
    }

    std::string NavLabel(int tab) { return g_nav.labels[tab].empty() ? kNavTabs[tab].label : g_nav.labels[tab]; }

    std::vector<int> g_navVisible; // какие вкладки нарисованы (номера вкладок по порядку) — для правки

    void MoveNavTab(int from, int to) { // from/to — места среди видимых
        if (from < 0 || to < 0 || from >= static_cast<int>(g_navVisible.size()) || to >= static_cast<int>(g_navVisible.size())) return;
        std::vector<int> order(g_nav.order, g_nav.order + kNavCount);
        const int tab = g_navVisible[from], other = g_navVisible[to];
        order.erase(std::find(order.begin(), order.end(), tab));
        auto at = std::find(order.begin(), order.end(), other);
        order.insert(to > from ? at + 1 : at, tab);
        std::copy(order.begin(), order.end(), g_nav.order);
        SaveNav();
    }

    void NavTabMenu(int place) {
        if (place < 0 || place >= static_cast<int>(g_navVisible.size())) return;
        const int tab = g_navVisible[place];
        if (ImGui::IsWindowAppearing()) snprintf(g_nav.edit, sizeof(g_nav.edit), "%s", NavLabel(tab).c_str());
        ImGui::TextDisabled("название вкладки");
        ImGui::SetNextItemWidth(Px(200));
        if (ImGui::InputTextWithHint("##navLabel", kNavTabs[tab].label, g_nav.edit, sizeof(g_nav.edit))) {
            const std::string text = Text::Trim(g_nav.edit);
            g_nav.labels[tab] = text == kNavTabs[tab].label ? std::string() : text;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveNav();
        if (tab != 3) {
            bool shown = !g_nav.hidden[tab];
            if (ImGui::Checkbox("показывать вкладку", &shown)) { g_nav.hidden[tab] = !shown; SaveNav(); ImGui::CloseCurrentPopup(); }
        } else {
            ImGui::TextDisabled("«Настройки» не прячется — иначе не выключить правку");
        }
        ImGui::Separator();
        ImGui::TextDisabled("все вкладки");
        static const char* kStyles[] = { "значок и текст", "только значки", "только текст" };
        for (int s = 0; s < 3; ++s)
            if (ImGui::RadioButton(kStyles[s], g_nav.style == s)) { g_nav.style = s; SaveNav(); }
        ImGui::Separator();
        if (ImGui::Button("вернуть меню как было")) {
            for (int t = 0; t < kNavCount; ++t) { g_nav.order[t] = t; g_nav.hidden[t] = false; g_nav.labels[t].clear(); }
            g_nav.style = 0;
            SaveNav();
            ImGui::CloseCurrentPopup();
        }
    }

    // Меню по центру: Главная · Сценарии · Процессы · Настройки, выбранная вкладка подсвечена и подсветка плавно переезжает.
    // В «Полной кастомизации» остров двигается и растягивается, вкладки переставляются и переименовываются.
    void DrawNavigation(ImDrawList* dl, const Layout& L, float dt) {
        LoadNav();
        const int open = SettingsPage::IsOpen() ? 3 : ProcessesPage::IsOpen() ? 2 : ScenariosPage::IsOpen() ? 1 : 0;
        struct Tab { Icon icon; std::string label; int id; };
        std::vector<Tab> tabs;
        for (int i = 0; i < kNavCount; ++i) {
            const int id = g_nav.order[i];
            if (!g_nav.hidden[id] || id == open) tabs.push_back({ kNavTabs[id].icon, NavLabel(id), id });
        }
        g_navVisible.clear();
        for (const Tab& t : tabs) g_navVisible.push_back(t.id);
        const int kCount = static_cast<int>(tabs.size());
        int selected = 0;
        for (int i = 0; i < kCount; ++i) if (tabs[i].id == open) selected = i;
        const float scale = HomeLayout::Scale("nav");
        const ImVec2 shift = HomeLayout::Shift("nav", L.screen);
        const float fontSize = Theme::semibold->LegacySize * scale;
        auto textWidth = [&](const std::string& t) { return Theme::semibold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, t.c_str()).x; };

        // узкое окно — только значки
        const float iconSize = Px(14) * scale, gap = Px(8) * scale, padX = Px(14) * scale, height = Px(34) * scale;
        const bool showIcon = g_nav.style != 2, showText = g_nav.style != 1;
        auto tabWidth = [&](const Tab& t, bool compact) {
            if (compact || !showText) return height;
            return padX * 2 + (showIcon ? iconSize + gap : 0.0f) + textWidth(t.label);
        };
        float full = 0.0f;
        for (const Tab& t : tabs) full += tabWidth(t, false);
        const bool compact = showText && showIcon && full + Px(8) > L.screen.x - Px(520) && !HomeLayout::Moved("nav");

        std::vector<float> widths(kCount);
        float total = Px(8) * scale;
        for (int i = 0; i < kCount; ++i) { widths[i] = tabWidth(tabs[i], compact); total += widths[i]; }
        const ImVec2 pos(std::round(L.screen.x * 0.5f - total * 0.5f + shift.x), std::round((g_titleHeight - height - Px(8) * scale) * 0.5f + Px(4) + shift.y));
        g_navMin = pos;
        g_navMax = ImVec2(pos.x + total, pos.y + height + Px(8) * scale);
        dl->AddRectFilled(g_navMin, g_navMax, Theme::Surface(0.6f), (g_navMax.y - g_navMin.y) * 0.5f);
        dl->AddRect(g_navMin, g_navMax, IM_COL32(255, 255, 255, 14), (g_navMax.y - g_navMin.y) * 0.5f);
        HomeLayout::Report("nav", "меню", ImVec2(g_navMin.x - Px(6), g_navMin.y - Px(4)), ImVec2(g_navMax.x + Px(6), g_navMax.y + Px(4)), true);

        float x = pos.x + Px(4) * scale;
        float selectedX = x;
        for (int i = 0; i < selected; ++i) selectedX += widths[i];
        if (g_navX < 0) g_navX = selectedX, g_navW = widths[selected];
        const float k = std::min(1.0f, dt * 14.0f);
        g_navX += (selectedX - g_navX) * k;
        g_navW += (widths[selected] - g_navW) * k;
        const ImVec2 hiMin(g_navX, pos.y + Px(4) * scale), hiMax(g_navX + g_navW, pos.y + Px(4) * scale + height);
        dl->AddRectFilled(hiMin, hiMax, IM_COL32(255, 255, 255, 18), height * 0.5f);
        dl->AddRect(hiMin, hiMax, IM_COL32(255, 255, 255, 22), height * 0.5f);

        static float s_hover[kNavCount] = {};
        for (int i = 0; i < kCount; ++i) {
            const ImVec2 a(x, pos.y + Px(4) * scale), b(x + widths[i], pos.y + Px(4) * scale + height);
            HomeLayout::ReportChild("nav", i, a, b, { MoveNavTab, NavTabMenu });
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            const bool clicked = ImGui::InvisibleButton("##tab", ImVec2(widths[i], height));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            s_hover[i] += ((hovered ? 1.0f : 0.0f) - s_hover[i]) * std::min(1.0f, dt * 16.0f);
            if (s_hover[i] > 0.01f && i != selected) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, static_cast<int>(14 * s_hover[i])), height * 0.5f);
            if ((compact || !showText) && hovered) ImGui::SetTooltip("%s", tabs[i].label.c_str());

            const bool on = i == selected;
            const ImU32 color = on ? Theme::kInk : (s_hover[i] > 0.5f ? Theme::kInkSoft : Theme::kInkMuted);
            const bool iconOnly = compact || !showText;
            if (showIcon || iconOnly) {
                const float cx = iconOnly ? (a.x + b.x) * 0.5f : a.x + padX + iconSize * 0.5f;
                Widgets::DrawIcon(dl, tabs[i].icon, ImVec2(cx, (a.y + b.y) * 0.5f), iconSize, color);
            }
            if (!iconOnly)
                dl->AddText(Theme::semibold, fontSize, ImVec2(a.x + padX + (showIcon ? iconSize + gap : 0.0f), (a.y + b.y) * 0.5f - fontSize * 0.55f),
                    color, tabs[i].label.c_str());

            if (clicked) {
                g_historyOpen = false;
                // открыта только одна страница; повторный клик по своей вкладке — назад на главную
                const int id = tabs[i].id;
                const bool again = id == open;
                if (SettingsPage::IsOpen() && (id != 3 || again)) SettingsPage::Close();
                if (ScenariosPage::IsOpen() && (id != 1 || again)) ScenariosPage::Close();
                if (ProcessesPage::IsOpen() && (id != 2 || again)) ProcessesPage::Close();
                if (!again && id == 1) ScenariosPage::Open();
                if (!again && id == 2) ProcessesPage::Open();
                if (!again && id == 3) SettingsPage::Open();
            }
            x += widths[i];
        }
    }

    // Кнопки окна справа
    void DrawWindowButtons(const Layout& L) {
        const float size = Px(30), gap = Px(4), y = Px(8);
        const bool plates = Theme::ButtonPlates();
        float x = L.screen.x - Px(18) - size;
        if (Widgets::IconButton("##close", ImVec2(x, y), size, Icon::Close, true, plates)) PostMessageW(g_window, WM_CLOSE, 0, 0);
        x -= size + gap;
        if (Widgets::IconButton("##maximize", ImVec2(x, y), size, Icon::Maximize, false, plates))
            ShowWindow(g_window, IsZoomed(g_window) ? SW_RESTORE : SW_MAXIMIZE);
        x -= size + gap;
        if (Widgets::IconButton("##minimize", ImVec2(x, y), size, Icon::Minimize, false, plates)) ShowWindow(g_window, SW_MINIMIZE);
        g_titleButtonsLeft = x;
    }

    void DrawHeader(ImDrawList* dl, const Layout& L, Mood mood, float dt) {
        DrawBrand(dl, mood);
        DrawNavigation(dl, L, dt);
        DrawWindowButtons(L);
    }

    // ---------------------------------------------------------------- сфера и приветствие
    void DrawOrbButton(const Layout& L) {
        ImGui::SetCursorScreenPos(ImVec2(L.orbCenter.x - L.orbRadius, L.orbCenter.y - L.orbRadius));
        if (ImGui::InvisibleButton("##orb", ImVec2(L.orbRadius * 2, L.orbRadius * 2))) Brain::ToggleListening();
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    void DrawGreeting(ImDrawList* dl, const Layout& L) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        const char* hello = now.wHour < 5 ? "не спится" : now.wHour < 12 ? "доброе утро" : now.wHour < 18 ? "привет" : "добрый вечер";
        const std::string first = std::string(hello) + ", ";
        const std::string name = Config::Get().name;

        const float y = L.orbCenter.y + L.orbRadius * 1.75f;
        const float w1 = Widgets::TextSize(Theme::hero, first.c_str()).x;
        const float w2 = Widgets::TextSize(Theme::hero, name.c_str()).x;
        const float x = L.screen.x * 0.5f - (w1 + w2) * 0.5f;
        Widgets::Text(dl, Theme::hero, ImVec2(x, y), Theme::kInkSoft, first.c_str());
        Widgets::Text(dl, Theme::hero, ImVec2(x + w1, y), Theme::kInk, name.c_str());

        std::string caption;
        bool live = false;
        Caption::Get(caption, live);
        if (g_app.transcribing) {
            caption = "секундочку…";
            live = true;
        }
        Widgets::TextWrappedCentered(dl, Theme::body, L.screen.x * 0.5f, y + Theme::hero->LegacySize * 1.6f,
            std::min(L.screen.x - Px(80), Px(720)), live ? Theme::kInk : Theme::kInkMuted, caption, 3);
    }

    // ---------------------------------------------------------------- поле ввода
    void SubmitInput() {
        if (Text::Trim(g_input).empty()) return;
        Brain::Interrupt();
        Brain::Submit(g_input);
        g_input[0] = '\0';
        g_focusInput = true;
    }

    void DrawComposer(ImDrawList* dl, const Layout& L, Mood mood) {
        const float width = std::min(L.screen.x - Px(32), Px(720) * L.dockScale);
        const ImVec2 p(L.dockCenterX - width * 0.5f, L.dockTop);
        const ImVec2 max(p.x + width, p.y + L.dockHeight);
        const bool typing = ImGui::GetIO().WantTextInput;

        dl->AddRectFilled(p, max, Theme::Surface(0.92f), Px(22));
        dl->AddRect(p, max, typing ? Theme::Accent().WithAlpha(0.45f) : IM_COL32(255, 255, 255, 16), Px(22), 0, Px(1.0f));

        // кнопка микрофона: градиент, кольца-волны, когда слушает
        const float mic = Px(44);
        const ImVec2 micPos(p.x + Px(8), p.y + (L.dockHeight - mic) * 0.5f);
        const ImVec2 micCenter(micPos.x + mic * 0.5f, micPos.y + mic * 0.5f);
        ImGui::SetCursorScreenPos(micPos);
        if (ImGui::InvisibleButton("##mic", ImVec2(mic, mic))) Brain::ToggleListening();
        const bool micHovered = ImGui::IsItemHovered();
        if (mood == Mood::Listening) {
            const float wave = std::fmod(g_time * 1.2f, 1.0f);
            dl->AddCircle(micCenter, mic * (0.5f + wave * 0.35f), Palette(0).WithAlpha(0.8f * (1.0f - wave)), 32, Px(2));
        }
        Visuals::GradientRoundRect(dl, micPos, ImVec2(micPos.x + mic, micPos.y + mic), Px(16), ImVec2(micPos.x + mic * 0.25f, micPos.y + mic * 0.2f),
            Palette(0).Mix({ 255, 255, 255 }, 0.35f).WithAlpha(1.0f), Palette(1).WithAlpha(1.0f));
        if (micHovered) dl->AddRect(micPos, ImVec2(micPos.x + mic, micPos.y + mic), IM_COL32(255, 255, 255, 120), Px(16), 0, Px(1.5f));
        const bool busy = mood == Mood::Speaking || mood == Mood::Thinking;
        Widgets::DrawIcon(dl, busy ? Icon::Stop : Icon::Mic, micCenter, mic * (micHovered ? 0.48f : 0.44f), IM_COL32(18, 11, 29, 255));

        // текстовое поле без рамки
        const float send = Px(44);
        ImGui::SetCursorScreenPos(ImVec2(micPos.x + mic + Px(12), p.y + (L.dockHeight - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::PushItemWidth(width - mic - send - Px(40));
        for (ImGuiCol col : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_Border })
            ImGui::PushStyleColor(col, IM_COL32(0, 0, 0, 0));
        if (g_focusInput) {
            ImGui::SetKeyboardFocusHere();
            g_focusInput = false;
        }
        const bool enter = ImGui::InputTextWithHint("##input", "напиши что-нибудь…   (пробел — говорить голосом)", g_input, sizeof(g_input),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor(4);
        ImGui::PopItemWidth();

        const bool clicked = Widgets::IconButton("##send", ImVec2(max.x - send - Px(8), p.y + (L.dockHeight - send) * 0.5f), send, Icon::Send);
        if (enter || clicked) SubmitInput();
    }

    void DrawQuickPhrases(const Layout& L) {
        if (!Config::Get().quickPhrases) return;
        static const char* kPhrases[] = { "который час?", "скажи что-нибудь милое", "открой стим", "что ты умеешь?" };
        const float gap = Px(8);
        float total = -gap;
        for (const char* phrase : kPhrases) total += Widgets::TextSize(Theme::semibold, phrase).x + Px(28) + gap;
        if (total > L.screen.x - Px(32)) return; // окно слишком узкое — прячем подсказки

        float x = L.dockCenterX - total * 0.5f;
        const float y = L.dockTop + L.dockHeight + Px(14);
        for (int i = 0; i < IM_ARRAYSIZE(kPhrases); ++i) {
            ImGui::PushID(i);
            ImVec2 size;
            if (Widgets::Chip("##chip", ImVec2(x, y), kPhrases[i], &size)) {
                Brain::Interrupt();
                Brain::Submit(kPhrases[i]);
            }
            ImGui::PopID();
            x += size.x + gap;
        }
    }

    // ---------------------------------------------------------------- нижняя строка
    // Строка статуса: имя слева, «таблетки» AI · STT · TTS по центру (подсвечены, когда готовы), время ответа рядом
    void DrawFooter(ImDrawList* dl, const Layout& L) {
        const float y = L.screen.y - Px(30);
        Widgets::Text(dl, Theme::semibold, ImVec2(Px(22), y), Theme::kInkMuted, Config::Get().name.c_str());

        struct Chip { Icon icon; const char* label; int state; };
        const Chip chips[] = { { Icon::Spark, "AI", g_app.aiChecked ? (g_app.aiReady ? 1 : 2) : 0 },
                               { Icon::Mic, "STT", g_app.audioReady && g_app.micEnabled ? 1 : 0 },
                               { Icon::Wave, "TTS", g_app.ttsReady ? 1 : 0 } };
        const float chipH = Px(26), gap = Px(6);
        float total = -gap;
        for (const Chip& c : chips) total += Px(34) + Widgets::TextSize(Theme::semibold, c.label).x + gap;
        float x = L.screen.x * 0.5f - total * 0.5f;
        for (const Chip& c : chips) {
            const float w = Px(34) + Widgets::TextSize(Theme::semibold, c.label).x;
            const ImVec2 a(x, y - Px(5)), b(x + w, y - Px(5) + chipH);
            const bool on = c.state == 1;
            dl->AddRectFilled(a, b, on ? Theme::Accent().WithAlpha(0.16f) : IM_COL32(255, 255, 255, 8), chipH * 0.5f);
            dl->AddRect(a, b, on ? Theme::Accent().WithAlpha(0.35f) : IM_COL32(255, 255, 255, 18), chipH * 0.5f);
            const ImU32 color = c.state == 2 ? Theme::kBad : on ? Theme::kInk : Theme::kInkMuted;
            Widgets::DrawIcon(dl, c.icon, ImVec2(a.x + Px(14), (a.y + b.y) * 0.5f), Px(12), color);
            Widgets::Text(dl, Theme::semibold, ImVec2(a.x + Px(24), (a.y + b.y) * 0.5f - Theme::semibold->LegacySize * 0.5f), color, c.label);
            x += w + gap;
        }
        if (const int latency = g_app.lastLatencyMs.load(); latency >= 0) {
            char text[48];
            sprintf_s(text, "%.1f с", latency / 1000.0f);
            Widgets::Text(dl, Theme::semibold, ImVec2(x + Px(8), y), Theme::kInkMuted, text);
        }

        bool wakeWord = Config::Get().wakeWord;
        const std::string labelText = "слушать всегда (на «" + Config::Get().assistant + "»)";
        const char* label = labelText.c_str();
        const float toggleX = L.screen.x - Px(62) - Widgets::TextSize(Theme::semibold, label).x;
        if (Widgets::Toggle("##wakeword", ImVec2(toggleX, y - Px(1)), &wakeWord, Theme::Accent().WithAlpha(1.0f))) {
            Config::Update([&](Settings& s) { s.wakeWord = wakeWord; });
            Caption::Set(IdleHint(wakeWord));
        }
        Widgets::Text(dl, Theme::semibold, ImVec2(toggleX + Px(40), y), Theme::kInkMuted, label);
    }

    void HandleHotkeys() {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && !SettingsPage::IsOpen() && !ScenariosPage::IsOpen() && !ProcessesPage::IsOpen()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) Brain::ToggleListening();
            if (ImGui::IsKeyPressed(ImGuiKey_H, false)) g_historyOpen = !g_historyOpen;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (SettingsPage::IsOpen()) SettingsPage::Close();
            else if (ScenariosPage::IsOpen()) ScenariosPage::Close();
            else if (ProcessesPage::IsOpen()) ProcessesPage::Close();
            else if (g_historyOpen) g_historyOpen = false;
            else {
                Brain::Interrupt();
                g_app.pttArmed = false;
            }
        }
    }

    // ---------------------------------------------------------------- история
    void DrawHistory(const Layout& L, float dt) {
        g_historyAnim += ((g_historyOpen ? 1.0f : 0.0f) - g_historyAnim) * std::min(1.0f, dt * 12.0f);
        if (g_historyAnim < 0.01f) return;

        const float width = std::min(Px(400) * HomeLayout::Scale("history"), L.screen.x - Px(24));
        const float ease = 1.0f - std::pow(1.0f - g_historyAnim, 3.0f);
        const float shiftX = std::min(0.0f, HomeLayout::Shift("history", L.screen).x); // шторку можно отодвинуть от правого края
        const float panelX = std::max(Px(12), L.screen.x - Px(12) - width * ease + (1.0f - ease) * Px(24) + shiftX);
        ImGui::SetNextWindowPos(ImVec2(panelX, g_titleHeight + Px(4)));
        HomeLayout::Report("history", "шторка разговора", ImVec2(panelX, g_titleHeight + Px(4)), ImVec2(panelX + width, L.screen.y - Px(12)), true);
        ImGui::SetNextWindowSize(ImVec2(width, L.screen.y - g_titleHeight - Px(16)));

        ImGui::Begin("##history", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetWindowPos();
        Widgets::Text(dl, Theme::display, ImVec2(origin.x + Px(20), origin.y + Px(18)), Theme::kInk, "разговор");

        ImGui::SetCursorPos(ImVec2(width - Px(150), Px(18)));
        ImGui::PushFont(Theme::semibold, Theme::semibold->LegacySize);
        if (ImGui::Button("очистить")) {
            Chat::Clear();
            Conversation::Clear();
        }
        ImGui::PopFont();
        if (Widgets::IconButton("##historyClose", ImVec2(origin.x + width - Px(48), origin.y + Px(14)), Px(32), Icon::Close)) g_historyOpen = false;

        ImGui::SetCursorPos(ImVec2(Px(16), Px(64)));
        ImGui::BeginChild("##messages", ImVec2(width - Px(32), 0));
        const auto messages = Chat::Snapshot();
        const float contentWidth = ImGui::GetContentRegionAvail().x;
        if (messages.empty()) {
            const char* empty = "тут будет наш разговор ✦";
            ImGui::SetCursorPos(ImVec2((contentWidth - Widgets::TextSize(Theme::body, empty).x) * 0.5f, Px(120)));
            ImGui::TextDisabled("%s", empty);
        }
        for (const auto& message : messages) Widgets::ChatBubble(message, contentWidth);

        // прокрутка вниз, когда появилось новое или дописывается ответ
        static size_t s_lastCount = 0, s_lastLength = 0;
        const size_t length = messages.empty() ? 0 : messages.back().text.size();
        if (messages.size() != s_lastCount || length != s_lastLength) ImGui::SetScrollHereY(1.0f);
        s_lastCount = messages.size();
        s_lastLength = length;

        ImGui::EndChild();
        ImGui::End();
    }

}

// ---------------------------------------------------------------------
void Ui::Init(HWND window) {
    g_window = window;
    g_titleHeight = Px(64); // меню «парит» чуть ниже края окна
    Visuals::Init();
}

void Ui::Frame(float dt, bool active) {
    g_time += dt;
    g_intro = std::min(1.0f, g_intro + dt * 1.1f);
    static bool s_setupChecked = false;
    if (!s_setupChecked) { s_setupChecked = true; if (SetupWizard::Needed()) SetupWizard::Open(); } // первый запуск — знакомство
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const Mood mood = CurrentMood();
    const Settings settings = Config::Get();
    SyncWallpaper(settings);
    Theme::Apply(settings);
    Theme::UpdatePalette(mood, dt);

    Layout L;
    L.screen = screen;
    L.orbRadius = std::clamp(std::min(screen.x, screen.y) * 0.13f, Px(70), Px(150)) * (0.7f + 0.3f * Intro(0.0f)); // сфера «распускается»
    L.orbCenter = ImVec2(screen.x * 0.5f, g_titleHeight + (screen.y - g_titleHeight) * 0.36f);
    L.dockHeight = Px(60);
    L.dockTop = screen.y - Px(90) - L.dockHeight + (1.0f - Intro(0.25f)) * Px(40); // поле ввода выезжает снизу
    { // «Полная кастомизация»: свои места и размеры блоков
        const ImVec2 orb = HomeLayout::Shift("orb", screen), dock = HomeLayout::Shift("dock", screen);
        L.orbCenter = ImVec2(L.orbCenter.x + orb.x, L.orbCenter.y + orb.y);
        L.orbRadius *= HomeLayout::Scale("orb");
        L.dockTop += dock.y;
        L.dockCenterX = screen.x * 0.5f + dock.x;
        L.dockScale = HomeLayout::Scale("dock");
    }

    ImDrawList* background = ImGui::GetBackgroundDrawList();
    Visuals::BackdropOptions backdrop;
    backdrop.style = static_cast<int>(settings.backdrop);
    backdrop.speed = settings.backdropSpeed / 100.0f;
    backdrop.animate = active || settings.animateInactive;
    // акрил Windows виден только на главном экране; под страницами фон Ани плотный — сквозь них видно её, а не рабочий стол
    const float page = std::max({ SettingsPage::Visibility(), ScenariosPage::Visibility(), ProcessesPage::Visibility() });
    const float effectOpacity = Graphics::Transparent() && settings.windowEffect > 0 ? settings.windowOpacity / 100.0f : 1.0f;
    backdrop.opacity = effectOpacity + (1.0f - effectOpacity) * page;
    backdrop.wallpaperDim = settings.wallpaperDim / 100.0f;
    backdrop.customColor = settings.animationColor >= 0;
    if (backdrop.customColor) backdrop.color = Theme::FromRgb(settings.animationColor);
    Visuals::DrawBackground(background, screen, dt, backdrop);
    Visuals::DrawOrb(background, L.orbCenter, L.orbRadius, mood, g_time, dt);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(screen);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawHeader(dl, L, mood, dt);
    const bool covered = SettingsPage::Covers() || ScenariosPage::Covers() || ProcessesPage::Covers(); // под страницами главный экран не рисуем — не просвечивает и быстрее
    if (!covered) {
        DrawOrbButton(L);
        DrawGreeting(dl, L);
        DrawComposer(dl, L, mood);
        DrawQuickPhrases(L);
        DrawFooter(dl, L);
    }
    // погода слева — если между краем окна и кольцами сферы хватает места (рисуем последней: раскрытая — поверх всего)
    const float weatherLeft = Px(20) - (1.0f - Intro(0.35f)) * Px(40); // выезжает слева
    const bool pageOpen = SettingsPage::IsOpen() || ScenariosPage::IsOpen() || ProcessesPage::IsOpen();
    const bool editing = HomeLayout::Editing();
    if (!covered) {
        const float dockW = std::min(screen.x - Px(32), Px(720) * L.dockScale);
        HomeLayout::Report("orb", "сфера", ImVec2(L.orbCenter.x - L.orbRadius * 1.15f, L.orbCenter.y - L.orbRadius * 1.15f),
                           ImVec2(L.orbCenter.x + L.orbRadius * 1.15f, L.orbCenter.y + L.orbRadius * 1.15f), true);
        HomeLayout::Report("dock", "поле ввода", ImVec2(L.dockCenterX - dockW * 0.5f, L.dockTop), ImVec2(L.dockCenterX + dockW * 0.5f, L.dockTop + L.dockHeight), true);
    }
    const ImVec2 weatherShift = HomeLayout::Shift("weather", screen), widgetsShift = HomeLayout::Shift("widgets", screen);
    const bool custom = editing || HomeLayout::Moved("orb"); // свои места — показываем всё, тесноту решает сам пользователь
    const bool weatherMoved = custom || HomeLayout::Moved("weather"), widgetsMoved = custom || HomeLayout::Moved("widgets");
    if (!covered && settings.weatherWidget && (weatherMoved || L.orbCenter.x - L.orbRadius * 1.7f > Px(20) + WeatherWidget::MiniWidth() + Px(12))) {
        const float centerY = g_titleHeight + (screen.y - g_titleHeight) * 0.36f + weatherShift.y; // место сферы по умолчанию + свой сдвиг
        WeatherWidget::Draw(dl, weatherLeft + weatherShift.x, centerY, g_titleHeight + Px(8), weatherMoved ? screen.y - Px(12) : L.dockTop - Px(12), !pageOpen && !editing, dt);
        ImVec2 a, b;
        WeatherWidget::Bounds(a, b);
        HomeLayout::Report("weather", "погода", a, b, false);
    }
    // справа — «Система» и «Сейчас играет», если между сферой и краем хватает места
    if (!covered && (widgetsMoved || L.orbCenter.x + L.orbRadius * 1.7f < screen.x - Px(20) - HomeWidgets::Width() - Px(12))) {
        HomeWidgets::Draw(dl, screen.x - Px(20) + widgetsShift.x, g_titleHeight + Px(12) + widgetsShift.y, widgetsMoved ? screen.y - Px(12) : L.dockTop - Px(12), dt, Intro(0.45f));
        ImVec2 a, b;
        HomeWidgets::Bounds(a, b);
        HomeLayout::Report("widgets", "система и музыка", a, b, false);
    }
    HandleHotkeys();
    ImGui::End();

    DrawHistory(L, dt);
    HomeLayout::DrawEditor(screen, g_titleHeight, pageOpen);
    if (SettingsPage::PollReveal()) { ScenariosPage::Close(); ProcessesPage::Close(); } // «где настройка …?»
    if (ScenariosPage::PollAdded()) { SettingsPage::Close(); ProcessesPage::Close(); }   // Аня собрала сценарий
    if (SettingsPage::IsOpen() && ScenariosPage::IsOpen()) ScenariosPage::Close(); // открыта может быть только одна страница
    if (SettingsPage::IsOpen() && ProcessesPage::IsOpen()) ProcessesPage::Close();
    ScenariosPage::Draw(screen, g_titleHeight, dt);
    ProcessesPage::Draw(screen, g_titleHeight, dt);
    SettingsPage::Draw(screen, g_titleHeight, dt);
    SetupWizard::Draw(screen, g_titleHeight, dt);
    Toast::Draw(screen, dt);
    ApplyWindowOptions(settings);
}

void Ui::SetHistory(bool open) { g_historyOpen = open; }

float Ui::TitleHeight() { return g_titleHeight; } // настройки начинаются под шапкой — её можно тянуть всегда

bool Ui::IsCaption(float x, float y) {
    if (y >= g_titleHeight || x >= g_titleButtonsLeft - Px(8) || HomeLayout::HitsBlock(x, y)) return false;
    return !(x >= g_navMin.x && x <= g_navMax.x && y >= g_navMin.y && y <= g_navMax.y);
}
