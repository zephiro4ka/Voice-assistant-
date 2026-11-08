#include "Pc.h"

#include "Com.h"
#include "Input.h"
#include "TextUtils.h"

#include "Fuzzy.h"
#include "Log.h"

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cmath>
#include <vector>

#pragma comment(lib, "version.lib")

namespace {
    // Громкость колонок по умолчанию (Core Audio)
    bool WithSpeakers(const auto& action) {
        ComScope com;
        Com<IMMDeviceEnumerator> devices;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                reinterpret_cast<void**>(&devices))) || !devices)
            return false;
        Com<IMMDevice> speakers;
        if (FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &speakers)) || !speakers) return false;
        Com<IAudioEndpointVolume> volume;
        if (FAILED(speakers->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&volume))) || !volume)
            return false;
        return action(volume.p);
    }

    // Полный путь к exe процесса
    std::wstring ProcessPath(DWORD pid) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return {};
        wchar_t path[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
        CloseHandle(process);
        return ok ? path : L"";
    }

    // «Описание файла» из свойств exe — так программа называет себя: «Яндекс Музыка», «Discord»
    std::wstring FileDescription(const std::wstring& path) {
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        if (size == 0) return {};
        std::vector<BYTE> data(size);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
        struct Translation { WORD language, codepage; }* translation = nullptr;
        UINT length = 0;
        if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translation), &length) || length < sizeof(Translation))
            return {};
        wchar_t key[64];
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", translation->language, translation->codepage);
        wchar_t* text = nullptr;
        if (!VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&text), &length) || !text) return {};
        return text;
    }

    ULONGLONG ToUll(const FILETIME& t) { return (static_cast<ULONGLONG>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }

    const char* Plural(int n, const char* one, const char* few, const char* many) {
        const int r100 = n % 100, r10 = n % 10;
        if (r100 >= 11 && r100 <= 14) return many;
        return r10 == 1 ? one : (r10 >= 2 && r10 <= 4) ? few : many;
    }
}

std::optional<int> Pc::Volume() {
    float level = 0.0f;
    if (!WithSpeakers([&](IAudioEndpointVolume* v) { return SUCCEEDED(v->GetMasterVolumeLevelScalar(&level)); })) return std::nullopt;
    return static_cast<int>(std::lround(level * 100.0f));
}

bool Pc::SetVolume(int percent) {
    const float level = std::clamp(percent, 0, 100) / 100.0f;
    return WithSpeakers([&](IAudioEndpointVolume* v) {
        v->SetMute(FALSE, nullptr);
        return SUCCEEDED(v->SetMasterVolumeLevelScalar(level, nullptr));
    });
}

Pc::AppVolume Pc::ChangeAppVolume(const std::string& spokenApp, int delta, int set, int mute) {
    ComScope com;
    Com<IMMDeviceEnumerator> devices;
    Com<IMMDevice> speakers;
    Com<IAudioSessionManager2> manager;
    Com<IAudioSessionEnumerator> sessions;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&devices))) ||
        FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &speakers)) ||
        FAILED(speakers->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&manager))) ||
        FAILED(manager->GetSessionEnumerator(&sessions)))
        return {};

    // у приложения бывает несколько звуковых сессий (Discord, браузер) — меняем все лучшие
    struct Candidate { ISimpleAudioVolume* volume; std::string name; float score; };
    std::vector<Candidate> found;
    int count = 0;
    sessions->GetCount(&count);
    for (int i = 0; i < count; ++i) {
        Com<IAudioSessionControl> control;
        Com<IAudioSessionControl2> control2;
        if (FAILED(sessions->GetSession(i, &control)) ||
            FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&control2))))
            continue;
        DWORD pid = 0;
        if (FAILED(control2->GetProcessId(&pid)) || pid == 0 || control2->IsSystemSoundsSession() == S_OK) continue;
        const std::wstring path = ProcessPath(pid);
        if (path.empty()) continue;
        std::wstring exe = path.substr(path.find_last_of(L'\\') + 1);
        exe = exe.substr(0, exe.find_last_of(L'.'));
        const std::wstring description = FileDescription(path);
        const std::string name = Text::ToUtf8(description.empty() ? exe : description);
        const float score = std::max(Fuzzy::TitleMatch(spokenApp, name), Fuzzy::TitleMatch(spokenApp, Text::ToUtf8(exe)));
        ISimpleAudioVolume* volume = nullptr;
        if (score < 0.6f || FAILED(control->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&volume)))) continue;
        found.push_back({ volume, name, score });
    }

    AppVolume result;
    float best = 0.0f;
    for (const auto& c : found) best = std::max(best, c.score);
    for (const auto& c : found) {
        if (c.score >= best - 0.05f) {
            float level = 0.0f;
            c.volume->GetMasterVolume(&level);
            int percent = static_cast<int>(std::lround(level * 100.0f));
            percent = std::clamp(set >= 0 ? set : percent + delta, 0, 100);
            c.volume->SetMasterVolume(percent / 100.0f, nullptr);
            if (mute >= 0) c.volume->SetMute(mute ? TRUE : FALSE, nullptr);
            else if (delta > 0 || set > 0) c.volume->SetMute(FALSE, nullptr); // прибавили — значит, хотят слышать
            BOOL muted = FALSE;
            c.volume->GetMute(&muted);
            result = { true, c.name, percent, muted != FALSE };
        }
        c.volume->Release();
    }
    Log::Write("громкость приложения «" + spokenApp + "»: " + (result.found ? result.name + " → " + std::to_string(result.percent) + "%" : "не нашла"));
    return result;
}

bool Pc::IsMuted() {
    BOOL muted = FALSE;
    WithSpeakers([&](IAudioEndpointVolume* v) { return SUCCEEDED(v->GetMute(&muted)); });
    return muted != FALSE;
}

bool Pc::SetMute(bool mute) {
    return WithSpeakers([&](IAudioEndpointVolume* v) { return SUCCEEDED(v->SetMute(mute ? TRUE : FALSE, nullptr)); });
}

std::string Pc::BatteryReport() {
    SYSTEM_POWER_STATUS power{};
    if (!GetSystemPowerStatus(&power) || power.BatteryFlag == 128 || power.BatteryLifePercent == 255)
        return "У этого компьютера нет батареи — он работает от сети";
    const int percent = power.BatteryLifePercent;
    std::string answer = "Заряд " + std::to_string(percent) + "%";
    if (power.ACLineStatus == 1) {
        answer += percent >= 100 ? ", батарея полная" : ", заряжается";
    } else if (power.BatteryLifeTime != static_cast<DWORD>(-1)) {
        const int minutes = static_cast<int>(power.BatteryLifeTime / 60);
        const int hours = minutes / 60;
        answer += ", хватит примерно на ";
        if (hours > 0) answer += std::to_string(hours) + " " + Plural(hours, "час", "часа", "часов") + " ";
        answer += std::to_string(minutes % 60) + " " + Plural(minutes % 60, "минуту", "минуты", "минут");
    }
    if (power.ACLineStatus != 1 && percent <= 20) answer += ". Пора подключить зарядку!";
    return answer;
}

std::string Pc::LoadReport() {
    // загрузка процессора — разница времени простоя за 300 мс
    FILETIME idle1, kernel1, user1, idle2, kernel2, user2;
    GetSystemTimes(&idle1, &kernel1, &user1);
    Sleep(300);
    GetSystemTimes(&idle2, &kernel2, &user2);
    const double idle = static_cast<double>(ToUll(idle2) - ToUll(idle1));
    const double total = static_cast<double>((ToUll(kernel2) - ToUll(kernel1)) + (ToUll(user2) - ToUll(user1)));
    const int cpu = total > 0 ? static_cast<int>(std::lround((1.0 - idle / total) * 100.0)) : 0;

    MEMORYSTATUSEX memory{ sizeof(memory) };
    GlobalMemoryStatusEx(&memory);
    const double gb = 1024.0 * 1024.0 * 1024.0;
    const int totalGb = static_cast<int>(std::lround(memory.ullTotalPhys / gb));
    const int usedGb = static_cast<int>(std::lround((memory.ullTotalPhys - memory.ullAvailPhys) / gb));

    std::string answer = "Процессор загружен на " + std::to_string(cpu) + "%, памяти занято " + std::to_string(usedGb) + " из " +
                         std::to_string(totalGb) + " " + Plural(totalGb, "гигабайта", "гигабайт", "гигабайт");
    if (cpu >= 85 || memory.dwMemoryLoad >= 90) answer += ". Компьютеру тяжеловато — может, закрыть что-нибудь лишнее?";
    else answer += ". Всё спокойно";
    return answer;
}

std::string Pc::Clipboard() {
    if (!OpenClipboard(nullptr)) return {};
    std::string text;
    if (const HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* chars = static_cast<const wchar_t*>(GlobalLock(data))) {
            text = Text::ToUtf8(chars);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

std::string Pc::CopySelection() {
    const DWORD before = GetClipboardSequenceNumber();
    Input::Chord({ VK_CONTROL, 'C' });
    for (int i = 0; i < 20 && GetClipboardSequenceNumber() == before; ++i) Sleep(25); // ждём, пока программа скопирует
    return GetClipboardSequenceNumber() == before ? std::string() : Clipboard();
}
