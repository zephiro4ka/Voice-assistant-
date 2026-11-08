#include "Rates.h"

#include "Http.h"
#include "Log.h"
#include "Platform.h"
#include "Settings.h"
#include "TextUtils.h"
#include "Weather.h"

#include "../third_party/nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

using nlohmann::json;

namespace {
    constexpr int kRefreshSeconds = 3600, kRetrySeconds = 60;

    std::mutex g_mutex;
    Rates::Data g_data;
    std::atomic<bool> g_reload{ false }, g_started{ false };
    std::atomic<int> g_enabled{ -1 };

    std::string CurrencyOf(const std::string& country) {
        static const std::pair<const char*, const char*> kCountries[] = {
            { "Узбекистан", "UZS" }, { "Uzbekistan", "UZS" }, { "Росси", "RUB" }, { "Russia", "RUB" }, { "Казахстан", "KZT" }, { "Kazakhstan", "KZT" },
            { "Беларус", "BYN" }, { "Belarus", "BYN" }, { "Украин", "UAH" }, { "Ukraine", "UAH" }, { "Киргиз", "KGS" }, { "Кыргыз", "KGS" }, { "Kyrgyz", "KGS" },
            { "Таджикистан", "TJS" }, { "Tajikistan", "TJS" }, { "Туркмени", "TMT" }, { "Turkmenistan", "TMT" }, { "Азербайджан", "AZN" }, { "Azerbaijan", "AZN" },
            { "Армени", "AMD" }, { "Armenia", "AMD" }, { "Грузи", "GEL" }, { "Georgia", "GEL" }, { "Молдов", "MDL" }, { "Moldova", "MDL" }, { "Турци", "TRY" }, { "Turkey", "TRY" },
            { "Норвеги", "NOK" }, { "Norway", "NOK" }, { "Швеци", "SEK" }, { "Sweden", "SEK" }, { "Польш", "PLN" }, { "Poland", "PLN" },
            { "Великобритани", "GBP" }, { "United Kingdom", "GBP" }, { "США", "USD" }, { "United States", "USD" } };
        for (const auto& [name, code] : kCountries) if (Text::Contains(country, name)) return code;
        return {};
    }

    std::string LocalCurrency() {
        const std::string chosen = Text::Trim(Config::Value("rates", "currency", "auto"));
        if (!chosen.empty() && chosen != "auto") return chosen;
        const std::string city = Text::Trim(Config::Value("weather", "city", ""));
        if (!city.empty())
            if (const auto found = Weather::FindCity(city)) {
                const std::string code = CurrencyOf(found->country);
                if (!code.empty()) return code;
            }
        return "USD";
    }

    std::vector<std::string> Wanted(const std::string& base) {
        std::vector<std::string> list;
        for (const char* code : { "USD", "EUR", "RUB", "GBP" })
            if (base != code && list.size() < 3) list.push_back(code);
        return list;
    }

    double Number(const json& value) {
        if (value.is_number()) return value.get<double>();
        return value.is_string() ? std::atof(value.get<std::string>().c_str()) : 0.0;
    }

    bool FromCentralBank(Rates::Data& out) { // официальный курс ЦБ Узбекистана: сразу с изменением за день
        const json list = json::parse(Http::HttpsGet(L"cbu.uz", L"/ru/arkhiv-kursov-valyut/json/", 12000), nullptr, false);
        if (!list.is_array()) return false;
        for (const std::string& code : Wanted("UZS"))
            for (const auto& row : list) {
                if (!row.is_object() || row.value("Ccy", "") != code) continue;
                const double nominal = std::max(1.0, row.contains("Nominal") ? Number(row["Nominal"]) : 1.0);
                out.items.push_back({ code, (row.contains("Rate") ? Number(row["Rate"]) : 0.0) / nominal, (row.contains("Diff") ? Number(row["Diff"]) : 0.0) / nominal });
                out.date = row.value("Date", "");
            }
        return !out.items.empty();
    }

    bool FromExchangeApi(const std::string& base, Rates::Data& out) {
        const json data = json::parse(Http::HttpsGet(L"open.er-api.com", Text::ToWide("/v6/latest/" + base), 12000), nullptr, false);
        if (!data.is_object() || data.value("result", "") != "success" || !data.contains("rates")) return false;
        const json& rates = data["rates"];
        for (const std::string& code : Wanted(base))
            if (rates.contains(code) && Number(rates[code]) > 0) out.items.push_back({ code, 1.0 / Number(rates[code]), 0.0 });
        return !out.items.empty();
    }

    void Loop() {
        for (;;) {
            g_reload = false;
            Rates::Data fresh;
            fresh.base = LocalCurrency();
            bool ok = false;
            try { ok = fresh.base == "UZS" ? FromCentralBank(fresh) : FromExchangeApi(fresh.base, fresh); } catch (...) {}
            if (ok) {
                fresh.ready = true;
                std::lock_guard lock(g_mutex);
                g_data = std::move(fresh);
            } else Log::Write("курсы валют: источник не ответил");
            for (int waited = 0; waited < (ok ? kRefreshSeconds : kRetrySeconds) && !g_reload.load(); ++waited) Sleep(1000);
        }
    }
}

Rates::Data Rates::Snapshot() {
    if (!g_started.exchange(true)) std::thread(Loop).detach();
    std::lock_guard lock(g_mutex);
    return g_data;
}

void Rates::Reload() {
    g_enabled = -1;
    {
        std::lock_guard lock(g_mutex);
        g_data = {};
    }
    g_reload = true;
}

bool Rates::Enabled() {
    if (g_enabled.load() < 0) g_enabled = Config::Value("rates", "widget", "1") == "1" ? 1 : 0;
    return g_enabled.load() == 1;
}
