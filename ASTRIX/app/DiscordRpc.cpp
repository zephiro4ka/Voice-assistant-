#include "DiscordRpc.h"

#include "Http.h"
#include "Log.h"
#include "MediaNow.h"
#include "Platform.h"
#include "Settings.h"
#include "State.h"
#include "TextUtils.h"

#include "../third_party/nlohmann/json.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <thread>

using nlohmann::json;

namespace {
    std::mutex g_mutex;
    std::string g_status = "выключен";
    std::atomic<bool> g_reload{ true };
    const long long g_startedAt = static_cast<long long>(std::time(nullptr));

    struct Options { bool enabled = false, music = true, video = true; std::string appId, image = "anya"; };

    // Какая картинка есть у приложения: «anya», если загружена под этим именем, иначе первая загруженная
    std::string PickImage(const std::string& appId) {
        const std::string body = Http::HttpsGet(L"discord.com", L"/api/v10/oauth2/applications/" + Text::ToWide(appId) + L"/assets", 8000);
        const json assets = json::parse(body, nullptr, false);
        if (!assets.is_array() || assets.empty()) return "anya";
        for (const auto& a : assets)
            if (a.value("name", "") == "anya") return "anya";
        const std::string first = assets.front().value("name", "anya");
        Log::Write("discord: картинка «" + first + "»");
        return first;
    }

    Options Load() {
        Options o;
        o.enabled = Config::Value("discord", "enabled", "0") == "1";
        o.appId = Text::Trim(Config::Value("discord", "app_id", ""));
        o.music = Config::Value("discord", "show_music", "1") == "1";
        o.video = Config::Value("discord", "show_video", "1") == "1";
        return o;
    }

    void SetStatus(const std::string& s) {
        std::lock_guard lock(g_mutex);
        g_status = s;
    }

    // ------------------------------------------------------------ канал Discord: \\.\pipe\discord-ipc-N, кадр = код + длина + JSON
    struct Pipe {
        HANDLE h = INVALID_HANDLE_VALUE;
        ~Pipe() { Close(); }
        void Close() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); h = INVALID_HANDLE_VALUE; }
        bool Open() {
            for (int i = 0; i < 10 && h == INVALID_HANDLE_VALUE; ++i)
                h = CreateFileW((L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i)).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            return h != INVALID_HANDLE_VALUE;
        }
        bool Write(uint32_t op, const json& payload) {
            const std::string body = payload.dump();
            std::string frame(8, '\0');
            const uint32_t length = static_cast<uint32_t>(body.size());
            memcpy(frame.data(), &op, 4);
            memcpy(frame.data() + 4, &length, 4);
            frame += body;
            DWORD written = 0;
            return WriteFile(h, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr) && written == frame.size();
        }
        // Ответы Discord читаем, чтобы канал не забился; true — ответ без ошибки
        bool Drain(bool wait) {
            for (int tries = 0; tries < (wait ? 20 : 1); ++tries) {
                DWORD available = 0;
                if (!PeekNamedPipe(h, nullptr, 0, nullptr, &available, nullptr)) return false;
                if (available >= 8) {
                    uint32_t header[2] = {};
                    DWORD read = 0;
                    if (!ReadFile(h, header, 8, &read, nullptr)) return false;
                    std::string body(header[1], '\0');
                    if (header[1] && !ReadFile(h, body.data(), header[1], &read, nullptr)) return false;
                    if (body.find("\"evt\":\"ERROR\"") != std::string::npos) {
                        Log::Write("discord: " + body.substr(0, 200));
                        return false;
                    }
                    return true;
                }
                if (wait) Sleep(100);
            }
            return !wait;
        }
    };

    bool IsBrowser(const std::string& app) { return app == "Edge" || app == "Chrome" || app == "Firefox"; }

    // Что показать сейчас; пустой объект — ничего
    json Activity(const Options& o) {
        // своя картинка из настроек ([discord] image) — главнее
        const auto track = MediaNow::Snapshot();
        json assets = { { "large_image", o.image }, { "large_text", "ASTRIX - Voice assistant" } };
        auto cut = [](const std::string& s) { return Text::ToUtf8(Text::ToWide(s).substr(0, 120)); }; // предел Discord — 128
        if (track.present && track.playing && !track.title.empty()) {
            const bool video = IsBrowser(track.app);
            if ((video && o.video) || (!video && o.music)) {
                json a = { { "type", video ? 3 : 2 }, { "details", cut(track.title) }, { "assets", assets } };
                const std::string by = track.artist.empty() ? track.app : track.artist;
                if (!by.empty()) a["state"] = cut(video ? by : "— " + by);
                if (track.duration > 1) {
                    const long long now = static_cast<long long>(std::time(nullptr));
                    a["timestamps"] = { { "start", (now - static_cast<long long>(track.position)) * 1000 },
                                        { "end", (now - static_cast<long long>(track.position) + static_cast<long long>(track.duration)) * 1000 } };
                }
                return a;
            }
        }
        const std::string me = Config::Get().assistant;
        const std::string doing = g_app.speaking ? "Разговаривает с " + Text::NameForm(me, 4) : g_app.capturing ? "Говорит с " + Text::NameForm(me, 4) : me + " рядом 💜";
        return { { "type", 0 }, { "details", doing }, { "timestamps", { { "start", g_startedAt * 1000 } } }, { "assets", assets } };
    }

    void Loop() {
        Pipe pipe;
        Options options;
        std::string sent; // последний отправленный статус — повторно не шлём
        for (int nonce = 0;; ++nonce) {
            if (g_reload.exchange(false)) {
                options = Load();
                if (options.enabled && !options.appId.empty()) {
                    const std::string own = Text::Trim(Config::Value("discord", "image", ""));
                    options.image = own.empty() ? "https://raw.githubusercontent.com/zephiro4ka/Voice-assistant-ASTRIX/main/assets/discord.png" : own;
                }
                if (pipe.h != INVALID_HANDLE_VALUE && !options.enabled) { // выключили — убрать статус
                    pipe.Write(1, { { "cmd", "SET_ACTIVITY" }, { "args", { { "pid", GetCurrentProcessId() } } }, { "nonce", std::to_string(nonce) } });
                    pipe.Drain(false);
                }
                pipe.Close();
                sent.clear();
            }
            if (!options.enabled) { SetStatus("выключен"); Sleep(1000); continue; }
            if (options.appId.empty()) { SetStatus("нужен ID приложения Discord"); Sleep(1000); continue; }

            if (pipe.h == INVALID_HANDLE_VALUE) {
                if (!pipe.Open()) { SetStatus("Discord не запущен"); Sleep(5000); continue; }
                pipe.Write(0, { { "v", 1 }, { "client_id", options.appId } });
                if (!pipe.Drain(true)) { pipe.Close(); SetStatus("Discord не принял ID приложения"); Sleep(10000); continue; }
                sent.clear();
            }

            const json activity = Activity(options);
            json keyed = activity;
            keyed.erase("timestamps"); // время сдвигается каждую секунду — повторно шлём только при смене трека или состояния
            const std::string key = keyed.dump();
            if (key != sent) {
                const json command = { { "cmd", "SET_ACTIVITY" }, { "args", { { "pid", GetCurrentProcessId() }, { "activity", activity } } },
                                       { "nonce", std::to_string(nonce) } };
                if (!pipe.Write(1, command) || !pipe.Drain(true)) { pipe.Close(); SetStatus("связь с Discord потеряна"); Sleep(3000); continue; }
                sent = key;
                SetStatus("показываю статус в Discord");
            } else {
                pipe.Drain(false);
            }
            for (int i = 0; i < 30 && !g_reload.load(); ++i) Sleep(100);
        }
    }
}

void DiscordRpc::Start() { std::thread(Loop).detach(); }
void DiscordRpc::Reload() { g_reload = true; }

std::string DiscordRpc::Status() {
    std::lock_guard lock(g_mutex);
    return g_status;
}
