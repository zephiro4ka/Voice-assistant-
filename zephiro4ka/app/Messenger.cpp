#include "Messenger.h"

#include "Platform.h"
#include "Fuzzy.h"
#include "Input.h"
#include "Launcher.h"
#include "Log.h"
#include "Settings.h"
#include "State.h"
#include "TextUtils.h"

#include <algorithm>
#include <thread>
#include <vector>

namespace {
    constexpr Clock kDiscordStartTimeoutMs = 25000; // Discord при запуске ещё и обновляется

    struct Contact {
        std::string spoken;   // как произносится: «зефирочник»
        std::string handle;   // ник в мессенджере: zephiro4nik
    };

    // Русские имена склоняются: «с прыщавым», «к прыщавому». Ник в Discord — в одной форме,
    // поэтому ищем по основе без окончания: «прыщавым» → «прыщав», и поиск Discord находит «прыщавый».
    std::wstring Stem(const std::wstring& word) {
        static const wchar_t* kEndings[] = { L"ому", L"ему", L"ыми", L"ими", L"ый", L"ий", L"ой", L"ым", L"им", L"ом", L"ем", L"ей",
                                             L"ая", L"яя", L"ую", L"юю", L"ы", L"и", L"а", L"я", L"у", L"ю", L"е", L"о" };
        for (const wchar_t* ending : kEndings) {
            const size_t n = wcslen(ending);
            if (word.size() >= n + 4 && word.compare(word.size() - n, n, ending) == 0) return word.substr(0, word.size() - n);
        }
        return word;
    }

    constexpr float kContactMatch = 0.6f; // насколько похожим должно быть услышанное имя на контакт

    // «Зефирочником», «Зефирочнику», «с Зефирочник» → ищем по началу слова, без предлогов и окончаний
    Contact ResolveContact(const std::string& spoken) {
        std::wstring who = Text::LowerRu(Text::ToWide(Text::Trim(spoken)));
        for (const wchar_t* tail : { L" в дискорде", L" в дискорд", L" в discord", L" в лс", L" в личку" }) {
            const size_t pos = who.find(tail);
            if (pos != std::wstring::npos) who.erase(pos);
        }
        // лишние слова вокруг имени: «с человеком прыщавый», «с пользователем по имени …»
        for (const wchar_t* filler : { L"человеком ", L"человека ", L"человек ", L"пользователем ", L"пользователя ", L"другом ",
                                       L"подругой ", L"по имени ", L"с ником ", L"ником ", L"с ", L"со ", L"к ", L"ко " }) {
            if (who.rfind(filler, 0) == 0) who.erase(0, wcslen(filler));
        }
        while (!who.empty() && !IsCharAlphaNumericW(who.back())) who.pop_back(); // «прыщавый.» → «прыщавый»

        // Сравниваем основы слов нечётко: whisper пишет «прищавым», «прощал» вместо «прыщавый»
        std::wstring glued;
        for (wchar_t ch : who) if (ch != L' ') glued.push_back(ch);
        const std::wstring heard = Stem(glued);
        const Contact* best = nullptr;
        float bestScore = 0.0f;
        std::vector<Contact> contacts;
        for (const auto& [name, handle] : Config::Contacts()) contacts.push_back({ name, handle });
        for (const auto& contact : contacts) {
            const std::wstring alias = Stem(Text::LowerRu(Text::ToWide(contact.spoken)));
            const float score = std::max(Fuzzy::Similarity(heard, alias), heard.rfind(alias, 0) == 0 ? 1.0f : 0.0f);
            if (score > bestScore) { bestScore = score; best = &contact; }
        }
        if (best && bestScore >= kContactMatch) return *best;
        // Нет в контактах — ищем как сказали, но без пробелов: whisper любит рвать слова («прыща вы»),
        // а быстрый поиск Discord находит человека и по склеенному началу имени («прыщавы» → «прыщавый»).
        return { Text::ToUtf8(who), Text::ToUtf8(heard) };
    }

    // Главное окно Discord (видимое, достаточно большое — не окошко обновления)
    HWND FindDiscordWindow() {
        struct Search { HWND found = nullptr; } search;
        EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
            if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!process) return TRUE;
            wchar_t path[MAX_PATH] = {};
            DWORD size = MAX_PATH;
            const bool isDiscord = QueryFullProcessImageNameW(process, 0, path, &size) &&
                                   Text::LowerRu(path).find(L"\\discord.exe") != std::wstring::npos;
            CloseHandle(process);
            RECT rc{};
            GetWindowRect(hwnd, &rc);
            if (isDiscord && (rc.right - rc.left > 500 || IsIconic(hwnd))) {
                reinterpret_cast<Search*>(param)->found = hwnd;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    // Windows не даёт фоновой программе забирать фокус; нажатие Alt снимает этот запрет
    void BringToFront(HWND window) {
        if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
        Input::Tap(VK_MENU);
        SetForegroundWindow(window);
        Sleep(350);
    }

    void OpenChat(Contact contact) {
        const Clock deadline = NowMs() + kDiscordStartTimeoutMs;
        HWND window = FindDiscordWindow();
        if (!window) Launcher::Open("discord");
        while (!window && g_app.running && NowMs() < deadline) {
            Sleep(300);
            window = FindDiscordWindow();
        }
        if (!window) {
            Caption::Set("Discord так и не открылся");
            Log::Write("Discord: окно не появилось");
            return;
        }
        Sleep(800); // Discord после появления окна ещё дорисовывает интерфейс
        BringToFront(window);
        Input::Chord({ VK_CONTROL, 'K' });   // быстрый переход Discord
        Sleep(400);
        Input::TypeText(contact.handle);
        Sleep(900);                           // ждём, пока Discord найдёт человека
        Input::Tap(VK_RETURN);
        Log::Write("Discord: открыла чат с «" + contact.handle + "»");
    }
}

std::string Messenger::OpenDiscordChat(const std::string& spokenName) {
    const Contact contact = ResolveContact(spokenName);
    if (contact.handle.empty()) return "С кем открыть чат?";
    std::thread(OpenChat, contact).detach();
    return "Открываю в Discord чат с «" + Text::Capitalize(contact.spoken) + "»";
}
