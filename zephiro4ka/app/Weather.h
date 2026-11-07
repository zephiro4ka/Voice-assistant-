#pragma once
// Погода (Open-Meteo, модель best_match — лучший прогноз для места, бесплатно и без ключей).
// Город и время поездок — в anya.ini, раздел [weather]. Прогноз обновляется в фоне раз в 10 минут.
//   «Какая погода?», «что сейчас на улице» → прямо сейчас + ближайшие часы
//   «Какая будет погода, когда я буду в колледже» → по дороге в колледж (08:00) и домой (15:10)
//   «Какая погода на весь день», «в целом» → за сегодня: от/до, в основном
//   «Какая погода завтра / послезавтра» → за тот день и поездки в колледж
//   «Когда будет дождь?», «нужен ли зонт» → ближайшие осадки на неделе

#include <optional>
#include <string>
#include <vector>

namespace Weather {
    void Start(); // фоновое обновление прогноза (для виджета и мгновенных ответов)

    // Ответ на вопрос о погоде; пусто — фраза не про погоду
    std::optional<std::string> Answer(const std::string& question);

    // ---- для виджета
    enum class Sky { Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Snow, Thunder };

    struct HourView {
        int hour;          // 0–23
        float temperature;
        int rainChance;    // %
        Sky sky;
        bool night;
    };

    struct Today {
        bool ready = false;
        std::string city;
        float temperature = 0, feelsLike = 0, wind = 0; // сейчас; ветер в км/ч
        int humidity = 0;
        Sky sky = Sky::Clear;
        bool night = false;
        std::string description;       // «переменная облачность»
        float low = 0, high = 0;       // за сегодня
        int currentHour = 0;
        std::vector<HourView> around;  // час назад, сейчас, +1, +2
        std::vector<HourView> day;     // все 24 часа сегодня
    };

    Today Snapshot(); // мгновенно, без сети

    // ---- для настроек
    struct City { std::string name, country, latitude, longitude; };
    std::optional<City> FindCity(const std::string& name); // по названию (сеть, ~0.5 с — звать не из UI-потока)
    void SetCity(const City& city);                        // сохранить в anya.ini и сразу обновить прогноз
    void Reload();                                         // перечитать настройки и скачать прогноз заново
}
