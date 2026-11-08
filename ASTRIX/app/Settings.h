#pragma once
// Настройки пользователя. Хранятся в anya.ini рядом с exe.

#include <functional>
#include <string>
#include <utility>
#include <vector>

// Куда отправлять «найди …» и «загугли …»
enum class SearchEngine { Google = 0, Yandex = 1, DuckDuckGo = 2, Bing = 3 };

// Анимированный фон главного экрана
enum class Backdrop { Aurora = 0, Threads = 1, Waves = 2, Stars = 3, Calm = 4 };
// Тема: цвет фона и панелей
enum class Look { Night = 0, Graphite = 1, Coal = 2, Midnight = 3, Custom = 4 };

// Характер Ани — как она разговаривает (через нейросеть)
enum class Personality { Strict = 0, Balanced = 1, Sweet = 2, Custom = 3 };

struct Settings {
    std::string assistant = "Аня";     // как зовут помощницу: так она отзывается и подписывается в окне, Discord и телеграме
    std::string name = "друг";
    std::string spokenName;               // как имя звучит вслух (ник не всегда читается красиво)
    std::wstring voice;       // имя голоса SAPI; пусто = авто (женский русский)
    int pitch = 6;            // -10..10, высота голосов Windows
    int neuralPitch = 0;      // -6..6, высота нейроголоса (0 — естественно, без обработки)
    int rate = 1;             // -10..10
    bool wakeWord = false;    // true: слушаю всегда и жду «Аня»; false: нажми и говори
    std::string model;        // путь к модели нейросети (.gguf); пусто = авто
    Personality personality = Personality::Balanced;
    std::string customPrompt;  // свой характер, если выбран Personality::Custom
    int silenceMs = 650;      // сколько тишины считать концом фразы (меньше — режет фразу на паузах)
    bool bodySounds = true;   // «Будь здоров!» на чих и «Всё в порядке?» на кашель
    SearchEngine search = SearchEngine::Google;
    bool quickPhrases = false; // подсказки-фразы под полем ввода
    bool weatherWidget = true;

    // внешний вид
    Look look = Look::Night;
    int lookColor = 0x1C1030;        // цвет своей темы (0xRRGGBB)
    int accent = 0;                  // любимый цвет: 0…5 — готовые, 6 — свой
    int accentColor = 0xA78BFA;      // свой любимый цвет
    Backdrop backdrop = Backdrop::Aurora;
    int backdropSpeed = 100;         // скорость анимации фона, %
    bool animateInactive = false;    // анимировать фон, когда окно не в фокусе
    int backgroundColor = -1;        // свой цвет фона; -1 — по теме
    int animationColor = -1;         // свой цвет анимации; -1 — по настроению Ани
    bool glass = true;               // полупрозрачные панели
    bool buttonPlates = false;       // подложка под кнопками окна
    bool tray = false;               // крестик сворачивает в трей
    int windowEffect = 2;            // эффект окна Windows 11: 0 — нет, 1 — Mica, 2 — акрил (размытие того, что за окном)
    int windowOpacity = 70;          // плотность фона Ани поверх эффекта, %
    std::string wallpaper;           // свои обои (путь к картинке); пусто — без обоев
    int wallpaperDim = 45;           // затемнение обоев, % — чтобы текст читался
    bool autoColors = false;         // цвета по обоям
    int savedLook = 0, savedLookColor = 0x1C1030, savedAccent = 0, savedAccentColor = 0xA78BFA, savedAnimationColor = -1; // до «цветов по обоям»
};

namespace Config {
    void Load();
    void Save();
    Settings Get();                                          // копия — безопасно из любого потока
    void Update(const std::function<void(Settings&)>& edit, bool save = true);

    // Значение из любого раздела anya.ini; если его нет — записываем значение по умолчанию,
    // чтобы было видно, что можно поменять
    std::string Value(const char* section, const char* key, const char* fallback);
    void SetValue(const char* section, const char* key, const std::string& value);

    // Контакты из раздела [contacts]: как имя звучит → ник в мессенджере («зефирочник» → «zephiro4nik»)
    std::vector<std::pair<std::string, std::string>> Contacts();
}
