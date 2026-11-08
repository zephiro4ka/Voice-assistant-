#include "PowerMode.h"

#include "Log.h"
#include "MediaNow.h"
#include "Pc.h"
#include "Platform.h"
#include "Settings.h"

#include <atomic>
#include <string>
#include <thread>

namespace {
    std::atomic<bool> g_standby{ false };
    bool g_wasMuted = false;
    bool g_wasPlaying = false;

    void RunHidden(const std::wstring& command) {
        STARTUPINFOW si{ sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::wstring line = command;
        if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    void MonitorsOff() { PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2); }

    // Шевельнуть мышь туда-обратно. На заблокированном компьютере Windows такой ввод не пропускает —
    // поэтому это лишь один из трёх способов в WakeDisplay
    void Nudge() {
        INPUT move{};
        move.type = INPUT_MOUSE;
        move.mi.dwFlags = MOUSEEVENTF_MOVE;
        move.mi.dx = 3;
        SendInput(1, &move, sizeof(INPUT));
        Sleep(40);
        move.mi.dx = -3;
        SendInput(1, &move, sizeof(INPUT));
    }

    // Включить экран — тремя способами сразу (проверено: каждый включает погасший экран, но не каждый работает везде),
    // и подержать его включённым 30 секунд, чтобы экран блокировки не погас, пока вводишь пароль
    void WakeDisplay() {
        SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
        SendMessageTimeoutW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, static_cast<LPARAM>(-1), SMTO_ABORTIFHUNG, 500, nullptr);
        Nudge();
        std::thread([] {
            SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
            for (int i = 0; i < 30 && !g_standby.load(); ++i) Sleep(1000);
            SetThreadExecutionState(ES_CONTINUOUS);
        }).detach();
    }
}

void PowerMode::Standby() {
    if (g_standby.exchange(true)) { MonitorsOff(); return; }
    Log::Write("питание: «Аня на связи» — экран выключен, компьютер не спит");
    const auto track = MediaNow::Snapshot();
    g_wasPlaying = track.playing;
    if (g_wasPlaying) MediaNow::Control(MediaNow::Action::PlayPause);
    g_wasMuted = Pc::IsMuted();
    Pc::SetMute(true);
    // экран блокировки с телефона не снять (Windows не пускает программы на защищённый рабочий стол),
    // поэтому блокируем, только если так выбрано в настройках
    if (Config::Value("telegram", "lock_on_standby", "0") == "1") LockWorkStation();

    // поток держит компьютер бодрым (сеть и бот работают), пока режим не выключат; экран гасим чуть позже блокировки
    std::thread([] {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
        Sleep(1500);
        MonitorsOff();
        for (int tick = 0; g_standby.load(); ++tick) {
            Sleep(1000);
            if (tick % 60 == 59) MonitorsOff(); // кто-то задел мышь — снова гасим
        }
        SetThreadExecutionState(ES_CONTINUOUS);
    }).detach();
}

void PowerMode::Wake() {
    const bool was = g_standby.exchange(false);
    WakeDisplay();
    Log::Write(std::string("питание: включаю экран") + (IsLocked() ? " (компьютер заблокирован)" : ""));
    if (!was) return;
    Log::Write("питание: проснулась");
    if (!g_wasMuted) Pc::SetMute(false);
    if (g_wasPlaying) MediaNow::Control(MediaNow::Action::PlayPause);
}

bool PowerMode::IsStandby() { return g_standby.load(); }

void PowerMode::Shutdown(int delay) { RunHidden(L"shutdown.exe /s /t " + std::to_wstring(delay) + L" /c \"Аня выключает компьютер. Отмена: shutdown /a\""); }
void PowerMode::Restart(int delay) { RunHidden(L"shutdown.exe /r /t " + std::to_wstring(delay)); }
void PowerMode::CancelShutdown() { RunHidden(L"shutdown.exe /a"); }
void PowerMode::Lock() { LockWorkStation(); }

namespace { std::atomic<bool> g_locked{ false }; }
void PowerMode::SetLocked(bool locked) { g_locked = locked; }
bool PowerMode::IsLocked() { return g_locked.load(); }
