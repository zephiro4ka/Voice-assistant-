#include "Weather.h"

#include "Http.h"
#include "Json.h"
#include "Log.h"
#include "Settings.h"
#include "State.h"
#include "TextUtils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace {
    constexpr Clock kCacheMs = 10 * 60 * 1000;   // прогноз обновляем раз в 10 минут
    constexpr Clock kRetryMs = 60 * 1000;        // нет интернета — пробуем снова через минуту
    constexpr int kRainChance = 50;              // с какой вероятности говорим «будет дождь»
    constexpr int kForecastDays = 7;

    struct Hour {
        int year, month, day, hour;
        double temperature;
        int rainChance;   // %
        int code;         // код погоды WMO
        bool isDay;
    };

    struct Current {
        bool valid = false;
        double temperature = 0, feelsLike = 0, wind = 0;
        int humidity = 0, code = 0;
        bool isDay = true;
    };

    struct Forecast {
        std::string city, cityIn;
        int utcOffsetSeconds = 0; // пояс города (Ташкент — UTC+5)
        bool hasOffset = false;
        Current current;
        std::vector<Hour> hours; // по местному времени города (timezone=auto)
    };

    struct Moment { int year, month, day, hour, minute, weekday; };

    std::mutex g_mutex;
    Forecast g_cache;
    Clock g_cachedAt = 0;
    Weather::Today g_today; // готовый вид для виджета

    // ------------------------------------------------------------ настройки
    struct Place {
        std::string city, cityIn, latitude, longitude; // cityIn — «в Ташкенте»
        int toCollegeHour, toCollegeMinute, homeHour, homeMinute;
    };

    void ParseTime(const std::string& text, int& h, int& m) {
        h = std::atoi(text.c_str());
        const size_t colon = text.find(':');
        m = colon == std::string::npos ? 0 : std::atoi(text.c_str() + colon + 1);
    }

    Place LoadPlace() {
        Place p;
        p.city = Config::Value("weather", "city", "Москва");
        const std::string cityIn = p.city == "Ташкент" ? "в Ташкенте" : "в городе " + p.city;
        p.cityIn = Config::Value("weather", "city_in", cityIn.c_str());
        p.latitude = Config::Value("weather", "latitude", "41.31");
        p.longitude = Config::Value("weather", "longitude", "69.28");
        ParseTime(Config::Value("weather", "to_college", "08:00"), p.toCollegeHour, p.toCollegeMinute);
        ParseTime(Config::Value("weather", "back_home", "15:10"), p.homeHour, p.homeMinute);
        return p;
    }

    // ------------------------------------------------------------ загрузка прогноза
    // Массив из блока "hourly": "key":[1.5,2,null,…] → числа (null → NaN)
    std::vector<double> NumberArray(const std::string& json, size_t from, const char* key) {
        std::vector<double> out;
        const std::string needle = std::string("\"") + key + "\":[";
        size_t pos = json.find(needle, from);
        if (pos == std::string::npos) return out;
        pos += needle.size();
        while (pos < json.size() && json[pos] != ']') {
            if (json.compare(pos, 4, "null") == 0) { out.push_back(NAN); pos += 4; }
            else { char* end = nullptr; out.push_back(std::strtod(json.c_str() + pos, &end)); pos = end - json.c_str(); }
            if (pos < json.size() && json[pos] == ',') ++pos;
        }
        return out;
    }

    // Число из блока "current":{…,"key":12.3,…}
    double NumberField(const std::string& json, size_t from, size_t to, const char* key) {
        const std::string needle = std::string("\"") + key + "\":";
        const size_t pos = json.find(needle, from);
        if (pos == std::string::npos || pos > to) return NAN;
        return std::strtod(json.c_str() + pos + needle.size(), nullptr);
    }

    bool Fetch(const Place& place, Forecast& result) {
        const std::wstring path = L"/v1/forecast?latitude=" + Text::ToWide(place.latitude) + L"&longitude=" + Text::ToWide(place.longitude) +
            L"&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
            L"&hourly=temperature_2m,precipitation_probability,weather_code,is_day"
            L"&models=best_match&timezone=auto&past_days=1&forecast_days=" + std::to_wstring(kForecastDays);
        const std::string json = Http::HttpsGet(L"api.open-meteo.com", path);
        const size_t hourly = json.find("\"hourly\":{");
        if (hourly == std::string::npos) return false;

        // время: "2026-11-04T08:00" — 16 символов
        std::vector<Hour> hours;
        const std::string timeKey = "\"time\":[";
        for (size_t pos = json.find(timeKey, hourly) + timeKey.size(); pos + 17 < json.size() && json[pos] == '"'; pos += 19) {
            Hour h{};
            h.year = std::atoi(json.c_str() + pos + 1);
            h.month = std::atoi(json.c_str() + pos + 6);
            h.day = std::atoi(json.c_str() + pos + 9);
            h.hour = std::atoi(json.c_str() + pos + 12);
            hours.push_back(h);
        }
        const auto temperature = NumberArray(json, hourly, "temperature_2m");
        const auto chance = NumberArray(json, hourly, "precipitation_probability");
        const auto code = NumberArray(json, hourly, "weather_code");
        const auto isDay = NumberArray(json, hourly, "is_day");
        if (hours.empty() || temperature.size() != hours.size() || code.size() != hours.size()) return false;
        for (size_t i = 0; i < hours.size(); ++i) {
            hours[i].temperature = temperature[i];
            hours[i].rainChance = i < chance.size() && !std::isnan(chance[i]) ? static_cast<int>(chance[i]) : 0;
            hours[i].code = std::isnan(code[i]) ? 0 : static_cast<int>(code[i]);
            hours[i].isDay = i < isDay.size() ? isDay[i] != 0.0 : (hours[i].hour >= 7 && hours[i].hour < 18);
        }

        Current current;
        if (const size_t start = json.find("\"current\":{"); start != std::string::npos) {
            const size_t end = json.find('}', start);
            current.temperature = NumberField(json, start, end, "temperature_2m");
            current.feelsLike = NumberField(json, start, end, "apparent_temperature");
            current.wind = NumberField(json, start, end, "wind_speed_10m");
            const double humidity = NumberField(json, start, end, "relative_humidity_2m");
            const double weather = NumberField(json, start, end, "weather_code");
            const double day = NumberField(json, start, end, "is_day");
            current.valid = !std::isnan(current.temperature) && !std::isnan(weather);
            current.humidity = std::isnan(humidity) ? 0 : static_cast<int>(humidity);
            current.code = std::isnan(weather) ? 0 : static_cast<int>(weather);
            current.isDay = std::isnan(day) || day != 0.0;
            if (std::isnan(current.feelsLike)) current.feelsLike = current.temperature;
            if (std::isnan(current.wind)) current.wind = 0;
        }

        if (const size_t at = json.find("\"utc_offset_seconds\":"); at != std::string::npos) {
            result.utcOffsetSeconds = std::atoi(json.c_str() + at + 21);
            result.hasOffset = true;
        }
        result.city = place.city;
        result.cityIn = place.cityIn;
        result.current = current;
        result.hours = std::move(hours);
        return true;
    }

    // Свежий прогноз из кэша; устарел — качаем (без блокировки: виджет в это время рисуется дальше)
    bool GetForecast(const Place& place, Forecast& forecast) {
        {
            std::lock_guard lock(g_mutex);
            if (g_cachedAt && NowMs() - g_cachedAt < kCacheMs) {
                forecast = g_cache;
                return true;
            }
        }
        Forecast fresh;
        const bool ok = Fetch(place, fresh);
        std::lock_guard lock(g_mutex);
        if (ok) {
            g_cache = std::move(fresh);
            g_cachedAt = NowMs();
        }
        if (g_cachedAt == 0) return false;
        forecast = g_cache; // нет сети — отвечаем по последнему прогнозу
        return true;
    }

    // ------------------------------------------------------------ сейчас
    // Время города из прогноза: точное UTC компьютера + пояс города. Часовой пояс Windows может стоять другой
    // (у Ани он UTC+1, а в Ташкенте UTC+5) — тогда часы на экране не совпадают с ташкентскими 8:00 и 15:10.
    // Без API Windows — только std::chrono, чтобы модуль переехал на Linux как есть
    Moment Now(const Forecast& f) {
        using namespace std::chrono;
        auto t = floor<minutes>(system_clock::now());
        if (f.hasOffset) t += duration_cast<minutes>(seconds(f.utcOffsetSeconds)); // без прогноза — UTC (ответы всё равно ждут прогноз)
        const auto day = floor<days>(t);
        const year_month_day date{ day };
        const hh_mm_ss clock{ t - day };
        return { static_cast<int>(date.year()), static_cast<int>(static_cast<unsigned>(date.month())), static_cast<int>(static_cast<unsigned>(date.day())),
                 static_cast<int>(clock.hours().count()), static_cast<int>(clock.minutes().count()), static_cast<int>(weekday{ day }.c_encoding()) };
    }

    // Номер дня по календарю (для разницы дат)
    int DayNumber(int y, int m, int d) {
        y -= m <= 2;
        const int era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
        const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy;
    }

    // 0 — сегодня, 1 — завтра, -1 — вчера (в прогнозе есть и вчерашний день — для «часа назад» после полуночи)
    int DayIndex(const Hour& h, const Moment& now) { return DayNumber(h.year, h.month, h.day) - DayNumber(now.year, now.month, now.day); }

    // Индекс часа «сейчас» в прогнозе (или -1)
    int NowIndex(const Forecast& f, const Moment& now) {
        for (size_t i = 0; i < f.hours.size(); ++i) {
            const Hour& h = f.hours[i];
            if (h.year == now.year && h.month == now.month && h.day == now.day && h.hour == now.hour) return static_cast<int>(i);
        }
        return -1;
    }

    // Часы нужного дня: 0 — сегодня, 1 — завтра…
    std::vector<const Hour*> DayHours(const Forecast& f, const Moment& now, int dayIndex) {
        std::vector<const Hour*> out;
        for (const auto& h : f.hours)
            if (DayIndex(h, now) == dayIndex) out.push_back(&h);
        return out;
    }

    const Hour* At(const std::vector<const Hour*>& day, int hour) {
        for (const Hour* h : day)
            if (h->hour == hour) return h;
        return nullptr;
    }

    // ------------------------------------------------------------ слова
    const char* Plural(int n, const char* one, const char* few, const char* many) {
        n = std::abs(n);
        const int r100 = n % 100, r10 = n % 10;
        if (r100 >= 11 && r100 <= 14) return many;
        return r10 == 1 ? one : (r10 >= 2 && r10 <= 4) ? few : many;
    }

    std::string Number(int v) { return v < 0 ? "минус " + std::to_string(-v) : std::to_string(v); }

    // «12 градусов», «21 градус», «минус 3 градуса»
    std::string Degrees(double t) {
        const int v = static_cast<int>(std::lround(t));
        return Number(v) + " " + Plural(v, "градус", "градуса", "градусов");
    }

    // после «от» и «до»: «от 9 градусов до 21 градуса»
    std::string DegreesAfterPreposition(double t) {
        const int v = static_cast<int>(std::lround(t));
        const bool one = std::abs(v) % 10 == 1 && std::abs(v) % 100 != 11;
        return Number(v) + (one ? " градуса" : " градусов");
    }

    bool IsRain(int code) { return (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95; }
    bool IsSnow(int code) { return (code >= 71 && code <= 77) || code == 85 || code == 86; }

    std::string Describe(int code) {
        static const std::map<int, const char*> kNames = {
            { 0, "ясно" }, { 1, "почти ясно" }, { 2, "переменная облачность" }, { 3, "пасмурно" }, { 45, "туман" }, { 48, "туман с изморозью" },
            { 51, "лёгкая морось" }, { 53, "морось" }, { 55, "сильная морось" }, { 56, "ледяная морось" }, { 57, "ледяная морось" },
            { 61, "небольшой дождь" }, { 63, "дождь" }, { 65, "сильный дождь" }, { 66, "ледяной дождь" }, { 67, "ледяной дождь" },
            { 71, "небольшой снег" }, { 73, "снег" }, { 75, "сильный снег" }, { 77, "снежная крупа" },
            { 80, "небольшой ливень" }, { 81, "ливень" }, { 82, "сильный ливень" }, { 85, "снегопад" }, { 86, "сильный снегопад" },
            { 95, "гроза" }, { 96, "гроза с градом" }, { 99, "гроза с сильным градом" },
        };
        const auto it = kNames.find(code);
        return it != kNames.end() ? it->second : "облачно";
    }

    Weather::Sky SkyOf(int code) {
        using Weather::Sky;
        if (code <= 1) return Sky::Clear;
        if (code == 2) return Sky::PartlyCloudy;
        if (code == 3) return Sky::Cloudy;
        if (code == 45 || code == 48) return Sky::Fog;
        if (code >= 51 && code <= 57) return Sky::Drizzle;
        if (IsSnow(code)) return Sky::Snow;
        if (code >= 95) return Sky::Thunder;
        if (IsRain(code)) return Sky::Rain;
        return Sky::Cloudy;
    }

    // «в 8 утра», «в 3 часа дня», «в 15:10» (если минуты не круглые)
    std::string AtTime(int hour, int minute) {
        if (minute != 0) return "в " + std::to_string(hour) + ":" + (minute < 10 ? "0" : "") + std::to_string(minute);
        if (hour == 0) return "в полночь";
        if (hour == 12) return "в полдень";
        const int h12 = hour > 12 ? hour - 12 : hour;
        const char* part = hour < 5 ? "ночи" : hour < 12 ? "утра" : hour < 17 ? "дня" : "вечера";
        return "в " + std::to_string(h12) + " " + Plural(h12, "час", "часа", "часов") + " " + part;
    }

    std::string DayName(int dayIndex, int weekday) {
        static const char* kDays[] = { "в воскресенье", "в понедельник", "во вторник", "в среду", "в четверг", "в пятницу", "в субботу" };
        if (dayIndex == 0) return "сегодня";
        if (dayIndex == 1) return "завтра";
        if (dayIndex == 2) return "послезавтра";
        return kDays[(weekday + dayIndex) % 7];
    }

    std::string Capital(std::string s) { return Text::Capitalize(s); }

    std::string HourReport(const Hour& h) {
        std::string s = Degrees(h.temperature) + ", " + Describe(h.code);
        if (h.rainChance >= 30 && !IsRain(h.code) && !IsSnow(h.code)) s += ", вероятность осадков " + std::to_string(h.rainChance) + "%";
        return s;
    }

    bool Wet(const Hour& h) { return IsRain(h.code) || IsSnow(h.code) || h.rainChance >= kRainChance; }

    // ------------------------------------------------------------ ответы
    // «Какая погода сейчас?» — что за окном и как поменяется в ближайшие часы
    std::string NowReport(const Forecast& f, const Moment& now) {
        const int index = NowIndex(f, now);
        if (index < 0 && !f.current.valid) return "Не получилось разобрать прогноз";
        const Current& c = f.current;
        const double temperature = c.valid ? c.temperature : f.hours[index].temperature;
        const int code = c.valid ? c.code : f.hours[index].code;

        std::string answer = "Сейчас " + f.cityIn + " " + Degrees(temperature) + ", " + Describe(code);
        if (c.valid && std::fabs(c.feelsLike - c.temperature) >= 2.0) answer += ", ощущается как " + Degrees(c.feelsLike);
        if (c.valid && c.wind >= 25.0) answer += ", сильный ветер";
        answer += ".";

        if (index >= 0 && index + 2 < static_cast<int>(f.hours.size())) {
            const Hour& in1 = f.hours[index + 1];
            const Hour& in2 = f.hours[index + 2];
            const long t0 = std::lround(temperature), t2 = std::lround(in2.temperature);
            if (t2 > t0 + 1) answer += " Через пару часов потеплеет до " + DegreesAfterPreposition(in2.temperature) + ".";
            else if (t2 < t0 - 1) answer += " Через пару часов похолодает до " + DegreesAfterPreposition(in2.temperature) + ".";
            else answer += " Ближайшие пару часов так и останется.";
            if (!IsRain(code) && (Wet(in1) || Wet(in2))) answer += " Скоро может пойти дождь — возьми зонтик.";
        }
        return answer;
    }

    // Погода в поездки: в колледж и домой. dayIndex < 0 — сам выбирает: сегодня, а если всё позади — завтра
    std::string CommuteReport(const Forecast& f, const Place& p, const Moment& now, int dayIndex) {
        const int nowMinutes = now.hour * 60 + now.minute;
        const bool movedToTomorrow = dayIndex < 0 && nowMinutes > p.homeHour * 60 + p.homeMinute;
        if (dayIndex < 0) dayIndex = movedToTomorrow ? 1 : 0;
        const auto day = DayHours(f, now, dayIndex);
        if (day.empty()) return "На этот день прогноза ещё нет";

        auto slot = [&](int hour, int minute) { return At(day, minute >= 30 ? hour + 1 : hour); }; // ближайший час
        const Hour* go = slot(p.toCollegeHour, p.toCollegeMinute);
        const Hour* back = slot(p.homeHour, p.homeMinute);
        const bool goAhead = dayIndex > 0 || nowMinutes < p.toCollegeHour * 60 + p.toCollegeMinute;
        const bool backAhead = dayIndex > 0 || nowMinutes < p.homeHour * 60 + p.homeMinute;

        // сегодня поездки уже позади — так и говорим, а не молча про завтра
        const std::string when = movedToTomorrow ? "Сегодня ты уже дома, а завтра" : Capital(DayName(dayIndex, now.weekday));
        std::string answer;
        if (go && goAhead) answer += when + " по дороге в колледж " + AtTime(p.toCollegeHour, p.toCollegeMinute) + " будет " + HourReport(*go) + ". ";
        if (back && backAhead)
            answer += std::string(answer.empty() ? when + " по" : "По") + " дороге домой " + AtTime(p.homeHour, p.homeMinute) + " — " + HourReport(*back) + ".";
        if (answer.empty()) return "Сегодня ты уже дома.";
        if ((go && goAhead && Wet(*go)) || (back && backAhead && Wet(*back))) answer += " Возьми зонтик!";
        return answer;
    }

    // «Какая погода на весь день / завтра» — от/до и что в основном
    std::string DayReport(const Forecast& f, const Moment& now, int dayIndex) {
        const auto day = DayHours(f, now, dayIndex);
        if (day.empty()) return "На этот день прогноза ещё нет";
        double low = 1e9, high = -1e9;
        std::map<int, int> codes;
        const Hour* wettest = nullptr; // самый «мокрый» из оставшихся часов
        for (const Hour* h : day) {
            low = std::min(low, h->temperature);
            high = std::max(high, h->temperature);
            if (h->hour >= 8 && h->hour <= 20) ++codes[h->code];
            if (dayIndex == 0 && h->hour < now.hour) continue; // прошедший дождь уже не важен
            if (!wettest || h->rainChance > wettest->rainChance) wettest = h;
        }
        const int typical = codes.empty() ? day.front()->code
            : std::max_element(codes.begin(), codes.end(), [](auto& a, auto& b) { return a.second < b.second; })->first;

        std::string answer = Capital(DayName(dayIndex, now.weekday)) + " от " + DegreesAfterPreposition(low) + " до " + DegreesAfterPreposition(high) +
            ", в основном " + Describe(typical) + ".";
        if (dayIndex == 0) {
            if (const int index = NowIndex(f, now); index >= 0 || f.current.valid)
                answer += " Сейчас " + Degrees(f.current.valid ? f.current.temperature : f.hours[index].temperature) + ".";
            if (const Hour* evening = At(day, 20); evening && now.hour < 19) answer += " Вечером будет " + Degrees(evening->temperature) + ".";
        }
        if (wettest && Wet(*wettest))
            answer += " Возможен дождь " + AtTime(wettest->hour, 0) + ", вероятность " + std::to_string(wettest->rainChance) + "%.";
        return answer;
    }

    // «Когда будет дождь?» — ближайшие осадки в пределах недели
    std::string RainReport(const Forecast& f, const Moment& now) {
        const int start = std::max(0, NowIndex(f, now));
        const Hour* maybe = nullptr; // слабый шанс — на случай, если уверенного дождя нет
        for (size_t i = start; i < f.hours.size(); ++i) {
            const Hour& h = f.hours[i];
            const bool wet = IsRain(h.code) || IsSnow(h.code);
            if (wet || h.rainChance >= kRainChance) {
                if (static_cast<int>(i) == start && wet) return "Прямо сейчас " + Describe(h.code) + ". Не забудь зонтик!";
                const std::string what = wet ? Describe(h.code) : "дождь";
                const std::string chance = h.rainChance > 0 ? ", вероятность " + std::to_string(h.rainChance) + "%" : "";
                return "Ближайшие осадки — " + what + " " + DayName(DayIndex(h, now), now.weekday) + " " + AtTime(h.hour, 0) + chance + ".";
            }
            if (!maybe && h.rainChance >= 25) maybe = &h;
        }
        if (maybe)
            return "Сильного дождя на неделе не обещают, но небольшой шанс есть " + DayName(DayIndex(*maybe, now), now.weekday) + " " +
                   AtTime(maybe->hour, 0) + " — около " + std::to_string(maybe->rainChance) + "%.";
        return "В ближайшую неделю дождя не обещают — зонтик можно не брать.";
    }

    bool HasAny(const std::wstring& text, std::initializer_list<const wchar_t*> words) {
        for (const wchar_t* w : words)
            if (text.find(w) != std::wstring::npos) return true;
        return false;
    }

    // ------------------------------------------------------------ вид для виджета
    Weather::HourView View(const Hour& h) {
        return { h.hour, static_cast<float>(h.temperature), h.rainChance, SkyOf(h.code), !h.isDay };
    }

    Weather::Today BuildToday(const Forecast& f, const Moment& now) {
        Weather::Today t;
        const int index = NowIndex(f, now);
        if (index < 0) return t;
        t.ready = true;
        t.city = f.city;
        t.currentHour = now.hour;
        const Hour& h = f.hours[index];
        const Current& c = f.current;
        t.temperature = static_cast<float>(c.valid ? c.temperature : h.temperature);
        t.feelsLike = static_cast<float>(c.valid ? c.feelsLike : h.temperature);
        t.wind = static_cast<float>(c.wind);
        t.humidity = c.humidity;
        const int code = c.valid ? c.code : h.code;
        t.sky = SkyOf(code);
        t.night = c.valid ? !c.isDay : !h.isDay;
        t.description = Describe(code);
        for (int i = index - 1; i <= index + 2; ++i)
            if (i >= 0 && i < static_cast<int>(f.hours.size())) t.around.push_back(View(f.hours[i]));
        t.low = 1e9f;
        t.high = -1e9f;
        for (const Hour* d : DayHours(f, now, 0)) {
            t.day.push_back(View(*d));
            t.low = std::min(t.low, static_cast<float>(d->temperature));
            t.high = std::max(t.high, static_cast<float>(d->temperature));
        }
        return t;
    }
}

void Weather::Start() {
    std::thread([] {
        for (Clock lastTry = 0;; std::this_thread::sleep_for(std::chrono::seconds(20))) {
            bool stale;
            {
                std::lock_guard lock(g_mutex);
                stale = !g_cachedAt || NowMs() - g_cachedAt >= kCacheMs;
            }
            if (stale && NowMs() - lastTry >= kRetryMs) {
                lastTry = NowMs();
                Forecast unused;
                if (!GetForecast(LoadPlace(), unused)) Log::Write("погода: нет прогноза, попробую через минуту");
            }
            std::lock_guard lock(g_mutex);
            if (g_cachedAt) g_today = BuildToday(g_cache, Now(g_cache)); // час сменился — виджет сдвигается сам
        }
    }).detach();
}

std::optional<Weather::City> Weather::FindCity(const std::string& name) {
    const std::string json = Http::HttpsGet(L"geocoding-api.open-meteo.com",
        L"/v1/search?count=1&language=ru&format=json&name=" + Text::ToWide(Text::UrlEncode(Text::Trim(name))));
    const size_t results = json.find("\"results\":[");
    if (results == std::string::npos) return std::nullopt;
    const size_t end = json.find('}', results);
    auto number = [&](const char* key) {
        const double v = NumberField(json, results, end, key);
        char text[32];
        snprintf(text, sizeof(text), "%.4f", v);
        return std::isnan(v) ? std::string() : std::string(text);
    };
    auto text = [&](const char* key) {
        const auto values = Json::FindStrings(json.substr(results, end - results), key, 1);
        return values.empty() ? std::string() : values.front();
    };
    City city{ text("name"), text("country"), number("latitude"), number("longitude") };
    if (city.name.empty() || city.latitude.empty() || city.longitude.empty()) return std::nullopt;
    return city;
}

void Weather::SetCity(const City& city) {
    Config::SetValue("weather", "city", city.name);
    Config::SetValue("weather", "city_in", city.name == "Ташкент" ? "в Ташкенте" : "в городе " + city.name);
    Config::SetValue("weather", "latitude", city.latitude);
    Config::SetValue("weather", "longitude", city.longitude);
    Reload();
}

void Weather::Reload() {
    {
        std::lock_guard lock(g_mutex);
        g_cachedAt = 0; // устарел — следующий вопрос или фоновый поток скачают заново
    }
    std::thread([] {
        Forecast fresh;
        if (!GetForecast(LoadPlace(), fresh)) return;
        std::lock_guard lock(g_mutex);
        g_today = BuildToday(g_cache, Now(g_cache));
    }).detach();
}

Weather::Today Weather::Snapshot() {
    std::lock_guard lock(g_mutex);
    return g_today;
}

std::optional<std::string> Weather::Answer(const std::string& question) {
    const std::wstring q = Text::LowerRu(Text::ToWide(question));
    // «почему дождь пахнет рыбой», «зачем нужен снег» — вопрос о мире, а не прогноз: пусть ответит нейросеть
    const bool why = HasAny(q, { L"почему", L"зачем", L"отчего", L"пахн", L"как образ", L"что такое", L"из чего", L"откуда" });
    const bool forecastWords = HasAny(q, { L"погод", L"прогноз", L"будет", L"когда", L"идет", L"пойдет", L"капает", L"льет", L"зонт",
                                           L"сегодня", L"завтра", L"сейчас", L"щас", L"на улице", L"за окном", L"ожидается", L"обещают" });
    const bool rain = HasAny(q, { L"дожд", L"зонт", L"снег", L"ливен", L"ливн", L"осадк", L"гроз" }) && forecastWords;
    const bool weather = rain || HasAny(q, { L"погод", L"температур", L"градус", L"холодно", L"жарко", L"тепло ли", L"на улице", L"за окном" });
    if (!weather || (why && !HasAny(q, { L"погод", L"прогноз" }))) return std::nullopt;

    const Place place = LoadPlace();
    Forecast forecast;
    if (!GetForecast(place, forecast)) {
        Log::Write("погода: не удалось получить прогноз");
        return "Не получилось узнать погоду — проверь интернет";
    }
    const Moment now = Now(forecast);

    const int day = HasAny(q, { L"послезавтра" }) ? 2 : HasAny(q, { L"завтра" }) ? 1 : -1;
    const bool college = HasAny(q, { L"колледж", L"учеб", L"на пар", L"по дороге", L"в дорогу", L"домой", L"поездк",
                                     L"когда я буду", L"когда буду", L"когда пойду", L"когда поеду", L"когда я пойду", L"когда я поеду" });
    const bool rightNow = HasAny(q, { L"сейчас", L"щас", L"прямо", L"на улице", L"за окном", L"в данный момент" });

    if (college) return CommuteReport(forecast, place, now, day);          // «когда я буду в колледже»
    if (rain && !rightNow) return RainReport(forecast, now);               // «когда будет дождь»
    if (day > 0) return DayReport(forecast, now, day);                     // «погода на завтра»
    if (rightNow) return NowReport(forecast, now);                         // «какая погода сейчас»
    if (HasAny(q, { L"на день", L"весь день", L"в целом", L"в общем", L"средн", L"за день", L"на сегодня", L"будет" }))
        return DayReport(forecast, now, 0);                                // «какая будет сегодня погода»
    return NowReport(forecast, now);                                       // просто «какая погода» — что сейчас
}
