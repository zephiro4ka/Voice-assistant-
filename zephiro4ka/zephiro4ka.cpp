// =====================================================================
//  Аня — голосовой помощник
//
//  Здесь только окно и главный цикл. Всё остальное — в папке app/:
//    Ui        — экран: шапка, сфера, поле ввода, история, настройки
//    WeatherWidget — погода слева: 4 часа, при наведении — весь день
//    SettingsPage — настройки на всё окно: разделы, поиск, карточки
//    DevProbe  — проверка интерфейса по сценарию и замер кадров ([debug] в anya.ini)
//    SystemMonitor — мини-диспетчер задач: загрузка и температура железа
//    MediaNow  — что сейчас играет (любой плеер Windows) и кнопки управления
//    HomeWidgets — карточки главного экрана: система и музыка
//    Telegram  — бот: управление с телефона, снимки и решение задач с экрана
//    PowerMode — «Аня на связи»: экран выключен, а бот работает
//    Screen    — снимок экрана в JPEG
//    DiscordRpc — статус в Discord
//    Autostart — запуск вместе с Windows
//    Scenarios — сценарии «фраза → блоки действий»;  ScenariosPage — их редактор
//    Wallpaper — свои обои и цвета по ним;  Toast — вопрос «Да / Нет» внизу экрана
//    ProcessMonitor — куда уходит память, процессы и службы, очистка ОЗУ;  ProcessesPage — вкладка «Процессы»
//    Widgets   — свои кнопки, иконки, пузыри чата
//    Visuals   — живая сфера, фон, градиенты
//    Theme     — шрифты, цвета, палитры настроений
//    Graphics  — DirectX 11
//    Ears      — микрофон → детектор речи → whisper
//    Voice     — голос: Silero (по умолчанию), Supertonic или Windows
//    SileroVoice — голос Silero через скрытый сервер на Python
//    Sidecar   — фоновые помощники, которые закрываются вместе с Аней
//    Http      — запросы к локальным помощникам и в интернет
//    Brain     — куда отправить фразу: навыки → болтовня → ИИ
//    Skills    — всё, что Аня делает сама: медиа, окна, диктовка, таймеры, счёт
//    Chatter   — живое общение без ИИ: «как дела», шутки, поддержка
//    Launcher  — «открой стим» / «закрой дискорд»
//    Messenger — «зайди в чат с Зефирочником» (Discord)
//    ScreenLinks — «открой видео …»: нажать ссылку, видимую в браузере
//    Browser   — Edge: вкладки, плавная прокрутка, скрыть/показать
//    Pc        — громкость, батарея, загрузка компьютера, буфер обмена
//    Fuzzy     — нечёткое сравнение услышанного с названиями
//    Weather   — погода: сейчас, в колледж, на день и завтра, когда будет дождь (фоновое обновление)
//    Input     — печать текста и нажатия клавиш
//    Log       — журнал anya.log: что услышала и что сделала
//    LocalAi   — нейросеть из файла .gguf: скрыто запускает llama.cpp на видеокарте
//    Settings / State / TextUtils / Json — общие вещи
// =====================================================================
#include "app/Platform.h"

#include "app/Brain.h"
#include "app/Ears.h"
#include "app/Graphics.h"
#include "app/Launcher.h"
#include "app/LocalAi.h"
#include "app/Settings.h"
#include "app/TextUtils.h"
#include "app/State.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "app/Voice.h"
#include "app/Autostart.h"
#include "app/DevProbe.h"
#include "app/DiscordRpc.h"
#include "app/MediaNow.h"
#include "app/PowerMode.h"
#include "app/ProcessMonitor.h"
#include "app/SystemMonitor.h"
#include "app/Telegram.h"
#include "app/Weather.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"
#include "Resource.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <wtsapi32.h>

#pragma comment(lib, "wtsapi32.lib")

#include <algorithm>
#include <string>
#include <thread>

#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
    // ---------------------------------------------------------------- трей: «Сворачивать в трей» в настройках
    constexpr UINT kTrayMessage = WM_APP + 7;
    constexpr UINT kStandbyMessage = WM_APP + 8; // ярлык «Выключить (Аня на связи)» → уже запущенной Ане
    constexpr UINT kShowMessage = WM_APP + 9;    // Аню запустили второй раз — показать первую
    constexpr UINT kWakeMessage = WM_APP + 10;   // «Anya.exe --wake» — включить экран и звук обратно
    bool g_trayIcon = false;
    bool g_quitting = false; // «Выход» из меню трея — закрыться по-настоящему

    void TrayIcon(HWND window, bool show) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = window;
        nid.uID = 1;
        if (!show) {
            if (g_trayIcon) Shell_NotifyIconW(NIM_DELETE, &nid);
            g_trayIcon = false;
            return;
        }
        if (g_trayIcon) return;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = kTrayMessage;
        nid.hIcon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_SMALL, 0));
        wcsncpy_s(nid.szTip, (Text::ToWide(Config::Get().assistant) + L" — нажми, чтобы открыть").c_str(), _TRUNCATE);
        g_trayIcon = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    }

    void RestoreFromTray(HWND window) {
        ShowWindow(window, SW_SHOW);
        if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
    }

    void TrayMenu(HWND window) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, (L"Открыть " + Text::ToWide(Text::NameForm(Config::Get().assistant, 3))).c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 2, L"Выход");
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(window); // иначе меню не закроется кликом мимо
        const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (choice == 1) RestoreFromTray(window);
        if (choice == 2) { g_quitting = true; DestroyWindow(window); }
    }

    float WindowDpi(HWND window) { return GetDpiForWindow(window) / 96.0f; }

    // Тёмная тема, скруглённые углы и тень Windows 11 для окна без системной рамки
    void StyleWindow(HWND window) {
        const BOOL dark = TRUE;
        const DWORD rounded = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(window, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        DwmSetWindowAttribute(window, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &rounded, sizeof(rounded));
        const MARGINS glass = { -1, -1, -1, -1 }; // всё окно — «стекло»: под прозрачными местами Windows рисует акрил/Mica
        DwmExtendFrameIntoClientArea(window, &glass);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }

    // Иконка окна точно под масштаб экрана: панель задач, Alt+Tab и заголовок берут её отсюда
    void SetWindowIcons(HWND window, HINSTANCE instance) {
        const UINT dpi = GetDpiForWindow(window);
        const int bigSize = GetSystemMetricsForDpi(SM_CXICON, dpi);     // ("small" в заголовках Windows — макрос)
        const int smallSize = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
        const auto load = [&](int size) {
            return reinterpret_cast<LPARAM>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_ZEPHIRO4KA), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
        };
        SendMessageW(window, WM_SETICON, ICON_BIG, load(bigSize));
        SendMessageW(window, WM_SETICON, ICON_SMALL, load(smallSize));
    }

    LRESULT HitTest(HWND window, LPARAM lParam) {
        POINT pt = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        ScreenToClient(window, &pt);
        RECT rc;
        GetClientRect(window, &rc);

        if (!IsZoomed(window)) {
            const int border = static_cast<int>(6 * Theme::dpi);
            const bool left = pt.x < border, right = pt.x >= rc.right - border;
            const bool top = pt.y < border, bottom = pt.y >= rc.bottom - border;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        // шапку можно тянуть везде, кроме меню по центру и кнопок справа
        if (Ui::IsCaption(static_cast<float>(pt.x), static_cast<float>(pt.y))) return HTCAPTION;
        return HTCLIENT;
    }

    LRESULT WINAPI WndProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_NCCALCSIZE:
            if (wParam == TRUE) {
                // своя рамка; развёрнутое окно не должно вылезать за край экрана
                if (IsZoomed(window)) {
                    const UINT dpi = GetDpiForWindow(window);
                    const int frameX = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    const int frameY = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    RECT& r = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0];
                    r.left += frameX;
                    r.right -= frameX;
                    r.top += frameY;
                    r.bottom -= frameY;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST:
            return HitTest(window, lParam);
        case WM_CLOSE:
            if (Config::Get().tray && !g_quitting) { // крестик — в трей, Аня продолжает слушать
                TrayIcon(window, true);
                ShowWindow(window, SW_HIDE);
                return 0;
            }
            break;
        case WM_WTSSESSION_CHANGE: // Win+L, блокировка из телеграма, вход обратно
            if (wParam == WTS_SESSION_LOCK) Telegram::OnLockChanged(true);
            if (wParam == WTS_SESSION_UNLOCK) Telegram::OnLockChanged(false);
            return 0;
        case kStandbyMessage:
            PowerMode::Standby();
            return 0;
        case kShowMessage:
            RestoreFromTray(window);
            return 0;
        case kWakeMessage:
            PowerMode::Wake();
            return 0;
        case kTrayMessage:
            if (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_LBUTTONDBLCLK) RestoreFromTray(window);
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) TrayMenu(window);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = { static_cast<LONG>(640 * Theme::dpi), static_cast<LONG>(560 * Theme::dpi) };
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_F8 && !(lParam & (1LL << 30))) {
                g_app.micEnabled = !g_app.micEnabled;
                Caption::Set(g_app.micEnabled ? "микрофон снова включён" : "микрофон выключен (F8 — включить)");
                return 0;
            }
            break;
        }

        if (ImGui_ImplWin32_WndProcHandler(window, msg, wParam, lParam)) return TRUE;

        switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) Graphics::RequestResize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // Alt не открывает системное меню
            break;
        case WM_DESTROY:
            WTSUnRegisterSessionNotification(window);
            TrayIcon(window, false);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, msg, wParam, lParam);
    }

    HWND CreateMainWindow(HINSTANCE instance, WNDCLASSEXW& wc) {
        wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance,
               LoadIconW(instance, MAKEINTRESOURCEW(IDI_ZEPHIRO4KA)), LoadCursorW(nullptr, IDC_ARROW),
               nullptr, nullptr, L"AnyaWindow", LoadIconW(instance, MAKEINTRESOURCEW(IDI_SMALL)) };
        RegisterClassExW(&wc);

        const float scale = GetDpiForSystem() / 96.0f;
        const int width = static_cast<int>(1180 * scale), height = static_cast<int>(800 * scale);
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int x = work.left + (work.right - work.left - width) / 2;
        const int y = work.top + (work.bottom - work.top - height) / 2;
        // без «перенаправления» — кадр идёт через DirectComposition с прозрачностью
        // без WS_SYSMENU: иначе Windows рисует свои маленькие «свернуть/развернуть/закрыть» за нашими (рамка-«стекло» на всё окно)
        return CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, Text::ToWide(Config::Get().assistant).c_str(), WS_OVERLAPPEDWINDOW & ~WS_SYSMENU, x, y, width, height,
            nullptr, nullptr, instance, nullptr);
    }

    // Обработать сообщения Windows. false — пора выходить.
    bool PumpMessages() {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) return false;
        }
        return true;
    }
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring args = commandLine ? commandLine : L"";
    const bool autostart = args.find(Autostart::kFlag) != std::wstring::npos;
    if (args.find(L"--clean-memory") != std::wstring::npos) return ProcessMonitor::CleanAsAdmin(); // окно UAC → только очистка ОЗУ

    // Аня уже запущена: ярлык «Выключить (Аня на связи)» передаёт ей команду, обычный запуск — показывает её окно
    if (const HWND running = FindWindowW(L"AnyaWindow", nullptr)) {
        const UINT message = args.find(L"--standby") != std::wstring::npos ? kStandbyMessage
                           : args.find(L"--wake") != std::wstring::npos ? kWakeMessage : kShowMessage;
        PostMessageW(running, message, 0, 0);
        return 0;
    }
    Config::Load();

    WNDCLASSEXW wc{};
    const HWND window = CreateMainWindow(instance, wc);
    if (!window || !Graphics::Create(window)) {
        Graphics::Destroy();
        UnregisterClassW(wc.lpszClassName, instance);
        return 1;
    }
    StyleWindow(window);
    SetWindowIcons(window, instance);
    WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION); // узнавать о блокировке Windows
    if (autostart) {
        TrayIcon(window, true); // вместе с Windows — тихо, в трее
    } else {
        ShowWindow(window, showCommand);
        UpdateWindow(window);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    Theme::Init(WindowDpi(window));
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(Graphics::Device(), Graphics::Context());
    Ui::Init(window);

    Voice::Start();
    Brain::Start();
    Ears::Start();
    Launcher::Start();
    Weather::Start(); // прогноз качается в фоне — для виджета и мгновенных ответов
    SystemMonitor::Start();
    MediaNow::Start();
    Telegram::Start();
    DiscordRpc::Start();
    Ai::Start(); // нейросеть грузится в фоне, Аня уже отвечает
    DevProbe::Start();

    LARGE_INTEGER frequency, lastFrame;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&lastFrame);
    auto msSince = [&](const LARGE_INTEGER& from) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (now.QuadPart - from.QuadPart) * 1000.0 / frequency.QuadPart;
    };

    while (PumpMessages()) {
        if (IsIconic(window) || !IsWindowVisible(window)) { // свёрнуто или в трее — не рисуем
            Sleep(30);
            continue;
        }
        // окно не в фокусе и Аня ничего не делает — фон замер, хватает ~20 кадров в секунду: видеокарта отдыхает
        const bool active = GetForegroundWindow() == window;
        const bool busy = g_app.speaking || g_app.thinking || g_app.transcribing || g_app.capturing || g_app.pttArmed;
        if (!active && !busy && !Config::Get().animateInactive) Sleep(35);

        LARGE_INTEGER frameStart;
        QueryPerformanceCounter(&frameStart);
        const double interval = msSince(lastFrame);
        lastFrame = frameStart;

        Graphics::ApplyPendingResize();
        DevProbe::BeforeFrame();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Ui::Frame(std::min(ImGui::GetIO().DeltaTime, 0.05f), active);
        ImGui::Render();

        Graphics::Clear(0.03f, 0.02f, 0.06f, Graphics::Transparent() ? 0.0f : 1.0f);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        DevProbe::AfterRender();
        DevProbe::FrameTime(msSince(frameStart), interval);
        Graphics::Present();
    }

    g_app.running = false;
    Voice::Stop();
    Ears::Shutdown();
    Voice::Shutdown();
    Brain::Shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    Graphics::Destroy();
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, instance);
    ExitProcess(0); // фоновые запросы к ИИ не должны задерживать выход
}
