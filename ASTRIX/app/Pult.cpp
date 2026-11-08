#include "Pult.h"

#include "Launcher.h"
#include "LocalAi.h"
#include "Log.h"
#include "MediaNow.h"
#include "Pc.h"
#include "Platform.h"
#include "PowerMode.h"
#include "Scenarios.h"
#include "Screen.h"
#include "Settings.h"
#include "Skills.h"
#include "SystemMonitor.h"
#include "TextUtils.h"
#include "Voice.h"

#include "../third_party/nlohmann/json.hpp"

#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

using nlohmann::json;

namespace {
    std::mutex g_mutex;
    std::vector<uint8_t> g_shot;
    std::string g_askState = "idle", g_answer;

    std::filesystem::path File() { return std::filesystem::path(Text::ExecutableDirectory()) / "pult.json"; }

    json Tile(const char* action, const char* label, const char* icon, const char* color = "") {
        return { { "a", action }, { "label", label }, { "icon", icon }, { "color", color }, { "arg", "" } };
    }

    json Defaults() {
        return { { "home", json::array({ Tile("vol_down", "Тише", "🔉"), Tile("vol_up", "Громче", "🔊"), Tile("mute", "Без звука", "🔇"),
                                         Tile("prev", "Назад", "⏮"), Tile("play", "Пауза", "⏯"), Tile("next", "Дальше", "⏭"),
                                         Tile("ask", "Ответ с экрана", "📸", "#6b4fd8"), Tile("lock", "Блокировка", "🔒"), Tile("standby", "Экран выкл", "🌙"),
                                         Tile("wake", "Экран вкл", "☀️"), Tile("shutdown", "Выключить", "⏻", "#7a2840") }) },
                 { "apps", json::array() },
                 { "settings", { { "accent", "#6b4fd8" }, { "cols", 3 }, { "status", true }, { "haptic", true }, { "hd", false }, { "sens", 18 }, { "pad", true } } } };
    }

    json LoadConfig() {
        json config = Defaults();
        std::ifstream in(File());
        if (!in) return config;
        const json saved = json::parse(in, nullptr, false);
        if (!saved.is_object()) return config;
        for (const char* key : { "home", "apps" }) if (saved.contains(key) && saved[key].is_array()) config[key] = saved[key];
        if (saved.contains("settings") && saved["settings"].is_object()) config["settings"].update(saved["settings"]);
        return config;
    }

    Pult::Reply Json(const json& value) { return { 200, "application/json", value.dump(-1, ' ', false, json::error_handler_t::replace) }; }
    Pult::Reply Message(const std::string& text) { return Json({ { "text", text } }); }
    Pult::Reply Error(int status, const std::string& text) { return { status, "application/json", json{ { "error", text } }.dump(-1, ' ', false, json::error_handler_t::replace) }; }

    Pult::Reply Volume(int delta) {
        const int now = std::clamp(Pc::Volume().value_or(0) + delta, 0, 100);
        Pc::SetVolume(now);
        return Message("Громкость " + std::to_string(now) + "%");
    }

    Pult::Reply OpenLink(std::string url) {
        url = Text::Trim(url);
        if (url.empty()) return Error(400, "Пустая ссылка");
        const size_t scheme = url.find("://");
        if (scheme == std::string::npos) url = "https://" + url;
        else if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return Error(400, "Открываю только http и https");
        ShellExecuteW(nullptr, L"open", Text::ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return Message("Открываю");
    }

    Pult::Reply Launch(const std::string& target) {
        const auto result = Launcher::Open(target);
        return result.found ? Message("Запускаю " + result.name) : Error(404, "Не нашла «" + target + "»");
    }

    Pult::Reply Action(const json& request) {
        const std::string action = request.value("a", ""), arg = request.value("arg", "");
        if (action == "vol_up") return Volume(8);
        if (action == "vol_down") return Volume(-8);
        if (action == "mute") { const bool mute = !Pc::IsMuted(); Pc::SetMute(mute); return Message(mute ? "Звук выключен" : "Звук включён"); }
        if (action == "play") { MediaNow::Control(MediaNow::Action::PlayPause); return Message("Готово"); }
        if (action == "next") { MediaNow::Control(MediaNow::Action::Next); return Message("Следующий трек"); }
        if (action == "prev") { MediaNow::Control(MediaNow::Action::Previous); return Message("Предыдущий трек"); }
        if (action == "lock") { PowerMode::Lock(); return Message("Заблокировала"); }
        if (action == "standby") { PowerMode::Standby(); return Message("Экран выключен, я на связи"); }
        if (action == "wake") { PowerMode::Wake(); return Message("Экран включён"); }
        if (action == "shutdown") { PowerMode::Shutdown(15); return Message("Выключаю через 15 секунд"); }
        if (action == "restart") { PowerMode::Restart(15); return Message("Перезагружаю через 15 секунд"); }
        if (action == "cancel_power") { PowerMode::CancelShutdown(); return Message("Отменила"); }
        if (action == "ai_profile") {
            const int index = std::atoi(arg.c_str());
            if (index < 0 || index >= Ai::kProfiles) return Error(400, "Нет такого профиля");
            Ai::SetProfile(index);
            return Message(std::string("Профиль нейросети: ") + Ai::ProfileName(index));
        }
        if (action == "say") { if (Text::Trim(arg).empty()) return Error(400, "Пустой текст"); Voice::Speak(arg); return Message("Сказала вслух"); }
        if (action == "launch") return Launch(arg);
        if (action == "link") return OpenLink(arg);
        if (action == "scenario") {
            for (const auto& s : Scenarios::Load()) if (s.name == arg) { Scenarios::Run(s); return Message("Запускаю «" + s.name + "»"); }
            return Error(404, "Нет такого сценария");
        }
        if (action == "command") {
            if (const auto done = Skills::Handle(arg)) return Message(*done);
            return Error(404, "Такую команду я не знаю");
        }
        return Error(400, "Неизвестное действие");
    }

    Pult::Reply Status() {
        const auto s = SystemMonitor::Snapshot();
        const auto track = MediaNow::Snapshot();
        return Json({ { "ready", s.ready }, { "cpu", s.cpu }, { "ram", s.ram }, { "gpu", s.hasGpu ? s.gpu : -1.0f }, { "cpuTemp", s.cpuTemp }, { "gpuTemp", s.gpuTemp },
                      { "volume", Pc::Volume().value_or(0) }, { "muted", Pc::IsMuted() }, { "locked", PowerMode::IsLocked() }, { "standby", PowerMode::IsStandby() },
                      { "track", track.present ? track.title + (track.artist.empty() ? "" : " — " + track.artist) : "" }, { "playing", track.playing } });
    }
}

Pult::Reply Pult::Handle(const std::string& method, const std::string& path, const std::string& body) {
    if (method == "GET" && path == "/api/config") return Json(LoadConfig());
    if (method == "GET" && path == "/api/status") return Status();
    if (method == "GET" && path == "/api/ai") {
        json names = json::array();
        for (int i = 0; i < Ai::kProfiles; ++i) names.push_back(Ai::ProfileName(i));
        return Json({ { "profile", Ai::Profile() }, { "names", names } });
    }
    if (method == "GET" && path == "/api/catalog") {
        json scenarios = json::array();
        for (const auto& s : Scenarios::Load()) scenarios.push_back(s.name);
        return Json({ { "programs", Launcher::AppNames() }, { "scenarios", scenarios } });
    }
    if (method == "POST" && path == "/api/config") {
        const json update = json::parse(body, nullptr, false);
        if (!update.is_object()) return Error(400, "Неверные данные");
        json config = LoadConfig();
        if (update.value("reset", false)) config["home"] = Defaults()["home"];
        for (const char* key : { "home", "apps" }) if (update.contains(key) && update[key].is_array()) config[key] = update[key];
        if (update.contains("settings") && update["settings"].is_object()) config["settings"].update(update["settings"]);
        std::lock_guard lock(g_mutex);
        std::ofstream(File()) << config.dump(2, ' ', false, json::error_handler_t::replace);
        return Json(config);
    }
    if (method == "POST" && path == "/api/action") {
        const json request = json::parse(body, nullptr, false);
        if (!request.is_object()) return Error(400, "Неверные данные");
        return Action(request);
    }
    if (method == "GET" && path == "/api/shot") { // снимок, по которому потом задаётся вопрос
        if (PowerMode::IsLocked()) return Error(423, "Компьютер заблокирован");
        if (Config::Value("privacy", "screen", "1") != "1") return Error(403, "Снимки экрана выключены в разделе «Приватность»");
        if (!Ai::CanSee()) return Error(409, "Нейросеть ещё не готова или у модели нет «глаз» (файла mmproj)");
        auto image = Screen::CaptureJpeg(1280, 85);
        if (image.empty()) return Error(500, "Не получилось снять экран");
        Reply reply{ 200, "image/jpeg", std::string(reinterpret_cast<const char*>(image.data()), image.size()) };
        std::lock_guard lock(g_mutex);
        g_shot = std::move(image);
        return reply;
    }
    if (method == "POST" && path == "/api/ask") {
        const json request = json::parse(body, nullptr, false);
        const std::string task = request.is_object() ? Text::Trim(request.value("task", "")) : "";
        std::vector<uint8_t> image;
        {
            std::lock_guard lock(g_mutex);
            if (g_askState == "busy") return Error(409, "Я уже решаю — подожди немного");
            if (task.empty() || g_shot.empty()) return Error(400, "Нужны снимок и задача");
            image = g_shot;
            g_askState = "busy";
            g_answer.clear();
        }
        std::thread([image = std::move(image), task] {
            const std::string answer = Ai::Quiet(task, Screen::Base64(image), true);
            std::lock_guard lock(g_mutex);
            g_answer = answer.empty() ? "Не получилось решить — нейросеть не ответила" : answer;
            g_askState = "done";
        }).detach();
        return Json({ { "state", "busy" } });
    }
    if (method == "GET" && path == "/api/ask") {
        std::lock_guard lock(g_mutex);
        return Json({ { "state", g_askState }, { "answer", g_answer } });
    }
    return Error(404, "Нет такого адреса");
}
