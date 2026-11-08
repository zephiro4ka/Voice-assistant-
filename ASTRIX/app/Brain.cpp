#include "Brain.h"
#include "Ears.h"

namespace { constexpr float kAskAgainBelow = 0.40f; }

#include "Browser.h"
#include "Chatter.h"
#include "Log.h"
#include "LocalAi.h"
#include "Settings.h"
#include "Skills.h"
#include "State.h"
#include "TextUtils.h"
#include "Voice.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// Как Аня решает, что делать с фразой:
//   1. «стоп» — замолчать
//   2. «спроси нейросеть …» — сразу к ИИ
//   3. навыки (Skills) — программы, медиа, окна, диктовка, таймеры… — мгновенно и без ИИ
//   4. болтовня (Chatter) — «как дела», шутки, поддержка — тоже без ИИ
//   5. всё остальное (домашка, сложные вопросы) — ИИ, если он запущен

namespace {
    std::thread g_thread;
    std::mutex g_mutex;
    std::condition_variable g_cv;
    std::deque<std::string> g_queue;

    void Reply(const std::string& text) {
        Chat::Add(Role::Anya, text);
        Caption::Set(text);
        Voice::Speak(text);
        Conversation::Push("assistant", text);
    }

    bool IsStopCommand(const std::string& cmd) {
        for (const char* word : { "стоп", "замолчи", "отмена", "хватит", "помолчи" })
            if (Text::Contains(cmd, word)) return true;
        return false;
    }

    // «Спроси нейросеть, как решить уравнение» → «как решить уравнение». Пусто — не просили.
    std::string ExplicitAiRequest(const std::string& cmd) {
        const std::wstring wide = Text::ToWide(cmd);
        const std::wstring lower = Text::LowerRu(wide);
        for (const wchar_t* prefix : { L"спроси у нейросети", L"спроси нейросеть", L"спроси у нейронки", L"спроси нейронку",
                                       L"спроси у ии", L"спроси ии", L"нейросеть", L"нейронка" }) {
            const size_t pos = lower.find(prefix);
            if (pos == std::wstring::npos) continue;
            const std::string rest = Text::Trim(Text::ToUtf8(wide.substr(pos + wcslen(prefix))));
            return rest.empty() ? cmd : rest;
        }
        return {};
    }

    void AskAi(const std::string& question) {
        if (!g_app.aiReady) {
            Reply(g_app.aiChecked ? "Нейросеть не запустилась — загляни в llama-server.log рядом со мной"
                                  : "Нейросеть ещё просыпается, спроси через несколько секунд");
            return;
        }
        g_app.thinking = true;
        const Ai::Answer answer = Ai::Ask(question);
        g_app.thinking = false;
        if (!answer.ok) {
            Reply("Нейросеть не ответила. Попробуй ещё раз чуть позже");
            return;
        }
        if (answer.commands.empty()) return; // обычный ответ уже прозвучал

        // нейросеть «перевела» просьбу в команды — выполняем их навыками
        std::string replies;
        bool done = false;
        for (const auto& command : answer.commands) {
            const auto reply = Skills::Handle(command);
            Log::Write("  нейросеть перевела в команду «" + command + "» → " + (reply ? "навык" : "не навык"));
            if (!reply) continue;
            done = true;
            if (!reply->empty()) replies += (replies.empty() ? "" : ". ") + *reply;
        }
        if (!done) Reply("Хм, не получилось это сделать. Скажи по-другому?");
        else if (!replies.empty()) Reply(replies);
        else Caption::Set("готово ✦");
    }

    void Process(const std::string& raw) {
        const std::string cmd = Text::Trim(raw);
        if (cmd.empty()) return;
        Chat::Add(Role::User, cmd);
        Caption::Set("«" + cmd + "»");

        if (IsStopCommand(cmd)) {
            Voice::Stop();
            if (Browser::StopScroll()) Caption::Set("остановила прокрутку"); // молча — просто перестаём листать
            else Reply("Хорошо");
            return;
        }
        if (const std::string question = ExplicitAiRequest(cmd); !question.empty()) {
            AskAi(question);
            return;
        }
        if (const auto reply = Skills::Handle(cmd)) {
            Log::Write("команда «" + cmd + "» → навык: " + (reply->empty() ? std::string("(молча)") : *reply));
            if (!reply->empty()) Reply(*reply);
            else Caption::Set("готово ✦");
            return;
        }
        // заготовленные ответы милые — у строгой Ани и у своего характера болтовню ведёт нейросеть
        const Personality personality = Config::Get().personality;
        const bool chatterFits = personality == Personality::Balanced || personality == Personality::Sweet || !g_app.aiReady;
        if (const auto reply = chatterFits ? Chatter::Reply(cmd) : std::nullopt) {
            Log::Write("команда «" + cmd + "» → болтовня");
            Conversation::Push("user", cmd);
            Reply(*reply);
            return;
        }
        // Обрывки фраз («я...», «Парни») — молча пропускаем: нейросеть на них отвечает ерундой.
        // Одно слово — всегда обрывок; два — если нейросети нет, отвечать всё равно нечем.
        const auto spaces = std::count(cmd.begin(), cmd.end(), ' ');
        if (spaces == 0 || (spaces == 1 && !g_app.aiReady)) {
            Log::Write("команда «" + cmd + "» → слишком коротко и непонятно, молчу");
            Caption::Set("…не расслышала «" + cmd + "»");
            return;
        }
        Log::Write("команда «" + cmd + "» → " + (g_app.aiReady ? "ИИ" : "не поняла"));
        if (g_app.aiReady) AskAi(cmd);
        else Reply(Chatter::NotUnderstood());
    }

    void Run() {
        while (g_app.running) {
            std::string text;
            {
                std::unique_lock lock(g_mutex);
                g_cv.wait(lock, [] { return !g_queue.empty() || !g_app.running; });
                if (!g_app.running) break;
                text = std::move(g_queue.front());
                g_queue.pop_front();
            }
            Process(text);
        }
    }
}

void Brain::Start() { g_thread = std::thread(Run); }

void Brain::Shutdown() {
    g_cv.notify_all();
    // поток может ждать ответа ИИ до 2 минут — не держим из-за него закрытие окна
    if (g_thread.joinable()) g_thread.detach();
}

void Brain::Submit(const std::string& command) {
    {
        std::lock_guard lock(g_mutex);
        g_queue.push_back(command);
    }
    g_cv.notify_one();
}

void Brain::Interrupt() {
    g_app.cancelGeneration++;
    Voice::Stop();
    std::lock_guard lock(g_mutex);
    g_queue.clear();
}

void Brain::OnHeard(const std::string& text) {
    if (Skills::IsDictating()) { // в диктовке всё сказанное — это текст, а не команды
        Skills::HandleDictation(text);
        return;
    }
    if (Browser::IsScrolling()) { // пока Аня листает — «стоп», «быстрее», «медленнее» работают и без «Аня»
        const std::wstring lower = Text::LowerRu(Text::ToWide(text));
        auto has = [&](const wchar_t* w) { return lower.find(w) != std::wstring::npos; };
        if (has(L"стоп") || has(L"хватит") || has(L"останов") || has(L"стой") || has(L"замри")) {
            Browser::StopScroll();
            Caption::Set("остановила прокрутку");
            return;
        }
        if (has(L"быстре")) { Browser::ChangeScrollSpeed(1.6f); Caption::Set("листаю быстрее"); return; }
        if (has(L"медленн") || has(L"помедлен")) { Browser::ChangeScrollSpeed(0.6f); Caption::Set("листаю медленнее"); return; }
    }

    const bool direct = g_app.pttArmed.exchange(false) || NowMs() < g_app.followUpUntil.load();

    std::string command;
    const bool called = Text::FindWakeWord(text, command, Config::Get().assistant);

    if (called && command.empty()) { // просто «Аня» — ждём, что скажут дальше
        g_app.followUpUntil = NowMs() + 9000;
        Caption::Set("да-да? слушаю ✦", true);
        Voice::Speak("Да?");
        return;
    }
    // обращаются к ней, но слова разобрались плохо — лучше переспросить, чем сделать не то
    if ((direct || called) && Ears::LastConfidence() < kAskAgainBelow) {
        Log::Write("  не уверена в расслышанном — переспрашиваю");
        g_app.followUpUntil = NowMs() + 9000;
        Caption::Set("не расслышала, повтори, пожалуйста ✦", true);
        Voice::Speak("Не расслышала, повтори, пожалуйста");
        return;
    }
    if (direct) { // нажали на шар или только что сказали её имя
        Submit(called ? command : text);
        return;
    }
    if (!called) {
        Log::Write("  без обращения «Аня» — не мне");
        g_app.utteranceEndAt = 0; // это было не нам — не считаем в скорость ответа
        Caption::Set("…" + Text::Trim(text) + "  (скажи «" + Config::Get().assistant + "», если это мне)");
        return;
    }
    Submit(command);
}

void Brain::OnBodySound(BodySounds::Kind kind) {
    static Clock s_lastReply = 0;
    constexpr Clock kQuietMs = 45000; // на серию «кхе-кхе-кхе» отвечаем один раз
    if (kind == BodySounds::Kind::None || Skills::IsDictating() || g_app.speaking || !Config::Get().bodySounds) return;
    if (s_lastReply && NowMs() - s_lastReply < kQuietMs) return;
    s_lastReply = NowMs();

    static const char* kCough[] = { "Всё в порядке? Не заболел?", "Всё в порядке? Не заболел?", "Ой, ты кашляешь. Может, тёплого чаю?" };
    static const char* kSneeze[] = { "Будь здоров!", "Будь здоров!", "Будь здоров, {name}!" };
    const char* const* options = kind == BodySounds::Kind::Cough ? kCough : kSneeze;
    std::string phrase = options[NowMs() % 3];
    for (size_t pos; (pos = phrase.find("{name}")) != std::string::npos;) phrase.replace(pos, 6, Config::Get().name);

    g_app.utteranceEndAt = 0; // это не вопрос — в скорость ответа не считаем
    Chat::Add(Role::Anya, phrase);
    Caption::Set(phrase);
    Voice::Speak(phrase);
}

void Brain::ToggleListening() {
    if (g_app.speaking || g_app.thinking) Interrupt();
    if (g_app.pttArmed) {
        g_app.pttArmed = false;
        Caption::Set("ок, не слушаю");
        return;
    }
    g_app.micEnabled = true;
    g_app.pttArmedAt = NowMs();
    g_app.pttArmed = true;
    Caption::Set("говори, я слушаю…", true);
}
