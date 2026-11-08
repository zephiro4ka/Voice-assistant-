#include "LocalAi.h"

#include "Platform.h"
#include "Http.h"
#include "Json.h"
#include "Log.h"
#include "Settings.h"
#include "Sidecar.h"
#include "State.h"
#include "TextUtils.h"
#include "Voice.h"

#include <shlobj.h>

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>


namespace {
    constexpr unsigned short kPort = 18080;     // свой порт, чтобы не мешать другим программам
    constexpr const char* kContextSize = "16384"; // на 2 потока по 8192: хватает и на снимок экрана, и на долгие размышления над задачей
    constexpr Clock kStartupTimeoutMs = 180000; // загрузка модели в видеокарту — обычно 5–20 с

    std::mutex g_mutex;
    std::vector<std::string> g_models;
    std::string g_activeModel;
    HANDLE g_process = nullptr;
    std::atomic<bool> g_vision{ false };      // у модели есть «глаза» (mmproj) и они разрешены в настройках
    std::atomic<bool> g_engineEyes{ false };  // движок сейчас запущен с ними: они занимают ~0,9 ГБ видеопамяти
    std::atomic<bool> g_wantEyes{ false };    // следующий запуск — с «глазами»
    std::atomic<int> g_busy{ 0 };             // сколько запросов выполняется: во время них не выгружаем
    struct BusyScope { BusyScope() { ++g_busy; } ~BusyScope() { --g_busy; } };
    std::atomic<Clock> g_lastUse{ 0 };    // когда нейросеть последний раз отвечала
    std::atomic<bool> g_unloaded{ false }; // выгружена из видеокарты за простоем — проснётся на вопрос
    std::mutex g_startMutex;    // перезапуски идут строго по очереди
    std::atomic<int> g_tuningVersion{ 0 };

    int AiInt(const char* key, const char* fallback) { return std::atoi(Config::Value("ai", key, fallback).c_str()); }

    // ------------------------------------------------------------ настройки ответов
    struct Preset { const char* name; int chat[6]; int task[6]; }; // температура%, top_p%, top_k, min_p%, повторы%, длина
    constexpr Preset kPresets[Ai::kProfiles - 1] = {
        { "Стандарт",  { 70, 90, 40, 5, 100, 600 },  { 60, 95, 20, 5, 100, 5000 } },
        { "Быстро",    { 50, 90, 40, 5, 105, 250 },  { 40, 90, 30, 5, 100, 1200 } },
        { "Умно",      { 50, 90, 40, 5, 100, 900 },  { 60, 95, 20, 5, 100, 8000 } },
        { "Креативно", { 110, 95, 80, 3, 110, 900 }, { 80, 95, 40, 3, 105, 5000 } },
    };
    constexpr const char* kFields[6] = { "temp", "top_p", "top_k", "min_p", "repeat", "tokens" };

    std::string Key(const char* group, const char* field) { return std::string(group) + "_" + field; }

    enum class Kind { Chat, Think, TelegramFast, TelegramDeep };

    std::string Hundredths(int value) {
        char text[32];
        snprintf(text, sizeof(text), "%.2f", value / 100.0);
        return text;
    }

    std::string Sampling(Kind kind) {
        const int profile = Ai::Profile();
        if (profile == 0) { // значения, с которыми программа настраивалась изначально
            switch (kind) {
            case Kind::Think: return "\"temperature\":0.6,\"top_p\":0.9,\"max_tokens\":1500,";
            case Kind::TelegramFast: return "\"temperature\":0.4,\"top_p\":0.9,\"max_tokens\":1400,";
            case Kind::TelegramDeep: return "\"temperature\":0.6,\"top_p\":0.95,\"top_k\":20,\"max_tokens\":5000,";
            default: return "\"temperature\":0.7,\"top_p\":0.9,\"max_tokens\":600,";
            }
        }
        const bool task = kind == Kind::Think || kind == Kind::TelegramDeep;
        const Preset& standard = kPresets[0];
        const int* defaults = task ? standard.task : standard.chat;
        const char* group = task ? "task" : "chat";
        int v[6];
        for (int i = 0; i < 6; ++i) v[i] = AiInt(Key(group, kFields[i]).c_str(), std::to_string(defaults[i]).c_str());
        if (kind == Kind::TelegramFast) v[5] = std::max(v[5], 1400); // письменный ответ длиннее голосового
        return "\"temperature\":" + Hundredths(std::clamp(v[0], 0, 200)) + ",\"top_p\":" + Hundredths(std::clamp(v[1], 0, 100)) +
               ",\"top_k\":" + std::to_string(std::clamp(v[2], 0, 500)) + ",\"min_p\":" + Hundredths(std::clamp(v[3], 0, 100)) +
               ",\"repeat_penalty\":" + Hundredths(std::clamp(v[4], 50, 200)) + ",\"max_tokens\":" + std::to_string(std::clamp(v[5], 16, 32000)) +
               ",\"seed\":" + std::to_string(AiInt("seed", "-1")) + ",";
    }

    int ThinkMode() { return AiInt("think_mode", "0"); } // 0 только по просьбе, 1 и для задач, 2 никогда

    // ------------------------------------------------------------ HTTP
    int Request(const wchar_t* method, const wchar_t* path, const std::string& body, int timeoutMs, const Http::OnData& onData) {
        return Http::Local(kPort, method, path, body, timeoutMs, onData);
    }

    bool Healthy() {
        return Request(L"GET", L"/health", "", 2000, [](const std::string&) { return true; }) == 200;
    }

    // ------------------------------------------------------------ поиск движка и моделей
    bool FileExists(const std::wstring& path) {
        const DWORD a = GetFileAttributesW(path.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    // llama-server.exe: рядом с exe (копируется при сборке) или в third_party проекта
    std::wstring FindServer() {
        std::wstring dir = Text::ExecutableDirectory();
        for (int level = 0; level < 5; ++level) {
            for (const wchar_t* sub : { L"\\llama.cpp", L"\\third_party\\llama.cpp", L"\\ASTRIX\\third_party\\llama.cpp" }) {
                const std::wstring path = dir + sub + L"\\llama-server.exe";
                if (FileExists(path)) return path;
            }
            const size_t slash = dir.find_last_of(L"\\/");
            if (slash == std::wstring::npos) break;
            dir.resize(slash);
        }
        return {};
    }

    void CollectGguf(const std::wstring& dir, std::vector<std::string>& out, int depth = 0) {
        if (depth > 4) return;
        WIN32_FIND_DATAW data;
        const HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) return;
        do {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring path = dir + L"\\" + name;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                CollectGguf(path, out, depth + 1);
                continue;
            }
            const std::wstring lower = Text::LowerRu(name);
            const bool gguf = lower.size() > 5 && lower.compare(lower.size() - 5, 5, L".gguf") == 0;
            const bool projector = lower.rfind(L"mmproj", 0) == 0; // «глаза» модели для картинок — не сама модель
            if (gguf && !projector) out.push_back(Text::ToUtf8(path));
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }

    // Модели из LM Studio (~/.lmstudio/models) и из папки models проекта
    std::vector<std::string> DiscoverModels() {
        std::vector<std::string> models;
        PWSTR profile = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile))) {
            CollectGguf(std::wstring(profile) + L"\\.lmstudio\\models", models);
            CoTaskMemFree(profile);
        }
        std::wstring dir = Text::ExecutableDirectory();
        for (int level = 0; level < 4; ++level) {
            CollectGguf(dir + L"\\models", models, 3);
            const size_t slash = dir.find_last_of(L"\\/");
            if (slash == std::wstring::npos) break;
            dir.resize(slash);
        }
        std::sort(models.begin(), models.end());
        models.erase(std::unique(models.begin(), models.end()), models.end());
        return models;
    }

    std::string PickModel(const std::vector<std::string>& models, const std::string& wanted) {
        if (!wanted.empty() && std::find(models.begin(), models.end(), wanted) != models.end()) return wanted;
        for (const char* preferred : { "Qwen3.5-4B", "Qwen3-4B", "qwen" })
            for (const auto& m : models)
                if (Text::Contains(m, preferred)) return m;
        return models.empty() ? std::string() : models.front();
    }

    std::string FileName(const std::string& path) {
        const size_t slash = path.find_last_of("\\/");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // «Глаза» модели: mmproj-*.gguf в той же папке
    std::wstring FindProjector(const std::string& model) {
        const std::wstring wide = Text::ToWide(model);
        const std::wstring dir = wide.substr(0, wide.find_last_of(L"\\/"));
        WIN32_FIND_DATAW data;
        const HANDLE find = FindFirstFileW((dir + L"\\mmproj*.gguf").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) return {};
        FindClose(find);
        return dir + L"\\" + data.cFileName;
    }

    // ------------------------------------------------------------ запуск движка
    bool LaunchServer(const std::wstring& server, const std::string& model) {
        const std::wstring found = Config::Value("ai", "vision", "1") == "1" ? FindProjector(model) : std::wstring();
        g_vision = !found.empty();
        const std::wstring projector = g_wantEyes.load() ? found : std::wstring(); // без снимка «глаза» не загружаем
        g_engineEyes = !projector.empty();
        const int threads = AiInt("threads", "0");
        const std::string extra = Text::Trim(Config::Value("ai", "engine_args", ""));
        const std::wstring arguments = L"-m \"" + Text::ToWide(model) + L"\"" +
            (projector.empty() ? L"" : L" --mmproj \"" + projector + L"\"") + // картинки: снимок экрана с задачей
            L" --host 127.0.0.1 --port " + std::to_wstring(kPort) +
            L" -c " + std::to_wstring(std::clamp(AiInt("context", kContextSize), 512, 262144)) +
            L" -np " + std::to_wstring(std::clamp(AiInt("parallel", "2"), 1, 8)) + // два запроса сразу: Аня отвечает голосом, пока решает задачу для телеграма
            L" -ngl " + std::to_wstring(std::clamp(AiInt("gpu_layers", "99"), 0, 999)) + // 99 — все слои в видеокарту (если её нет, движок сам останется на процессоре)
            (threads > 0 ? L" -t " + std::to_wstring(threads) : L"") +
            L" -fa on -ctk q8_0 -ctv q8_0" + // сжатый кэш контекста: −400 МБ видеопамяти, качество то же
            L" --load-mode none" +          // без отображения файла в память: 0,7 ГБ ОЗУ вместо 2,8
            L" --jinja" +   // шаблон чата из самой модели
            (extra.empty() ? L"" : L" " + Text::ToWide(extra)); // свои параметры из настроек — последними, чтобы могли всё переопределить
        Log::Write("нейросеть: параметры движка — контекст " + std::to_string(AiInt("context", kContextSize)) + ", слоёв " + std::to_string(AiInt("gpu_layers", "99")));
        g_process = Sidecar::Launch(server, arguments, L"llama-server.log");
        return g_process != nullptr;
    }

    void StartBlocking(bool waking = false) {
        std::lock_guard startLock(g_startMutex);
        if (!waking) { // проснуться — не значит пропасть: пока модель грузится, вопросы ждут её, а не получают отказ
            g_app.aiReady = false;
            g_app.aiChecked = false;
        }

        const auto models = DiscoverModels();
        const std::string model = PickModel(models, Config::Get().model);
        {
            std::lock_guard lock(g_mutex);
            g_models = models;
            g_activeModel = model;
        }
        const std::wstring server = FindServer();
        if (model.empty() || server.empty()) {
            if (model.empty()) Chat::Add(Role::System, "Не нашла модель нейросети (.gguf) — ни в LM Studio, ни в папке models.");
            if (server.empty()) Chat::Add(Role::System, "Не нашла движок нейросети (third_party/llama.cpp/llama-server.exe).");
            g_app.aiChecked = true;
            return;
        }

        Sidecar::Stop(g_process);
        if (!LaunchServer(server, model)) {
            Chat::Add(Role::System, "Не получилось запустить нейросеть.");
            g_app.aiChecked = true;
            return;
        }

        // ждём, пока модель загрузится в видеокарту
        const Clock deadline = NowMs() + kStartupTimeoutMs;
        while (g_app.running && NowMs() < deadline) {
            if (Sidecar::HasExited(g_process)) {
                Chat::Add(Role::System, "Нейросеть закрылась при запуске — подробности в llama-server.log рядом с Аней.");
                break;
            }
            if (Healthy()) {
                g_app.aiReady = true;
                break;
            }
            Sleep(400);
        }
        if (waking && !Healthy()) g_app.aiReady = false;
        g_app.aiChecked = true;
    }

    // Запуск Ани: только находим модель. В видеокарту она загрузится при первом вопросе
    void PrepareLazy() {
        const auto models = DiscoverModels();
        const std::string model = PickModel(models, Config::Get().model);
        if (model.empty() || FindServer().empty()) { StartBlocking(); return; } // скажет, чего не хватает
        {
            std::lock_guard lock(g_mutex);
            g_models = models;
            g_activeModel = model;
        }
        g_vision = Config::Value("ai", "vision", "1") == "1" && !FindProjector(model).empty();
        g_unloaded = true;
        g_app.aiReady = true;
        g_app.aiChecked = true;
    }

    // ------------------------------------------------------------ разговор
    std::string StripThinking(const std::string& s) {
        std::string out;
        size_t pos = 0;
        for (;;) {
            const size_t open = s.find("<think>", pos);
            if (open == std::string::npos) return out + s.substr(pos);
            out += s.substr(pos, open - pos);
            const size_t close = s.find("</think>", open);
            if (close == std::string::npos) return out; // ещё думает — ничего не показываем
            pos = close + 8;
        }
    }

    // Конец ближайшего готового куска речи (позиция после него) или npos.
    // Нейросеть пишет быстрее, чем Аня говорит, поэтому отдаём голосу по одному предложению:
    // короткие куски синтезируются, пока звучит предыдущий, — без пауз между ними.
    // Самый первый кусок и слишком длинные предложения режем ещё и по запятой.
    size_t FindSpeakableCut(const std::string& s, size_t from, bool firstPiece) {
        const size_t commaAfter = firstPiece ? 40 : 120; // байт UTF-8: ≈ 20 и 60 русских букв
        for (size_t i = from; i + 1 < s.size(); ++i) {
            const char c = s[i], next = s[i + 1];
            const bool gap = next == ' ' || next == '\n';
            if ((c == '.' || c == '!' || c == '?' || c == '\n') && gap) return i + 1;
            if (c == ',' && gap && i - from > commaAfter) return i + 1;
        }
        return std::string::npos;
    }

    constexpr const char* kCommandTag = "КОМАНДА:";

    // Как Аня разговаривает — по выбранному характеру
    std::string Persona(const Settings& cfg) {
        switch (cfg.personality) {
        case Personality::Strict:
            return "Характер: строгая, холодная и немногословная. Отвечай сухо и только по делу: одно-два коротких предложения, "
                   "без шуток, ласковых слов, восклицаний и эмоций.";
        case Personality::Sweet:
            return "Характер: милая, нежная и очень преданная. Ты искренне заботишься о пользователе и радуешься ему, "
                   "можешь ласково похвалить, подбодрить и немножко пококетничать. Отвечай тепло и коротко: одно-три предложения.";
        case Personality::Custom:
            if (!Text::Trim(cfg.customPrompt).empty()) return "Характер, который задал пользователь: " + cfg.customPrompt;
            [[fallthrough]];
        default:
            return "Характер: дружелюбная и спокойная — вежливая и тёплая, но без лишних нежностей, иногда с лёгким юмором. "
                   "Отвечай коротко и по делу: обычно одно-три предложения.";
        }
    }

    // Модель сама не знает, какой сейчас день: сообщаем в каждом запросе
    std::string CurrentMoment() {
        static const char* kDays[] = { "воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота" };
        static const char* kMonths[] = { "января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа", "сентября", "октября", "ноября", "декабря" };
        SYSTEMTIME t{};
        GetLocalTime(&t);
        char clock[16];
        snprintf(clock, sizeof(clock), "%02u:%02u", t.wHour, t.wMinute);
        return std::string("Сейчас ") + kDays[t.wDayOfWeek] + ", " + std::to_string(t.wDay) + " " + kMonths[t.wMonth - 1] + " " + std::to_string(t.wYear) + " года, время " + clock + ".";
    }

    std::string SystemPrompt(const std::string& userName) {
        std::string friends;
        for (const auto& [spoken, handle] : Config::Contacts()) friends += (friends.empty() ? "" : ", ") + spoken;
        if (friends.empty()) friends = "(пока никого)";

        const std::string aboutMe = Text::Trim(Config::Value("ai", "about_me", ""));
        const std::string extra = Text::Trim(Config::Value("ai", "extra_prompt", "")) + (aboutMe.empty() ? "" : " Что важно знать о пользователе: " + aboutMe);
        return (Text::Trim(extra).empty() ? std::string() : "Особые указания пользователя (важнее всего остального, кроме формата команд): " + extra + "\n\n") +
               "Ты — " + Config::Get().assistant + ", голосовая помощница-девушка. Пользователя зовут " + userName + ". " + Persona(Config::Get()) + " "
               + CurrentMoment() + " Никогда не говори, что не знаешь дату или время. Время называй как человек: «восемь сорок девять вечера», а не «двадцать часов сорок девять минут». Не предлагай зайти к кому-то в чат или открыть браузер, если об этом не просили. "
               "Тебя создал Зефир (его ещё зовут Зефирка или Мара, ник zephiro4ka) — он твой создатель и разработчик программы ASTRIX. Если спрашивают о создателе, называй его по имени "
               "и говори о нём тепло и с благодарностью, в своём характере. Тот, кто сейчас с тобой говорит, может быть другим человеком. "
               "Ты живёшь на его компьютере и умеешь им управлять через свои команды.\n\n"
               "Если пользователь просит что-то СДЕЛАТЬ на компьютере — открыть программу или сайт, закрыть, зайти к кому-то в чат, "
               "поставить паузу, поменять громкость, напечатать текст, поставить таймер, найти в интернете — не отвечай словами. "
               "Ответь только строкой вида\n"
               "КОМАНДА: <команда>\n"
               "(несколько действий — несколько таких строк). Команды:\n"
               "открой <программа или сайт>\n"
               "закрой <программа>\n"
               "зайди в чат с <имя> — личные сообщения в Discord\n"
               "открой видео <название> — нажать на видео или ссылку, которые сейчас видны в браузере\n"
               "какая сегодня погода | какая будет погода | когда будет дождь\n"
               "пролистай ниже | пролистай выше | прошлая вкладка | следующая вкладка | открой закрытую вкладку | скрой браузер | покажи браузер\n"
               "какие вкладки открыты | перейди на вкладку <название> | громкость на <число> | сколько заряда | загрузка компьютера | прочитай выделенное\n"
               "пауза | продолжи | следующий трек | предыдущий трек | громче | тише | выключи звук\n"
               "напечатай <текст>\n"
               "сверни все | сделай скриншот | заблокируй компьютер | почисти память\n"
               "таймер на <число> минут | напомни через <число> минут <о чём>\n"
               "найди <запрос>\n"
               "Речь распознаётся с ошибками: «дискорт», «открать», «прощал» — догадайся, что имелось в виду. "
               "Имя друга пиши в начальной форме; известные друзья: " + friends + " — если имя похоже на кого-то из них, выбери его. "
               "Никогда не говори, что не можешь управлять компьютером.\n\n"
               "На обычные вопросы и разговор отвечай по-русски, коротко и в своём характере. "
               "Если просят помочь с учёбой или задачей — объясняй понятно и по шагам, но без лишней воды. "
               "Ответ будет озвучен, поэтому без markdown, списков, эмодзи и кода. Склоняй числа и даты правильно: «тридцать первое октября», «в тысяча девятьсот сорок первом году».";
    }

    // Начало ответа — это «КОМАНДА:»? Пока ответ короче метки, решить нельзя — ждём следующих слов.
    enum class Reply { Undecided, Command, Speech };
    Reply Classify(const std::string& visible) {
        const std::wstring text = Text::LowerRu(Text::ToWide(Text::Trim(visible)));
        const std::wstring tag = Text::LowerRu(Text::ToWide(kCommandTag));
        if (text.size() < tag.size()) return tag.rfind(text, 0) == 0 ? Reply::Undecided : Reply::Speech;
        return text.rfind(tag, 0) == 0 ? Reply::Command : Reply::Speech;
    }

    // «КОМАНДА: открой discord\nКОМАНДА: зайди в чат с прыщавый» → { "открой discord", "зайди в чат с прыщавый" }
    std::vector<std::string> ExtractCommands(const std::string& text) {
        std::vector<std::string> commands;
        const std::wstring wide = Text::ToWide(text);
        const std::wstring lower = Text::LowerRu(wide);
        const std::wstring tag = Text::LowerRu(Text::ToWide(kCommandTag));
        for (size_t pos = lower.find(tag); pos != std::wstring::npos; pos = lower.find(tag, pos + tag.size())) {
            const size_t start = pos + tag.size();
            const size_t end = std::min(wide.find(L'\n', start), lower.find(tag, start));
            std::wstring line = wide.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            std::erase_if(line, [](wchar_t ch) { return ch == L'<' || ch == L'>'; }); // модель иногда копирует «<имя>» из шаблона
            const std::string command = Text::Trim(Text::ToUtf8(line));
            if (!command.empty()) commands.push_back(command);
        }
        return commands;
    }

    // Задача или домашка — решать по шагам «вслух» (быстро и верно: 2–6 секунд)
    bool IsTask(const std::string& prompt) {
        for (const char* marker : { "реши", "решить", "уравнени", "задач", "домашк", "докажи", "контрольн", "посчитай", "вычисли", "сколько будет",
                                    "найди корн", "найди x", "найди икс", "найди значение", "найди площадь", "найди периметр" })
            if (Text::Contains(prompt, marker)) return true;
        return false;
    }

    // Долгие размышления — только когда просят прямо: «подумай хорошо», «подумай как следует»
    bool WantsDeepThinking(const std::string& prompt) {
        for (const char* marker : { "подумай хорошо", "подумай как следует", "подумай подольше", "хорошенько подумай", "подумай внимательно" })
            if (Text::Contains(prompt, marker)) return true;
        return false;
    }

    // Формат OpenAI: его понимает llama-server. Размышления модели включаем только для задач —
    // они идут в отдельное поле reasoning_content и не озвучиваются; на всё остальное ответ приходит сразу.
    enum class Mode { Quick, Think, Hurry };

    std::string BuildChatBody(const std::string& userName, Mode mode) {
        const bool think = mode == Mode::Think;
        std::string system = SystemPrompt(userName);
        if (mode == Mode::Hurry) // задача — решаем «вслух»: коротко по шагам и с итогом
            system += " Сейчас отвечай сразу, без долгих раздумий: коротко покажи решение по шагам и обязательно назови итоговый ответ.";
        std::string body = std::string("{\"stream\":true,") + Sampling(think ? Kind::Think : Kind::Chat) +
                           "\"chat_template_kwargs\":{\"enable_thinking\":" + (think ? "true" : "false") + "},\"messages\":[";
        body += "{\"role\":\"system\",\"content\":\"" + Json::Escape(system) + "\"}";
        for (const auto& m : Conversation::Recent())
            body += ",{\"role\":\"" + m.role + "\",\"content\":\"" + Json::Escape(m.content) + "\"}";
        return body + "]}";
    }

    // Игра или видео на весь экран: окно закрывает монитор целиком
    bool FullscreenAppInFront() {
        const HWND window = GetForegroundWindow();
        if (!window || window == GetShellWindow()) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId()) return false;
        wchar_t name[64] = {};
        GetClassNameW(window, name, 64);
        if (wcscmp(name, L"WorkerW") == 0 || wcscmp(name, L"Progman") == 0) return false; // рабочий стол
        MONITORINFO info{ sizeof(info) };
        RECT rect{};
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info) || !GetWindowRect(window, &rect)) return false;
        return rect.left <= info.rcMonitor.left && rect.top <= info.rcMonitor.top && rect.right >= info.rcMonitor.right && rect.bottom >= info.rcMonitor.bottom;
    }

    // Выгрузить из видеокарты, если долго не спрашивали ([ai] idle_minutes, 0 — никогда): видеокарта свободна для игр
    void IdleWatcher() {
        for (;;) {
            Sleep(15000);
            if (g_unloaded.load() || !g_app.aiReady || g_app.thinking || g_busy.load() > 0) continue;
            const int minutes = std::atoi(Config::Value("ai", "idle_minutes", "10").c_str());
            const Clock idle = NowMs() - g_lastUse.load();
            const bool idleLong = minutes > 0 && idle >= static_cast<Clock>(minutes) * 60000;
            const bool game = idle >= 60000 && Config::Value("ai", "game_unload", "1") == "1" && FullscreenAppInFront();
            if (!idleLong && !game) continue;
            std::lock_guard startLock(g_startMutex);
            Sidecar::Stop(g_process); // g_app.aiReady оставляем: нейросеть доступна, просто спит
            g_unloaded = true;
            Log::Write(game ? "нейросеть: выгружена — открыта полноэкранная программа" : "нейросеть: выгружена из видеокарты после " + std::to_string(minutes) + " мин простоя");
        }
    }

    // Перед вопросом: выгружена — загрузить снова (5–15 с)
    void EnsureAwake(bool eyes = false) {
        static std::mutex s_wakeMutex;
        std::lock_guard wake(s_wakeMutex); // загрузка уже идёт (начата заранее) — дождаться её
        g_lastUse = NowMs();
        const bool asleep = g_unloaded.exchange(false);
        if (!asleep && !(eyes && g_vision.load() && !g_engineEyes.load())) {
            std::lock_guard wait(g_startMutex); // модель как раз загружается — дождаться
            return;
        }
        g_wantEyes = eyes;
        Caption::Set("просыпаюсь…", true);
        Log::Write("нейросеть: загружаю обратно");
        StartBlocking(true);
        g_lastUse = NowMs();
    }
}

// ---------------------------------------------------------------------
void Ai::Start() {
    g_lastUse = NowMs();
    if (Config::Value("ai", "preload", "0") == "1") std::thread([] { StartBlocking(); }).detach();
    else std::thread(PrepareLazy).detach();
    static bool s_watching = false;
    if (!s_watching) { s_watching = true; std::thread(IdleWatcher).detach(); }
}

void Ai::Prewarm() {
    if (g_unloaded.load()) std::thread([] { EnsureAwake(); }).detach();
}

void Ai::Restart() {
    g_lastUse = NowMs();
    g_unloaded = false; // запуск сам загрузит модель
    g_wantEyes = false;
    std::thread([] { StartBlocking(); }).detach();
}

Ai::Answer Ai::Ask(const std::string& prompt) {
    BusyScope busy;
    EnsureAwake();
    const int generation = g_app.cancelGeneration.load();
    Conversation::Push("user", prompt);

    const size_t message = Chat::Add(Role::Anya, "…");
    std::string full, pending, error;
    size_t spoken = 0;
    Reply kind = Reply::Undecided; // команда или обычный ответ — решаем по первым словам
    Clock thinkDeadline = 0;       // до какого момента модели можно размышлять (0 — без размышлений)
    bool thinkingTooLong = false;

    // Ответ приходит потоком «data: {...}» — по кусочку текста в каждой строке
    auto onData = [&](const std::string& chunk) {
        if (g_app.cancelGeneration.load() != generation) return false;
        if (thinkDeadline && full.empty() && NowMs() > thinkDeadline) { // всё ещё думает, а время вышло
            thinkingTooLong = true;
            return false;
        }
        pending += chunk;
        for (size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (line.rfind("data:", 0) != 0) continue;
            line.erase(0, 5);
            if (line.find("[DONE]") != std::string::npos) continue;
            if (line.find("\"error\"") != std::string::npos) {
                const auto m = Json::FindStrings(line, "message", 1);
                error = m.empty() ? "ошибка движка" : m[0];
                return false;
            }
            const auto content = Json::FindStrings(line, "content", 1); // reasoning_content сюда не попадает
            if (content.empty()) continue;
            full += content[0];

            const std::string visible = StripThinking(full);
            if (kind == Reply::Undecided) kind = Classify(visible);
            if (kind != Reply::Speech) continue; // команду не озвучиваем и не показываем — её выполнят навыки
            for (size_t cut; (cut = FindSpeakableCut(visible, spoken, spoken == 0)) != std::string::npos && cut > spoken;) {
                Voice::Speak(visible.substr(spoken, cut - spoken));
                spoken = cut;
            }
            const std::string shown = Text::Trim(visible).empty() ? "…" : visible;
            Chat::Update(message, shown);
            Caption::Set(shown, true);
        }
        return true;
    };

    const int thinkMode = ThinkMode();
    const bool think = thinkMode != 2 && (WantsDeepThinking(prompt) || (thinkMode == 1 && IsTask(prompt)));
    const Mode mode = think ? Mode::Think : IsTask(prompt) ? Mode::Hurry : Mode::Quick;
    if (think) { // размышления ограничены по времени, сразу даём понять, что Аня не зависла
        Caption::Set("думаю…", true);
        Voice::Speak("Хорошо, подумаю");
    }
    // Ник «zephiro4ka» модель коверкает («зепhiro4ka»), поэтому называем пользователя так, как имя звучит вслух
    const Settings cfg = Config::Get();
    const std::string userName = cfg.spokenName.empty() ? cfg.name : cfg.spokenName;
    if (think) thinkDeadline = NowMs() + static_cast<Clock>(std::clamp(AiInt("think_seconds", "10"), 3, 300)) * 1000;
    int code = Request(L"POST", L"/v1/chat/completions", BuildChatBody(userName, mode), 180000, onData);

    // Размышления не уложились в бюджет (или съели весь ответ) — просим ответить сразу, коротко и по шагам
    if (think && g_app.cancelGeneration.load() == generation && (thinkingTooLong || Text::Trim(StripThinking(full)).empty())) {
        Log::Write("нейросеть думала слишком долго — прошу ответить сразу");
        Caption::Set("ещё секундочку…", true);
        thinkDeadline = 0;
        full.clear();
        pending.clear();
        kind = Reply::Undecided;
        code = Request(L"POST", L"/v1/chat/completions", BuildChatBody(userName, Mode::Hurry), 120000, onData);
    }
    const std::string visible = StripThinking(full);
    const std::string answer = Text::Trim(visible);

    if (g_app.cancelGeneration.load() != generation) {
        Chat::Update(message, answer.empty() ? "(перебили)" : answer);
        return { true, {} };
    }
    if (code != 200 || answer.empty()) {
        if (code == 0) g_app.aiReady = false;
        Chat::Update(message, Role::System, "Нейросеть не ответила" + (error.empty() ? std::string() : ": " + error));
        return {};
    }

    if (Classify(visible) == Reply::Command) {
        Answer result{ true, ExtractCommands(visible) };
        std::string shown;
        for (const auto& c : result.commands) shown += (shown.empty() ? "⚙ " : " · ") + c;
        Chat::Update(message, Role::System, shown.empty() ? "⚙ (пустая команда)" : shown);
        return result;
    }

    if (spoken < visible.size()) Voice::Speak(visible.substr(spoken));
    Chat::Update(message, answer);
    Caption::Set(answer);
    Conversation::Push("assistant", answer);
    return { true, {} };
}

bool Ai::LooksLikeTask(const std::string& prompt) { return IsTask(prompt); }

std::string Ai::Quiet(const std::string& prompt, const std::string& imageBase64, bool deepRequested) {
    const bool deep = deepRequested && ThinkMode() != 2;
    BusyScope busy;
    EnsureAwake(!imageBase64.empty());
    if (!g_app.aiReady) return {};
    const Settings cfg = Config::Get();
    const std::string userName = cfg.spokenName.empty() ? cfg.name : cfg.spokenName;
    std::string system = SystemPrompt(userName) +
        " Сейчас ты отвечаешь в телеграме: пиши обычным текстом, без команд. Не используй LaTeX, знаки $ и Markdown — "
        "формулы пиши как в тетради: x², √(x+1), a·b, (a+1)/(b-2), D = b² − 4ac.";
    if (!imageBase64.empty())
        system += " Тебе прислали снимок экрана. Если на нём задача — реши её по шагам и в конце напиши строку «Ответ: …».";
    if (deep) // как настоящий решатель: сначала подумать, потом коротко и чисто записать
        system += " Это задача: хорошо подумай, выбери метод (для задач с параметром — разложение на множители, замена или графический анализ), "
                  "проверь ответ подстановкой. В ответе — только чистое решение по шагам без перебора неудачных попыток, и в конце «Ответ: …».";
    std::string user = "\"" + Json::Escape(prompt) + "\"";
    if (!imageBase64.empty() && g_vision)
        user = "[{\"type\":\"text\",\"text\":\"" + Json::Escape(prompt) + "\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64," +
               imageBase64 + "\"}}]";
    // режим размышлений — с настройками, которые рекомендуют для Qwen: температура 0.6, top_p 0.95, top_k 20
    const std::string sampling = Sampling(deep ? Kind::TelegramDeep : Kind::TelegramFast);
    const std::string body = "{\"stream\":false," + sampling +
                             "\"chat_template_kwargs\":{\"enable_thinking\":" + (deep ? "true" : "false") + "},\"messages\":["
                             "{\"role\":\"system\",\"content\":\"" + Json::Escape(system) + "\"},{\"role\":\"user\",\"content\":" + user + "}]}";
    std::string response;
    const int code = Request(L"POST", L"/v1/chat/completions", body, deep ? 420000 : 180000, [&](const std::string& chunk) { response += chunk; return true; });
    if (code != 200) {
        Log::Write("тихий ответ: код " + std::to_string(code) + " " + response.substr(0, 200));
        return {};
    }
    const auto content = Json::FindStrings(response, "content", 1);
    const std::string answer = content.empty() ? std::string() : Text::PrettyMath(Text::Trim(StripThinking(content[0])));
    if (deep && answer.empty()) { // размышления не уложились в предел — отвечаем сразу, по шагам, но отвечаем
        Log::Write("тихий ответ: размышления не уложились — решаю сразу");
        return Quiet(prompt, imageBase64, false);
    }
    return answer;
}

std::string Ai::AskJson(const std::string& instruction, const std::string& request) {
    BusyScope busy;
    EnsureAwake();
    if (!g_app.aiReady) return {};
    const std::string body = "{\"stream\":false,\"temperature\":0.2,\"max_tokens\":1200,\"response_format\":{\"type\":\"json_object\"},"
                             "\"chat_template_kwargs\":{\"enable_thinking\":false},\"messages\":["
                             "{\"role\":\"system\",\"content\":\"" + Json::Escape(instruction) + "\"},"
                             "{\"role\":\"user\",\"content\":\"" + Json::Escape(request) + "\"}]}";
    std::string response;
    const int code = Request(L"POST", L"/v1/chat/completions", body, 120000, [&](const std::string& chunk) { response += chunk; return true; });
    if (code != 200) {
        Log::Write("ответ в JSON: код " + std::to_string(code) + " " + response.substr(0, 200));
        return {};
    }
    const auto content = Json::FindStrings(response, "content", 1);
    return content.empty() ? std::string() : Text::Trim(StripThinking(content[0]));
}

const char* Ai::ProfileName(int index) {
    if (index >= 0 && index < kProfiles - 1) return kPresets[index].name;
    return "Свой";
}

int Ai::Profile() { return std::clamp(AiInt("profile", "0"), 0, kProfiles - 1); }

void Ai::SetProfile(int index) {
    index = std::clamp(index, 0, kProfiles - 1);
    Config::SetValue("ai", "profile", std::to_string(index));
    if (index < kProfiles - 1)
        for (int i = 0; i < 6; ++i) {
            Config::SetValue("ai", Key("chat", kFields[i]).c_str(), std::to_string(kPresets[index].chat[i]));
            Config::SetValue("ai", Key("task", kFields[i]).c_str(), std::to_string(kPresets[index].task[i]));
        }
    ++g_tuningVersion;
}

int Ai::TuningVersion() { return g_tuningVersion.load(); }

void Ai::ResetTuning() {
    SetProfile(0);
    const std::pair<const char*, const char*> kDefaults[] = { { "seed", "-1" }, { "think_mode", "0" }, { "think_seconds", "10" }, { "vision", "1" },
                                      { "threads", "0" }, { "context", kContextSize }, { "parallel", "2" }, { "gpu_layers", "99" }, { "engine_args", "" }, { "extra_prompt", "" } };
    for (const auto& [key, value] : kDefaults)
        Config::SetValue("ai", key, value);
    ++g_tuningVersion;
}

bool Ai::CanSee() { return g_vision && (g_app.aiReady || g_unloaded.load()); }

std::vector<std::string> Ai::Models() {
    std::lock_guard lock(g_mutex);
    return g_models;
}

std::string Ai::ActiveModel() {
    std::lock_guard lock(g_mutex);
    return FileName(g_activeModel);
}
