#include "ScenarioMaker.h"

#include "LocalAi.h"
#include "Log.h"
#include "Scenarios.h"
#include "ScenariosPage.h"
#include "SettingsPage.h"
#include "State.h"
#include "Voice.h"
#include "TextUtils.h"

#include "../third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using nlohmann::json;

namespace {
    using MakerClock = std::chrono::steady_clock;
    std::atomic<bool> g_waiting{ false };   // спросила «рассказывай» — следующая фраза и есть сценарий
    MakerClock::time_point g_askedAt;

    std::string Lower(const std::string& text) { return Text::ToUtf8(Text::LowerRu(Text::ToWide(text))); }
    bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

    // «создай сценарий …», «можешь сделать новый сценарий?» — всё после слова «сценарий» уже описание
    std::string AfterTrigger(const std::string& command, bool* triggered) {
        *triggered = false;
        const std::wstring wide = Text::ToWide(command), lower = Text::LowerRu(wide);
        const size_t at = lower.find(L"сценари");
        if (at == std::wstring::npos) return {};
        static const wchar_t* kVerbs[] = { L"созда", L"сдела", L"добав", L"придума", L"запиш", L"запиc", L"собер", L"нов" };
        const std::wstring head = lower.substr(0, at);
        if (std::none_of(std::begin(kVerbs), std::end(kVerbs), [&](const wchar_t* v) { return head.find(v) != std::wstring::npos; })) return {};
        *triggered = true;
        size_t end = at;
        while (end < lower.size() && std::iswalpha(lower[end])) ++end; // «сценарий», «сценария»
        std::string rest = Text::Trim(Text::ToUtf8(wide.substr(end)));
        while (!rest.empty() && std::string(":,-.?!").find(rest[0]) != std::string::npos) rest = Text::Trim(rest.substr(1));
        return rest;
    }

    const char* kPrompt = R"(Ты собираешь сценарий для голосового ассистента «Аня» на компьютере.
Сценарий — фраза запуска и блоки действий по порядку. Типы блоков:
- "launch": запустить программу. text — её название как в меню «Пуск» (например "Steam", "Discord", "Яндекс Музыка").
- "link": открыть сайт. text — полный адрес https://… (YouTube — https://www.youtube.com, Кинопоиск — https://www.kinopoisk.ru).
- "place": поставить окно. text — программа (пусто — окно из прошлого блока), monitor — номер монитора (1 — основной),
  position: 0 — развернуть, 1 — левая половина, 2 — правая половина, 3 — по центру, 4 — во весь экран (видео в браузере).
- "volume": громкость, number — проценты 0…100.
- "media": number 0 — пауза/продолжить, 1 — следующий трек.
- "wait": подождать, number — секунды.
- "say": сказать вслух, text — фраза.
Ответь ТОЛЬКО JSON без пояснений, вот так:
{"name":"Уютный вечер","phrase":"уютный вечер","blocks":[{"type":"link","text":"https://www.youtube.com","monitor":2,"position":4},{"type":"volume","number":30},{"type":"say","text":"Приятного вечера!"}]}
phrase — короткая фраза в нижнем регистре, которую скажут после «Аня», чтобы запустить сценарий. Если её не назвали — придумай по смыслу.
Рассказ пользователя: )";

    Scenarios::Step StepOf(const std::string& type, bool* ok) {
        static const std::pair<const char*, Scenarios::Step> kTypes[] = { { "launch", Scenarios::Step::Launch }, { "link", Scenarios::Step::OpenLink },
            { "place", Scenarios::Step::Place }, { "volume", Scenarios::Step::Volume }, { "media", Scenarios::Step::Media },
            { "wait", Scenarios::Step::Wait }, { "say", Scenarios::Step::Say } };
        for (const auto& [name, step] : kTypes)
            if (type == name) { *ok = true; return step; }
        *ok = false;
        return Scenarios::Step::Say;
    }

    std::optional<Scenarios::Scenario> Parse(const std::string& answer) {
        const size_t open = answer.find('{'), close = answer.rfind('}');
        if (open == std::string::npos || close == std::string::npos || close < open) return std::nullopt;
        const json j = json::parse(answer.substr(open, close - open + 1), nullptr, false);
        if (j.is_discarded() || !j.is_object()) return std::nullopt;
        Scenarios::Scenario s;
        s.name = Text::Trim(j.value("name", std::string("Новый сценарий")));
        s.phrase = Lower(Text::Trim(j.value("phrase", std::string())));
        if (s.phrase.empty()) s.phrase = Lower(s.name);
        if (!j.contains("blocks") || !j["blocks"].is_array()) return std::nullopt;
        for (const auto& b : j["blocks"]) {
            if (!b.is_object()) continue;
            bool ok = false;
            Scenarios::Block block;
            block.step = StepOf(b.value("type", std::string()), &ok);
            if (!ok) continue;
            if (b.contains("text") && b["text"].is_string()) block.text = b["text"].get<std::string>();
            if (b.contains("monitor") && b["monitor"].is_number()) block.monitor = std::max(1, b["monitor"].get<int>());
            if (b.contains("position") && b["position"].is_number()) block.position = std::clamp(b["position"].get<int>(), 0, 4);
            if (b.contains("number") && b["number"].is_number()) block.number = b["number"].get<int>();
            if (block.step == Scenarios::Step::Volume) block.number = std::clamp(block.number, 0, 100);
            if (block.step == Scenarios::Step::Wait) block.number = std::clamp(block.number, 1, 600);
            s.blocks.push_back(block);
        }
        if (s.blocks.empty()) return std::nullopt;
        return s;
    }

    // «открою YouTube, поставлю громкость 30 и скажу …» — что получилось, одной фразой
    std::string Describe(const Scenarios::Scenario& s) {
        std::vector<std::string> parts;
        for (const auto& b : s.blocks) {
            switch (b.step) {
            case Scenarios::Step::Launch: parts.push_back("запущу " + b.text); break;
            case Scenarios::Step::OpenLink: parts.push_back("открою " + b.text); break;
            case Scenarios::Step::Place: parts.push_back("поставлю окно" + (b.monitor > 1 ? " на " + std::to_string(b.monitor) + "-й монитор" : std::string())); break;
            case Scenarios::Step::Volume: parts.push_back("сделаю громкость " + std::to_string(b.number) + "%"); break;
            case Scenarios::Step::Media: parts.push_back(b.number ? "включу следующий трек" : "нажму паузу/плей"); break;
            case Scenarios::Step::Wait: parts.push_back("подожду " + std::to_string(b.number) + " с"); break;
            case Scenarios::Step::Say: parts.push_back("скажу «" + b.text + "»"); break;
            default: break;
            }
        }
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) out += (i == 0 ? "" : i + 1 == parts.size() ? " и " : ", ") + parts[i];
        return out;
    }

    std::string Build(const std::string& story) {
        if (!g_app.aiReady) { // нейросеть выгружена или ещё грузится — подождём её
            Voice::Speak("Секунду, собираю сценарий");
            Ai::Quiet("ok"); // разбудить
            for (int i = 0; i < 120 && !g_app.aiReady; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!g_app.aiReady) return "Нейросеть ещё не запустилась — без неё я не соберу сценарий. Попробуй через минуту";
        }
        const std::string answer = Ai::AskJson(std::string(kPrompt) + "(рассказ — в следующем сообщении)", story);
        const auto scenario = Parse(answer);
        if (!scenario) Log::Write("сценарий не собрался, ответ нейросети: " + answer.substr(0, 400));
        if (!scenario) return "Не получилось собрать сценарий. Расскажи по-другому — что открыть и что сделать по шагам, — "
                              "или собери его сама во вкладке «Сценарии»";
        ScenariosPage::Add(*scenario); // сохранит и покажет — в потоке интерфейса
        return "Готово — сценарий «" + scenario->name + "». Скажи «" + scenario->phrase + "», и я " + Describe(*scenario) +
               ". Поправить можно во вкладке «Сценарии»";
    }
}

std::optional<std::string> ScenarioMaker::Handle(const std::string& command) {
    if (g_waiting && MakerClock::now() - g_askedAt < std::chrono::minutes(3)) {
        g_waiting = false;
        const std::string norm = Lower(command);
        if (Has(norm, "отмена") || Has(norm, "не надо") || Has(norm, "передумал") || Has(norm, "отбой")) return "Хорошо, не создаю";
        return Build(command);
    }
    g_waiting = false;
    bool triggered = false;
    const std::string story = AfterTrigger(command, &triggered);
    if (!triggered) return std::nullopt;
    if (story.size() < 8) { // одно «создай сценарий» — сначала выслушать
        g_waiting = true;
        g_askedAt = MakerClock::now();
        return "Давай! Расскажи, что мне делать по шагам и какой фразой запускать — например: «уютный вечер: открой YouTube "
               "на втором мониторе во весь экран и сделай громкость 30»";
    }
    return Build(story);
}

std::optional<std::string> ScenarioMaker::FindSetting(const std::string& command) {
    const std::string norm = Lower(command);
    const bool aboutSettings = Has(norm, "настройк") || Has(norm, "в настройках");
    const bool where = norm.rfind("где ", 0) == 0 || Has(norm, " где ") || Has(norm, "как поменять") || Has(norm, "как изменить") ||
                       Has(norm, "как настроить") || Has(norm, "как отключить") || Has(norm, "как выключить") || Has(norm, "как включить") ||
                       Has(norm, "как убрать") || Has(norm, "как сделать") || Has(norm, "как поставить");
    if (!where && !(aboutSettings && (Has(norm, "покажи") || Has(norm, "найди") || Has(norm, "открой")))) return std::nullopt;
    // «как включить блютуз» — это не про Аню; спрашиваем нейросеть только если прямо сказали «настройка»
    const auto found = SettingsPage::Reveal(command, aboutSettings);
    if (!found) return aboutSettings ? std::optional<std::string>("Не нашла такой настройки. Попробуй сказать по-другому") : std::nullopt;
    return "Вот здесь: «" + found->section + "» → «" + found->title + "». Подсветила оранжевым";
}
