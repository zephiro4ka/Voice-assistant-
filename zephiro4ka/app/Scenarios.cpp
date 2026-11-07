#include "Scenarios.h"

#include "Browser.h"
#include "Fuzzy.h"
#include "Input.h"
#include "Launcher.h"
#include "Log.h"
#include "MediaNow.h"
#include "Pc.h"
#include "Platform.h"
#include "TextUtils.h"
#include "Voice.h"

#include "../third_party/nlohmann/json.hpp"

#include <shellapi.h>

#include <algorithm>
#include <functional>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

using nlohmann::json;

namespace {
    std::mutex g_mutex;

    std::filesystem::path File() { return std::filesystem::path(Text::ExecutableDirectory()) / "scenarios.json"; }

    // Мониторы слева направо: 1 — самый левый … (основной обычно первый)
    std::vector<RECT> Monitors() {
        std::vector<RECT> list;
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM param) -> BOOL {
            MONITORINFO info{ sizeof(info) };
            GetMonitorInfoW(monitor, &info);
            reinterpret_cast<std::vector<RECT>*>(param)->push_back(info.rcWork);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&list));
        std::sort(list.begin(), list.end(), [](const RECT& a, const RECT& b) { return a.left < b.left; });
        return list;
    }

    // Окно программы — ждём, пока появится после запуска (до timeoutMs)
    HWND WaitWindow(const std::string& app, int timeoutMs) {
        for (int waited = 0; waited <= timeoutMs; waited += 300) {
            if (HWND w = static_cast<HWND>(Launcher::MainWindowOf(app))) return w;
            Sleep(300);
        }
        return nullptr;
    }

    void Place(HWND window, int monitor, int position) {
        const auto monitors = Monitors();
        if (!window || monitors.empty()) return;
        const RECT r = monitors[std::clamp(monitor, 1, static_cast<int>(monitors.size())) - 1];
        const int w = r.right - r.left, h = r.bottom - r.top;
        ShowWindow(window, SW_RESTORE);
        int x = r.left, y = r.top, cw = w, ch = h;
        switch (static_cast<Scenarios::Position>(position)) {
        case Scenarios::Position::LeftHalf: cw = w / 2; break;
        case Scenarios::Position::RightHalf: x = r.left + w / 2; cw = w - w / 2; break;
        case Scenarios::Position::Center: cw = w * 2 / 3; ch = h * 2 / 3; x = r.left + (w - cw) / 2; y = r.top + (h - ch) / 2; break;
        default: break;
        }
        SetWindowPos(window, nullptr, x, y, cw, ch, SWP_NOZORDER | SWP_NOACTIVATE);
        const auto pos = static_cast<Scenarios::Position>(position);
        if (pos == Scenarios::Position::Maximize || pos == Scenarios::Position::FullScreen) ShowWindow(window, SW_MAXIMIZE);
        if (pos == Scenarios::Position::FullScreen) { // видео на весь экран: F11 в браузере
            Input::Tap(VK_MENU);
            SetForegroundWindow(window);
            Sleep(250);
            Input::Tap(VK_F11);
        }
    }

    json ToJson(const Scenarios::Scenario& s) {
        json blocks = json::array();
        for (const auto& b : s.blocks)
            blocks.push_back({ { "step", static_cast<int>(b.step) }, { "text", b.text }, { "monitor", b.monitor }, { "position", b.position }, { "number", b.number },
                               { "id", b.id }, { "x", b.x }, { "y", b.y }, { "next", b.next }, { "color", b.color }, { "title", b.title } });
        return { { "name", s.name }, { "phrase", s.phrase }, { "enabled", s.enabled }, { "blocks", blocks },
                 { "start", { { "x", s.startX }, { "y", s.startY }, { "next", s.startNext } } }, { "link_style", s.linkStyle } };
    }

    Scenarios::Scenario FromJson(const json& j) {
        Scenarios::Scenario s;
        s.name = j.value("name", s.name);
        s.phrase = j.value("phrase", "");
        s.enabled = j.value("enabled", true);
        for (const auto& b : j.value("blocks", json::array())) {
            Scenarios::Block block;
            block.step = static_cast<Scenarios::Step>(std::clamp(b.value("step", 0), 0, static_cast<int>(Scenarios::Step::kCount) - 1));
            block.text = b.value("text", "");
            block.monitor = b.value("monitor", 1);
            block.position = b.value("position", 0);
            block.number = b.value("number", 0);
            block.id = b.value("id", 0);
            block.x = b.value("x", 0.0f);
            block.y = b.value("y", 0.0f);
            if (b.contains("next") && b["next"].is_array())
                for (const auto& n : b["next"]) if (n.is_number_integer()) block.next.push_back(n.get<int>());
            block.color = b.value("color", -1);
            block.title = b.value("title", "");
            s.blocks.push_back(block);
        }
        if (j.contains("start") && j["start"].is_object()) {
            const auto& st = j["start"];
            s.hasLayout = true;
            s.startX = st.value("x", 0.0f);
            s.startY = st.value("y", 0.0f);
            if (st.contains("next") && st["next"].is_array())
                for (const auto& n : st["next"]) if (n.is_number_integer()) s.startNext.push_back(n.get<int>());
        }
        s.linkStyle = std::clamp(j.value("link_style", 0), 0, 2);
        Scenarios::Normalize(s);
        return s;
    }

    // Пример, чтобы было понятно, как это устроено
    std::vector<Scenarios::Scenario> Example() {
        Scenarios::Scenario s;
        s.name = "Уютный вечер";
        s.phrase = "уютный вечер";
        s.blocks = { { Scenarios::Step::OpenLink, "https://www.kinopoisk.ru", 2, static_cast<int>(Scenarios::Position::FullScreen), 0 },
                     { Scenarios::Step::Launch, "NTE", 1, 0, 0 },
                     { Scenarios::Step::Place, "", 1, static_cast<int>(Scenarios::Position::Maximize), 0 },
                     { Scenarios::Step::Volume, "", 1, 0, 40 },
                     { Scenarios::Step::Say, "Уютного вечера!", 1, 0, 0 } };
        return { s };
    }
}

std::vector<Scenarios::Scenario> Scenarios::Load() {
    std::lock_guard lock(g_mutex);
    std::ifstream in(File());
    if (!in) { auto example = Example(); for (auto& e : example) Normalize(e); return example; }
    const json data = json::parse(in, nullptr, false);
    std::vector<Scenario> list;
    if (data.is_array())
        for (const auto& j : data) list.push_back(FromJson(j));
    return list;
}

void Scenarios::Save(const std::vector<Scenario>& scenarios) {
    std::lock_guard lock(g_mutex);
    json data = json::array();
    for (const auto& s : scenarios) data.push_back(ToJson(s));
    std::ofstream(File()) << data.dump(2);
}

void Scenarios::Normalize(Scenario& s) {
    int maxId = 0;
    for (const auto& b : s.blocks) maxId = std::max(maxId, b.id);
    bool missing = false;
    for (auto& b : s.blocks) if (b.id <= 0) { b.id = ++maxId; missing = true; }
    if (s.hasLayout && !missing) return;
    if (!s.hasLayout) { // цепочка → холст: слева направо, по три блока в ряд, связи по порядку
        s.startX = 0, s.startY = 0;
        s.startNext.clear();
        for (size_t i = 0; i < s.blocks.size(); ++i) {
            auto& b = s.blocks[i];
            const int place = static_cast<int>(i) + 1, row = place / 3; // змейкой: ряд вправо, следующий — влево
            const int col = row % 2 == 0 ? place % 3 : 2 - place % 3;
            b.x = static_cast<float>(col) * 380.0f;
            b.y = static_cast<float>(row) * 220.0f;
            b.next.clear();
            if (i + 1 < s.blocks.size()) b.next.push_back(s.blocks[i + 1].id);
        }
        if (!s.blocks.empty()) s.startNext.push_back(s.blocks[0].id);
        s.hasLayout = true;
    }
}

std::vector<Scenarios::Block> Scenarios::Order(const Scenario& s) {
    if (!s.hasLayout) return s.blocks;
    std::vector<Block> out;
    std::vector<int> seen;
    auto find = [&](int id) -> const Block* {
        for (const auto& b : s.blocks) if (b.id == id) return &b;
        return nullptr;
    };
    std::function<void(const std::vector<int>&)> walk = [&](const std::vector<int>& ids) {
        std::vector<const Block*> nodes;
        for (int id : ids) if (const Block* b = find(id); b && std::find(seen.begin(), seen.end(), id) == seen.end()) nodes.push_back(b);
        std::sort(nodes.begin(), nodes.end(), [](const Block* a, const Block* b) { return a->y < b->y || (a->y == b->y && a->x < b->x); });
        for (const Block* b : nodes) {
            if (std::find(seen.begin(), seen.end(), b->id) != seen.end()) continue; // по кругу не ходим
            seen.push_back(b->id);
            out.push_back(*b);
            walk(b->next);
        }
    };
    walk(s.startNext);
    return out;
}

void Scenarios::Run(const Scenario& source) {
    Scenario scenario = source;
    Normalize(scenario);
    scenario.blocks = Order(scenario); // по стрелкам холста
    std::thread([scenario] {
        Log::Write("сценарий «" + scenario.name + "»: старт");
        std::string lastApp; // для блока «Положение окна» без названия
        for (const auto& b : scenario.blocks) {
            switch (b.step) {
            case Step::Launch:
                if (const auto r = Launcher::Open(b.text); r.found) lastApp = b.text;
                else Log::Write("сценарий: не нашла программу «" + b.text + "»");
                break;
            case Step::OpenLink: {
                std::string url = Text::Trim(b.text);
                if (url.find("://") == std::string::npos) url = "https://" + url;
                ShellExecuteW(nullptr, L"open", Text::ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                Sleep(1500);
                if (b.monitor > 0) Place(Browser::Window(true), b.monitor, b.position); // ссылку сразу ставим куда сказано
                lastApp = "браузер";
                break;
            }
            case Step::Place: {
                const std::string app = Text::Trim(b.text).empty() ? lastApp : b.text;
                HWND window = app == "браузер" ? Browser::Window(true) : WaitWindow(app, 15000); // игра может грузиться долго
                Place(window, b.monitor, b.position);
                break;
            }
            case Step::Volume: Pc::SetVolume(b.number); break;
            case Step::Media: MediaNow::Control(b.number == 1 ? MediaNow::Action::Next : MediaNow::Action::PlayPause); break;
            case Step::Wait: Sleep(static_cast<DWORD>(std::clamp(b.number, 0, 600)) * 1000); break;
            case Step::Say: Voice::Speak(b.text); break;
            default: break;
            }
        }
        Log::Write("сценарий «" + scenario.name + "»: готово");
    }).detach();
}

std::optional<std::string> Scenarios::TryRun(const std::string& command) {
    const std::string said = Text::ToUtf8(Text::LowerRu(Text::ToWide(command)));
    for (const auto& s : Load()) {
        const std::string phrase = Text::ToUtf8(Text::LowerRu(Text::ToWide(Text::Trim(s.phrase))));
        if (!s.enabled || phrase.empty()) continue;
        if (said.find(phrase) != std::string::npos || Fuzzy::TitleMatch(phrase, said) >= 0.85f) {
            Run(s);
            return "Запускаю «" + s.name + "»";
        }
    }
    return std::nullopt;
}

const char* Scenarios::StepName(Step step) {
    static const char* kNames[] = { "Запустить программу", "Открыть ссылку", "Положение окна", "Громкость", "Музыка", "Подождать", "Сказать" };
    return kNames[static_cast<int>(step)];
}

int Scenarios::MonitorCount() { return static_cast<int>(Monitors().size()); }
