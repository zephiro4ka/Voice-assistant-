#include "Launcher.h"

#include "Platform.h"
#include "Fuzzy.h"
#include "Input.h"
#include "TextUtils.h"

#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace {
    constexpr float kMatchThreshold = 0.72f;

    struct App {
        std::wstring displayName;
        std::wstring launchTarget;  // shell:AppsFolder\… или путь к ярлыку
        std::string key;            // фонетический ключ всего названия
        std::vector<std::string> words;
        bool junk = false;          // «Удалить …», «Документация …» и т.п.
    };

    std::mutex g_mutex;
    std::vector<App> g_apps;
    std::thread g_indexer;

    // Как люди называют программы по-русски → часть английского названия
    struct Alias { const char* spoken; const char* target; };
    constexpr Alias kAliases[] = {
        { "стим", "steam" }, { "стимчик", "steam" }, { "дискорд", "discord" }, { "дис", "discord" },
        { "телега", "telegram" }, { "телеграм", "telegram" }, { "тг", "telegram" },
        { "хром", "chrome" }, { "гугл хром", "chrome" }, { "лиса", "firefox" }, { "файрфокс", "firefox" },
        { "яндекс музыка", "яндекс музыка" }, { "музыку", "яндекс музыка" }, { "музыка", "яндекс музыка" },
        { "вс код", "visual studio code" }, { "вскод", "visual studio code" }, { "вэ эс код", "visual studio code" },
        { "студию", "visual studio" }, { "визуал студио", "visual studio" }, { "вижуал студио", "visual studio" },
        { "обс", "obs studio" }, { "обску", "obs studio" }, { "ворд", "word" }, { "эксель", "excel" },
        { "блендер", "blender" }, { "роблокс", "roblox" }, { "майнкрафт", "minecraft" }, { "спотифай", "spotify" },
        { "терминал", "терминал" }, { "консоль", "командная строка" }, { "командную строку", "командная строка" },
        { "настройки", "настройки" }, { "калькулятор", "калькулятор" }, { "блокнот", "блокнот" },
        { "пейнт", "paint" }, { "паинт", "paint" }, { "проводник", "проводник" }, { "диспетчер задач", "диспетчер задач" },
        { "курсор", "cursor" }, { "чат гпт", "chatgpt" }, { "чатгпт", "chatgpt" }, { "лм студио", "lm studio" },
        { "вотсап", "whatsapp" }, { "зум", "zoom" }, { "эпик", "epic games" }, { "эпик геймс", "epic games" },
    };

    // Насколько сказанное похоже на название (0..1)
    float Score(const std::string& query, const std::string& key, const std::vector<std::string>& words) {
        if (query.size() < 2) return 0.0f;
        if (key == query) return 1.0f;
        float best = Fuzzy::Similarity(query, key);
        if (query.size() >= 3 && key.rfind(query, 0) == 0) best = std::max(best, 0.9f - 0.01f * (key.size() - query.size()));
        // совпадение с одним словом или с несколькими словами подряд: «студио код» внутри «visual studio code»
        for (size_t i = 0; i < words.size(); ++i) {
            std::string run;
            for (size_t j = i; j < words.size(); ++j) {
                run += words[j];
                const float s = Fuzzy::Similarity(query, run) - (i == 0 ? 0.0f : 0.06f);
                best = std::max(best, s);
                if (query.size() >= 3 && run.rfind(query, 0) == 0 && i == 0) best = std::max(best, 0.86f);
            }
        }
        return best;
    }

    bool IsJunk(const std::wstring& name) {
        const std::wstring lower = Text::LowerRu(name);
        for (const wchar_t* word : { L"uninstall", L"удал", L"documentation", L"readme", L"website", L"сайт", L"support",
                                     L"help", L"manual", L"release notes", L"faq", L"license" })
            if (lower.find(word) != std::wstring::npos) return true;
        return false;
    }

    App MakeApp(const std::wstring& name, const std::wstring& target) {
        App app;
        app.displayName = name;
        app.launchTarget = target;
        app.words = Fuzzy::PhoneticWords(Text::ToUtf8(name));
        app.key = Fuzzy::Join(app.words);
        app.junk = IsJunk(name);
        return app;
    }

    // ------------------------------------------------------------ сбор списка программ
    void IndexAppsFolder(std::vector<App>& out) {
        IShellItem* folder = nullptr;
        if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder)))) return;
        IEnumShellItems* items = nullptr;
        if (SUCCEEDED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) {
            IShellItem* item = nullptr;
            while (items->Next(1, &item, nullptr) == S_OK) {
                LPWSTR name = nullptr, parsing = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) &&
                    SUCCEEDED(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &parsing)))
                    out.push_back(MakeApp(name, L"shell:AppsFolder\\" + std::wstring(parsing)));
                CoTaskMemFree(name);
                CoTaskMemFree(parsing);
                item->Release();
            }
            items->Release();
        }
        folder->Release();
    }

    void IndexShortcuts(REFKNOWNFOLDERID folderId, std::vector<App>& out) {
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(folderId, 0, nullptr, &path))) return;
        const std::wstring dir = path;
        CoTaskMemFree(path);

        WIN32_FIND_DATAW data;
        const HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) return;
        do {
            const std::wstring file = data.cFileName;
            const size_t dot = file.find_last_of(L'.');
            if (dot == std::wstring::npos) continue;
            const std::wstring ext = Text::LowerRu(file.substr(dot));
            if (ext == L".lnk" || ext == L".url") out.push_back(MakeApp(file.substr(0, dot), dir + L"\\" + file));
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }

    void BuildIndex() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        std::vector<App> apps;
        IndexShortcuts(FOLDERID_Desktop, apps);       // ярлыки рабочего стола (часто — игры Steam)
        IndexShortcuts(FOLDERID_PublicDesktop, apps);
        IndexAppsFolder(apps);                        // всё из «Пуска», включая Store
        CoUninitialize();

        std::lock_guard lock(g_mutex);
        g_apps = std::move(apps);
    }

    // Убираем вежливые и лишние слова: «открой мне пожалуйста игру стим» → «стим»
    std::string CleanQuery(const std::string& spoken) {
        std::wstring text = Text::LowerRu(Text::ToWide(spoken));
        for (const wchar_t* filler : { L"пожалуйста", L"мне", L"приложение", L"программу", L"игру", L"окошко", L"окно", L"пж", L"плиз" })
            for (size_t pos; (pos = text.find(filler)) != std::wstring::npos;) text.erase(pos, wcslen(filler));
        return Text::Trim(Text::ToUtf8(text));
    }

    // Подставить английское название, если сказали «по-русски» (стим → steam)
    std::string ApplyAlias(const std::string& query) {
        const std::string lower = Text::ToUtf8(Text::LowerRu(Text::ToWide(query)));
        for (const auto& alias : kAliases)
            if (lower == alias.spoken) return alias.target;
        return query;
    }

    const App* FindBest(const std::vector<App>& apps, const std::string& spoken) {
        const std::string query = Fuzzy::Join(Fuzzy::PhoneticWords(ApplyAlias(CleanQuery(spoken))));
        const App* best = nullptr;
        float bestScore = 0.0f;
        for (const auto& app : apps) {
            float score = Score(query, app.key, app.words) - (app.junk ? 0.3f : 0.0f);
            const bool better = score > bestScore + 0.001f ||
                (std::abs(score - bestScore) <= 0.001f && best && app.displayName.size() < best->displayName.size());
            if (better) {
                best = &app;
                bestScore = score;
            }
        }
        return bestScore >= kMatchThreshold ? best : nullptr;
    }

    // ------------------------------------------------------------ окна запущенных программ
    struct OpenWindow {
        HWND handle;
        DWORD pid;
        std::wstring title, exeName;
        bool visible;
        long area;
    };

    // Окна программ. includeHidden — и спрятанные в трей (Discord, Steam, Telegram прячут туда главное окно)
    std::vector<OpenWindow> ListWindows(bool includeHidden) {
        struct Search { bool includeHidden; std::vector<OpenWindow> found; } search{ includeHidden, {} };
        EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(param);
            const bool visible = IsWindowVisible(hwnd) != FALSE;
            if ((!visible && !s.includeHidden) || GetWindow(hwnd, GW_OWNER) || (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
            wchar_t title[256] = {};
            GetWindowTextW(hwnd, title, 256);
            if (!title[0]) return TRUE;
            for (const wchar_t* service : { L"Default IME", L"MSCTFIME UI", L"GDI+ Window" })
                if (wcsncmp(title, service, wcslen(service)) == 0) return TRUE;

            RECT rc{};
            GetWindowRect(hwnd, &rc);
            OpenWindow w{ hwnd, 0, title, L"", visible, (rc.right - rc.left) * (rc.bottom - rc.top) };
            GetWindowThreadProcessId(hwnd, &w.pid);
            if (w.pid == GetCurrentProcessId()) return TRUE; // себя не трогаем
            if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, w.pid)) {
                wchar_t path[MAX_PATH] = {};
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(process, 0, path, &size)) {
                    std::wstring exe = path;
                    exe = exe.substr(exe.find_last_of(L'\\') + 1);
                    w.exeName = exe.substr(0, exe.find_last_of(L'.'));
                }
                CloseHandle(process);
            }
            s.found.push_back(w);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    bool IsBrowserExe(const std::wstring& exe) {
        const std::wstring lower = Text::LowerRu(exe);
        for (const wchar_t* b : { L"msedge", L"chrome", L"firefox", L"brave", L"opera", L"browser", L"vivaldi", L"yandex" })
            if (lower == b) return true;
        return false;
    }

    struct Match { DWORD pid = 0; std::wstring name; float score = 0.0f; };

    // Процесс, чьё имя exe или заголовок окна больше всего похожи на сказанное.
    // У браузера заголовок — это открытая вкладка («Steam — магазин…»), поэтому браузер узнаём только по имени exe.
    Match BestProcess(const std::string& spoken, const std::vector<OpenWindow>& windows) {
        const std::string query = Fuzzy::Join(Fuzzy::PhoneticWords(ApplyAlias(CleanQuery(spoken))));
        Match best;
        for (const auto& w : windows) {
            const auto exeWords = Fuzzy::PhoneticWords(Text::ToUtf8(w.exeName));
            float score = Score(query, Fuzzy::Join(exeWords), exeWords);
            if (!IsBrowserExe(w.exeName)) {
                const auto titleWords = Fuzzy::PhoneticWords(Text::ToUtf8(w.title));
                score = std::max(score, Score(query, Fuzzy::Join(titleWords), titleWords) - 0.05f);
            }
            if (score > best.score) best = { w.pid, w.exeName.empty() ? w.title : w.exeName, score };
        }
        if (best.score < kMatchThreshold) return {};
        return best;
    }

    // Главное окно программы: видимое, а если всё спрятано в трей — самое большое из спрятанных
    HWND MainWindow(const std::vector<OpenWindow>& windows, DWORD pid) {
        const OpenWindow* best = nullptr;
        for (const auto& w : windows) {
            if (w.pid != pid) continue;
            if (!best || (w.visible && !best->visible) || (w.visible == best->visible && w.area > best->area)) best = &w;
        }
        return best ? best->handle : nullptr;
    }

    void BringToFront(HWND window) {
        if (!IsWindowVisible(window)) ShowWindow(window, SW_SHOW); // из трея
        if (IsIconic(window)) ShowWindow(window, SW_RESTORE);      // свёрнуто
        Input::Tap(VK_MENU); // Windows не даёт фоновой программе забирать фокус; нажатие Alt снимает этот запрет
        SetForegroundWindow(window);
    }
}

// ---------------------------------------------------------------------
void Launcher::Start() {
    g_indexer = std::thread(BuildIndex);
    g_indexer.detach();
}

Launcher::Result Launcher::Find(const std::string& spokenName) {
    std::lock_guard lock(g_mutex);
    const App* app = FindBest(g_apps, spokenName);
    return app ? Result{ true, Text::ToUtf8(app->displayName) } : Result{};
}

Launcher::Result Launcher::Open(const std::string& spokenName) {
    // уже запущена (свёрнута или спрятана в трей) — просто показываем: повторный запуск окно не вернёт
    if (const auto shown = Show(spokenName); shown.found) return shown;

    std::wstring target, name;
    {
        std::lock_guard lock(g_mutex);
        const App* app = FindBest(g_apps, spokenName);
        if (!app) return {};
        target = app->launchTarget;
        name = app->displayName;
    }
    const bool ok = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    return { ok, Text::ToUtf8(name) };
}

Launcher::Result Launcher::Close(const std::string& spokenName) {
    const auto windows = ListWindows(false);
    const Match match = BestProcess(spokenName, windows);
    if (!match.pid) return {};
    for (const auto& w : windows)
        if (w.pid == match.pid) PostMessageW(w.handle, WM_CLOSE, 0, 0);
    return { true, Text::ToUtf8(match.name) };
}

Launcher::Result Launcher::Minimize(const std::string& spokenName) {
    const auto windows = ListWindows(false);
    const Match match = BestProcess(spokenName, windows);
    if (!match.pid) return {};
    for (const auto& w : windows)
        if (w.pid == match.pid && !IsIconic(w.handle)) ShowWindow(w.handle, SW_MINIMIZE);
    return { true, Text::ToUtf8(match.name) };
}

void* Launcher::MainWindowOf(const std::string& spokenName) {
    const auto windows = ListWindows(false);
    const Match match = BestProcess(spokenName, windows);
    return match.pid ? MainWindow(windows, match.pid) : nullptr;
}

std::vector<std::string> Launcher::AppNames() {
    std::lock_guard lock(g_mutex);
    std::vector<std::string> names;
    for (const auto& app : g_apps) names.push_back(Text::ToUtf8(app.displayName));
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

Launcher::Result Launcher::Show(const std::string& spokenName) {
    const auto windows = ListWindows(true);
    const Match match = BestProcess(spokenName, windows);
    if (!match.pid) return {};
    const HWND window = MainWindow(windows, match.pid);
    if (!window) return {};
    if (!IsWindowVisible(window)) { // всё спрятано в трей: среди скрытых окон есть служебные пустые (у Steam и других на Chromium)
        std::wstring target; // повторный запуск сама программа превратит в показ своего настоящего окна
        {
            std::lock_guard lock(g_mutex);
            if (const App* app = FindBest(g_apps, spokenName)) target = app->launchTarget;
        }
        if (!target.empty() && reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32)
            return { true, Text::ToUtf8(match.name) };
    }
    BringToFront(window);
    return { true, Text::ToUtf8(match.name) };
}
