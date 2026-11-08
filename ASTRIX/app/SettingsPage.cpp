#include "SettingsPage.h"

#include "Autostart.h"
#include "HomeLayout.h"
#include "Uninstall.h"
#include "Brain.h"
#include "DiscordRpc.h"
#include "Telegram.h"
#include "Wallpaper.h"
#include "LocalAi.h"
#include "CozyArt.h"
#include "Http.h"
#include "Json.h"
#include "Platform.h"
#include <shellapi.h>
#include <filesystem>
#include "Rates.h"
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
#include <fstream>
#include <unordered_map>

using Theme::Px;
using Widgets::Icon;

namespace {
    // ---------------------------------------------------------------- разделы
    enum Section { General, Appearance, VoiceSection, Hearing, AiSection, SearchSection, Overlay, WidgetsSection, Connections, Privacy, Learning, SystemSection, Updates, kSections };

    struct SectionInfo { Icon icon; const char* name; const char* about; };
    const SectionInfo kInfo[kSections] = {
        { Icon::User, "Общие", "Основные параметры программы" },
        { Icon::Sun, "Интерфейс", "Темы, цвета, шрифты, анимации и оформление" },
        { Icon::Wave, "Голос", "Выбор голоса, тембр, скорость речи и озвучивание" },
        { Icon::Mic, "Распознавание", "Микрофон, прослушивание и распознавание речи" },
        { Icon::Spark, "Интеллект", "Нейросеть, модель, память, контекст и параметры ИИ" },
        { Icon::Search, "Поиск", "Браузер, поиск файлов и приложений" },
        { Icon::Layers, "Оверлей", "Плавающие окна, положение и отображение помощника" },
        { Icon::Grid, "Виджеты", "Управление дополнительными элементами интерфейса" },
        { Icon::Phone, "Связь", "Discord, Telegram, телефон и внешние подключения" },
        { Icon::Lock, "Приватность", "Разрешения, доступы и защита данных" },
        { Icon::Book, "Обучение", "Персонализация поведения и предпочтений помощника" },
        { Icon::Gear, "Система", "Автозапуск, производительность, системные параметры и полная кастомизация" },
        { Icon::Download, "Обновления", "Обновление, информация о версии ASTRIX и удаление программы" },
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
    void DrawWallpaperDim(float w) { SliderSetting("##wallpaperDim", &Settings::wallpaperDim, 0, 100, w, "%d%%"); }
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

    void DrawBackdropSpeed(float w) { // середина ползунка — обычная скорость
        const int speed = Config::Get().backdropSpeed;
        int position = speed <= 100 ? (speed - 20) * 50 / 80 : 50 + (speed - 100) * 50 / 200;
        if (Widgets::Slider("##backdropSpeed", &position, 0, 100, "%d%%", ControlWidth(w))) {
            const int value = position <= 50 ? 20 + position * 80 / 50 : 100 + (position - 50) * 200 / 50;
            Config::Update([&](Settings& s) { s.backdropSpeed = value; }, false);
        }
    }
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
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), Theme::Overlay(hovered ? 90 : 40), Px(8));
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
    void DrawWindowOpacity(float w) { SliderSetting("##windowOpacity", &Settings::windowOpacity, 0, 100, w, "%d%%"); }

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
    void DrawRemoteDesktop(float) { IniToggle("##remoteDesktop", "telegram", "remote_desktop", "1", Telegram::Reload); }
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
                : "Программа лежит в общей папке, поэтому удалю только её файлы (ASTRIX.exe, настройки, сценарии, журналы, модели):\n\n" + folder +
                  "\n\nАвтозапуск тоже отключится. Программа закроется.";
        }
        if (!ImGui::IsPopupOpen("##confirmRemove")) s_text.clear();
        if (Confirm("##confirmRemove", s_text.c_str(), "удалить навсегда", true, true)) Uninstall::RemoveEverything();
    }

    void DrawToCollege(float) { TimeSetting("##toCollege", g_toCollege, sizeof(g_toCollege), "to_college", "08:00"); }
    void DrawBackHome(float) { TimeSetting("##backHome", g_backHome, sizeof(g_backHome), "back_home", "15:10"); }

    // inlineControl — переключатель справа от названия, а не под ним
    // ---- тонкая настройка нейросети
    std::unordered_map<std::string, int> g_aiValues; // не читать anya.ini каждый кадр
    int g_aiSeen = -1;
    char g_aiExtra[480] = "", g_aiArgs[480] = "", g_aiAbout[480] = "";

    int& AiCached(const char* key, const char* fallback) {
        if (g_aiSeen != Ai::TuningVersion()) { // профиль сменили (в том числе с телефона)
            g_aiSeen = Ai::TuningVersion();
            g_aiValues.clear();
            Copy(g_aiExtra, Config::Value("ai", "extra_prompt", ""));
            Copy(g_aiArgs, Config::Value("ai", "engine_args", ""));
            Copy(g_aiAbout, Config::Value("ai", "about_me", ""));
        }
        const auto it = g_aiValues.find(key);
        if (it != g_aiValues.end()) return it->second;
        return g_aiValues[key] = std::atoi(Config::Value("ai", key, fallback).c_str());
    }

    void AiSlider(const char* key, const char* fallback, int min, int max, float w, const char* format, bool sampling) {
        int& value = AiCached(key, fallback);
        const std::string id = std::string("##ai_") + key;
        if (Widgets::Slider(id.c_str(), &value, min, max, format, ControlWidth(w))) {
            Config::SetValue("ai", key, std::to_string(value));
            if (sampling && Ai::Profile() != Ai::kProfiles - 1) { // тронули ползунок — значит, это уже свой профиль
                Config::SetValue("ai", "profile", std::to_string(Ai::kProfiles - 1));
                g_aiValues.erase("profile");
            }
        }
    }

    void DrawAiProfile(float w) {
        const char* names[Ai::kProfiles];
        for (int i = 0; i < Ai::kProfiles; ++i) names[i] = Ai::ProfileName(i);
        int current = Ai::Profile();
        AiCached("profile", "0");
        if (Widgets::Combo("##aiProfile", &current, names, Ai::kProfiles, ControlWidth(w))) Ai::SetProfile(current);
    }
    void DrawChatTemp(float w) { AiSlider("chat_temp", "70", 0, 200, w, "%d%%", true); }
    void DrawChatTopP(float w) { AiSlider("chat_top_p", "90", 0, 100, w, "%d%%", true); }
    void DrawChatTopK(float w) { AiSlider("chat_top_k", "40", 0, 200, w, "%d", true); }
    void DrawChatMinP(float w) { AiSlider("chat_min_p", "5", 0, 100, w, "%d%%", true); }
    void DrawChatRepeat(float w) { AiSlider("chat_repeat", "100", 50, 150, w, "%d%%", true); }
    void DrawChatTokens(float w) { AiSlider("chat_tokens", "600", 50, 4000, w, "%d", true); }
    void DrawTaskTemp(float w) { AiSlider("task_temp", "60", 0, 200, w, "%d%%", true); }
    void DrawTaskTopP(float w) { AiSlider("task_top_p", "95", 0, 100, w, "%d%%", true); }
    void DrawTaskTopK(float w) { AiSlider("task_top_k", "20", 0, 200, w, "%d", true); }
    void DrawTaskMinP(float w) { AiSlider("task_min_p", "5", 0, 100, w, "%d%%", true); }
    void DrawTaskRepeat(float w) { AiSlider("task_repeat", "100", 50, 150, w, "%d%%", true); }
    void DrawTaskTokens(float w) { AiSlider("task_tokens", "5000", 200, 16000, w, "%d", true); }
    void DrawAiSeed(float w) { AiSlider("seed", "-1", -1, 9999, w, "%d", true); }
    void DrawThinkMode(float w) {
        static const char* kNames[] = { "только когда попросишь", "ещё и для задач", "никогда" };
        int current = std::clamp(AiCached("think_mode", "0"), 0, 2);
        if (Widgets::Combo("##thinkMode", &current, kNames, 3, ControlWidth(w))) {
            Config::SetValue("ai", "think_mode", std::to_string(current));
            AiCached("think_mode", "0") = current;
        }
    }
    void DrawThinkSeconds(float w) { AiSlider("think_seconds", "10", 3, 300, w, "%d с", false); }
    void AiTextField(const char* id, char* buffer, size_t size, const char* key, const char* hint, float w) {
        AiCached("profile", "0"); // подхватить значение из файла
        ImGui::SetNextItemWidth(ControlWidth(w));
        ImGui::InputTextWithHint(id, hint, buffer, size);
        if (ImGui::IsItemDeactivatedAfterEdit()) Config::SetValue("ai", key, Text::Trim(buffer));
    }
    void DrawExtraPrompt(float w) { AiTextField("##aiExtra", g_aiExtra, sizeof(g_aiExtra), "extra_prompt", "например: отвечай с лёгким сарказмом", w); }
    void DrawGpuLayers(float w) { AiSlider("gpu_layers", "99", 0, 99, w, "%d", false); }
    void DrawAiThreads(float w) { AiSlider("threads", "0", 0, 64, w, "%d", false); }
    void DrawAiContext(float w) {
        static const int kSizes[] = { 2048, 4096, 8192, 16384, 32768, 65536, 131072 };
        static const char* kNames[] = { "2 048", "4 096", "8 192", "16 384", "32 768", "65 536", "131 072" };
        int& value = AiCached("context", "16384");
        int current = 3;
        for (int i = 0; i < 7; ++i) if (kSizes[i] == value) current = i;
        if (Widgets::Combo("##aiContext", &current, kNames, 7, ControlWidth(w))) {
            value = kSizes[current];
            Config::SetValue("ai", "context", std::to_string(value));
        }
    }
    void DrawAiParallel(float w) { AiSlider("parallel", "2", 1, 4, w, "%d", false); }
    void DrawAiVision(float) { IniToggle("##aiVision", "ai", "vision", "1", [] {}); }
    void DrawEngineArgs(float w) { AiTextField("##aiArgs", g_aiArgs, sizeof(g_aiArgs), "engine_args", "например: --mlock", w); }
    void DrawPrivacyScreen(float) { IniToggle("##privacyScreen", "privacy", "screen", "1", [] {}); }
    void DrawClearLog(float) {
        static float s_done = 0;
        if (ImGui::Button("очистить журнал")) {
            std::ofstream(std::filesystem::path(Text::ExecutableDirectory()) / "anya.log", std::ios::trunc);
            s_done = static_cast<float>(ImGui::GetTime());
        }
        if (s_done > 0 && ImGui::GetTime() - s_done < 3) { ImGui::SameLine(); ImGui::TextDisabled("очищено"); }
    }
    void DrawAboutMe(float w) { AiTextField("##aboutMe", g_aiAbout, sizeof(g_aiAbout), "about_me", "например: учусь в колледже, люблю короткие ответы", w); }
    void DrawFileSearch(float w) {
        static const char* kNames[] = { "ASTRIX (по умолчанию)", "Поиск Windows", "Everything" };
        static int s_current = -1;
        if (s_current < 0) s_current = std::clamp(std::atoi(Config::Value("search", "files", "0").c_str()), 0, 2);
        if (Widgets::Combo("##fileSearch", &s_current, kNames, 3, ControlWidth(w))) Config::SetValue("search", "files", std::to_string(s_current));
    }
    constexpr const char* kVersion = "1.3.1";
    std::mutex g_updateMutex;
    std::string g_updateStatus;
    std::atomic<bool> g_updateChecking{ false };
    void DrawUpdates(float) {
        ImGui::TextUnformatted((std::string("ASTRIX ") + kVersion).c_str());
        if (ImGui::Button(g_updateChecking.load() ? "проверяю…" : "проверить обновления") && !g_updateChecking.exchange(true))
            std::thread([] {
                const auto tag = Json::FindStrings(Http::HttpsGet(L"api.github.com", L"/repos/zephiro4ka/Voice-assistant-ASTRIX/releases/latest", 12000), "tag_name", 1);
                std::lock_guard lock(g_updateMutex);
                g_updateStatus = tag.empty() ? "не удалось проверить — нет связи с GitHub" : tag[0] == std::string("v") + kVersion ? "у тебя последняя версия" : "вышла версия " + tag[0];
                g_updateChecking = false;
            }).detach();
        ImGui::SameLine();
        if (ImGui::Button("страница релизов")) ShellExecuteW(nullptr, L"open", L"https://github.com/zephiro4ka/Voice-assistant-ASTRIX/releases/latest", nullptr, nullptr, SW_SHOWNORMAL);
        std::lock_guard lock(g_updateMutex);
        if (!g_updateStatus.empty()) ImGui::TextDisabled("%s", g_updateStatus.c_str());
    }
    void DrawCozy(float) {
        bool on = Theme::Cozy();
        if (Widgets::Toggle("##cozy", ImGui::GetCursorScreenPos(), &on, Accent())) Theme::SetCozy(on, Theme::CozyPattern());
    }
    void DrawCozyDark(float) {
        bool on = Theme::CozyDark();
        if (Widgets::Toggle("##cozyDark", ImGui::GetCursorScreenPos(), &on, Accent())) Theme::SetCozyDark(on);
    }
    void DrawCozyPattern(float w) {
        const char* names[CozyArt::kPatterns];
        for (int i = 0; i < CozyArt::kPatterns; ++i) names[i] = CozyArt::PatternName(i);
        int current = std::clamp(Theme::CozyPattern(), 0, CozyArt::kPatterns - 1);
        if (Widgets::Combo("##cozyPattern", &current, names, CozyArt::kPatterns, ControlWidth(w))) Theme::SetCozy(Theme::Cozy(), current);
    }
    void DrawFontFace(float w) {
        std::vector<const char*> names;
        for (int i = 0; i < Theme::FontCount(); ++i) names.push_back(Theme::FontName(i));
        int current = Theme::FontIndex();
        if (Widgets::Combo("##fontFace", &current, names.data(), Theme::FontCount(), ControlWidth(w))) Theme::SetFont(current, Theme::FontScale());
    }
    void DrawFontSize(float w) {
        static int s_scale = -1;
        if (s_scale < 0) s_scale = Theme::FontScale();
        Widgets::Slider("##fontSize", &s_scale, 80, 120, "%d%%", ControlWidth(w));
        if (ImGui::IsItemDeactivatedAfterEdit()) Theme::SetFont(Theme::FontIndex(), s_scale); // шрифты грузим, когда ползунок отпустили
    }
    void DrawAiPreload(float) { IniToggle("##aiPreload", "ai", "preload", "0", [] {}); }
    void DrawGameUnload(float) { IniToggle("##gameUnload", "ai", "game_unload", "1", [] {}); }
    void DrawRatesWidget(float) { IniToggle("##ratesWidget", "rates", "widget", "1", Rates::Reload); }
    void DrawRatesCurrency(float w) {
        static const char* kCodes[] = { "auto", "UZS", "RUB", "KZT", "BYN", "KGS", "TJS", "UAH", "AZN", "AMD", "GEL", "TRY", "NOK", "EUR", "USD" };
        static const char* kNames[] = { "по городу из настроек", "UZS — сум", "RUB — рубль", "KZT — тенге", "BYN — белорусский рубль", "KGS — сом", "TJS — сомони", "UAH — гривна",
                                        "AZN — манат", "AMD — драм", "GEL — лари", "TRY — лира", "NOK — норвежская крона", "EUR — евро", "USD — доллар" };
        static int s_current = -1;
        if (s_current < 0) {
            s_current = 0;
            const std::string now = Config::Value("rates", "currency", "auto");
            for (int i = 0; i < IM_ARRAYSIZE(kCodes); ++i) if (now == kCodes[i]) s_current = i;
        }
        if (Widgets::Combo("##ratesCurrency", &s_current, kNames, IM_ARRAYSIZE(kNames), ControlWidth(w))) {
            Config::SetValue("rates", "currency", kCodes[s_current]);
            Rates::Reload();
        }
    }
    void DrawAiApply(float) {
        if (ImGui::Button("перезапустить нейросеть")) Ai::Restart();
        ImGui::SameLine();
        if (ImGui::Button("всё по умолчанию")) ImGui::OpenPopup("##confirmAiReset");
        if (Confirm("##confirmAiReset", "Все настройки нейросети вернутся как при установке, и она перезапустится.", "вернуть", false, false)) {
            Ai::ResetTuning();
            Ai::Restart();
        }
    }

    void DrawLearnGuide(float width);

    struct Item { Section section; const char* title; const char* hint; void (*draw)(float width); bool inlineControl = false; };
    const Item kItems[] = {
        { General, "Имя помощницы", "Так она отзывается на голос и подписывается в окне, Discord и телеграме. Склоняю сама: «Лиза» → «с Лизой».", DrawAssistant },
        { General, "Как тебя зовут", "Так Аня пишет твоё имя на экране и в разговоре.", DrawName },
        { General, "Как произносить имя вслух", "Ник не всегда читается красиво: например, «Sasha_07» можно произносить как «Саша».", DrawSpokenName },
        { General, "Слушать всегда", "Аня ждёт слово «Аня». Выключи — тогда нажимай на сферу или пробел.", DrawWakeWord, true },
        { SystemSection, "Запускать вместе с Windows", "Аня тихо стартует в трее, когда включаешь компьютер.", DrawAutostart, true },

        { Appearance, "Эффект окна", "Как в Windows 11: сквозь Аню видно размытый фон. Акрил — заметнее, Mica — спокойнее.", DrawWindowEffect },
        { Appearance, "Плотность фона", "Насколько фон Ани закрывает эффект окна. Меньше — прозрачнее.", DrawWindowOpacity },
        { Appearance, "Свои обои", "Любая картинка на фоне Ани. Анимация фона идёт поверх неё.", DrawWallpaper },
        { Appearance, "Затемнение обоев", "Чтобы текст и сфера хорошо читались на яркой картинке.", DrawWallpaperDim },
        { Appearance, "Цвета по обоям", "Тема, любимый цвет и цвет анимации подбираются под картинку. Только в Ане — Windows не трогаю. "
                                       "Выключишь — вернутся прежние цвета.", DrawAutoColors, true },
        { Appearance, "Домашний уют", "Бумага в клетку, рисунки от руки и нарисованная Аня вместо обоев и анимации.", DrawCozy, true },
        { Appearance, "Тёмная тетрадь", "Тёмный вариант «Домашнего уюта»: ночной лист и рисунки мелом.", DrawCozyDark, true },
        { Appearance, "Рисунки на листе", "Что нарисовано в тетради в формате «Домашний уют».", DrawCozyPattern },
        { Appearance, "Шрифт", "Как выглядит весь текст программы.", DrawFontFace },
        { Appearance, "Размер интерфейса", "Текст и блоки вместе: от 80% до 120%, посередине — обычный размер.", DrawFontSize },
        { Appearance, "Тема", "Цвет фона и панелей. «Своя» — любой цвет из палитры.", DrawLook },
        { Appearance, "Любимый цвет", "Этим цветом я крашу кнопки, переключатели и всё, что ты выбираешь, — и сферу, когда отдыхаю.", DrawAccent },
        { Appearance, "Фон", "Анимация на главном экране.", DrawBackdrop },
        { Appearance, "Скорость анимации", "Насколько быстро движется фон.", DrawBackdropSpeed },
        { SystemSection, "Анимировать в неактивном окне", "Выключено — пока ты в другом окне, фон замирает и видеокарта отдыхает. "
                                                     "Когда я слушаю или говорю, всё оживает само.", DrawAnimateInactive, true },
        { Appearance, "Цвет фона", "Свой цвет фона поверх темы.", DrawBackgroundColor },
        { Appearance, "Цвет анимации", "Без него фон переливается цветами моего настроения.", DrawAnimationColor },
        { Appearance, "Стекло", "Полупрозрачные панели вместо плотных.", DrawGlass, true },
        { Appearance, "Подложка под кнопками окна", "Подложка за кнопками «свернуть», «развернуть» и «закрыть». Без неё — только значки.", DrawPlates, true },
        { SystemSection, "Сворачивать в трей", "Крестик прячет меня в трей у часов — я продолжаю слушать. Выйти — правой кнопкой по значку.", DrawTray, true },

        { VoiceSection, "Голос", "Silero звучит живее всех, Supertonic и голоса Windows — запасные.", DrawVoice },
        { VoiceSection, "Высота нейроголоса", "0 — естественно, без обработки.", DrawNeuralPitch },
        { VoiceSection, "Высота голосов Windows", "Только для голосов Windows.", DrawPitch },
        { VoiceSection, "Скорость речи", "Насколько быстро она говорит.", DrawRate },

        { Hearing, "Пауза — конец фразы", "Меньше — отвечает быстрее, но может обрезать фразу на паузе.", DrawSilence },
        { Hearing, "Реакция на кашель и чих", "«Будь здоров!» на чих и «Всё в порядке? Не заболел?» на кашель.", DrawBodySounds, true },

        { Learning, "Как это работает", "Нажми на ссылку — перейду к настройке и подсвечу её.", DrawLearnGuide },
        { AiSection, "Характер", "Как Аня разговаривает. Можно написать свой характер.", DrawPersonality },
        { AiSection, "Освобождать видеокарту", "Если долго не спрашиваешь — нейросеть выгружается, и видеокарта свободна для игр. "
                                               "Первый ответ после этого — на 5–15 секунд дольше.", DrawAiIdle },
        { AiSection, "Нейросеть", "Модель .gguf для домашки и сложных вопросов. Работает прямо на компьютере.", DrawModel },
        { AiSection, "Профиль ответов", "Стандарт, Быстро, Умно, Креативно или Свой. Меняется сразу и с пульта на телефоне.", DrawAiProfile },
        { AiSection, "Свои указания", "Добавляются к системному промпту: как отвечать, о чём помнить.", DrawExtraPrompt },
        { AiSection, "Размышления", "Когда модель думает «про себя» перед ответом. Долго, зато точнее.", DrawThinkMode },
        { AiSection, "Время на размышления", "Сколько модель может думать при голосовом ответе. Потом ответит сразу.", DrawThinkSeconds },
        { AiSection, "Разговор: температура", "Выше — живее и неожиданнее, ниже — точнее и однообразнее.", DrawChatTemp },
        { AiSection, "Разговор: top-p", "Какую долю вероятных слов рассматривать.", DrawChatTopP },
        { AiSection, "Разговор: top-k", "Из скольких лучших слов выбирать. 0 — без ограничения.", DrawChatTopK },
        { AiSection, "Разговор: min-p", "Отсекает слишком маловероятные слова.", DrawChatMinP },
        { AiSection, "Разговор: штраф за повторы", "100% — без штрафа, выше — меньше повторов.", DrawChatRepeat },
        { AiSection, "Разговор: длина ответа", "Предел в токенах (примерно слоги).", DrawChatTokens },
        { AiSection, "Задачи: температура", "Для размышлений и задач из телеграма.", DrawTaskTemp },
        { AiSection, "Задачи: top-p", "Какую долю вероятных слов рассматривать.", DrawTaskTopP },
        { AiSection, "Задачи: top-k", "Из скольких лучших слов выбирать. 0 — без ограничения.", DrawTaskTopK },
        { AiSection, "Задачи: min-p", "Отсекает слишком маловероятные слова.", DrawTaskMinP },
        { AiSection, "Задачи: штраф за повторы", "100% — без штрафа, выше — меньше повторов.", DrawTaskRepeat },
        { AiSection, "Задачи: длина ответа", "Предел в токенах вместе с размышлениями.", DrawTaskTokens },
        { AiSection, "Seed", "Одинаковое число — повторяемые ответы, −1 — каждый раз случайно.", DrawAiSeed },
        { AiSection, "Загружать при запуске", "Выключено — видеокарта свободна, пока не спросишь; первый ответ на 5–15 секунд дольше.", DrawAiPreload, true },
        { AiSection, "Освобождать в играх", "Если минуту нет вопросов и на весь экран открыта игра или видео — нейросеть выгружается.", DrawGameUnload, true },
        { AiSection, "Глаза модели", "Понимать картинки. Загружаются только когда приходит снимок экрана (+0,9 ГБ видеопамяти до выгрузки).", DrawAiVision, true },
        { AiSection, "Слои на видеокарте", "99 — всё на видеокарте, 0 — только процессор. Не хватает видеопамяти — убавь. Нужен перезапуск.", DrawGpuLayers },
        { AiSection, "Потоки процессора", "0 — движок выберет сам. Нужен перезапуск.", DrawAiThreads },
        { AiSection, "Размер контекста", "Сколько нейросеть помнит за раз, делится между запросами. Больше — больше памяти. Нужен перезапуск.", DrawAiContext },
        { AiSection, "Запросов одновременно", "2 — можно говорить с Аней, пока она решает задачу для телеграма. Нужен перезапуск.", DrawAiParallel },
        { AiSection, "Свои параметры движка", "Для знающих: дополнительные аргументы llama-server. Ошибка в них не даст нейросети запуститься.", DrawEngineArgs },
        { AiSection, "Применить", "Настройки ответов работают сразу, настройки движка — после перезапуска.", DrawAiApply },

        { SearchSection, "Поисковик", "Куда Аня отправляет «найди …», «загугли …» и «найди в интернете …».", DrawSearchEngine },

        { Overlay, "Поверх всех окон", "Окно Ани не прячется за другими программами — удобно поверх игры.", DrawAlwaysOnTop, true },
        { Overlay, "Быстрые фразы", "Подсказки-кнопки под полем ввода.", DrawQuickPhrases, true },

        { WidgetsSection, "Погода", "Слева от сферы: 4 часа, а при наведении — весь день.", DrawWeatherWidget, true },
        { WidgetsSection, "Город", "Для погоды, прогноза и виджета.", DrawCity },
        { WidgetsSection, "Курсы валют", "Карточка на главном экране, под блоками системы и музыки.", DrawRatesWidget, true },
        { WidgetsSection, "Валюта курсов", "В какой валюте показывать. По умолчанию — валюта страны твоего города.", DrawRatesCurrency },

        { Connections, "Телеграм-бот", "Управляй компьютером с телефона: снимок экрана, «реши с экрана», громкость, блокировка, "
                                       "«выключить, но остаться на связи».", DrawTelegram, true },
        { Connections, "Токен бота", "Выдаёт @BotFather. Никому не показывай — с ним можно управлять ботом.", DrawTelegramToken },
        { Privacy, "Кому можно", "ID в телеграме того, кого бот слушается. Всем остальным он отвечает отказом.", DrawTelegramOwner },
        { Connections, "Уведомления в телеграме", "Сообщения, которые бот шлёт сам: запуск, свежая кнопка пульта, блокировка компьютера. Выключено — бот пишет только в ответ.", DrawGreet, true },
        { Connections, "Пульт в телеграме", "Приложение в меню бота: быстрые команды, запуск программ и удалённый рабочий стол. Вход только для владельца. "
                                            "Пока включено, работает защищённый туннель cloudflared.", DrawRemoteDesktop, true },
        { Privacy, "Блокировать при «Выключить»", "Надёжнее, но тогда «☀ Включить» покажет экран блокировки — пароль придётся вводить "
                                                     "за компьютером: снять блокировку с телефона Windows не даёт.", DrawLockOnStandby, true },
        { Connections, "Статус в Discord", "«Играет в ASTRIX», а когда играет музыка или видео — «Слушает» или «Смотрит».", DrawDiscord, true },
        { Connections, "ID приложения Discord", "Создай приложение «ASTRIX» на discord.com/developers и вставь его Application ID. "
                                                "Картинка — ассет «anya» в Rich Presence → Art Assets.", DrawDiscordApp },
        { Connections, "Показывать музыку", "«Слушает: трек — исполнитель» из Яндекс Музыки, Spotify и других плееров.", DrawDiscordMusic, true },
        { SystemSection, "Полная кастомизация", "Сфера, погода, карточки справа, поле ввода и шторка разговора — тяни мышью куда хочешь, "
                                                "а за уголок — меняй размер. Выключи галочку или нажми «готово», чтобы закончить.", DrawEditBlocks },
        { SystemSection, "Настройки по умолчанию", "Если что-то сломалось или запуталась — вернуть всё как было при установке.", DrawResetAll },
        { Updates, "Удалить программу", "Полностью: все данные, настройки и автозапуск. Перед удалением спрошу ещё раз.", DrawRemoveAll },
        { Privacy, "Снимки экрана", "Разрешить боту и нейросети снимать экран по запросу с телефона.", DrawPrivacyScreen, true },
        { Privacy, "Журнал", "Файл с действиями программы лежит только на этом компьютере.", DrawClearLog },
        { AiSection, "Что помнить обо мне", "Интересы, привычки, как к тебе обращаться. Нейросеть учитывает это в каждом ответе.", DrawAboutMe },
        { SearchSection, "Поиск файлов", "Как выполнять «найди файл …»: своим поиском ASTRIX, через Windows или через Everything.", DrawFileSearch },
        { Updates, "Версия", "Проверить, вышла ли новая версия на GitHub.", DrawUpdates },
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
        const float itemHeight = Px(37), pad = Px(7);
        const bool compact = width < Px(120);
        const ImVec2 max(pos.x + width, pos.y + pad * 2 + itemHeight * static_cast<float>(kSections));
        dl->AddRectFilled(pos, max, Theme::Surface(1.12f), Px(20));
        dl->AddRect(pos, max, Theme::Overlay(14), Px(20));

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
            const float k = Theme::Ease(ImGui::GetIO().DeltaTime, 8.7f);
            s_hover[i] += ((hovered ? 1.0f : 0.0f) - s_hover[i]) * k;
            s_pick[i] += ((selected ? 1.0f : 0.0f) - s_pick[i]) * k;
            const float fill = std::max(s_pick[i] * 22.0f, s_hover[i] * 12.0f);
            if (fill > 0.5f) dl->AddRectFilled(a, b, Theme::Overlay(static_cast<int>(s_hover[i] * 12.0f)), Px(12));
            if (s_pick[i] > 0.01f) { // выбранный раздел виден сразу: залит любимым цветом и обведён
                dl->AddRectFilled(a, b, Theme::Accent().WithAlpha(0.34f * s_pick[i]), Px(12));
                dl->AddRect(a, b, Theme::Accent().WithAlpha(0.75f * s_pick[i]), Px(12), 0, Px(1.2f));
            }
            if (s_pick[i] > 0.02f) {
                const float half = (b.y - a.y - Px(20)) * 0.5f * s_pick[i], mid = (a.y + b.y) * 0.5f; // полоска растёт от центра
                dl->AddRectFilled(ImVec2(a.x, mid - half), ImVec2(a.x + Px(3), mid + half), Theme::Accent().WithAlpha(s_pick[i]), Px(2));
            }
            const ImU32 color = selected ? Theme::kInk : Theme::kInkMuted;
            const float cy = (a.y + b.y) * 0.5f;
            Widgets::DrawIcon(dl, kInfo[i].icon, ImVec2(compact ? (a.x + b.x) * 0.5f : a.x + Px(20), cy), Px(15), color);
            if (compact) {
                if (hovered) ImGui::SetTooltip("%s", kInfo[i].name);
            } else {
                ImFont* font = selected ? Theme::title : Theme::semibold;
                Widgets::Text(dl, font, ImVec2(a.x + Px(38), cy - font->LegacySize * 0.55f), color, kInfo[i].name);
            }
        }
    }

    bool DrawSearchField(ImDrawList* dl, ImVec2 pos, float width) {
        const float height = Px(38);
        static bool s_active = false; // поле в фокусе (с прошлого кадра) — подсветить рамку
        const bool active = s_active;
        const ImVec2 max(pos.x + width, pos.y + height);
        dl->AddRectFilled(pos, max, Theme::Overlay(10), height * 0.5f);
        dl->AddRect(pos, max, active ? Theme::Palette(0).WithAlpha(0.6f) : Theme::Overlay(24), height * 0.5f, 0, Px(1.2f));
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

    // Текст по центру с переносом по словам; возвращает высоту
    float CenteredText(ImDrawList* dl, ImFont* font, float centerX, float top, float maxWidth, ImU32 color, const std::string& text) {
        std::vector<std::string> lines(1);
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find(' ', at);
            if (end == std::string::npos) end = text.size();
            const std::string word = text.substr(at, end - at);
            const std::string candidate = lines.back().empty() ? word : lines.back() + " " + word;
            if (!lines.back().empty() && Widgets::TextSize(font, candidate.c_str()).x > maxWidth) lines.push_back(word);
            else lines.back() = candidate;
            at = end + 1;
        }
        const float height = font->LegacySize * 1.3f;
        for (size_t i = 0; i < lines.size(); ++i)
            Widgets::Text(dl, font, ImVec2(centerX - Widgets::TextSize(font, lines[i].c_str()).x * 0.5f, top + height * i), color, lines[i].c_str());
        return height * lines.size();
    }

    // «Обучение»: что на что влияет — со ссылками на сами настройки
    void DrawLearnGuide(float width) {
        struct Link { const char* what; const char* title; };
        static const Link kLinks[] = {
            { "Манера речи: строгая, дружелюбная, милая или твоя собственная.", "Характер" },
            { "Правила на каждый ответ: как отвечать и чего избегать.", "Свои указания" },
            { "Факты о тебе. Нейросеть учитывает их в каждом разговоре, сама ничего не запоминает.", "Что помнить обо мне" },
            { "Как к тебе обращаться и как произносить ник вслух.", "Как произносить имя вслух" },
            { "Выше — живее и неожиданнее, ниже — точнее.", "Разговор: температура" },
            { "Когда модель сначала думает, а потом отвечает.", "Размышления" },
            { "Какая модель отвечает на вопросы.", "Нейросеть" },
        };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rowWidth = std::min(width, Px(620));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float centerX = start.x + rowWidth * 0.5f;
        float y = start.y;
        for (const Link& link : kLinks) {
            int index = -1;
            for (int i = 0; i < static_cast<int>(std::size(kItems)); ++i) if (std::strcmp(kItems[i].title, link.title) == 0) index = i;
            if (index < 0) continue;
            y += CenteredText(dl, Theme::semibold, centerX, y, rowWidth, Theme::kInkSoft, link.what) + Px(1);
            const std::string label = std::string(kInfo[kItems[index].section].name) + " → " + link.title;
            const ImVec2 size = Widgets::TextSize(Theme::semibold, label.c_str());
            const ImVec2 at(centerX - size.x * 0.5f, y);
            ImGui::SetCursorScreenPos(at);
            ImGui::PushID(link.title);
            if (ImGui::InvisibleButton("##link", size)) g_revealRequest = index;
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            const ImU32 color = Theme::Accent().Mix({ 255, 255, 255 }, hovered ? 0.45f : 0.15f).WithAlpha(1.0f);
            Widgets::Text(dl, Theme::semibold, at, color, label.c_str());
            dl->AddLine(ImVec2(at.x, at.y + size.y + Px(1)), ImVec2(at.x + size.x, at.y + size.y + Px(1)), color, Px(1));
            y += size.y + Px(9);
        }
        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy(ImVec2(rowWidth, y - start.y));
    }

    // Карточка раздела: заголовок, пояснение и его настройки (только подходящие под поиск)
    void DrawCard(Section section, const std::wstring& query, float width) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pad = Px(22), inner = width - pad * 2;
        const ImVec2 start = ImGui::GetCursorScreenPos();

        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + pad));
        ImGui::BeginGroup();
        const ImVec2 head = ImGui::GetCursorScreenPos();
        const float nameWidth = Widgets::TextSize(Theme::hero, kInfo[section].name).x, nameLeft = head.x + (inner - nameWidth + Px(34)) * 0.5f;
        Widgets::DrawIcon(dl, kInfo[section].icon, ImVec2(nameLeft - Px(20), head.y + Theme::hero->LegacySize * 0.55f), Px(20), Theme::kInk);
        Widgets::Text(dl, Theme::hero, ImVec2(nameLeft, head.y), Theme::kInk, kInfo[section].name);
        const float aboutHeight = CenteredText(dl, Theme::semibold, head.x + inner * 0.5f, head.y + Theme::hero->LegacySize + Px(6), inner, Theme::kInkMuted, kInfo[section].about);
        ImGui::Dummy(ImVec2(inner, Theme::hero->LegacySize + Px(6) + aboutHeight + Px(4)));
        const bool grid = section == WidgetsSection; // виджеты — блоками, как в сценариях
        const float gridLeft = head.x;
        float gridTop = ImGui::GetCursorScreenPos().y, gridRow = 0;
        int blockIndex = 0;

        bool first = true;
        ImVec2 glowMin(0, 0), glowMax(0, 0);
        for (const Item& item : kItems) {
            if (item.section != section || !Matches(item, query)) continue;
            const bool glowing = &item - kItems == g_reveal;
            if (grid) {
                const float gap = Px(12), blockW = (inner - gap) * 0.5f, blockPad = Px(14);
                static float s_rowHeight[16] = {};
                static std::unordered_map<std::string, float> s_blockControl;
                const int column = blockIndex % 2, rowIndex = std::min(blockIndex / 2, 15);
                const ImVec2 a(gridLeft + column * (blockW + gap), gridTop), b(a.x + blockW, a.y + std::max(s_rowHeight[rowIndex], Px(90)));
                dl->AddRectFilled(a, b, Theme::Overlay(11), Px(16));
                dl->AddRect(a, b, Theme::Overlay(20), Px(16), 0, Px(1));
                dl->AddRectFilled(ImVec2(a.x + Px(16), a.y), ImVec2(b.x - Px(16), a.y + Px(3)), Theme::Accent().WithAlpha(0.9f), Px(2));
                ImGui::PushID(item.title);
                const float blockCenter = a.x + blockW * 0.5f;
                float blockY = a.y + blockPad;
                blockY += CenteredText(dl, Theme::display, blockCenter, blockY, blockW - blockPad * 2, Theme::kInk, Text::Brand(item.title, Config::Get().assistant));
                blockY += CenteredText(dl, Theme::semibold, blockCenter, blockY + Px(2), blockW - blockPad * 2, Theme::kInkMuted, Text::Brand(item.hint, Config::Get().assistant)) + Px(10);
                float& control = s_blockControl[item.title];
                ImGui::SetCursorScreenPos(ImVec2(blockCenter - control * 0.5f, blockY));
                ImGui::BeginGroup();
                item.draw(blockW - blockPad * 2);
                ImGui::EndGroup();
                control = ImGui::GetItemRectSize().x;
                gridRow = std::max(gridRow, ImGui::GetItemRectMax().y + blockPad - a.y);
                ImGui::PopID();
                if (glowing) { glowMin = a; glowMax = b; }
                if (column == 1) { s_rowHeight[rowIndex] = gridRow; gridTop += gridRow + gap; gridRow = 0; }
                else s_rowHeight[rowIndex] = std::max(s_rowHeight[rowIndex], gridRow); // высоту ряда задаёт самый высокий блок
                ++blockIndex;
                continue;
            }
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (!first) {
                dl->AddLine(ImVec2(at.x, at.y), ImVec2(at.x + inner, at.y), Theme::Overlay(10), Px(1));
            }
            ImGui::Dummy(ImVec2(0, Px(first ? 2.0f : 7.0f)));
            first = false;

            ImGui::PushID(item.title);
            const ImVec2 row = ImGui::GetCursorScreenPos();
            const float centerX = row.x + inner * 0.5f;
            float y = row.y + CenteredText(dl, Theme::display, centerX, row.y, inner, Theme::kInk, Text::Brand(item.title, Config::Get().assistant));
            y += Px(3);
            y += CenteredText(dl, Theme::semibold, centerX, y, std::min(inner, Px(620)), Theme::kInkMuted, Text::Brand(item.hint, Config::Get().assistant));
            y += Px(7);
            // управление — тоже по центру: ширину узнаём на прошлом кадре
            static std::unordered_map<std::string, float> s_widths;
            float& controlWidth = s_widths[item.title];
            ImGui::SetCursorScreenPos(ImVec2(row.x + (inner - controlWidth) * 0.5f, y));
            ImGui::BeginGroup();
            item.draw(inner);
            ImGui::EndGroup();
            controlWidth = ImGui::GetItemRectSize().x;
            ImGui::Dummy(ImVec2(0, Px(5)));
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
        if (grid) {
            ImGui::SetCursorScreenPos(ImVec2(gridLeft, gridTop + gridRow + Px(10)));
            ImGui::Dummy(ImVec2(inner, 0));
        }
        ImGui::EndGroup();

        const ImVec2 cardMax(start.x + width, ImGui::GetItemRectMax().y + pad - Px(14));
        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled(start, cardMax, Theme::Surface(1.12f), Px(26));
        dl->AddRect(start, cardMax, Theme::Overlay(16), Px(26));
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
    g_anim += ((g_open ? 1.0f : 0.0f) - g_anim) * Theme::Ease(dt, 8.7f);
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
    DrawSidebar(dl, ImVec2(origin.x + Px(20), contentTop), sideWidth); // верх меню — на одной линии с карточкой настроек
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
    g_contentAnim += (1.0f - g_contentAnim) * Theme::Ease(dt, 5.6f);
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
