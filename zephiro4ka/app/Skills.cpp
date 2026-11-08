#include "Skills.h"

#include "Browser.h"
#include "Input.h"
#include "FileSearch.h"
#include "Launcher.h"
#include "Messenger.h"
#include "Pc.h"
#include "PowerMode.h"
#include "Scenarios.h"
#include "ScenarioMaker.h"
#include "SystemMonitor.h"
#include "Telegram.h"
#include "ScreenLinks.h"
#include "Weather.h"
#include "Settings.h"
#include "State.h"
#include "TextUtils.h"
#include "Voice.h"

#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <functional>
#include <random>
#include <thread>
#include <vector>

namespace {
    std::atomic<bool> g_dictating{ false };
    std::mt19937 g_random{ std::random_device{}() };

    // Обычный вектор, а не initializer_list: список фраз живёт столько же, сколько команда
    using Phrases = std::vector<const char*>;

    // ------------------------------------------------------------ разбор фраз
    // Нижний регистр, без знаков препинания и лишних пробелов: «Поставь, пожалуйста, паузу!» → «поставь пожалуйста паузу»
    std::string Normalize(const std::string& text) {
        std::wstring out;
        bool space = true;
        for (wchar_t ch : Text::LowerRu(Text::ToWide(text))) {
            if (ch == L'×') ch = L'*'; // «375×124» — знаки умножения и деления, как их пишет whisper
            if (ch == L'÷') ch = L'/';
            const bool keep = IsCharAlphaNumericW(ch) || ch == L'+' || ch == L'-' || ch == L'*' || ch == L'/' || ch == L'.' || ch == L',';
            if (keep && !(ch == L'.' || ch == L',')) {
                out.push_back(ch);
                space = false;
            } else if ((ch == L'.' || ch == L',') && !out.empty() && iswdigit(out.back())) {
                out.push_back(L'.'); // десятичная дробь 2,5
                space = false;
            } else if (!space) {
                out.push_back(L' ');
                space = true;
            }
        }
        while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) out.pop_back();
        return Text::ToUtf8(out);
    }

    bool Has(const std::string& norm, const char* phrase) { return norm.find(phrase) != std::string::npos; }

    bool HasAny(const std::string& norm, const Phrases& phrases) {
        for (const char* p : phrases)
            if (Has(norm, p)) return true;
        return false;
    }

    // Текст после глагола: «открой-ка мне стим» → «мне стим». Пусто, если глагола нет.
    std::string AfterVerb(const std::string& original, const Phrases& verbs) {
        const std::wstring wide = Text::ToWide(original);
        const std::wstring lower = Text::LowerRu(wide);
        for (const char* verb : verbs) {
            const std::wstring v = Text::ToWide(verb);
            size_t pos = lower.find(v);
            if (pos == std::wstring::npos || (pos > 0 && IsCharAlphaW(lower[pos - 1]))) continue;
            pos += v.size();
            while (pos < lower.size() && (IsCharAlphaW(lower[pos]) || lower[pos] == L'-')) ++pos; // «открой-ка»
            const std::string rest = Text::Trim(Text::ToUtf8(wide.substr(pos)));
            if (!rest.empty()) return rest;
        }
        return {};
    }

    // Числа словами и цифрами: «двадцать пять» → 25, «2.5» → 2.5
    std::optional<double> WordNumber(const std::string& w) {
        struct Entry { const char* word; int value; };
        static const Entry kNumbers[] = {
            { "ноль", 0 }, { "один", 1 }, { "одна", 1 }, { "одну", 1 }, { "два", 2 }, { "две", 2 }, { "три", 3 }, { "четыре", 4 },
            { "пять", 5 }, { "шесть", 6 }, { "семь", 7 }, { "восемь", 8 }, { "девять", 9 }, { "десять", 10 }, { "одиннадцать", 11 },
            { "двенадцать", 12 }, { "тринадцать", 13 }, { "четырнадцать", 14 }, { "пятнадцать", 15 }, { "шестнадцать", 16 },
            { "семнадцать", 17 }, { "восемнадцать", 18 }, { "девятнадцать", 19 }, { "двадцать", 20 }, { "тридцать", 30 },
            { "сорок", 40 }, { "пятьдесят", 50 }, { "шестьдесят", 60 }, { "семьдесят", 70 }, { "восемьдесят", 80 },
            { "девяносто", 90 }, { "сто", 100 }, { "двести", 200 }, { "триста", 300 }, { "тысяча", 1000 }, { "тысячу", 1000 },
        };
        if (!w.empty() && (std::isdigit(static_cast<unsigned char>(w[0])))) {
            char* end = nullptr;
            const double v = std::strtod(w.c_str(), &end);
            if (end && *end == '\0') return v;
        }
        for (const auto& n : kNumbers)
            if (w == n.word) return n.value;
        return std::nullopt;
    }

    std::vector<std::string> Words(const std::string& norm) {
        std::vector<std::string> words;
        std::string word;
        for (char c : norm + " ") {
            if (c == ' ') {
                if (!word.empty()) words.push_back(word);
                word.clear();
            } else {
                word += c;
            }
        }
        return words;
    }

    // Читает число начиная со слова i («двадцать пять» — два слова). Сдвигает i.
    std::optional<double> ReadNumber(const std::vector<std::string>& words, size_t& i) {
        std::optional<double> total;
        while (i < words.size()) {
            const auto v = WordNumber(words[i]);
            if (!v) break;
            total = total.value_or(0) + *v;
            ++i;
        }
        return total;
    }

    std::string FormatNumber(double v) {
        char buf[64];
        if (std::fabs(v - std::round(v)) < 1e-9) sprintf_s(buf, "%.0f", v);
        else sprintf_s(buf, "%.4g", v);
        std::string s = buf;
        for (char& c : s) if (c == '.') c = ',';
        return s;
    }

    bool LooksLikeQuestion(const std::string& norm) {
        const std::string padded = " " + norm + " ";
        for (const char* marker : { " как ", " почему ", " зачем ", " что такое ", " объясни", " расскажи", " научи", " помоги",
                                    " можно ли ", " где ", " когда ", " какой ", " какая ", " что ", " ли " })
            if (padded.find(marker) != std::string::npos) return true;
        return false;
    }

    // ------------------------------------------------------------ окна и клавиши
    bool ForegroundIsMine() {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        return pid == GetCurrentProcessId();
    }

    struct KeyCommand {
        Phrases phrases;
        std::function<void()> action;
        const char* reply;
    };

    // Порядок важен: более конкретные фразы — выше
    const std::vector<KeyCommand>& KeyCommands() {
        static const std::vector<KeyCommand> kCommands = {
            // окна
            { { "сверни все", "покажи рабочий стол", "убери все окна" }, [] { Input::Chord({ VK_LWIN, 'D' }); }, "Сворачиваю всё" },
            { { "сверни окно", "сверни это" }, [] { Input::Chord({ VK_LWIN, VK_DOWN }); }, "" },
            { { "разверни окно", "на весь экран", "разверни это" }, [] { Input::Chord({ VK_LWIN, VK_UP }); }, "" },
            { { "переключи окно", "следующее окно", "другое окно" }, [] { Input::Chord({ VK_MENU, VK_ESCAPE }); }, "" },
            { { "сделай скриншот", "скриншот", "снимок экрана" }, [] { Input::Chord({ VK_LWIN, VK_SNAPSHOT }); }, "Готово, скриншот в папке «Изображения — Снимки экрана»" },
            { { "заблокируй компьютер", "заблокируй пк", "заблокируй экран" }, [] { LockWorkStation(); }, "Блокирую. Возвращайся скорее!" },
            // медиа
            { { "поставь на паузу", "поставь паузу", "на паузу", "пауза", "останови видео", "останови музыку", "стоп видео" },
              [] { Input::Tap(VK_MEDIA_PLAY_PAUSE); }, "" },
            { { "сними с паузы", "продолжи видео", "продолжи музыку", "продолжай", "воспроизведи", "плей", "играй дальше" },
              [] { Input::Tap(VK_MEDIA_PLAY_PAUSE); }, "" },
            { { "следующий трек", "следующая песня", "следующее видео", "переключи трек", "следующую", "скипни", "пропусти" },
              [] { Input::Tap(VK_MEDIA_NEXT_TRACK); }, "" },
            { { "предыдущий трек", "предыдущая песня", "предыдущее видео", "прошлый трек", "верни трек" },
              [] { Input::Tap(VK_MEDIA_PREV_TRACK); }, "" },
            { { "выключи звук", "отключи звук", "без звука", "включи звук", "верни звук", "замьють" }, [] { Input::Tap(VK_VOLUME_MUTE); }, "" },
            { { "сделай громче", "погромче", "громче", "прибавь звук", "увеличь громкость", "прибавь громкость" }, [] { Input::Tap(VK_VOLUME_UP, 4); }, "" },
            { { "сделай тише", "потише", "тише", "убавь звук", "уменьши громкость", "убавь громкость" }, [] { Input::Tap(VK_VOLUME_DOWN, 4); }, "" },
            // правка текста
            { { "выдели все", "выделить все" }, [] { Input::Chord({ VK_CONTROL, 'A' }); }, "" },
            { { "скопируй", "копируй" }, [] { Input::Chord({ VK_CONTROL, 'C' }); }, "Скопировала" },
            { { "вставь", "вставить" }, [] { Input::Chord({ VK_CONTROL, 'V' }); }, "" },
            { { "вырежи", "вырезать" }, [] { Input::Chord({ VK_CONTROL, 'X' }); }, "" },
            { { "отмени действие", "отмени", "верни как было" }, [] { Input::Chord({ VK_CONTROL, 'Z' }); }, "" },
            { { "сохрани" }, [] { Input::Chord({ VK_CONTROL, 'S' }); }, "Сохранила" },
            { { "нажми энтер", "нажми enter", "нажми ввод", "отправь" }, [] { Input::Tap(VK_RETURN); }, "" },
            { { "нажми пробел" }, [] { Input::Tap(VK_SPACE); }, "" },
            { { "нажми эскейп", "нажми escape", "нажми esc" }, [] { Input::Tap(VK_ESCAPE); }, "" },
            { { "новая строка", "с новой строки" }, [] { Input::Tap(VK_RETURN); }, "" },
        };
        return kCommands;
    }

    // ------------------------------------------------------------ отдельные навыки

    std::string ShortTitle(const std::string& name); // ниже, рядом с видео

    // «первую», «второй», «3» → номер; 0 — номера нет
    int Ordinal(const std::string& norm) {
        static const std::pair<const char*, int> kStems[] = { { "перв", 1 }, { "втор", 2 }, { "трет", 3 }, { "четверт", 4 }, { "пят", 5 },
                                                              { "шест", 6 }, { "седьм", 7 }, { "восьм", 8 }, { "девят", 9 }, { "десят", 10 } };
        for (const auto& word : Words(norm)) {
            if (!word.empty() && std::isdigit(static_cast<unsigned char>(word[0]))) return std::atoi(word.c_str());
            for (const auto& [stem, n] : kStems)
                if (word.rfind(stem, 0) == 0) return n;
        }
        return 0;
    }

    // «Перейди по первой ссылке», «открой второе видео», «нажми на третий результат»
    std::optional<std::string> OpenNthLink(const std::string& norm) {
        if (!HasAny(norm, { "ссылк", "результат", "видео", "ролик", "сайт" }) ||
            !HasAny(norm, { "перейди", "открой", "нажми", "зайди", "кликни", "включи", "давай" }))
            return std::nullopt;
        const int n = Ordinal(norm);
        if (n <= 0) return std::nullopt;
        if (const auto opened = ScreenLinks::OpenNth(n); opened.found) return "Открываю «" + ShortTitle(opened.title) + "»";
        return "Не вижу на странице столько ссылок";
    }

    // «Напиши в поиске котики» — в поле поиска на странице (или в адресную строку) и сразу искать
    std::optional<std::string> TypeInSearch(const std::string& command) {
        const std::string query = AfterVerb(command, { "напиши в поиске", "напиши в поиск", "введи в поиске", "введи в поиск", "напечатай в поиске",
                                                        "вбей в поиск", "вбей в поиске", "найди в поиске", "поищи в поиске", "набери в поиске" });
        if (query.empty()) return std::nullopt;
        if (Browser::Focus()) {
            if (!ScreenLinks::FocusSearchField()) Browser::Keys({ VK_CONTROL, 'L' }); // на странице нет поиска — адресная строка
            Sleep(120);
            Input::Chord({ VK_CONTROL, 'A' }); // старый запрос заменяем
            Input::TypeText(query);
            Input::Tap(VK_RETURN);
            return "Ищу: " + query;
        }
        Input::Chord({ VK_LWIN, 'S' }); // браузера нет — поиск Windows
        Sleep(400);
        Input::TypeText(query);
        return "Ищу на компьютере: " + query;
    }

    // Громкость: «прибавь громкость» — вся система, «прибавь громкость у яндекс музыки» — только это приложение
    std::optional<std::string> ChangeVolume(const std::string& norm) {
        const bool up = HasAny(norm, { "прибав", "увелич", "громче", "повысь", "подними", "добавь" });
        const bool down = HasAny(norm, { "убав", "уменьш", "тише", "понизь", "опусти", "снизь", "приглуши" });
        const bool muteOff = HasAny(norm, { "выключи звук", "отключи звук", "без звука", "замьют", "заглуши" });
        const bool muteOn = HasAny(norm, { "включи звук", "верни звук" });
        const bool volumeWord = HasAny(norm, { "громкост", "звук" }) || HasAny(norm, { "громче", "тише" });
        if (!volumeWord || !(up || down || muteOff || muteOn)) return std::nullopt;

        // что осталось после служебных слов — название приложения («яндекс музыки», «дискорде»)
        static const char* kService[] = { "прибавь", "прибавить", "увеличь", "увеличить", "сделай", "погромче", "громче", "повысь", "подними",
            "добавь", "убавь", "убавить", "уменьши", "уменьшить", "потише", "тише", "понизь", "опусти", "снизь", "приглуши", "выключи",
            "отключи", "включи", "верни", "замьють", "замьюти", "заглуши", "громкость", "громкости", "звук", "звука", "у", "в", "во", "для",
            "на", "немного", "чуть", "чуть-чуть", "пожалуйста", "процентов", "процента", "процент", "без", "еще", "ещё", "можешь", "мне" };
        static const char* kWholeSystem[] = { "компьютер", "компе", "пк", "систем", "устройств", "везде", "общ", "колонк", "наушник" };
        std::string app;
        int number = -1;
        const auto words = Words(norm);
        for (size_t i = 0; i < words.size();) {
            if (auto n = ReadNumber(words, i)) { number = static_cast<int>(*n); continue; }
            const std::string& w = words[i++];
            if (std::find_if(std::begin(kService), std::end(kService), [&](const char* s) { return w == s; }) != std::end(kService)) continue;
            app += (app.empty() ? "" : " ") + w;
        }
        const bool system = app.empty() || std::any_of(std::begin(kWholeSystem), std::end(kWholeSystem), [&](const char* s) { return app.find(s) != std::string::npos; });
        const int step = number > 0 ? number : 8;

        if (system) {
            if (muteOff || muteOn) { Input::Tap(VK_VOLUME_MUTE); return ""; }
            if (number < 0) { Input::Tap(up ? VK_VOLUME_UP : VK_VOLUME_DOWN, 4); return ""; } // шаг 8% с окошком громкости Windows
            const int now = Pc::Volume().value_or(50), target = std::clamp(now + (up ? step : -step), 0, 100);
            return Pc::SetVolume(target) ? "Громкость " + std::to_string(target) + "%" : std::string("Не получилось поменять громкость");
        }

        const int delta = muteOff || muteOn ? 0 : (up ? step : -step), mute = muteOff ? 1 : muteOn ? 0 : -1;
        auto r = Pc::ChangeAppVolume(app, delta, -1, mute);
        if (!r.found && HasAny(app, { "браузер", "ютуб", "youtube", "вкладк" })) // в микшере браузер называется по имени: «Microsoft Edge»
            for (const char* browser : { "Microsoft Edge", "Google Chrome", "Firefox", "Brave", "Opera", "Yandex" })
                if ((r = Pc::ChangeAppVolume(browser, delta, -1, mute)).found) break;
        if (!r.found) { // «сделай музыку погромче», а такого приложения в микшере нет — меняем общую громкость
            if (muteOff || muteOn) Input::Tap(VK_VOLUME_MUTE);
            else Input::Tap(up ? VK_VOLUME_UP : VK_VOLUME_DOWN, step / 2);
            return "Не нашла «" + app + "» в микшере — поменяла общую громкость";
        }
        if (muteOff) return "Выключила звук у " + r.name;
        if (muteOn) return "Включила звук у " + r.name;
        return r.name + ": громкость " + std::to_string(r.percent) + "%";
    }

    // Телефон, питание, железо: «реши задачу на экране», «выключи компьютер» (а бот остаётся), «какая температура процессора»
    std::optional<std::string> PhoneAndPower(const std::string& norm) {
        if (HasAny(norm, { "температур", "нагрев", "как там комп", "состояние компьютер", "загрузка компьютер" }) &&
            HasAny(norm, { "процессор", "видеокарт", "компьютер", "комп", "железо", "железа", "пк" }))
            return SystemMonitor::Report();

        const bool phone = HasAny(norm, { "телеграм", "телефон" });
        if (HasAny(norm, { "реши" }) && (HasAny(norm, { "экран", "задачу на" }) || phone)) {
            if (!Telegram::Ready()) return std::string("Телеграм-бот не на связи — проверь его в настройках, раздел «Телефон и Discord»");
            Telegram::SolveScreenAsync();
            return std::string("Смотрю на экран — решение пришлю в телеграм");
        }
        if (phone && HasAny(norm, { "скинь", "отправь", "пришли" }) && HasAny(norm, { "экран", "скрин", "снимок" })) {
            if (!Telegram::Ready()) return std::string("Телеграм-бот не на связи");
            Telegram::SendScreenAsync();
            return std::string("Отправила снимок экрана в телеграм");
        }

        if (HasAny(norm, { "отмени выключение", "не выключай" })) { PowerMode::CancelShutdown(); return std::string("Отменила выключение"); }
        if (HasAny(norm, { "перезагрузи компьютер", "перезагрузи пк", "перезагрузка компьютера" })) {
            PowerMode::Restart(15);
            return std::string("Перезагружаю через 15 секунд. Передумаешь — скажи «отмени выключение»");
        }
        if (HasAny(norm, { "выключи компьютер", "выключи пк", "завершение работы", "заверши работу", "выключи комп" })) {
            if (HasAny(norm, { "совсем", "полностью", "по-настоящему", "по настоящему" }) || !Telegram::Ready()) {
                PowerMode::Shutdown(15);
                return std::string("Выключаю компьютер через 15 секунд. Передумаешь — скажи «отмени выключение»");
            }
            PowerMode::Standby();
            return std::string("Выключаю экран и звук, а сама остаюсь на связи в телеграме");
        }
        if (PowerMode::IsStandby() && HasAny(norm, { "включи экран", "проснись", "включи компьютер" })) { PowerMode::Wake(); return std::string("Я тут!"); }
        return std::nullopt;
    }

    // «Удали слово», «сотри два слова», «удали букву», «удали строку», «удали всё»
    bool DeleteText(const std::string& norm) {
        if (!HasAny(norm, { "удали", "сотри", "стери", "очисти", "убери" })) return false;
        const bool letters = HasAny(norm, { "букв", "символ" }), words = HasAny(norm, { "слов" }), line = HasAny(norm, { "строк", "строчк" });
        const bool all = !letters && !words && !line && HasAny(norm, { " все", " всё", "текст", "поле", "написанное" });
        if (!letters && !words && !line && !all) return false;

        int count = 1;
        const auto list = Words(norm);
        for (size_t i = 0; i < list.size();)
            if (auto n = ReadNumber(list, i)) { count = std::clamp(static_cast<int>(*n), 1, 50); break; }
            else ++i;
        if (all) {
            Input::Chord({ VK_CONTROL, 'A' });
            Input::Tap(VK_DELETE);
        } else if (line) {
            Input::Chord({ VK_SHIFT, VK_HOME });
            Input::Tap(VK_BACK);
        } else if (words) {
            for (int i = 0; i < count; ++i) Input::Chord({ VK_CONTROL, VK_BACK });
        } else {
            Input::Tap(VK_BACK, count);
        }
        return true;
    }
    std::string TimeNow() {
        SYSTEMTIME t{};
        GetLocalTime(&t);
        char buf[32];
        sprintf_s(buf, "Сейчас %02u:%02u", t.wHour, t.wMinute);
        return buf;
    }

    std::string DateToday() {
        static const char* kDays[] = { "воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота" };
        static const char* kMonths[] = { "января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа", "сентября", "октября", "ноября", "декабря" };
        SYSTEMTIME t{};
        GetLocalTime(&t);
        return std::string("Сегодня ") + kDays[t.wDayOfWeek] + ", " + std::to_string(t.wDay) + " " + kMonths[t.wMonth - 1];
    }

    bool OpenTarget(const wchar_t* target) {
        return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", target, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    }

    // Сайты и системные вещи, которых нет в списке программ
    std::optional<std::string> OpenBuiltIn(const std::string& what) {
        struct Target { const char* word; const wchar_t* path; const char* reply; };
        static const Target kTargets[] = {
            { "ютуб", L"https://www.youtube.com", "Открываю ютуб" },
            { "youtube", L"https://www.youtube.com", "Открываю ютуб" },
            { "вк", L"https://vk.com", "Открываю ВКонтакте" },
            { "вконтакте", L"https://vk.com", "Открываю ВКонтакте" },
            { "гугл", L"https://www.google.com", "Открываю гугл" },
            { "параметры", L"ms-settings:", "Открываю параметры" },
            { "блокнот", L"notepad.exe", "Открываю блокнот" },
            { "калькулятор", L"calc.exe", "Открываю калькулятор" },
            { "проводник", L"explorer.exe", "Открываю проводник" },
            { "загрузки", L"shell:Downloads", "Открываю загрузки" },
            { "диспетчер задач", L"taskmgr.exe", "Открываю диспетчер задач" },
        };
        const std::string norm = Normalize(what);
        for (const auto& t : kTargets)
            if (Has(norm, t.word)) return OpenTarget(t.path) ? t.reply : "Ой, не получилось открыть";
        return std::nullopt;
    }

    // «поставь таймер на 5 минут», «напомни через полчаса выключить чайник»
    std::optional<std::string> Timer(const std::string& original, const std::string& norm) {
        if (!HasAny(norm, { "таймер", "напомни через", "засеки", "разбуди через" })) return std::nullopt;

        const auto words = Words(norm);
        double seconds = 0;
        size_t reminderStart = 0;
        for (size_t i = 0; i < words.size(); ++i) {
            if (words[i] == "полчаса") { seconds += 1800; reminderStart = i + 1; continue; }
            if (words[i] == "полминуты") { seconds += 30; reminderStart = i + 1; continue; }
            size_t j = i;
            std::optional<double> n = ReadNumber(words, j);
            const std::string& unit = j < words.size() ? words[j] : words[i];
            if (!n && (unit.rfind("минут", 0) == 0 || unit.rfind("час", 0) == 0 || unit.rfind("секунд", 0) == 0)) n = 1; // «через минуту»
            if (!n || j >= words.size()) continue;
            if (unit.rfind("сек", 0) == 0) seconds += *n;
            else if (unit.rfind("мин", 0) == 0) seconds += *n * 60;
            else if (unit.rfind("час", 0) == 0) seconds += *n * 3600;
            else continue;
            reminderStart = j + 1;
            i = j;
        }
        if (seconds <= 0) return "На сколько поставить таймер? Скажи, например, «таймер на пять минут»";

        std::string about;
        for (size_t i = reminderStart; i < words.size(); ++i) about += (about.empty() ? "" : " ") + words[i];
        if (Has(norm, "таймер") || Has(norm, "засеки")) about.clear();

        const long long total = static_cast<long long>(seconds);
        std::thread([total, about] {
            const Clock end = NowMs() + static_cast<Clock>(total) * 1000;
            while (g_app.running && NowMs() < end) Sleep(200);
            if (!g_app.running) return;
            const std::string text = about.empty() ? "Динь-динь! Время вышло" : "Напоминаю: " + about;
            Chat::Add(Role::Anya, text);
            Caption::Set(text, true);
            Voice::Speak(text);
        }).detach();

        const long long minutes = total / 60, secs = total % 60;
        std::string span = minutes ? FormatNumber(static_cast<double>(minutes)) + " мин" : "";
        if (secs) span += (span.empty() ? "" : " ") + FormatNumber(static_cast<double>(secs)) + " сек";
        return about.empty() ? "Засекла " + span : "Хорошо, напомню через " + span;
    }

    // «сколько будет двадцать пять умножить на четыре», «15 + 7»
    std::optional<std::string> Calculate(const std::string& norm) {
        std::string expr = " " + norm + " ";
        // любая форма «умножить/умноженное/умножимая на» и «делить/разделённое на» → знак
        for (auto [stem, sign] : { std::pair{ "умнож", " * " }, { "дел", " / " } }) {
            for (size_t pos = 0; (pos = expr.find(stem, pos)) != std::string::npos;) {
                const size_t start = expr.rfind(' ', pos) + 1, end = expr.find(' ', pos);
                const std::string word = expr.substr(start, end - start);
                const bool divide = std::string(stem) == "дел";
                if (divide && !(word.rfind("дел", 0) == 0 || word.rfind("разд", 0) == 0 || word.rfind("подел", 0) == 0)) { pos = end; continue; }
                if (expr.compare(end, 4, " на ") != 0) { pos = end; continue; }
                expr.replace(start - 1, end + 4 - (start - 1), sign);
                pos = start;
            }
        }
        for (auto [from, to] : { std::pair{ " умножить на ", " * " }, { " умножь на ", " * " }, { " умноженное на ", " * " }, { " умножено на ", " * " },
                                 { " делить на ", " / " }, { " разделить на ", " / " },
                                 { " поделить на ", " / " }, { " подели на ", " / " }, { " плюс ", " + " }, { " минус ", " - " },
                                 { " x ", " * " }, { " х ", " * " }, { " на ", " * " } }) {
            for (size_t pos; (pos = expr.find(from)) != std::string::npos;) expr.replace(pos, strlen(from), to);
        }
        for (char op : { '+', '-', '*', '/' }) // «2+2» → «2 + 2»
            for (size_t pos = 0; (pos = expr.find(op, pos)) != std::string::npos; pos += 3) expr.replace(pos, 1, std::string(" ") + op + " ");

        const auto words = Words(expr);
        // есть цифры — числа словами не считаем: «в одну строчку 2 + 2 * 2» — это не «1 + …»
        const bool digitsOnly = expr.find_first_of("0123456789") != std::string::npos;
        std::vector<double> numbers;
        std::vector<char> ops;
        for (size_t i = 0; i < words.size();) {
            const bool numeric = !digitsOnly || std::isdigit(static_cast<unsigned char>(words[i][0]));
            if (auto n = numeric ? ReadNumber(words, i) : std::nullopt) {
                if (numbers.size() == ops.size()) numbers.push_back(*n);
                continue;
            }
            const std::string& w = words[i++];
            if (w.size() == 1 && std::string("+-*/").find(w[0]) != std::string::npos && numbers.size() == ops.size() + 1) ops.push_back(w[0]);
        }
        if (numbers.size() < 2 || numbers.size() != ops.size() + 1) return std::nullopt;

        // сначала умножение и деление, потом сложение и вычитание
        for (size_t i = 0; i < ops.size();) {
            if (ops[i] == '*' || ops[i] == '/') {
                if (ops[i] == '/' && numbers[i + 1] == 0) return "На ноль делить нельзя — даже мне";
                numbers[i] = ops[i] == '*' ? numbers[i] * numbers[i + 1] : numbers[i] / numbers[i + 1];
                numbers.erase(numbers.begin() + i + 1);
                ops.erase(ops.begin() + i);
            } else {
                ++i;
            }
        }
        double result = numbers[0];
        for (size_t i = 0; i < ops.size(); ++i) result = ops[i] == '+' ? result + numbers[i + 1] : result - numbers[i + 1];
        return "Получается " + FormatNumber(result);
    }

    std::optional<std::string> Chance(const std::string& norm) {
        if (HasAny(norm, { "монетк", "орел или решка", "орёл или решка" }))
            return std::uniform_int_distribution<int>(0, 1)(g_random) ? "Выпал орёл!" : "Выпала решка!";
        if (HasAny(norm, { "кинь кубик", "брось кубик", "кубик" }))
            return "Выпало " + std::to_string(std::uniform_int_distribution<int>(1, 6)(g_random));
        if (HasAny(norm, { "случайное число", "загадай число" })) {
            const auto words = Words(norm);
            std::vector<double> bounds;
            for (size_t i = 0; i < words.size();) {
                if (auto n = ReadNumber(words, i)) bounds.push_back(*n);
                else ++i;
            }
            const int lo = bounds.size() >= 2 ? static_cast<int>(bounds[0]) : 1;
            const int hi = bounds.size() >= 2 ? static_cast<int>(bounds[1]) : 100;
            return "Пусть будет " + std::to_string(std::uniform_int_distribution<int>(std::min(lo, hi), std::max(lo, hi))(g_random));
        }
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------
static std::optional<std::string> HandleOne(const std::string& command); // одна команда — ниже

namespace {
    // «открой дискорд и зайди в чат с Зефирочником» → две команды по очереди.
    // Делим только там, где после «и» / «потом» начинается новое действие — «чай и кофе» не трогаем.
    std::vector<std::string> SplitCompound(const std::string& command) {
        static const char* kJoins[] = { ", а потом ", " а потом ", ", потом ", " потом ", ", затем ", " затем ", ", и ", " и ", ", " };
        static const char* kVerbs[] = { "открой", "запусти", "зайди", "перейди", "закрой", "включи", "выключи", "поставь",
                                        "напечатай", "найди", "сверни", "разверни", "сделай", "скопируй", "вставь", "сохрани", "поставь" };
        const std::wstring wide = Text::ToWide(command);
        const std::wstring lower = Text::LowerRu(wide);
        for (const char* join : kJoins) {
            const std::wstring j = Text::ToWide(join);
            for (size_t pos = lower.find(j); pos != std::wstring::npos; pos = lower.find(j, pos + 1)) {
                const std::wstring rest = lower.substr(pos + j.size());
                for (const char* verb : kVerbs) {
                    if (rest.rfind(Text::ToWide(verb), 0) != 0) continue;
                    std::vector<std::string> parts = SplitCompound(Text::ToUtf8(wide.substr(0, pos)));
                    for (auto& tail : SplitCompound(Text::ToUtf8(wide.substr(pos + j.size())))) parts.push_back(tail);
                    return parts;
                }
            }
        }
        return { command };
    }
}

namespace {
    // «…чат с X», «…чатик с X», «…личку к X», «…переписочку с X» — с любым глаголом и в любой форме слова.
    // Ищем слово, которое начинается на «чат»/«лс»/«личк»/«перепис»/«диалог», а за ним — «с»/«со»/«к»/«ко».
    // Возвращает X или пустую строку.
    std::string ChatTarget(const std::string& command) {
        static const wchar_t* kChatStems[] = { L"чат", L"лс", L"личк", L"перепис", L"диалог", L"сообщен" };
        static const wchar_t* kLinks[] = { L"с", L"со", L"к", L"ко" };

        const std::wstring wide = Text::ToWide(command);
        const std::wstring lower = Text::LowerRu(wide);
        struct Word { size_t start, end; };
        std::vector<Word> words;
        for (size_t i = 0; i < lower.size();) {
            while (i < lower.size() && !IsCharAlphaW(lower[i])) ++i;
            const size_t start = i;
            while (i < lower.size() && IsCharAlphaW(lower[i])) ++i;
            if (i > start) words.push_back({ start, i });
        }

        for (size_t w = 0; w + 2 < words.size(); ++w) {
            const std::wstring word = lower.substr(words[w].start, words[w].end - words[w].start);
            const std::wstring link = lower.substr(words[w + 1].start, words[w + 1].end - words[w + 1].start);
            const bool chatWord = std::any_of(std::begin(kChatStems), std::end(kChatStems), [&](const wchar_t* s) { return word.rfind(s, 0) == 0; });
            const bool linkWord = std::any_of(std::begin(kLinks), std::end(kLinks), [&](const wchar_t* s) { return link == s; });
            if (chatWord && linkWord) return Text::Trim(Text::ToUtf8(wide.substr(words[w + 2].start)));
        }
        // «открой дискорд с чатом прощал» — без «с»/«к», но раз речь про Discord, после «чат…» идёт имя
        const bool aboutDiscord = lower.find(L"дискорд") != std::wstring::npos || lower.find(L"дискорт") != std::wstring::npos ||
                                  lower.find(L"discord") != std::wstring::npos;
        for (size_t w = 0; aboutDiscord && w + 1 < words.size(); ++w) {
            const std::wstring word = lower.substr(words[w].start, words[w].end - words[w].start);
            if (word.rfind(L"чат", 0) == 0) return Text::Trim(Text::ToUtf8(wide.substr(words[w + 1].start)));
        }
        return {};
    }
}

namespace {
    // «открой видео под названием Всё проще, чем кажется на ютубе» → «Всё проще, чем кажется»
    // Пусто, если это не просьба открыть видео/ссылку с экрана.
    std::string VideoTitle(const std::string& command) {
        const std::wstring wide = Text::ToWide(command);
        const std::wstring lower = Text::LowerRu(wide);
        auto has = [&](const wchar_t* s) { return lower.find(s) != std::wstring::npos; };

        const bool open = has(L"открой") || has(L"открыть") || has(L"включи") || has(L"запусти") || has(L"зайди") || has(L"перейди") || has(L"нажми");
        const bool media = has(L"видео") || has(L"видос") || has(L"ролик") || has(L"клип") || has(L"ссылк");
        const bool control = has(L"пауз") || has(L"громче") || has(L"тише") || has(L"звук") || has(L"следующ") || has(L"предыдущ");
        if (!open || !media || control) return {};

        size_t start = std::wstring::npos;
        for (const wchar_t* marker : { L"под названием", L"с названием", L"называется", L"название", L"про " }) {
            const size_t pos = lower.find(marker);
            if (pos != std::wstring::npos) { start = pos + wcslen(marker); break; }
        }
        if (start == std::wstring::npos) { // «открой видео Всё проще…» — название сразу после слова «видео»
            for (const wchar_t* word : { L"видео", L"видос", L"ролик", L"клип", L"ссылк" }) {
                const size_t pos = lower.find(word);
                if (pos == std::wstring::npos) continue;
                start = pos;
                while (start < lower.size() && IsCharAlphaW(lower[start])) ++start;
                break;
            }
        }
        std::wstring title = wide.substr(start);
        std::wstring titleLower = Text::LowerRu(title);
        for (const wchar_t* tail : { L"на ютубе", L"в ютубе", L"на youtube", L"в youtube", L"в браузере", L"на экране", L"на странице" }) {
            for (size_t pos; (pos = titleLower.find(tail)) != std::wstring::npos;) {
                title.erase(pos, wcslen(tail));
                titleLower.erase(pos, wcslen(tail));
            }
        }
        return Text::Trim(Text::ToUtf8(title));
    }

    // Подпись ссылки бывает длинной («… от pasted1m 78 тыс. просмотров 10 дней назад») — вслух говорим только название
    std::string ShortTitle(const std::string& name) {
        std::wstring w = Text::ToWide(name);
        const size_t by = w.find(L" от ");
        if (by != std::wstring::npos) w.resize(by);
        if (w.size() > 70) w = w.substr(0, 70) + L"…";
        return Text::Trim(Text::ToUtf8(w));
    }
}

namespace {
    // Короткое название вкладки для озвучки: «(9) YouTube – Использование памяти – 343 МБ» → «YouTube»,
    // «• ГС - Голосовой Ассистент — закреплено» → «ГС - Голосовой Ассистент»
    std::string TabTitle(const std::string& title) {
        std::wstring w = Text::ToWide(title);
        while (!w.empty() && (w[0] == L'•' || w[0] == L' ')) w.erase(0, 1);
        if (!w.empty() && w[0] == L'(') { const size_t close = w.find(L") "); if (close != std::wstring::npos) w.erase(0, close + 2); }
        for (const wchar_t* tail : { L" – Использование памяти", L" — закреплено", L" - Microsoft Edge", L" — Microsoft Edge", L" - Google Chrome" }) {
            const size_t pos = w.find(tail);
            if (pos != std::wstring::npos) w.erase(pos);
        }
        if (w.size() > 60) w = w.substr(0, 60) + L"…";
        return Text::Trim(Text::ToUtf8(w));
    }

    // Что после слова «вкладка» — название, на которую перейти. Пусто — это не переход.
    std::string TabRequest(const std::string& norm) {
        const bool go = norm.find("переключ") != std::string::npos || norm.find("перейди") != std::string::npos ||
                        norm.find("открой") != std::string::npos || norm.find("покажи") != std::string::npos ||
                        norm.find("зайди") != std::string::npos || norm.find("вернись") != std::string::npos;
        const size_t pos = norm.find("вкладк");
        if (!go || pos == std::string::npos) return {};
        size_t start = norm.find(' ', pos);
        if (start == std::string::npos) return {};
        std::string rest = Text::Trim(norm.substr(start));
        for (const char* filler : { "на ", "с ", "со ", "где ", "под названием ", "называется " })
            if (rest.rfind(filler, 0) == 0) rest = Text::Trim(rest.substr(strlen(filler)));
        if (rest.empty() || rest.rfind("нов", 0) == 0 || rest.rfind("еще", 0) == 0 || rest.rfind("в браузере", 0) == 0) return {};
        return rest;
    }

    // Браузер: окно, вкладки, прокрутка. Клавиши уходят именно браузеру — Аня сама выводит его вперёд.
    // Вкладки — как удобно пользователю с вертикальными вкладками: «прошлая» — вниз по списку, «следующая» — вверх.
    std::optional<std::string> HandleBrowser(const std::string& norm) {
        auto has = [&](std::initializer_list<const char*> phrases) {
            for (const char* p : phrases)
                if (norm.find(p) != std::string::npos) return true;
            return false;
        };
        const char* kNoBrowser = "Браузер не открыт";

        // прокрутка
        const bool scroll = has({ "пролист", "пролест", "листай", "прокрут", "скролл", "полистай" });
        if (scroll && has({ "вниз", "ниже", "дальше" })) return Browser::StartScroll(+1) ? "" : kNoBrowser;
        if (scroll && has({ "вверх", "выше", "наверх" })) return Browser::StartScroll(-1) ? "" : kNoBrowser;
        if (scroll) return Browser::StartScroll(+1) ? "" : kNoBrowser;

        // окно браузера
        // «закрой вкладку в браузере» — это про вкладку, а не про всё окно браузера
        const bool browser = has({ "браузер", "эдж", "edge", "эйдж" }) && !has({ "вкладк" });
        if (browser && has({ "скрой", "сверни", "спрячь", "убери" })) return Browser::Hide() ? "Спрятала браузер" : kNoBrowser;
        if (browser && has({ "покажи", "разверни", "верни" })) return Browser::Show() ? "" : kNoBrowser;
        if (browser && has({ "открой", "открыть", "запусти", "включи", "перейди", "зайди" })) { // сам браузер, без новой вкладки
            Browser::Open();
            return "";
        }
        if (browser && has({ "закрой", "выключи" })) return Browser::Close() ? "Закрываю браузер" : kNoBrowser;

        // вкладки
        if (has({ "закрыт" }) && has({ "вкладк" })) return Browser::Keys({ VK_CONTROL, VK_SHIFT, 'T' }) ? "Вернула вкладку" : kNoBrowser;
        if (has({ "прошл", "предыдущ" }) && has({ "вкладк" })) return Browser::Keys({ VK_CONTROL, VK_TAB }) ? "" : kNoBrowser;
        if (has({ "следующ" }) && has({ "вкладк" })) return Browser::Keys({ VK_CONTROL, VK_SHIFT, VK_TAB }) ? "" : kNoBrowser;
        // какие вкладки открыты
        if (has({ "какие вкладки", "список вкладок", "сколько вкладок", "что открыто в браузере", "перечисли вкладки" })) {
            const auto tabs = Browser::Tabs();
            if (tabs.empty()) return std::string("Не вижу открытых вкладок — браузер закрыт?");
            std::string answer = "Открыто " + std::to_string(tabs.size()) + (tabs.size() == 1 ? " вкладка: " : tabs.size() < 5 ? " вкладки: " : " вкладок: ");
            for (size_t i = 0; i < tabs.size() && i < 8; ++i) answer += (i ? "; " : "") + TabTitle(tabs[i]);
            if (tabs.size() > 8) answer += " и ещё " + std::to_string(tabs.size() - 8);
            return answer + ".";
        }
        // «перейди на вкладку гитхаб», «открой вкладку с ютубом» — переход по названию (но «открой новую вкладку» — новая)
        if (const std::string wanted = TabRequest(norm); !wanted.empty()) {
            std::string title;
            if (Browser::SwitchToTab(wanted, title)) return "Перешла на «" + TabTitle(title) + "»";
            return "Не нашла вкладку «" + wanted + "»";
        }
        if (has({ "новая вкладка", "новую вкладку", "открой вкладку", "создай вкладку", "сделай вкладку", "добавь вкладку" })) return Browser::Keys({ VK_CONTROL, 'T' }) ? "" : kNoBrowser;
        if (has({ "закрой вкладку", "закрой эту вкладку" })) return Browser::Keys({ VK_CONTROL, 'W' }) ? "" : kNoBrowser;
        if (has({ "обнови страницу", "перезагрузи страницу" })) return Browser::Keys({ VK_F5 }) ? "" : kNoBrowser;
        if (has({ "вернись назад", "назад на страницу", "страница назад", "предыдущая страница" })) return Browser::Keys({ VK_MENU, VK_LEFT }) ? "" : kNoBrowser;
        if (has({ "вперед на страницу", "следующая страница" })) return Browser::Keys({ VK_MENU, VK_RIGHT }) ? "" : kNoBrowser;
        if (has({ "полный экран", "во весь экран" })) return Browser::Keys({ 'F' }) ? "" : kNoBrowser;
        return std::nullopt;
    }
}

namespace {
    // Компьютер: точная громкость, батарея, загрузка, выделенный текст и буфер обмена
    std::optional<std::string> HandlePc(const std::string& norm) {
        auto has = [&](std::initializer_list<const char*> phrases) {
            for (const char* p : phrases)
                if (norm.find(p) != std::string::npos) return true;
            return false;
        };

        if (has({ "громкост" })) {
            const auto words = Words(norm);
            for (size_t i = 0; i < words.size();) {
                if (const auto n = ReadNumber(words, i)) {
                    if (!Pc::SetVolume(static_cast<int>(*n))) return std::string("Не получилось поменять громкость");
                    return "Громкость " + std::to_string(std::clamp(static_cast<int>(*n), 0, 100)) + "%";
                }
                ++i;
            }
            if (has({ "какая", "сколько", "скажи" })) {
                const auto level = Pc::Volume();
                return level ? "Громкость " + std::to_string(*level) + "%" : std::string("Не получилось узнать громкость");
            }
            if (has({ "половин" })) return Pc::SetVolume(50) ? std::string("Громкость 50%") : std::string("Не получилось поменять громкость");
            if (has({ "максимум", "на полную", "полную" })) return Pc::SetVolume(100) ? std::string("Громкость на максимум") : std::string("Не получилось");
        }

        if (has({ "заряд", "батаре", "аккумулятор" })) return Pc::BatteryReport();
        if (has({ "как там комп", "загрузка компьютера", "загрузка процессора", "загрузка пк", "нагрузка", "оперативк", "сколько памяти" }))
            return Pc::LoadReport();

        if (has({ "прочитай выделен", "прочитай что выделен", "озвучь выделен", "прочитай текст", "прочитай это", "прочти выделен" })) {
            std::string text = Pc::CopySelection();
            if (Text::Trim(text).empty()) return std::string("Сначала выдели текст, который прочитать");
            if (text.size() > 2000) text = text.substr(0, 2000) + "…";
            return text;
        }
        if (has({ "что в буфере", "прочитай буфер", "что я скопировал", "что скопировано" })) {
            std::string text = Pc::Clipboard();
            if (Text::Trim(text).empty()) return std::string("В буфере обмена нет текста");
            if (text.size() > 2000) text = text.substr(0, 2000) + "…";
            return text;
        }
        return std::nullopt;
    }
}

std::optional<std::string> Skills::Handle(const std::string& command) {
    if (auto made = ScenarioMaker::Handle(command)) return made;          // «создай сценарий …» (и рассказ после него)
    if (auto scenario = Scenarios::TryRun(command)) return scenario;      // свои сценарии — главнее всего
    if (auto setting = ScenarioMaker::FindSetting(command)) return setting; // «где настройка …?» — показать и подсветить
    if (auto power = PhoneAndPower(Normalize(command))) return power;     // до погоды: «температура процессора» — не про погоду
    if (auto forecast = Weather::Answer(command)) return forecast;
    if (auto file = FileSearch::Handle(command)) return file;             // до общего «найди …»
    if (auto search = TypeInSearch(command)) return search;               // до «найди …» и «напечатай …»
    if (auto link = OpenNthLink(Normalize(command))) return link;          // до «открой видео „название“»
    if (auto volume = ChangeVolume(Normalize(command))) return volume;     // до «громкость на 30» и клавиш громкости
    if (auto browser = HandleBrowser(Normalize(command))) return browser;
    if (auto pc = HandlePc(Normalize(command))) return pc;
    // видео/ссылка, которая видна на экране, — нажимаем именно её, без поиска в интернете
    if (const std::string title = VideoTitle(command); !title.empty()) {
        if (const auto opened = ScreenLinks::Open(title); opened.found) return "Включаю «" + ShortTitle(opened.title) + "»";
        return "Не вижу на экране видео «" + title + "». Открой страницу, где оно есть, и скажи ещё раз";
    }

    // чат в Discord: Messenger сам откроет Discord, поэтому «открой дискорд и…» отдельно не выполняем
    if (const std::string who = ChatTarget(command); !who.empty()) return Messenger::OpenDiscordChat(who);

    const auto parts = SplitCompound(command);
    if (parts.size() == 1) return HandleOne(command);

    std::string replies;
    bool handled = false;
    for (const auto& part : parts) {
        const auto reply = HandleOne(part);
        if (!reply) continue;
        handled = true;
        if (!reply->empty()) replies += (replies.empty() ? "" : ". ") + *reply;
    }
    if (!handled) return std::nullopt;
    return replies;
}

static std::optional<std::string> HandleOne(const std::string& command) {
    const std::string norm = Normalize(command);

    // диктовка
    if (HasAny(norm, { "начни диктовку", "режим диктовки", "включи диктовку", "давай диктовку", "диктовка" })) {
        g_dictating = true;
        return "Диктуй, я печатаю в активное окно. Скажи «стоп диктовка», когда закончишь";
    }
    if (DeleteText(norm)) return "";
    if (const std::string text = AfterVerb(command, { "напечатай", "набери", "введи", "впиши" }); !text.empty()) {
        Input::TypeText(text);
        return "";
    }

    // «закрой окно» — до общего «закрой …»
    if (HasAny(norm, { "закрой окно", "закрой это окно", "закрой это" })) {
        if (ForegroundIsMine()) return "Сначала кликни на окно, которое закрыть, — а то я закрою себя";
        PostMessageW(GetForegroundWindow(), WM_CLOSE, 0, 0);
        return "";
    }

    // Клавиши жмём только на короткие команды. «Объясни, как вставить формулу» — это вопрос, а не «вставь».
    const bool shortCommand = Words(norm).size() <= 6 && !LooksLikeQuestion(norm);
    for (const auto& cmd : KeyCommands()) {
        if (!shortCommand || !HasAny(norm, cmd.phrases)) continue;
        cmd.action();
        return std::string(cmd.reply);
    }

    if (auto r = Timer(command, norm)) return r;
    const bool hasDigit = norm.find_first_of("0123456789") != std::string::npos;
    const bool mathWords = hasDigit && HasAny(norm, { " плюс ", " минус ", "умнож", "делить", "дели на", "деленн" });
    if (HasAny(norm, { "сколько будет", "посчитай", "вычисли", "это сколько", "сколько это", "сколько получится" }) || mathWords ||
        norm.find_first_of("+*/") != std::string::npos)
        if (auto r = Calculate(norm)) return r;
    if (auto r = Chance(norm)) return r;

    if (HasAny(norm, { "который час", "сколько времени", "скажи время", "сколько сейчас времени" })) return TimeNow();
    if (HasAny(norm, { "какое число", "какое сегодня число", "какой сегодня день", "какая сегодня дата", "какой день недели" })) return DateToday();

    if (const std::string what = AfterVerb(command, { "скрой", "сверни", "спрячь", "убери" }); !what.empty() && !HasAny(norm, { "окн", "все" })) {
        if (const auto r = Launcher::Minimize(what); r.found) return "Спрятала " + r.name;
        return "Не вижу, чтобы «" + what + "» было открыто";
    }
    if (const std::string what = AfterVerb(command, { "покажи", "разверни", "верни" }); !what.empty() && !HasAny(norm, { "окн", "рабочий стол" })) {
        if (const auto r = Launcher::Show(what); r.found) return "";
        // «покажи мне шутку» — не про окно: пусть разберутся другие навыки или нейросеть
    }
    if (const std::string what = AfterVerb(command, { "закрой", "выключи", "заверши", "выруби", "закрыть" }); !what.empty()) {
        if (const auto r = Launcher::Close(what); r.found) return "Закрываю " + r.name;
        return "Не вижу, чтобы «" + what + "» было открыто";
    }
    if (const std::string what = AfterVerb(command, { "открой", "запусти", "включи", "открыть", "запустить", "зайди в", "откройте", "открою", "откроет" }); !what.empty()) {
        if (auto r = OpenBuiltIn(what)) return r;
        if (const auto r = Launcher::Open(what); r.found) return "Открываю " + r.name;
        return "Не нашла «" + what + "» на компьютере";
    }
    // «найди корни», «найди x», «найди площадь» — это задача, а не поиск в интернете: её решит нейросеть
    const bool mathTask = HasAny(norm, { "корн", "уравнен", "икс", "производн", "интеграл", "значение", "площад", "периметр", "объем",
                                         "скорост", "процент", "дроб", "сумму", "разност", "произведени", "частное", "решени", "неизвестн" }) ||
                          std::any_of(norm.begin(), norm.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (const std::string query = AfterVerb(command, { "найди в интернете", "найди", "загугли", "поищи" });
        !query.empty() && (!mathTask || HasAny(norm, { "в интернете", "загугли", "в гугле" }))) {
        static const char* kSearchUrl[] = { "https://www.google.com/search?q=", "https://yandex.ru/search/?text=",
                                            "https://duckduckgo.com/?q=", "https://www.bing.com/search?q=" };
        OpenTarget(Text::ToWide(kSearchUrl[static_cast<int>(Config::Get().search)] + Text::UrlEncode(query)).c_str());
        return "Ищу: " + query;
    }

    if (HasAny(norm, { "что ты умеешь", "что ты можешь", "помощь", "твои команды" })) return Skills::Help();
    return std::nullopt;
}

bool Skills::IsDictating() { return g_dictating; }

void Skills::HandleDictation(const std::string& spoken) {
    const std::string norm = Normalize(spoken);
    if (HasAny(norm, { "стоп диктовк", "закончи диктовку", "конец диктовки", "хватит диктовки", "выключи диктовку", "останови диктовку" })) {
        g_dictating = false;
        Chat::Add(Role::Anya, "Диктовка закончена");
        Caption::Set("диктовка закончена");
        Voice::Speak("Готово");
        return;
    }
    if (HasAny(norm, { "новая строка", "с новой строки", "новый абзац" })) {
        Input::Tap(VK_RETURN);
        return;
    }
    if (Words(norm).size() <= 5 && DeleteText(norm)) return; // «удали слово» посреди диктовки — правка, а не текст
    Chat::Add(Role::User, "✎ " + spoken);
    Caption::Set("✎ " + spoken, true);
    Input::TypeText(spoken + " ");
}

std::string Skills::Help() {
    return "Я умею без интернета: открывать и закрывать любые программы — «открой стим», «закрой дискорд»; "
           "ставить видео и музыку на паузу, переключать треки, менять громкость; печатать под диктовку — «напечатай привет» "
           "или «начни диктовку»; открывать чат в Discord — «зайди в чат с Зефирочником»; "
           "включать видео, которое видно на экране, — «открой видео Всё проще, чем кажется»; "
           "рассказывать погоду на дорогу в колледж и когда будет дождь; "
           "листать страницу — «пролистай ниже», и «стоп»; переключать вкладки — «прошлая вкладка», «следующая вкладка», «открой закрытую вкладку»; "
           "прятать браузер — «скрой браузер»; рассказывать, какие вкладки открыты, и переходить на нужную; "
           "ставить точную громкость — «громкость на 30»; говорить заряд батареи и загрузку компьютера; читать выделенный текст. "
           "сворачивать окна, делать скриншот, ставить таймеры, считать, говорить время. "
           "А для домашки и сложных вопросов скажи «спроси нейросеть» — и я позову ИИ.";
}
