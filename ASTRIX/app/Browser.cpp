#include "Browser.h"

#include "Com.h"
#include "Fuzzy.h"
#include "Input.h"

#include <UIAutomation.h>
#include <shellapi.h>

#include <memory>
#include "State.h"
#include "TextUtils.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace {
    constexpr int kScrollTickMs = 40;          // шаг прокрутки: 25 раз в секунду — выглядит плавно
    constexpr float kDefaultSpeed = 18.0f;     // «щелчков» колеса за тик (120 — одна прокрутка колёсиком)
    constexpr Clock kScrollLimitMs = 120000;   // сама остановится через 2 минуты, если забыли сказать «стоп»

    std::atomic<int> g_scrollDirection{ 0 };   // 0 — стоит; +1 вниз; -1 вверх
    std::atomic<float> g_scrollSpeed{ kDefaultSpeed };
    std::atomic<int> g_scrollSession{ 0 };

    std::wstring ExeName(HWND window) {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return {};
        wchar_t path[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
        CloseHandle(process);
        if (!ok) return {};
        const std::wstring lower = Text::LowerRu(path);
        return lower.substr(lower.find_last_of(L'\\') + 1);
    }

    bool IsEdge(const std::wstring& exe) { return exe == L"msedge.exe"; }
    bool IsAnyBrowser(const std::wstring& exe) {
        for (const wchar_t* name : { L"msedge.exe", L"chrome.exe", L"firefox.exe", L"brave.exe", L"opera.exe", L"browser.exe", L"vivaldi.exe", L"yandex.exe" })
            if (exe == name) return true;
        return false;
    }

    // Главные окна браузера сверху вниз (служебные окна без заголовка пропускаем)
    std::vector<HWND> BrowserWindows(bool edgeOnly) {
        struct Search { bool edgeOnly; std::vector<HWND> found; } search{ edgeOnly, {} };
        EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(param);
            if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) || GetWindowTextLengthW(hwnd) == 0) return TRUE;
            const std::wstring exe = ExeName(hwnd);
            if (s.edgeOnly ? IsEdge(exe) : IsAnyBrowser(exe)) s.found.push_back(hwnd);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    // Сначала Edge (он у пользователя по умолчанию), иначе любой другой браузер
    std::vector<HWND> AllWindows() {
        auto windows = BrowserWindows(true);
        return windows.empty() ? BrowserWindows(false) : windows;
    }

    // Колесо мыши крутит то окно, что под курсором: если курсор не над браузером — переносим его в центр страницы
    void AimCursorAt(HWND window) {
        POINT cursor{};
        GetCursorPos(&cursor);
        const HWND under = GetAncestor(WindowFromPoint(cursor), GA_ROOT);
        if (under == window) return;
        RECT rc{};
        GetWindowRect(window, &rc);
        SetCursorPos((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
    }

    void Wheel(int amount) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(amount);
        SendInput(1, &input, sizeof(INPUT));
    }

    void ScrollLoop(int session, HWND window) {
        const Clock started = NowMs();
        float carry = 0.0f; // дробные «щелчки» копим, чтобы скорость была ровной
        while (g_app.running && g_scrollSession == session && g_scrollDirection != 0 && NowMs() - started < kScrollLimitMs) {
            if (!IsWindow(window) || IsIconic(window)) break;
            carry += g_scrollSpeed;
            const int step = static_cast<int>(carry);
            carry -= static_cast<float>(step);
            if (step > 0) Wheel(-g_scrollDirection.load() * step); // отрицательное колесо — вниз
            Sleep(kScrollTickMs);
        }
        if (g_scrollSession == session) g_scrollDirection = 0;
    }
}

HWND Browser::Window(bool restore) {
    const auto windows = AllWindows();
    for (HWND w : windows)
        if (!IsIconic(w)) return w;
    if (windows.empty()) return nullptr;
    if (restore) {
        ShowWindow(windows.front(), SW_RESTORE);
        Sleep(500); // браузер дорисовывает страницу
    }
    return windows.front();
}

bool Browser::Focus() {
    const HWND window = Window(true);
    if (!window) return false;
    Input::Tap(VK_MENU); // Windows не даёт фоновой программе забирать фокус; нажатие Alt снимает этот запрет
    SetForegroundWindow(window);
    Sleep(200);
    return true;
}

bool Browser::Hide() {
    StopScroll();
    const auto windows = AllWindows();
    for (HWND w : windows) ShowWindow(w, SW_MINIMIZE);
    return !windows.empty();
}

bool Browser::Show() { return Focus(); }

void Browser::Open() {
    if (Focus()) return; // уже открыт — просто выводим вперёд, без новых вкладок
    ShellExecuteW(nullptr, L"open", L"msedge.exe", nullptr, nullptr, SW_SHOWNORMAL); // Edge — браузер пользователя
}

bool Browser::Close() {
    StopScroll();
    const auto windows = AllWindows();
    for (HWND w : windows) PostMessageW(w, WM_CLOSE, 0, 0);
    return !windows.empty();
}

bool Browser::Keys(std::initializer_list<WORD> chord) {
    if (!Focus()) return false;
    Input::Chord(chord);
    return true;
}

// ------------------------------------------------------------------ вкладки
namespace {
    struct Tab {
        Com<IUIAutomationElement> element;
        std::string title;
    };

    // Вкладки браузера — элементы «вкладка» в его окне. Вкладки внутри самих сайтов (например, фильтры YouTube)
    // отбрасываем: они лежат внутри страницы, а настоящие вкладки — снаружи неё.
    std::vector<std::unique_ptr<Tab>> ReadTabs(IUIAutomation* automation, HWND window) {
        std::vector<std::unique_ptr<Tab>> tabs;
        Com<IUIAutomationElement> root;
        if (FAILED(automation->ElementFromHandle(window, &root)) || !root) return tabs;

        auto condition = [&](int controlType) {
            VARIANT v;
            v.vt = VT_I4;
            v.lVal = controlType;
            IUIAutomationCondition* c = nullptr;
            automation->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &c);
            return c;
        };

        RECT page{};
        bool hasPage = false;
        {
            Com<IUIAutomationCondition> isDocument;
            isDocument.p = condition(UIA_DocumentControlTypeId);
            Com<IUIAutomationElement> document;
            if (SUCCEEDED(root->FindFirst(TreeScope_Descendants, isDocument.p, &document)) && document)
                hasPage = SUCCEEDED(document->get_CurrentBoundingRectangle(&page));
        }

        Com<IUIAutomationCondition> isTab;
        isTab.p = condition(UIA_TabItemControlTypeId);
        Com<IUIAutomationElementArray> found;
        if (FAILED(root->FindAll(TreeScope_Descendants, isTab.p, &found)) || !found) return tabs;
        int count = 0;
        found->get_Length(&count);
        for (int i = 0; i < count; ++i) {
            auto tab = std::make_unique<Tab>();
            if (FAILED(found->GetElement(i, &tab->element)) || !tab->element) continue;
            RECT r{};
            tab->element->get_CurrentBoundingRectangle(&r);
            const POINT center{ (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
            if (hasPage && PtInRect(&page, center)) continue; // «вкладка» на самой странице
            BSTR name = nullptr;
            if (SUCCEEDED(tab->element->get_CurrentName(&name)) && name) {
                tab->title = Text::Trim(Text::ToUtf8(name));
                SysFreeString(name);
            }
            if (!tab->title.empty()) tabs.push_back(std::move(tab));
        }
        return tabs;
    }

    bool CreateAutomation(Com<IUIAutomation>& automation) {
        return SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                   reinterpret_cast<void**>(&automation))) && automation;
    }
}

std::vector<std::string> Browser::Tabs() {
    ComScope com;
    std::vector<std::string> titles;
    const HWND window = Window(true);
    Com<IUIAutomation> automation;
    if (!window || !CreateAutomation(automation)) return titles;
    for (const auto& tab : ReadTabs(automation.p, window)) titles.push_back(tab->title);
    return titles;
}

bool Browser::SwitchToTab(const std::string& spokenTitle, std::string& title) {
    ComScope com;
    const HWND window = Window(true);
    Com<IUIAutomation> automation;
    if (!window || !CreateAutomation(automation)) return false;

    const auto tabs = ReadTabs(automation.p, window);
    const Tab* best = nullptr;
    float bestScore = 0.0f;
    for (const auto& tab : tabs) {
        const float score = Fuzzy::TitleMatch(spokenTitle, tab->title);
        if (score > bestScore) { bestScore = score; best = tab.get(); }
    }
    if (!best || bestScore < 0.6f) return false;

    Focus();
    // выбрать вкладку штатно, а если браузер не умеет — кликнуть по ней
    Com<IUIAutomationSelectionItemPattern> select;
    const bool selected = SUCCEEDED(best->element->GetCurrentPatternAs(UIA_SelectionItemPatternId, __uuidof(IUIAutomationSelectionItemPattern),
                              reinterpret_cast<void**>(&select))) && select && SUCCEEDED(select->Select());
    if (!selected) {
        RECT r{};
        best->element->get_CurrentBoundingRectangle(&r);
        SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
        INPUT click[2] = {};
        click[0].type = click[1].type = INPUT_MOUSE;
        click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, click, sizeof(INPUT));
    }
    title = best->title;
    return true;
}

// ------------------------------------------------------------------ прокрутка
bool Browser::StartScroll(int direction) {
    if (!Focus()) return false;
    const HWND window = Window(false);
    AimCursorAt(window);
    g_scrollSpeed = kDefaultSpeed;
    g_scrollDirection = direction > 0 ? 1 : -1;
    std::thread(ScrollLoop, ++g_scrollSession, window).detach();
    return true;
}

bool Browser::StopScroll() {
    const bool wasScrolling = g_scrollDirection.exchange(0) != 0;
    ++g_scrollSession;
    return wasScrolling;
}

bool Browser::IsScrolling() { return g_scrollDirection != 0; }

void Browser::ChangeScrollSpeed(float factor) {
    g_scrollSpeed = std::clamp(g_scrollSpeed.load() * factor, 4.0f, 120.0f);
}
