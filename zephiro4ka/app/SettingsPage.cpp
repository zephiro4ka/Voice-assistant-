#include "SettingsPage.h"

#include "Autostart.h"
#include "HomeLayout.h"
#include "Uninstall.h"
#include "Brain.h"
#include "DiscordRpc.h"
#include "Telegram.h"
#include "Wallpaper.h"
#include "LocalAi.h"
#include "Settings.h"
#include "TextUtils.h"
#include "Theme.h"
#include "Voice.h"
#include "Weather.h"
#include "Widgets.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>

using Theme::Px;
using Widgets::Icon;

namespace {
    // ---------------------------------------------------------------- разделы
    enum Section { General, Appearance, VoiceSection, Hearing, AiSection, SearchSection, Overlay, WidgetsSection, Connections, Customization, kSections };

    struct SectionInfo { Icon icon; const char* name; const char* about; };
    const SectionInfo kInfo[kSections] = {
        { Icon::User, "Общие", "Как тебя зовут и как Аня тебя слушает" },
        { Icon::Sun, "Внешний вид", "Тема, любимый цвет и фон главного экрана" },
        { Icon::Wave, "Голос", "Каким голосом и как быстро она говорит" },
        { Icon::Mic, "Слух", "Когда фраза закончилась и на какие звуки реагировать" },
        { Icon::Spark, "ИИ", "Характер и нейросеть для сложных вопросов" },
        { Icon::Search, "Поиск", "Где искать то, что ты просишь найти" },
        { Icon::Layers, "Оверлей", "Окно Ани поверх других программ" },
        { Icon::Grid, "Виджеты", "Карточки на главном экране" },
        { Icon::Phone, "Телефон и Discord", "Телеграм-бот для управления с телефона и статус в Discord" },
        { Icon::Gear, "Полная кастомизация", "Свои места для любых блоков, сброс настроек и удаление программы" },
    };

    bool g_open = false;
    bool g_justOpened = false;
    float g_anim = 0.0f;
    int g_section = General;
    int g_lastShown = -1;
    char g_query[128] = "";
    float g_contentAnim = 1.0f;   // появление карточек при смене раздела или поиска
    bool g_colorDirty = false;    // цвет меняли в палитре — сохраним в файл, когда палитра закроется

    char g_name[64] = "", g_spoken[64] = "", g_city[96] = "", g_toCollege[16] = "", g_backHome[16] = "";
    char g_prompt[1024] = "";
    char g_token[128] = "", g_owner[32] = "", g_discordApp[32] = "";

    std::mutex g_cityMutex;
    std::string g_cityStatus; // «ищу…», «нашла: …», «не нашла»
    bool g_citySearching = false;

    // копия строки в буфер ImGui (вместо strncpy_s — работает и на Linux)
    void Copy(char* buffer, size_t size, const std::string& text) { snprintf(buffer, size, "%s", text.c_str()); }
    template <size_t N> void Copy(char (&buffer)[N], const std::string& text) { Copy(buffer, N, text); }

    float ControlWidth(float width) { return std::min(width, Px(380)); }
    ImU32 Accent() { return Theme::Accent().WithAlpha(1.0f); }
    ImU32 PageColor(float alpha) { return Theme::Background(1).Mix(Theme::Background(0), 0.5f).WithAlpha(alpha); }

    // ---------------------------------------------------------------- маленькие помощники
    void ToggleSetting(const char* id, bool Settings::*field) {
        bool value = Config::Get().*field;
        if (Widgets::Toggle(id, ImGui::GetCursorScreenPos(), &value, Accent()))
            Config::Update([&](Settings& s) { s.*field = value; });
    }

    // Слайдер пишет в настройки на лету, а в файл — когда отпустили
    void SliderSetting(const char* id, int Settings::*field, int min, int max, float width, const char* format = "%d") {
        int value = Config::Get().*field;
        if (Widgets::Slider(id, &value, min, max, format, ControlWidth(width))) Config::Update([&](Settings& s) { s.*field = value; }, false);
        if (ImGui::IsItemDeactivatedAfterEdit()) Config::Save();
    }

    void TextSetting(const char* id, char* buffer, size_t size, float width, std::string Settings::*field, const char* fallback) {
        ImGui::SetNextItemWidth(ControlWidth(width));
        if (ImGui::InputText(id, buffer, size)) {
            const std::string value = Text::Trim(buffer);
            Config::Update([&](Settings& s) { s.*field = value.empty() ? fallback : value; });
        }
    }

    // «8:00», «08:00» → «08:00»; ерунда — пусто
    std::string NormalizeTime(const char* text) {
        const char* colon = std::strchr(text, ':');
        if (!colon || !std::isdigit(static_cast<unsigned char>(text[0])) || !std::isdigit(static_cast<unsigned char>(colon[1]))) return {};
        const int h = std::atoi(text), m = std::atoi(colon + 1);
        if (h < 0 || h > 23 || m < 0 || m > 59) return {};
        char out[8];
        snprintf(out, sizeof(out), "%02d:%02d", h, m);
        return out;
    }

    void TimeSetting(const char* id, char* buffer, size_t size, const char* key, const char* fallback) {
        ImGui::SetNextItemWidth(Px(90));
        ImGui::InputTextWithHint(id, fallback, buffer, size);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const std::string time = NormalizeTime(buffer);
            if (time.empty()) Copy(buffer, size, Config::Value("weather", key, fallback)); // не время — вернуть как было
            else { Config::SetValue("weather", key, time); Copy(buffer, size, time); }
        }
    }

    // ---------------------------------------------------------------- элементы настроек
    char g_assistant[64] = "";
    void DrawAssistant(float w) {
        TextSetting("##assistant", g_assistant, sizeof(g_assistant), w, &Settings::assistant, "Аня");
    }
    void DrawName(float w) { TextSetting("##name", g_name, sizeof(g_name), w, &Settings::name, "друг"); }

    void DrawSpokenName(float w) { TextSetting("##spoken", g_spoken, sizeof(g_spoken), w, &Settings::spokenName, ""); }

    void DrawWakeWord(float) { ToggleSetting("##wakeWord", &Settings::wakeWord); }

    void DrawVoice(float w) {
        const auto voices = Voice::List();
        const std::wstring current = Config::Get().voice;
        const Voice::Info* chosen = Voice::Choose(voices, current);
        const std::string preview = current.empty()
            ? "авто — " + (chosen ? Text::ToUtf8(chosen->name) : std::string("нет голосов"))
            : Text::ToUtf8(current);
                if (Widgets::BeginCombo("##voice", preview.c_str(), ControlWidth(w))) {
            auto select = [](const std::wstring& name) {
                Config::Update([&](Settings& s) { s.voice = name; });
                Voice::ReloadVoice();
            };
            if (Widgets::ComboItem("авто (Silero — Ксения)", current.empty())) select(L"");
            for (const auto& v : voices) {
                const std::string label = Text::ToUtf8(v.name) + (v.female ? "  ♀" : "");
                if (Widgets::ComboItem(label.c_str(), v.name == current)) select(v.name);
            }
            Widgets::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("послушать")) {
            Brain::Interrupt();
            Voice::Speak("Привет, " + Config::Get().name + "! Вот так я звучу. Нравится?");
        }
    }

    void DrawNeuralPitch(float w) { SliderSetting("##neuralPitch", &Settings::neuralPitch, -6, 6, w); }
    void DrawPitch(float w) { SliderSetting("##pitch", &Settings::pitch, -10, 10, w); }
    void DrawRate(float w) { SliderSetting("##rate", &Settings::rate, -5, 6, w); }
    void DrawSilence(float w) { SliderSetting("##silence", &Settings::silenceMs, 300, 2000, w, "%d мс"); }
    void DrawBodySounds(float) { ToggleSetting("##bodySounds", &Settings::bodySounds); }

    void DrawPersonality(float w) {
        static const char* kNames[] = { "Строгая и холодная", "По умолчанию (50 на 50)", "Милая и верная", "Свой характер (промт)" };
        int current = static_cast<int>(Config::Get().personality);
                if (Widgets::Combo("##personality", &current, kNames, IM_ARRAYSIZE(kNames), ControlWidth(w)))
            Config::Update([&](Settings& s) { s.personality = static_cast<Personality>(current); });
        if (current != static_cast<int>(Personality::Custom)) return;

        if (ImGui::InputTextMultiline("##customPrompt", g_prompt, sizeof(g_prompt), ImVec2(w, Px(96)), ImGuiInputTextFlags_WordWrap))
            Config::Update([&](Settings& s) { s.customPrompt = g_prompt; }, false);
        if (ImGui::IsItemDeactivatedAfterEdit()) Config::Save();
        if (!g_prompt[0]) ImGui::TextDisabled("например: «отвечай как пират» или «будь дерзкой и шути»");
    }

    void DrawModel(float w) {
        const auto models = Ai::Models();
        const std::string current = Config::Get().model;
        auto fileName = [](const std::string& path) { return path.substr(path.find_last_of("\\/") + 1); };
        const std::string preview = models.empty() ? std::string("модели .gguf не найдены")
                                  : current.empty() ? "авто — " + Ai::ActiveModel() : fileName(current);
                if (!Widgets::BeginCombo("##model", preview.c_str(), ControlWidth(w))) return;
        auto select = [](const std::string& model) {
            Config::Update([&](Settings& s) { s.model = model; });
            Ai::Restart(); // перезапуск нейросети с новой моделью
        };
        if (Widgets::ComboItem("авто", current.empty())) select("");
        for (const auto& m : models)
            if (Widgets::ComboItem(fileName(m).c_str(), m == current)) select(m);
        Widgets::EndCombo();
    }

    void DrawAiIdle(float w) {
        static const char* kNames[] = { "никогда", "через 5 минут", "через 10 минут", "через 30 минут" };
        static const int kMinutes[] = { 0, 5, 10, 30 };
        const int now = std::atoi(Config::Value("ai", "idle_minutes", "10").c_str());
        int current = 2;
        for (int i = 0; i < 4; ++i) if (kMinutes[i] == now) current = i;
                if (Widgets::Combo("##aiIdle", &current, kNames, 4, ControlWidth(w))) Config::SetValue("ai", "idle_minutes", std::to_string(kMinutes[current]));
    }

    void DrawSearchEngine(float w) {
        static const char* kNames[] = { "Google", "Яндекс", "DuckDuckGo", "Bing" };
        int current = static_cast<int>(Config::Get().search);
                if (Widgets::Combo("##search", &current, kNames, IM_ARRAYSIZE(kNames), ControlWidth(w)))
            Config::Update([&](Settings& s) { s.search = static_cast<SearchEngine>(current); });
    }

    void DrawAlwaysOnTop(float) { ToggleSetting("##onTop", &Settings::alwaysOnTop); }
    void DrawQuickPhrases(float) { ToggleSetting("##quickPhrases", &Settings::quickPhrases); }
    void DrawWeatherWidget(float) { ToggleSetting("##weatherWidget", &Settings::weatherWidget); }

    void FindCityAsync(const std::string& name) {
        {
            std::lock_guard lock(g_cityMutex);
            if (g_citySearching) return;
            g_citySearching = true;
            g_cityStatus = "ищу…";
        }
        std::thread([name] {
            const auto city = Weather::FindCity(name);
            if (city) Weather::SetCity(*city);
            std::lock_guard lock(g_cityMutex);
            g_citySearching = false;
            g_cityStatus = city ? "нашла: " + city->name + (city->country.empty() ? "" : ", " + city->country) : "не нашла такой город";
        }).detach();
    }

    void DrawCity(float w) {
        ImGui::SetNextItemWidth(ControlWidth(w) - Px(90));
        const bool enter = ImGui::InputTextWithHint("##city", "например, Ташкент", g_city, sizeof(g_city), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("найти") || enter) && !Text::Trim(g_city).empty()) FindCityAsync(g_city);
        std::lock_guard lock(g_cityMutex);
        if (!g_cityStatus.empty()) ImGui::TextDisabled("%s", g_cityStatus.c_str());
    }

    // ---------------------------------------------------------------- внешний вид
    ImVec4 ToVec(int rgb) {
        const Theme::Color c = Theme::FromRgb(rgb);
        return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f);
    }
    int FromVec(const ImVec4& v) { return Theme::ToRgb({ v.x * 255.0f, v.y * 255.0f, v.z * 255.0f }); }

    // Палитра во всплывающем окне; true — цвет поменяли
    bool ColorPopup(const char* id, int& rgb) {
        bool changed = false;
        if (ImGui::BeginPopup(id)) {
            ImVec4 v = ToVec(rgb);
            ImGui::SetNextItemWidth(Px(220));
            if (ImGui::ColorPicker3("##picker", &v.x, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel)) {
                rgb = FromVec(v);
                changed = true;
            }
            ImGui::EndPopup();
        }
        return changed;
    }

    void DrawLook(float w) {
        static const char* kNames[] = { "Ночь", "Графит", "Уголь", "Полночь", "Своя" };
        const Settings s = Config::Get();
        const ImVec2 tile(Px(92), Px(58));
        const float gap = Px(14), rowHeight = tile.y + Theme::semibold->LegacySize + Px(20);
        const int perRow = std::max(1, static_cast<int>((w + gap) / (tile.x + gap)));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        for (int i = 0; i < IM_ARRAYSIZE(kNames); ++i) {
            const auto look = Theme::LookPreset(i, s.lookColor);
            const ImVec2 pos(start.x + (i % perRow) * (tile.x + gap), start.y + Px(4) + (i / perRow) * rowHeight);
            ImGui::PushID(i);
            if (Widgets::ThemeTile("##look", pos, tile, look.top.WithAlpha(1.0f), look.bottom.WithAlpha(1.0f), look.surface.WithAlpha(1.0f),
                    kNames[i], static_cast<int>(s.look) == i)) {
                Config::Update([&](Settings& x) { x.look = static_cast<Look>(i); });
                if (i == 4) ImGui::OpenPopup("##lookColor");
            }
            int rgb = s.lookColor;
            if (i == 4 && ColorPopup("##lookColor", rgb)) {
                Config::Update([&](Settings& x) { x.lookColor = rgb; }, false);
                g_colorDirty = true;
            }
            ImGui::PopID();
        }
        const int rows = (IM_ARRAYSIZE(kNames) + perRow - 1) / perRow;
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + rows * rowHeight));
        ImGui::Dummy(ImVec2(w, 0));
    }

    void DrawAccent(float w) {
        const Settings s = Config::Get();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = Px(12), step = Px(38);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float cy = start.y + r + Px(6);
        for (int i = 0; i < Theme::kAccentPresets; ++i) {
            ImGui::PushID(i);
            if (Widgets::Swatch("##accent", ImVec2(start.x + r + Px(4) + i * step, cy), r, Theme::AccentPreset(i).WithAlpha(1.0f), s.accent == i))
                Config::Update([&](Settings& x) { x.accent = i; });
            ImGui::PopID();
        }
        // свой цвет: кружок с радужной каймой и палитрой
        const ImVec2 own(start.x + r + Px(4) + Theme::kAccentPresets * step + Px(12), cy);
        for (int k = 0; k < 24; ++k) {
            float red, green, blue;
            ImGui::ColorConvertHSVtoRGB(k / 24.0f, 0.55f, 1.0f, red, green, blue);
            const float a0 = k * 6.2831853f / 24.0f, a1 = (k + 1) * 6.2831853f / 24.0f;
            dl->PathArcTo(own, r + Px(1.5f), a0, a1 + 0.02f, 3);
            dl->PathStroke(ImGui::ColorConvertFloat4ToU32(ImVec4(red, green, blue, 1.0f)), 0, Px(3));
        }
        if (Widgets::Swatch("##accentOwn", own, r - Px(3), Theme::FromRgb(s.accentColor).WithAlpha(1.0f), s.accent == Theme::kAccentPresets)) {
            Config::Update([&](Settings& x) { x.accent = Theme::kAccentPresets; });
            ImGui::OpenPopup("##accentColor");
        }
        Widgets::Text(dl, Theme::semibold, ImVec2(own.x + r + Px(10), cy - Theme::semibold->LegacySize * 0.55f), Theme::kInkSoft, "свой цвет");
        int rgb = s.accentColor;
        if (ColorPopup("##accentColor", rgb)) {
            Config::Update([&](Settings& x) { x.accentColor = rgb; x.accent = Theme::kAccentPresets; }, false);
            g_colorDirty = true;
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x, cy + r + Px(10)));
        ImGui::Dummy(ImVec2(w, 0));
    }

    void DrawWallpaper(float w) {
        const std::string path = Config::Get().wallpaper;
        if (ImGui::Button(path.empty() ? "выбрать картинку…" : "другая картинка…")) Wallpaper::PickAsync();
        if (!path.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("убрать")) {
                Config::Update([](Settings& x) { x.wallpaper.clear(); });
                if (Config::Get().autoColors) Wallpaper::ApplyAutoColors(false);
            }
            const std::string name = path.substr(path.find_last_of("\\/") + 1);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
            ImGui::TextDisabled("%s", name.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    void DrawWallpaperDim(float w) { SliderSetting("##wallpaperDim", &Settings::wallpaperDim, 0, 90, w, "%d%%"); }
    void DrawAutoColors(float) {
        bool on = Config::Get().autoColors;
        if (Widgets::Toggle("##autoColors", ImGui::GetCursorScreenPos(), &on, Accent())) Wallpaper::ApplyAutoColors(on && Wallpaper::Loaded());
    }

    void DrawBackdrop(float w) {
        static const char* kNames[] = { "Сияние", "Нити", "Волны", "Звёздный полёт", "Спокойный (без движения)" };
        int current = static_cast<int>(Config::Get().backdrop);
                if (Widgets::Combo("##backdrop", &current, kNames, IM_ARRAYSIZE(kNames), ControlWidth(w)))
            Config::Update([&](Settings& x) { x.backdrop = static_cast<Backdrop>(current); });
    }

    void DrawBackdropSpeed(float w) { SliderSetting("##backdropSpeed", &Settings::backdropSpeed, 20, 300, w, "%d%%"); }
    void DrawAnimateInactive(float) { ToggleSetting("##animateInactive", &Settings::animateInactive); }

    // Свой цвет поверх автоматического: образец цвета (нажми — палитра) и «сбросить»
    void ColorOverride(const char* id, int Settings::*field, const Theme::Color& fallback, const char* autoLabel) {
        const int value = Config::Get().*field;
        const ImVec2 pos = ImGui::GetCursorScreenPos(), size(Px(48), Px(28));
        ImGui::PushID(id);
        if (ImGui::InvisibleButton("##swatch", size)) ImGui::OpenPopup("##color");
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), (value >= 0 ? Theme::FromRgb(value) : fallback).WithAlpha(1.0f), Px(8));
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(255, 255, 255, hovered ? 90 : 40), Px(8));
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (size.y - ImGui::GetFrameHeight()) * 0.5f);
        if (value >= 0) {
            if (ImGui::Button("сбросить")) Config::Update([&](Settings& x) { x.*field = -1; });
        } else {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::TextDisabled("%s", autoLabel);
        }
        int rgb = value >= 0 ? value : Theme::ToRgb(fallback);
        if (ColorPopup("##color", rgb)) {
            Config::Update([&](Settings& x) { x.*field = rgb; }, false);
            g_colorDirty = true;
        }
        ImGui::PopID();
    }

    void DrawBackgroundColor(float) {
        const Settings s = Config::Get();
        ColorOverride("bg", &Settings::backgroundColor, Theme::LookPreset(static_cast<int>(s.look), s.lookColor).top, "по теме");
    }
    void DrawAnimationColor(float) { ColorOverride("anim", &Settings::animationColor, Theme::Palette(0), "по моему настроению"); }
    void DrawGlass(float) { ToggleSetting("##glass", &Settings::glass); }
    void DrawPlates(float) { ToggleSetting("##plates", &Settings::buttonPlates); }
    void DrawTray(float) { ToggleSetting("##tray", &Settings::tray); }

    // ---------------------------------------------------------------- автозапуск, эффект окна
    void DrawAutostart(float) {
        bool on = Autostart::Enabled();
        if (Widgets::Toggle("##autostart", ImGui::GetCursorScreenPos(), &on, Accent())) Autostart::Set(on);
    }

    void DrawWindowEffect(float w) {
        static const char* kNames[] = { "Нет — плотный фон", "Mica — мягкий оттенок обоев", "Акрил — размытие того, что за окном" };
        int current = Config::Get().windowEffect;
                if (Widgets::Combo("##windowEffect", &current, kNames, IM_ARRAYSIZE(kNames), ControlWidth(w))) Config::Update([&](Settings& x) { x.windowEffect = current; });
    }
    void DrawWindowOpacity(float w) { SliderSetting("##windowOpacity", &Settings::windowOpacity, 20, 100, w, "%d%%"); }

    // ---------------------------------------------------------------- телеграм и Discord (раздел anya.ini, не Settings)
    void IniToggle(const char* id, const char* section, const char* key, const char* fallback, void (*reload)()) {
        bool on = Config::Value(section, key, fallback) == "1";
        if (Widgets::Toggle(id, ImGui::GetCursorScreenPos(), &on, Accent())) {
            Config::SetValue(section, key, on ? "1" : "0");
            reload();
        }
    }

    void IniText(const char* id, char* buffer, size_t size, const char* section, const char* key, const char* hint, float w, void (*reload)(),
                 ImGuiInputTextFlags flags = 0) {
        ImGui::SetNextItemWidth(ControlWidth(w));
        ImGui::InputTextWithHint(id, hint, buffer, size, flags);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            Config::SetValue(section, key, Text::Trim(buffer));
            reload();
        }
    }

    void StatusLine(const std::string& status, bool good) {
        ImGui::PushStyleColor(ImGuiCol_Text, good ? Theme::kOk : Theme::kInkMuted);
        ImGui::TextUnformatted(("● " + status).c_str());
        ImGui::PopStyleColor();
    }

    void DrawTelegram(float) { IniToggle("##telegram", "telegram", "enabled", "1", Telegram::Reload); }
    void DrawTelegramToken(float w) {
        IniText("##token", g_token, sizeof(g_token), "telegram", "token", "123456:ABC… от @BotFather", w, Telegram::Reload, ImGuiInputTextFlags_Password);
        StatusLine(Telegram::Status(), Telegram::Ready());
    }
    void DrawTelegramOwner(float w) {
        IniText("##owner", g_owner, sizeof(g_owner), "telegram", "owner", "твой ID в телеграме", w, Telegram::Reload, ImGuiInputTextFlags_CharsDecimal);
    }
    void DrawGreet(float) { IniToggle("##greet", "telegram", "greet", "1", [] {}); }
    void DrawLockOnStandby(float) { IniToggle("##lockOnStandby", "telegram", "lock_on_standby", "0", Telegram::Reload); }
    void DrawDiscord(float) { IniToggle("##discord", "discord", "enabled", "0", DiscordRpc::Reload); }
    void DrawDiscordApp(float w) {
        IniText("##discordApp", g_discordApp, sizeof(g_discordApp), "discord", "app_id", "Application ID", w, DiscordRpc::Reload, ImGuiInputTextFlags_CharsDecimal);
        StatusLine(DiscordRpc::Status(), DiscordRpc::Status().rfind("показываю", 0) == 0);
    }
    void DrawDiscordMusic(float) { IniToggle("##discordMusic", "discord", "show_music", "1", DiscordRpc::Reload); }
    void DrawDiscordVideo(float) { IniToggle("##discordVideo", "discord", "show_video", "1", DiscordRpc::Reload); }

    // ---------------------------------------------------------------- полная кастомизация
    void DrawEditBlocks(float) {
        bool on = HomeLayout::Editing();
        if (ImGui::Checkbox("После нажатия на данную галочку вы можете редактировать позицию ЛЮБОГО БЛОКА", &on)) {
            HomeLayout::SetEditing(on);
            if (on) SettingsPage::Close(); // сразу на главный экран — там блоки уже можно тянуть
        }
    }

    // Окно подтверждения: danger — красная кнопка; needCheck — сначала поставить «я понимаю»
    bool Confirm(const char* id, const char* text, const char* action, bool danger, bool needCheck) {
        bool confirmed = false;
        ImGui::SetNextWindowSize(ImVec2(Px(460), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(22), Px(20)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Px(18));
        if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            static bool s_understood = false;
            if (ImGui::IsWindowAppearing()) s_understood = false;
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, Px(8)));
            if (needCheck) ImGui::Checkbox("Да, я понимаю — это нельзя отменить", &s_understood);
            ImGui::Dummy(ImVec2(0, Px(8)));
            const bool enabled = !needCheck || s_understood;
            ImGui::BeginDisabled(!enabled);
            if (danger) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 55, 60, 255)), ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(225, 70, 75, 255));
            if (ImGui::Button(action)) confirmed = true;
            if (danger) ImGui::PopStyleColor(2);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("отмена")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
        return confirmed;
    }

    void DrawResetAll(float) {
        if (ImGui::Button("вернуть всё по умолчанию")) ImGui::OpenPopup("##confirmReset");
        if (Confirm("##confirmReset", "Все настройки вернутся как были при установке: имя, голос, внешний вид, блоки и показатели. "
                                       "Телеграм-бот, Discord и контакты останутся. Программа перезапустится.", "сбросить и перезапустить", false, false))
            Uninstall::ResetSettings();
    }

    void DrawRemoveAll(float) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(170, 45, 52, 230));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(205, 60, 66, 255));
        if (ImGui::Button("удалить программу полностью")) ImGui::OpenPopup("##confirmRemove");
        ImGui::PopStyleColor(2);
        static std::string s_text;
        if (ImGui::IsPopupOpen("##confirmRemove") && s_text.empty()) {
            const std::string folder = Text::ToUtf8(Uninstall::Folder());
            s_text = Uninstall::SafeToDeleteFolder()
                ? "Будет удалена папка программы со ВСЕМИ данными — настройки, токен бота, сценарии, журналы, голоса и нейросеть, "
                  "если они лежат в ней:\n\n" + folder + "\n\nАвтозапуск тоже отключится. Программа закроется."
                : "Программа лежит в общей папке, поэтому удалю только её файлы (Anya.exe, настройки, сценарии, журналы, модели):\n\n" + folder +
                  "\n\nАвтозапуск тоже отключится. Программа закроется.";
        }
        if (!ImGui::IsPopupOpen("##confirmRemove")) s_text.clear();
        if (Confirm("##confirmRemove", s_text.c_str(), "удалить навсегда", true, true)) Uninstall::RemoveEverything();
    }

    void DrawToCollege(float) { TimeSetting("##toCollege", g_toCollege, sizeof(g_toCollege), "to_college", "08:00"); }
    void DrawBackHome(float) { TimeSetting("##backHome", g_backHome, sizeof(g_backHome), "back_home", "15:10"); }

    // inlineControl — переключатель справа от названия, а не под ним
    struct Item { Section section; const char* title; const char* hint; void (*draw)(float width); bool inlineControl = false; };
    const Item kItems[] = {
        { General, "Имя помощницы", "Так она отзывается на голос и подписывается в окне, Discord и телеграме. Склоняю сама: «Лиза» → «с Лизой».", DrawAssistant },
        { General, "Как тебя зовут", "Так Аня пишет твоё имя на экране и в разговоре.", DrawName },
        { General, "Как произносить имя вслух", "Ник не всегда читается красиво: например, «zephiro4ka» можно произносить как «Зефирочка».", DrawSpokenName },
        { General, "Слушать всегда", "Аня ждёт слово «Аня». Выключи — тогда нажимай на сферу или пробел.", DrawWakeWord, true },
        { General, "Запускать вместе с Windows", "Аня тихо стартует в трее, когда включаешь компьютер.", DrawAutostart, true },

        { Appearance, "Эффект окна", "Как в Windows 11: сквозь Аню видно размытый фон. Акрил — заметнее, Mica — спокойнее.", DrawWindowEffect },
        { Appearance, "Плотность фона", "Насколько фон Ани закрывает эффект окна. Меньше — прозрачнее.", DrawWindowOpacity },
        { Appearance, "Свои обои", "Любая картинка на фоне Ани. Анимация фона идёт поверх неё.", DrawWallpaper },
        { Appearance, "Затемнение обоев", "Чтобы текст и сфера хорошо читались на яркой картинке.", DrawWallpaperDim },
        { Appearance, "Цвета по обоям", "Тема, любимый цвет и цвет анимации подбираются под картинку. Только в Ане — Windows не трогаю. "
                                       "Выключишь — вернутся прежние цвета.", DrawAutoColors, true },
        { Appearance, "Тема", "Цвет фона и панелей. «Своя» — любой цвет из палитры.", DrawLook },
        { Appearance, "Любимый цвет", "Этим цветом я крашу кнопки, переключатели и всё, что ты выбираешь, — и сферу, когда отдыхаю.", DrawAccent },
        { Appearance, "Фон", "Анимация на главном экране.", DrawBackdrop },
        { Appearance, "Скорость анимации", "Насколько быстро движется фон.", DrawBackdropSpeed },
        { Appearance, "Анимировать в неактивном окне", "Выключено — пока ты в другом окне, фон замирает и видеокарта отдыхает. "
                                                     "Когда я слушаю или говорю, всё оживает само.", DrawAnimateInactive, true },
        { Appearance, "Цвет фона", "Свой цвет фона поверх темы.", DrawBackgroundColor },
        { Appearance, "Цвет анимации", "Без него фон переливается цветами моего настроения.", DrawAnimationColor },
        { Appearance, "Стекло", "Полупрозрачные панели вместо плотных.", DrawGlass, true },
        { Appearance, "Подложка под кнопками окна", "Подложка за кнопками «свернуть», «развернуть» и «закрыть». Без неё — только значки.", DrawPlates, true },
        { Appearance, "Сворачивать в трей", "Крестик прячет меня в трей у часов — я продолжаю слушать. Выйти — правой кнопкой по значку.", DrawTray, true },

        { VoiceSection, "Голос", "Silero звучит живее всех, Supertonic и голоса Windows — запасные.", DrawVoice },
        { VoiceSection, "Высота нейроголоса", "0 — естественно, без обработки.", DrawNeuralPitch },
        { VoiceSection, "Высота голосов Windows", "Только для голосов Windows.", DrawPitch },
        { VoiceSection, "Скорость речи", "Насколько быстро она говорит.", DrawRate },

        { Hearing, "Пауза — конец фразы", "Меньше — отвечает быстрее, но может обрезать фразу на паузе.", DrawSilence },
        { Hearing, "Реакция на кашель и чих", "«Будь здоров!» на чих и «Всё в порядке? Не заболел?» на кашель.", DrawBodySounds, true },

        { AiSection, "Характер", "Как Аня разговаривает. Можно написать свой характер.", DrawPersonality },
        { AiSection, "Освобождать видеокарту", "Если долго не спрашиваешь — нейросеть выгружается, и видеокарта свободна для игр. "
                                               "Первый ответ после этого — на 5–15 секунд дольше.", DrawAiIdle },
        { AiSection, "Нейросеть", "Модель .gguf для домашки и сложных вопросов. Работает прямо на компьютере.", DrawModel },

        { SearchSection, "Поисковик", "Куда Аня отправляет «найди …», «загугли …» и «найди в интернете …».", DrawSearchEngine },

        { Overlay, "Поверх всех окон", "Окно Ани не прячется за другими программами — удобно поверх игры.", DrawAlwaysOnTop, true },
        { Overlay, "Быстрые фразы", "Подсказки-кнопки под полем ввода.", DrawQuickPhrases, true },

        { WidgetsSection, "Погода", "Слева от сферы: 4 часа, а при наведении — весь день.", DrawWeatherWidget, true },
        { WidgetsSection, "Город", "Для погоды, прогноза и виджета.", DrawCity },

        { Connections, "Телеграм-бот", "Управляй компьютером с телефона: снимок экрана, «реши с экрана», громкость, блокировка, "
                                       "«выключить, но остаться на связи».", DrawTelegram, true },
        { Connections, "Токен бота", "Выдаёт @BotFather. Никому не показывай — с ним можно управлять ботом.", DrawTelegramToken },
        { Connections, "Кому можно", "ID в телеграме того, кого бот слушается. Всем остальным он отвечает отказом.", DrawTelegramOwner },
        { Connections, "Сообщение при запуске", "«Аня запущена и на связи» в телеграме каждый раз, когда Аня стартует.", DrawGreet, true },
        { Connections, "Блокировать при «Выключить»", "Надёжнее, но тогда «☀ Включить» покажет экран блокировки — пароль придётся вводить "
                                                     "за компьютером: снять блокировку с телефона Windows не даёт.", DrawLockOnStandby, true },
        { Connections, "Статус в Discord", "«Играет в Anya», а когда играет музыка или видео — «Слушает» или «Смотрит».", DrawDiscord, true },
        { Connections, "ID приложения Discord", "Создай приложение «Anya» на discord.com/developers и вставь его Application ID. "
                                                "Картинка — ассет «anya» в Rich Presence → Art Assets.", DrawDiscordApp },
        { Connections, "Показывать музыку", "«Слушает: трек — исполнитель» из Яндекс Музыки, Spotify и других плееров.", DrawDiscordMusic, true },
        { Customization, "Полная кастомизация", "Сфера, погода, карточки справа, поле ввода и шторка разговора — тяни мышью куда хочешь, "
                                                "а за уголок — меняй размер. Выключи галочку или нажми «готово», чтобы закончить.", DrawEditBlocks },
        { Customization, "Настройки по умолчанию", "Если что-то сломалось или запуталась — вернуть всё как было при установке.", DrawResetAll },
        { Customization, "Удалить программу", "Полностью: все данные, настройки и автозапуск. Перед удалением спрошу ещё раз.", DrawRemoveAll },
        { Connections, "Показывать видео", "«Смотрит: название видео» из браузера (YouTube).", DrawDiscordVideo, true },
    };

    // ---------------------------------------------------------------- «где эта настройка?» — подсветка
    std::atomic<int> g_revealRequest{ -1 }; // номер пункта, который попросили показать (из любого потока)
    int g_reveal = -1;                      // какой пункт светится сейчас
    float g_revealTime = 0.0f;              // сколько он уже светится
    bool g_revealScroll = false;            // надо доехать до него прокруткой
    float g_scrollTo = -1.0f;
    constexpr float kRevealSeconds = 6.0f;
    ImU32 Orange(float alpha) { return IM_COL32(255, 152, 48, static_cast<int>(255 * std::clamp(alpha, 0.0f, 1.0f))); }

    // ---------------------------------------------------------------- поиск
    std::wstring Lower(const char* text) { return Text::LowerRu(Text::ToWide(text)); }

    bool Matches(const Item& item, const std::wstring& query) {
        if (query.empty()) return item.section == g_section;
        const std::wstring hay = Lower(item.title) + L" " + Lower(item.hint) + L" " + Lower(kInfo[item.section].name);
        return hay.find(query) != std::wstring::npos;
    }

    // ---------------------------------------------------------------- части страницы
    void DrawSidebar(ImDrawList* dl, ImVec2 pos, float width) {
        const float itemHeight = Px(40), pad = Px(7);
        const bool compact = width < Px(120);
        const ImVec2 max(pos.x + width, pos.y + pad * 2 + itemHeight * static_cast<float>(kSections));
        dl->AddRectFilled(pos, max, Theme::Surface(0.9f), Px(20));
        dl->AddRect(pos, max, IM_COL32(255, 255, 255, 14), Px(20));

        for (int i = 0; i < kSections; ++i) {
            const ImVec2 a(pos.x + pad, pos.y + pad + i * itemHeight), b(pos.x + width - pad, a.y + itemHeight - Px(4));
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##section", ImVec2(b.x - a.x, b.y - a.y)) && (i != g_section || g_query[0])) {
                g_section = i;
                g_query[0] = '\0';
                g_contentAnim = 0.0f;
            }
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool selected = i == g_section && !g_query[0];
            static float s_hover[kSections] = {}, s_pick[kSections] = {};
            const float k = std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
            s_hover[i] += ((hovered ? 1.0f : 0.0f) - s_hover[i]) * k;
            s_pick[i] += ((selected ? 1.0f : 0.0f) - s_pick[i]) * k;
            const float fill = std::max(s_pick[i] * 22.0f, s_hover[i] * 12.0f);
            if (fill > 0.5f) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, static_cast<int>(fill)), Px(12));
            if (s_pick[i] > 0.02f) {
                const float half = (b.y - a.y - Px(20)) * 0.5f * s_pick[i], mid = (a.y + b.y) * 0.5f; // полоска растёт от центра
                dl->AddRectFilled(ImVec2(a.x, mid - half), ImVec2(a.x + Px(3), mid + half), Theme::Accent().WithAlpha(s_pick[i]), Px(2));
            }
            const ImU32 color = selected ? Theme::kInk : Theme::kInkSoft;
            const float cy = (a.y + b.y) * 0.5f;
            Widgets::DrawIcon(dl, kInfo[i].icon, ImVec2(compact ? (a.x + b.x) * 0.5f : a.x + Px(20), cy), Px(15), color);
            if (compact) {
                if (hovered) ImGui::SetTooltip("%s", kInfo[i].name);
            } else {
                Widgets::Text(dl, Theme::semibold, ImVec2(a.x + Px(38), cy - Theme::semibold->LegacySize * 0.55f), color, kInfo[i].name);
            }
        }
    }

    bool DrawSearchField(ImDrawList* dl, ImVec2 pos, float width) {
        const float height = Px(38);
        static bool s_active = false; // поле в фокусе (с прошлого кадра) — подсветить рамку
        const bool active = s_active;
        const ImVec2 max(pos.x + width, pos.y + height);
        dl->AddRectFilled(pos, max, IM_COL32(255, 255, 255, 10), height * 0.5f);
        dl->AddRect(pos, max, active ? Theme::Palette(0).WithAlpha(0.6f) : IM_COL32(255, 255, 255, 24), height * 0.5f, 0, Px(1.2f));
        Widgets::DrawIcon(dl, Icon::Search, ImVec2(pos.x + Px(20), pos.y + height * 0.5f), Px(15), Theme::kInkMuted);

        ImGui::SetCursorScreenPos(ImVec2(pos.x + Px(36), pos.y + (height - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::SetNextItemWidth(width - Px(48));
        for (ImGuiCol col : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_Border })
            ImGui::PushStyleColor(col, IM_COL32(0, 0, 0, 0));
        if (g_justOpened) ImGui::SetKeyboardFocusHere();
        const bool changed = ImGui::InputTextWithHint("##settingsSearch", "поиск настроек…", g_query, sizeof(g_query));
        s_active = ImGui::IsItemActive();
        ImGui::PopStyleColor(4);
        return changed;
    }

    // Карточка раздела: заголовок, пояснение и его настройки (только подходящие под поиск)
    void DrawCard(Section section, const std::wstring& query, float width) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pad = Px(30), inner = width - pad * 2;
        const ImVec2 start = ImGui::GetCursorScreenPos();

        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + pad));
        ImGui::BeginGroup();
        const ImVec2 head = ImGui::GetCursorScreenPos();
        Widgets::DrawIcon(dl, kInfo[section].icon, ImVec2(head.x + Px(10), head.y + Theme::display->LegacySize * 0.55f), Px(17), Theme::kInk);
        Widgets::Text(dl, Theme::display, ImVec2(head.x + Px(30), head.y), Theme::kInk, kInfo[section].name);
        ImGui::Dummy(ImVec2(inner, Theme::display->LegacySize + Px(4)));
        ImGui::PushFont(Theme::semibold, Theme::semibold->LegacySize);
        ImGui::TextDisabled("%s", kInfo[section].about);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, Px(10)));

        bool first = true;
        ImVec2 glowMin(0, 0), glowMax(0, 0);
        for (const Item& item : kItems) {
            if (item.section != section || !Matches(item, query)) continue;
            const bool glowing = &item - kItems == g_reveal;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (!first) {
                dl->AddLine(ImVec2(at.x, at.y), ImVec2(at.x + inner, at.y), IM_COL32(255, 255, 255, 10), Px(1));
            }
            ImGui::Dummy(ImVec2(0, Px(first ? 6.0f : 16.0f)));
            first = false;

            ImGui::PushID(item.title);
            const ImVec2 row = ImGui::GetCursorScreenPos();
            const float textWidth = item.inlineControl ? inner - Px(64) : inner;
            ImGui::PushFont(Theme::title, Theme::title->LegacySize);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kInk);
            ImGui::TextUnformatted(Text::Brand(item.title, Config::Get().assistant).c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, Px(2)));
            ImGui::PushFont(Theme::semibold, Theme::semibold->LegacySize);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kInkMuted);
            ImGui::TextUnformatted(Text::Brand(item.hint, Config::Get().assistant).c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            if (item.inlineControl) { // переключатель — справа, на уровне названия
                const ImVec2 after = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(row.x + inner - Px(36), row.y + Px(3)));
                item.draw(inner);
                ImGui::SetCursorScreenPos(after);
                ImGui::Dummy(ImVec2(0, Px(2)));
            } else {
                ImGui::Dummy(ImVec2(0, Px(4)));
                item.draw(inner);
            }
            ImGui::Dummy(ImVec2(0, Px(12)));
            ImGui::PopID();
            if (glowing) {
                glowMin = ImVec2(row.x - Px(14), row.y - Px(10));
                glowMax = ImVec2(row.x + inner + Px(14), ImGui::GetCursorScreenPos().y - Px(2));
                if (g_revealScroll) { // доехать, чтобы пункт был в верхней трети
                    g_scrollTo = ImGui::GetScrollY() + (row.y - ImGui::GetWindowPos().y) - ImGui::GetWindowHeight() * 0.3f;
                    g_revealScroll = false;
                }
            }
        }
        ImGui::EndGroup();

        const ImVec2 cardMax(start.x + width, ImGui::GetItemRectMax().y + pad - Px(14));
        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled(start, cardMax, Theme::Surface(0.9f), Px(26));
        dl->AddRectFilledMultiColor(ImVec2(start.x + Px(1), start.y + Px(1)), ImVec2(cardMax.x - Px(1), start.y + Px(60)),
            IM_COL32(255, 255, 255, 14), IM_COL32(255, 255, 255, 14), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0)); // блик стекла
        dl->AddRect(start, cardMax, IM_COL32(255, 255, 255, 16), Px(26));
        if (glowMax.x > glowMin.x) { // оранжевое свечение: пульсирует и плавно гаснет
            const float t = g_revealTime;
            const float fade = std::min(1.0f, t * 4.0f) * std::clamp((kRevealSeconds - t) / 1.5f, 0.0f, 1.0f);
            const float pulse = 0.65f + 0.35f * std::sin(t * 5.0f);
            for (int i = 3; i >= 1; --i) // мягкий ореол
                dl->AddRect(ImVec2(glowMin.x - Px(2.0f * i), glowMin.y - Px(2.0f * i)), ImVec2(glowMax.x + Px(2.0f * i), glowMax.y + Px(2.0f * i)),
                    Orange(fade * pulse * 0.12f * (4 - i)), Px(14 + 2.0f * i), 0, Px(2));
            dl->AddRectFilled(glowMin, glowMax, Orange(fade * pulse * 0.13f), Px(14));
            dl->AddRect(glowMin, glowMax, Orange(fade * (0.55f + 0.35f * pulse)), Px(14), 0, Px(1.6f));
        }
        dl->ChannelsMerge();
        ImGui::SetCursorScreenPos(ImVec2(start.x, cardMax.y + Px(16)));
        ImGui::Dummy(ImVec2(width, 0));
    }

    void LoadBuffers() {
        const Settings s = Config::Get();
        Copy(g_name, s.name);
        Copy(g_assistant, s.assistant);
        Copy(g_spoken, s.spokenName);
        Copy(g_prompt, s.customPrompt);
        Copy(g_city, Config::Value("weather", "city", "Москва"));
        Copy(g_toCollege, Config::Value("weather", "to_college", "08:00"));
        Copy(g_backHome, Config::Value("weather", "back_home", "15:10"));
        Copy(g_token, Config::Value("telegram", "token", ""));
        Copy(g_owner, Config::Value("telegram", "owner", ""));
        Copy(g_discordApp, Config::Value("discord", "app_id", ""));
        g_query[0] = '\0';
        std::lock_guard lock(g_cityMutex);
        if (!g_citySearching) g_cityStatus.clear();
    }
}

void SettingsPage::Open(int section) {
    if (section >= 0 && section < kSections && section != g_section) {
        g_section = section;
        g_contentAnim = 0.0f;
    }
    if (g_open) return;
    LoadBuffers();
    g_open = true;
    g_justOpened = true;
    g_contentAnim = 0.0f;
}

void SettingsPage::Close() { g_open = false; }
bool SettingsPage::IsOpen() { return g_open; }
bool SettingsPage::Covers() { return g_open && g_anim > 0.97f; }
float SettingsPage::Visibility() { return g_anim; }

void SettingsPage::Draw(ImVec2 screen, float top, float dt) {
    g_anim += ((g_open ? 1.0f : 0.0f) - g_anim) * std::min(1.0f, dt * 14.0f);
    if (g_anim < 0.01f) return;
    const float ease = 1.0f - std::pow(1.0f - g_anim, 3.0f);
    const float slide = (1.0f - ease) * Px(14); // въезжает чуть снизу

    ImGui::SetNextWindowPos(ImVec2(0, top));
    ImGui::SetNextWindowSize(ImVec2(screen.x, screen.y - top));
    if (g_justOpened) ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, PageColor(Config::Get().glass ? 0.22f : 0.5f)); // лёгкое затемнение, фон Ани виден целиком
    if (g_reveal >= 0 && (g_revealTime += dt) > kRevealSeconds) g_reveal = -1;
    ImGui::Begin("##settingsPage", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + slide);
    const ImVec2 size = ImGui::GetWindowSize();
    const float margin = Px(16);

    // разделы слева — плавающая карточка (в узком окне — только значки)
    const float sideWidth = size.x < Px(760) ? Px(54) : Px(200);
    const float headerY = origin.y + Px(24);
    DrawSidebar(dl, ImVec2(origin.x + Px(20), headerY + Px(6)), sideWidth);

    // колонка настроек — по центру окна, но не наезжая на меню слева
    const float sideRight = origin.x + Px(20) + sideWidth + Px(28);
    float columnWidth = std::min(Px(900), origin.x + size.x - margin - Px(12) - sideRight);
    float left = std::max(origin.x + (size.x - columnWidth) * 0.5f, sideRight);
    columnWidth = std::min(columnWidth, origin.x + size.x - margin - Px(12) - left);
    const float right = left + columnWidth;
    Widgets::Text(dl, Theme::hero, ImVec2(left, headerY), Theme::kInk, "Настройки");

    const float titleWidth = Widgets::TextSize(Theme::hero, "Настройки").x;
    float searchWidth = std::min(Px(320), columnWidth - titleWidth - Px(24));
    ImVec2 searchPos(right - searchWidth, headerY);
    float contentTop = headerY + Px(64);
    if (searchWidth < Px(180)) { // не влезает рядом с заголовком — под ним во всю ширину
        searchWidth = right - left;
        searchPos = ImVec2(left, contentTop);
        contentTop += Px(52);
    }
    if (DrawSearchField(dl, searchPos, searchWidth)) { g_lastShown = -1; g_contentAnim = 0.3f; } // новый запрос — список снова сверху

    // карточки
    const std::wstring query = Lower(Text::Trim(g_query).c_str());
    const float cardWidth = columnWidth;
    ImGui::SetCursorScreenPos(ImVec2(left - Px(4), contentTop));
    if (g_lastShown != g_section) {
        ImGui::SetNextWindowScroll(ImVec2(0, 0));
        g_lastShown = g_section;
    }
    // прокрутка — до правого края окна (полоса прокрутки у края, как в Astra)
    ImGui::BeginChild("##settingsCards", ImVec2(origin.x + size.x - Px(6) - (left - Px(4)), origin.y + size.y - contentTop), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    Widgets::SmoothScroll(&g_scrollTo);
    g_contentAnim += (1.0f - g_contentAnim) * std::min(1.0f, dt * 9.0f);
    const float appear = 1.0f - std::pow(1.0f - g_contentAnim, 3.0f);
    ImGui::SetCursorPos(ImVec2(Px(4), Px(2) + (1.0f - appear) * Px(18))); // карточки чуть выезжают снизу
    bool any = false;
    for (int s = 0; s < kSections; ++s) {
        const bool show = std::any_of(std::begin(kItems), std::end(kItems), [&](const Item& item) {
            return item.section == s && Matches(item, query);
        });
        if (!show) continue;
        any = true;
        DrawCard(static_cast<Section>(s), query, cardWidth);
    }
    if (!any) {
        const std::string empty = "ничего не нашла по «" + Text::Trim(g_query) + "»";
        ImGui::Dummy(ImVec2(0, Px(40)));
        ImGui::SetCursorPosX((cardWidth - Widgets::TextSize(Theme::body, empty.c_str()).x) * 0.5f);
        ImGui::TextDisabled("%s", empty.c_str());
    }
    ImGui::Dummy(ImVec2(0, Px(12)));
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    g_justOpened = false;
    if (g_colorDirty && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) { // палитру закрыли — сохраняем
        Config::Save();
        g_colorDirty = false;
    }
}

// ---------------------------------------------------------------- «где эта настройка?»
namespace {
    // слово → основа: «погоды» и «погода» совпадут
    std::wstring Stem(const std::wstring& w) { return w.substr(0, std::clamp<size_t>(w.size() - 1, 3, 5)); }

    std::vector<std::wstring> Stems(const std::wstring& text) {
        static const std::wstring kSkip[] = { L"где", L"как", L"мне", L"это", L"эта", L"этот", L"там", L"тут", L"аня", L"есть", L"можно",
            L"настройка", L"настройки", L"настройку", L"настроек", L"настройках", L"настроить", L"найти", L"найди", L"покажи",
            L"поменять", L"изменить", L"включить", L"выключить", L"отключить", L"сделать", L"поставить", L"убрать", L"менять",
            L"меняется", L"пункт", L"где-то", L"пожалуйста", L"потерялся", L"потерялась", L"подскажи", L"скажи", L"чтобы", L"что" };
        std::vector<std::wstring> out;
        std::wstring word;
        auto flush = [&] {
            if (word.size() >= 3 && std::find(std::begin(kSkip), std::end(kSkip), word) == std::end(kSkip)) out.push_back(Stem(word));
            word.clear();
        };
        for (wchar_t c : Text::LowerRu(text) + L" ") {
            if (std::iswalnum(c)) word += c;
            else flush();
        }
        return out;
    }

    int Score(const Item& item, const std::vector<std::wstring>& stems) {
        const std::wstring title = Text::LowerRu(Text::ToWide(item.title)), hint = Text::LowerRu(Text::ToWide(item.hint)),
                           section = Text::LowerRu(Text::ToWide(kInfo[item.section].name));
        int score = 0;
        for (const auto& s : stems) {
            if (title.find(s) != std::wstring::npos) score += 3;
            else if (section.find(s) != std::wstring::npos) score += 2;
            else if (hint.find(s) != std::wstring::npos) score += 1;
        }
        return score;
    }

    int FindByWords(const std::string& question, int* bestScore) {
        const auto stems = Stems(Text::ToWide(question));
        int best = -1, top = 0;
        for (int i = 0; i < static_cast<int>(std::size(kItems)); ++i)
            if (const int s = Score(kItems[i], stems); s > top) { top = s; best = i; }
        if (bestScore) *bestScore = top;
        return best;
    }

    // слова не помогли — нейросеть выбирает пункт по смыслу («где сделать её прозрачной» → «Плотность фона»)
    int FindByAi(const std::string& question) {
        std::string list;
        for (int i = 0; i < static_cast<int>(std::size(kItems)); ++i)
            list += std::to_string(i + 1) + ". " + kInfo[kItems[i].section].name + " → " + kItems[i].title + ": " + kItems[i].hint + "\n";
        const std::string answer = Ai::Quiet("Вот пункты настроек программы:\n" + list + "\nПользователь спрашивает: «" + question +
            "». Какой пункт ему нужен? Ответь только номером пункта, без слов. Если ни один не подходит — ответь 0.");
        const int n = std::atoi(Text::Trim(answer).c_str());
        return n >= 1 && n <= static_cast<int>(std::size(kItems)) ? n - 1 : -1;
    }
}

std::optional<SettingsPage::Found> SettingsPage::Reveal(const std::string& question, bool askAi) {
    int score = 0;
    int index = FindByWords(question, &score);
    if (score < (askAi ? 2 : 3)) index = askAi ? FindByAi(question) : -1; // без нейросети — только если слово есть в названии
    if (index < 0) return std::nullopt;
    g_revealRequest = index;
    return Found{ kInfo[kItems[index].section].name, kItems[index].title };
}

bool SettingsPage::PollReveal() {
    const int index = g_revealRequest.exchange(-1);
    if (index < 0) return false;
    Open(kItems[index].section);
    g_query[0] = '\0';
    g_reveal = index;
    g_revealTime = 0.0f;
    g_revealScroll = true;
    g_lastShown = g_section; // прокрутку не сбрасывать наверх — сами доедем
    return true;
}
