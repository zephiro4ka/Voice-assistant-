#include "Telegram.h"

#include "Http.h"
#include "Input.h"
#include "LocalAi.h"
#include "Log.h"
#include "MediaNow.h"
#include "Pc.h"
#include "PowerMode.h"
#include "Screen.h"
#include "Settings.h"
#include "Skills.h"
#include "State.h"
#include "SystemMonitor.h"
#include "TextUtils.h"
#include "Voice.h"

#include "../third_party/nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>

using nlohmann::json;

namespace {
    constexpr const wchar_t* kHost = L"api.telegram.org";

    std::mutex g_mutex;
    std::string g_token;
    long long g_owner = 0;
    bool g_enabled = false;
    std::string g_status = "выключен";
    std::atomic<bool> g_ready{ false };
    std::atomic<int> g_generation{ 0 }; // Reload — старый поток опроса сам завершится
    std::atomic<bool> g_solving{ false };

    // кнопки под полем ввода в телеграме
    const char* kScreen = "📸 Экран";
    const char* kSolve = "🧠 Реши с экрана";
    const char* kStatus = "📊 Состояние";
    const char* kLock = "🔒 Блокировка";
    const char* kShowLock = "🔆 Показать экран блокировки"; // вместо «Блокировки», пока компьютер заблокирован
    const char* kPause = "⏯ Пауза";
    const char* kNext = "⏭ Дальше";
    const char* kLouder = "🔊 Громче";
    const char* kQuieter = "🔉 Тише";
    const char* kStandby = "🌙 Выключить (бот на связи)";
    const char* kWake = "☀ Включить";
    const char* kPowerOff = "⏻ Выключить совсем";

    json Keyboard() {
        return { { "keyboard", json::array({ json::array({ kScreen, kSolve }), json::array({ kStatus, PowerMode::IsLocked() ? kShowLock : kLock }),
                                             json::array({ kPause, kNext, kLouder, kQuieter }), json::array({ kStandby, kWake }),
                                             json::array({ kPowerOff }) }) },
                 { "resize_keyboard", true }, { "is_persistent", true } };
    }

    struct Config_ { std::string token; long long owner; };
    Config_ Current() {
        std::lock_guard lock(g_mutex);
        return { g_token, g_owner };
    }

    std::wstring Path(const std::string& token, const std::string& method) { return Text::ToWide("/bot" + token + "/" + method); }

    json Call(const std::string& method, const json& body, int timeoutMs = 20000) {
        const auto c = Current();
        if (c.token.empty()) return {};
        const std::string response = Http::HttpsPost(kHost, Path(c.token, method), body.dump(), "application/json", timeoutMs);
        return json::parse(response, nullptr, false);
    }

    void Reply(long long chat, const std::string& text, const json& markup = json(), bool html = false) {
        // у телеграма предел — 4096 символов в сообщении (HTML не режем — он всегда короткий)
        const std::wstring wide = Text::ToWide(text.empty() ? "Готово ✅" : text);
        const size_t piece = html ? wide.size() + 1 : 3800;
        for (size_t at = 0; at < wide.size(); at += piece) {
            json body = { { "chat_id", chat }, { "text", Text::ToUtf8(wide.substr(at, piece)) } };
            if (html) body["parse_mode"] = "HTML";
            if (!markup.is_null() && at + piece >= wide.size()) body["reply_markup"] = markup;
            Call("sendMessage", body);
        }
    }

    std::string Html(const std::string& text) { // экранирование для parse_mode=HTML
        std::string out;
        for (char c : text) out += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : std::string(1, c);
        return out;
    }

    // ▰▰▰▱▱▱▱▱▱▱ — полоска из 10 делений
    std::string Bar(float percent) {
        const int filled = std::clamp(static_cast<int>(std::lround(percent / 10.0f)), 0, 10);
        std::string bar;
        for (int i = 0; i < 10; ++i) bar += i < filled ? "▰" : "▱";
        return bar;
    }

    std::string Temp(float t) {
        if (t <= 0) return "";
        const char* mark = t < 60 ? "🟢" : t < 80 ? "🟡" : "🔴";
        char text[32];
        snprintf(text, sizeof(text), "  %s %d°C", mark, static_cast<int>(std::lround(t)));
        return text;
    }

    std::string VolumeText() {
        Sleep(120); // Windows применяет нажатия клавиш громкости не мгновенно
        const int v = Pc::Volume().value_or(0);
        return std::string(Pc::IsMuted() ? "🔇" : "🔊") + " <b>Громкость " + std::to_string(v) + "%</b>\n<code>" + Bar(static_cast<float>(v)) + "</code>";
    }

    bool Photo(long long chat, const std::vector<uint8_t>& jpeg, const std::string& caption) {
        const auto c = Current();
        if (c.token.empty() || jpeg.empty()) return false;
        const std::string boundary = "----AnyaBoundary" + std::to_string(GetTickCount64());
        std::string body;
        auto field = [&](const char* name, const std::string& value) {
            body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
        };
        field("chat_id", std::to_string(chat));
        if (!caption.empty()) field("caption", Text::ToUtf8(Text::ToWide(caption).substr(0, 1000)));
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"screen.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
        body.append(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
        body += "\r\n--" + boundary + "--\r\n";
        const std::string response = Http::HttpsPost(kHost, Path(c.token, "sendPhoto"), body, "multipart/form-data; boundary=" + boundary, 60000);
        return response.find("\"ok\":true") != std::string::npos;
    }

    // Фото с телефона: скачать самое большое
    std::string DownloadPhoto(const json& photos) {
        if (!photos.is_array() || photos.empty()) return {};
        const std::string id = photos.back().value("file_id", "");
        const json file = Call("getFile", { { "file_id", id } });
        const std::string path = file.contains("result") ? file["result"].value("file_path", "") : "";
        if (path.empty()) return {};
        return Http::HttpsGet(kHost, Text::ToWide("/file/bot" + Current().token + "/" + path), 30000);
    }

    // Снимок экрана → нейросеть → ответ (снимок отдельно, чтобы было видно, что она решала)
    void SolveScreen(long long chat, const std::string& question) {
        if (g_solving.exchange(true)) { Reply(chat, "Я уже решаю — подожди немного"); return; }
        Reply(chat, "Смотрю на экран и думаю над задачей… 🧠 Сложную решаю внимательно — это займёт 1–3 минуты.");
        const auto shot = Screen::CaptureJpeg(0, 80);
        const auto forAi = Screen::CaptureJpeg(1280, 85);
        Photo(chat, shot, "Вот что на экране");
        std::string answer;
        if (!Ai::CanSee()) answer = "Нейросеть ещё не готова или у модели нет «глаз» (файла mmproj) — не могу прочитать экран.";
        else answer = Ai::Quiet(question.empty() ? "Реши задачу на этом снимке экрана." : question, Screen::Base64(forAi), true);
        Reply(chat, answer.empty() ? "Не получилось решить — нейросеть не ответила" : answer);
        g_solving = false;
    }

    // Красивая сводка: разделы, полоски загрузки, цветные температуры
    std::string StatusText() {
        const auto s = SystemMonitor::Snapshot();
        if (!s.ready) return "⏳ Ещё собираю данные о компьютере";
        char line[160];
        std::string text = "<b>💻 Компьютер</b>\n\n";
        snprintf(line, sizeof(line), "🧠 <b>Процессор</b> — %d%%%s\n<code>%s</code>\n", static_cast<int>(s.cpu), Temp(s.cpuTemp).c_str(), Bar(s.cpu).c_str());
        text += line;
        if (s.hasGpu) {
            snprintf(line, sizeof(line), "🎮 <b>Видеокарта</b> — %d%%%s\n<code>%s</code>\n", static_cast<int>(s.gpu), Temp(s.gpuTemp).c_str(), Bar(s.gpu).c_str());
            text += line;
            snprintf(line, sizeof(line), "🎞 <b>Видеопамять</b> — %.1f из %.1f ГБ\n", s.vramUsedGb, s.vramTotalGb);
            text += line;
        }
        snprintf(line, sizeof(line), "🧩 <b>Память</b> — %.1f из %.0f ГБ\n<code>%s</code> %d%%\n", s.ramUsedGb, s.ramTotalGb, Bar(s.ram).c_str(), static_cast<int>(s.ram));
        text += line;
        snprintf(line, sizeof(line), "💾 <b>Диск C</b> — свободно %.0f из %.0f ГБ\n", s.diskFreeGb, s.diskTotalGb);
        text += line;

        const auto track = MediaNow::Snapshot();
        if (track.present) {
            std::string title = track.title;
            for (const char* ext : { ".mp3", ".flac", ".wav", ".m4a", ".ogg" }) // имя файла — без расширения и подчёркиваний
                if (title.size() > strlen(ext) && title.compare(title.size() - strlen(ext), strlen(ext), ext) == 0) title.resize(title.size() - strlen(ext));
            std::replace(title.begin(), title.end(), '_', ' ');
            text += std::string("\n") + (track.playing ? "▶️ <b>Играет</b>" : "⏸ <b>На паузе</b>") + "\n🎵 " + Html(title) +
                    (track.artist.empty() ? "" : " — <i>" + Html(track.artist) + "</i>") + (track.app.empty() ? "" : "\n<i>" + Html(track.app) + "</i>") + "\n";
        }
        text += "\n" + VolumeText();
        if (PowerMode::IsStandby()) text += "\n\n🌙 <i>Экран выключен — я на связи</i>";
        return text;
    }

    void HandleText(long long chat, const std::string& raw) {
        const std::string text = Text::Trim(raw);
        const std::wstring lower = Text::LowerRu(Text::ToWide(text));
        auto is = [&](const char* button, const wchar_t* command) { return text == button || lower == command; };

        if (lower == L"/start" || lower == L"/help" || lower == L"помощь") {
            Reply(chat, "Привет! Я " + Config::Get().assistant + " 💜 Кнопки внизу — быстрые действия. А ещё можно писать мне как дома: «открой стим», "
                        "«громкость 30», «какая погода», «реши задачу» — или прислать фото задачи.", Keyboard());
            return;
        }
        if (is(kScreen, L"/screen")) { Photo(chat, Screen::CaptureJpeg(0, 85), ""); return; }
        if (is(kSolve, L"/solve") || (lower.find(L"реши") != std::wstring::npos && lower.find(L"экран") != std::wstring::npos)) {
            std::thread(SolveScreen, chat, std::string()).detach();
            return;
        }
        if (is(kStatus, L"/status")) { Reply(chat, StatusText(), json(), true); return; }
        if (is(kLock, L"/lock")) { PowerMode::Lock(); return; } // ответ с новой кнопкой пришлёт OnLockChanged
        if (is(kShowLock, L"/showlock")) {
            PowerMode::Wake();
            Reply(chat, "🔆 Включила экран — на нём экран блокировки. Войди в Windows за компьютером, и кнопка снова станет «🔒 Блокировка».");
            return;
        }
        if (is(kPause, L"/pause")) { MediaNow::Control(MediaNow::Action::PlayPause); Reply(chat, "⏯"); return; }
        if (is(kNext, L"/next")) { MediaNow::Control(MediaNow::Action::Next); Reply(chat, "⏭"); return; }
        // шаг 8%: одно нажатие клавиши громкости Windows — 2%
        if (is(kLouder, L"/louder")) { Input::Tap(VK_VOLUME_UP, 4); Reply(chat, VolumeText(), json(), true); return; }
        if (is(kQuieter, L"/quieter")) { Input::Tap(VK_VOLUME_DOWN, 4); Reply(chat, VolumeText(), json(), true); return; }
        if (is(kStandby, L"/off")) {
            PowerMode::Standby();
            Reply(chat, std::string("🌙 Экран выключен, звук тоже") + (Config::Value("telegram", "lock_on_standby", "0") == "1" ? ", компьютер заблокирован" : "") +
                        ". Я на связи — нажми «☀ Включить», когда понадобится.");
            return;
        }
        if (is(kWake, L"/on")) { PowerMode::Wake(); Reply(chat, "☀ Включила экран и звук"); return; }
        if (is(kPowerOff, L"/shutdown")) {
            const json confirm = { { "inline_keyboard", json::array({ json::array({ { { "text", "Да, выключить" }, { "callback_data", "power_off" } },
                                                                                      { { "text", "Перезагрузить" }, { "callback_data", "power_restart" } } }),
                                                                        json::array({ { { "text", "Отмена" }, { "callback_data", "power_cancel" } } }) }) } };
            Reply(chat, "Выключить компьютер совсем? Тогда и я отключусь — включить его с телефона уже не получится.", confirm);
            return;
        }
        if (lower.rfind(L"скажи ", 0) == 0 || lower.rfind(L"/say ", 0) == 0) {
            Voice::Speak(Text::ToUtf8(Text::ToWide(text).substr(lower.find(L' ') + 1)));
            Reply(chat, "Сказала вслух 🗣");
            return;
        }

        // всё остальное — как будто сказали Ане: сначала её навыки, иначе — нейросеть (тихо, ответом сюда)
        if (const auto done = Skills::Handle(text)) { Reply(chat, *done); return; }
        std::thread([chat, text] {
            const bool task = Ai::LooksLikeTask(text);
            if (task) Reply(chat, "Думаю над задачей… 🧠 Это займёт 1–3 минуты.");
            const std::string answer = Ai::Quiet(text, "", task);
            Reply(chat, answer.empty() ? "Нейросеть пока не готова — попробуй через минутку" : answer);
        }).detach();
    }

    void HandleCallback(const json& query) {
        const long long from = query["from"].value("id", 0LL);
        const std::string data = query.value("data", "");
        Call("answerCallbackQuery", { { "callback_query_id", query.value("id", "") } });
        if (from != Current().owner) return;
        if (data == "power_off") { Reply(from, "Выключаю через 15 секунд. До встречи! 👋"); PowerMode::Shutdown(15); }
        if (data == "power_restart") { Reply(from, "Перезагружаю через 15 секунд — вернусь, как только Windows запустит меня"); PowerMode::Restart(15); }
        if (data == "power_cancel") { PowerMode::CancelShutdown(); Reply(from, "Отменила"); }
    }

    void HandleUpdate(const json& update, long long startedAt) {
        if (update.contains("callback_query")) { HandleCallback(update["callback_query"]); return; }
        if (!update.contains("message")) return;
        const json& message = update["message"];
        const long long chat = message["chat"].value("id", 0LL);
        const long long from = message.contains("from") ? message["from"].value("id", 0LL) : 0;
        if (message.value("date", 0LL) < startedAt - 120) return; // старые сообщения (пока Аня была закрыта) не выполняем

        if (from != Current().owner) {
            Log::Write("телеграм: чужой пользователь " + std::to_string(from) + " — отказ");
            Reply(chat, "⛔ Это личный бот. У тебя нет доступа.");
            return;
        }
        if (message.contains("photo")) { // фото задачи с телефона
            const std::string caption = message.value("caption", "");
            std::thread([chat, photos = message["photo"], caption] {
                Reply(chat, "Смотрю на фото и думаю… 🧠 Это займёт 1–3 минуты.");
                const std::string image = DownloadPhoto(photos);
                const std::vector<uint8_t> bytes(image.begin(), image.end());
                const std::string answer = image.empty() ? "" : Ai::Quiet(caption.empty() ? "Реши задачу на фото." : caption, Screen::Base64(bytes), true);
                Reply(chat, answer.empty() ? "Не получилось прочитать фото" : answer);
            }).detach();
            return;
        }
        if (message.contains("text")) {
            const std::string text = message["text"].get<std::string>();
            Log::Write("телеграм: «" + text + "»");
            HandleText(chat, text);
        }
    }

    void Poll(int generation) {
        const long long startedAt = static_cast<long long>(std::time(nullptr));
        long long offset = 0;
        bool greeted = false;
        while (generation == g_generation.load()) {
            const auto c = Current();
            const std::string response = Http::HttpsGet(kHost, Path(c.token, "getUpdates") + L"?timeout=25&offset=" + std::to_wstring(offset), 35000);
            if (generation != g_generation.load()) break;
            const json data = json::parse(response, nullptr, false);
            if (data.is_discarded() || !data.value("ok", false)) {
                g_ready = false;
                {
                    std::lock_guard lock(g_mutex);
                    g_status = response.empty() ? "нет связи с телеграмом" : "ошибка: " + (data.is_discarded() ? std::string("ответ не разобран") : data.value("description", "неизвестно"));
                    Log::Write("телеграм: " + g_status);
                }
                Sleep(5000);
                continue;
            }
            if (!g_ready.exchange(true)) {
                const json me = Call("getMe", json::object());
                std::lock_guard lock(g_mutex);
                g_status = me.contains("result") ? "на связи как @" + me["result"].value("username", "бот") : "на связи";
                Log::Write("телеграм: " + g_status);
            }
            if (!greeted) { // один раз за запуск — показать кнопки (если не выключено в настройках)
                greeted = true;
                if (Config::Value("telegram", "greet", "1") == "1") Reply(c.owner, Config::Get().assistant + " запущена и на связи 💜", Keyboard());
            }
            for (const auto& update : data["result"]) {
                offset = std::max(offset, update.value("update_id", 0LL) + 1);
                try { HandleUpdate(update, startedAt); } catch (...) { Log::Write("телеграм: не смогла разобрать сообщение"); }
            }
        }
    }
}

void Telegram::Start() { Reload(); }

void Telegram::Reload() {
    const std::string token = Text::Trim(Config::Value("telegram", "token", ""));
    const long long owner = std::atoll(Config::Value("telegram", "owner", "").c_str()); // только этот пользователь
    const bool enabled = Config::Value("telegram", "enabled", "1") == "1";
    const int generation = ++g_generation;
    g_ready = false;
    {
        std::lock_guard lock(g_mutex);
        g_token = token;
        g_owner = owner;
        g_enabled = enabled && !token.empty() && owner != 0;
        g_status = !enabled ? "выключен" : token.empty() ? "нет токена бота" : owner == 0 ? "не указан ID владельца" : "подключаюсь…";
    }
    if (enabled && !token.empty() && owner != 0) std::thread(Poll, generation).detach();
}

bool Telegram::Ready() { return g_ready.load(); }

std::string Telegram::Status() {
    std::lock_guard lock(g_mutex);
    return g_status;
}

bool Telegram::SendText(const std::string& text) {
    if (!Ready()) return false;
    Reply(Current().owner, text);
    return true;
}

bool Telegram::SendPhoto(const std::vector<uint8_t>& jpeg, const std::string& caption) {
    return Ready() && Photo(Current().owner, jpeg, caption);
}

void Telegram::OnLockChanged(bool locked) {
    PowerMode::SetLocked(locked);
    if (!Ready()) return;
    std::thread([locked] { // из оконной процедуры — сеть не ждём
        Reply(Current().owner, locked ? "🔒 Компьютер заблокирован" : "🔓 Компьютер разблокирован", Keyboard());
    }).detach();
}

void Telegram::SolveScreenAsync() {
    if (!Ready()) return;
    std::thread(SolveScreen, Current().owner, std::string()).detach();
}

void Telegram::SendScreenAsync() {
    if (!Ready()) return;
    std::thread([] { Photo(Current().owner, Screen::CaptureJpeg(0, 85), "Снимок экрана"); }).detach();
}
