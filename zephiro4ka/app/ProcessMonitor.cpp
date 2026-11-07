#include "ProcessMonitor.h"
#include "Settings.h"

#include "Platform.h"
#include "TextUtils.h"

#include <pdh.h>
#include <psapi.h>
#include <shellapi.h>
#include <winternl.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "version.lib")

namespace {
    // Полная запись о процессе из NtQuerySystemInformation: память и время ЦП всех процессов — без прав администратора
    struct ProcessEntry {
        ULONG NextEntryOffset, NumberOfThreads;
        LARGE_INTEGER WorkingSetPrivateSize;
        ULONG HardFaultCount, NumberOfThreadsHighWatermark;
        ULONGLONG CycleTime;
        LARGE_INTEGER CreateTime, UserTime, KernelTime;
        UNICODE_STRING ImageName;
        LONG BasePriority;
        HANDLE UniqueProcessId, InheritedFromUniqueProcessId;
        ULONG HandleCount, SessionId;
        ULONG_PTR UniqueProcessKey;
        SIZE_T PeakVirtualSize, VirtualSize;
        ULONG PageFaultCount;
        SIZE_T PeakWorkingSetSize, WorkingSetSize;
    };

    std::mutex g_mutex;
    ProcessMonitor::Snapshot g_snapshot;
    std::atomic<bool> g_active{ false };
    std::unordered_map<uint32_t, uint64_t> g_lastCpu; // pid → время ЦП прошлого замера (100 нс)
    ULONGLONG g_lastTick = 0;

    // Системные процессы Windows — не трогаем и считаем «системой»
    // Счётчики Windows о памяти — точнее, чем «доступно»: свободно, кэш ожидания, память ядра (без прав администратора)
    struct MemoryCounters {
        PDH_HQUERY query = nullptr;
        PDH_HCOUNTER free = nullptr, standby[3] = {}, paged = nullptr, nonpaged = nullptr, modified = nullptr;
        bool ok = false;
        void Open() {
            if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return;
            ok = PdhAddEnglishCounterW(query, L"\\Memory\\Free & Zero Page List Bytes", 0, &free) == ERROR_SUCCESS;
            PdhAddEnglishCounterW(query, L"\\Memory\\Standby Cache Normal Priority Bytes", 0, &standby[0]);
            PdhAddEnglishCounterW(query, L"\\Memory\\Standby Cache Reserve Bytes", 0, &standby[1]);
            PdhAddEnglishCounterW(query, L"\\Memory\\Standby Cache Core Bytes", 0, &standby[2]);
            PdhAddEnglishCounterW(query, L"\\Memory\\Pool Paged Resident Bytes", 0, &paged);
            PdhAddEnglishCounterW(query, L"\\Memory\\Pool Nonpaged Bytes", 0, &nonpaged);
            PdhAddEnglishCounterW(query, L"\\Memory\\Modified Page List Bytes", 0, &modified);
        }
        static uint64_t Value(PDH_HCOUNTER counter) {
            PDH_FMT_COUNTERVALUE v{};
            return counter && PdhGetFormattedCounterValue(counter, PDH_FMT_LARGE, nullptr, &v) == ERROR_SUCCESS && v.CStatus == ERROR_SUCCESS
                ? static_cast<uint64_t>(v.largeValue) : 0;
        }
    };

    // Кто держит память ядра: метки выделений (как poolmon). Понятные подписи — для самых частых
    std::vector<ProcessMonitor::PoolTag> PoolTags() {
        struct Tag { char tag[4]; ULONG pagedAllocs, pagedFrees; SIZE_T pagedUsed; ULONG nonPagedAllocs, nonPagedFrees; SIZE_T nonPagedUsed; };
        std::vector<BYTE> buffer(1 << 18);
        ULONG needed = 0;
        NTSTATUS status;
        while ((status = NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(22) /* SystemPoolTagInformation */, buffer.data(),
                    static_cast<ULONG>(buffer.size()), &needed)) == static_cast<NTSTATUS>(0xC0000004L))
            buffer.resize(needed + 4096);
        std::vector<ProcessMonitor::PoolTag> out;
        if (status < 0) return out;
        static const std::unordered_map<std::string, const char*> kKnown = {
            { "MmSt", "отображённые файлы (служебные данные)" }, { "MmCa", "кэш файловой системы" }, { "File", "открытые файлы" },
            { "Ntff", "файловая система NTFS" }, { "NtFs", "файловая система NTFS" }, { "Ntfx", "файловая система NTFS" },
            { "FMfn", "фильтры файловой системы (антивирус и др.)" }, { "Toke", "права доступа процессов" }, { "Proc", "описания процессов" },
            { "Thre", "описания потоков" }, { "Even", "события" }, { "smNp", "сжатие памяти" }, { "SmMs", "сжатие памяти" },
            { "Ndis", "сеть" }, { "NDnd", "сеть" }, { "Tcp ", "сеть (TCP)" }, { "Wdf0", "драйверы (WDF)" }, { "WdfL", "драйверы (WDF)" },
            { "DxgK", "видеодрайвер (DirectX)" }, { "VidL", "видеодрайвер" }, { "Nv  ", "видеодрайвер NVIDIA" }, { "NVRM", "видеодрайвер NVIDIA" },
            { "Vi54", "видеопамять (драйвер)" }, { "CM31", "реестр" }, { "CM25", "реестр" }, { "CM16", "реестр" }, { "ObNm", "имена объектов" },
            { "MmRe", "управление памятью" }, { "MmPb", "управление памятью" }, { "ConT", "консоль" }, { "Usbp", "USB" }, { "HidP", "клавиатура и мышь" },
        };
        const ULONG count = *reinterpret_cast<const ULONG*>(buffer.data());
        const auto* tags = reinterpret_cast<const Tag*>(buffer.data() + sizeof(ULONG_PTR));
        for (ULONG i = 0; i < count; ++i) {
            const std::string tag(tags[i].tag, 4);
            const auto known = kKnown.find(tag);
            const std::string description = known != kKnown.end() ? known->second : "";
            if (tags[i].pagedUsed > (1u << 20)) out.push_back({ tag, description, tags[i].pagedUsed, true });
            if (tags[i].nonPagedUsed > (1u << 20)) out.push_back({ tag, description, tags[i].nonPagedUsed, false });
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.bytes > b.bytes; });
        if (out.size() > 15) out.resize(15);
        return out;
    }

    // [limits] в anya.ini: имя exe = байт
    std::unordered_map<std::string, uint64_t> LoadLimits() {
        std::unordered_map<std::string, uint64_t> limits;
        wchar_t buffer[8192] = {};
        const std::wstring ini = Text::ExecutableDirectory() + L"\\anya.ini";
        if (!GetPrivateProfileSectionW(L"limits", buffer, 8192, ini.c_str())) return limits;
        for (const wchar_t* entry = buffer; *entry; entry += wcslen(entry) + 1) {
            const std::wstring line = entry;
            const size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            const uint64_t bytes = _wcstoui64(line.c_str() + eq + 1, nullptr, 10);
            if (bytes) limits[Text::ToUtf8(Text::LowerRu(line.substr(0, eq)))] = bytes;
        }
        return limits;
    }

    // Жёсткий потолок рабочего набора: лишнее Windows держит на диске (программа медленнее, но памяти ест не больше)
    void ApplyLimit(uint32_t pid, uint64_t bytes) {
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return;
        SetProcessWorkingSetSizeEx(process, 1 << 20, static_cast<SIZE_T>(bytes), QUOTA_LIMITS_HARDWS_MAX_ENABLE | QUOTA_LIMITS_HARDWS_MIN_DISABLE);
        CloseHandle(process);
    }

    bool IsSystemProcess(const std::wstring& lower, uint32_t pid) {
        if (pid <= 4) return true;
        static const std::set<std::wstring> kSystem = { L"registry", L"memory compression", L"smss.exe", L"csrss.exe", L"wininit.exe",
            L"services.exe", L"lsass.exe", L"winlogon.exe", L"fontdrvhost.exe", L"dwm.exe", L"lsaiso.exe", L"secure system" };
        return kSystem.count(lower) > 0;
    }

    std::wstring FileDescription(const std::wstring& path) {
        static std::unordered_map<std::wstring, std::wstring> s_cache;
        if (const auto it = s_cache.find(path); it != s_cache.end()) return it->second;
        std::wstring result;
        DWORD ignored = 0;
        if (const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored)) {
            std::vector<BYTE> data(size);
            struct Translation { WORD language, codepage; }* tr = nullptr;
            UINT length = 0;
            wchar_t* text = nullptr;
            if (GetFileVersionInfoW(path.c_str(), 0, size, data.data()) &&
                VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&tr), &length) && length >= sizeof(Translation)) {
                wchar_t key[64];
                swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr->language, tr->codepage);
                if (VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&text), &length) && text && *text) result = text;
            }
        }
        s_cache[path] = result;
        return result;
    }

    std::wstring ExePath(uint32_t pid) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return {};
        wchar_t path[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
        CloseHandle(process);
        return ok ? path : L"";
    }

    // pid → понятные названия служб («Диспетчер печати», «Служба времени Windows»…)
    std::unordered_map<uint32_t, std::vector<std::string>> ServicesByProcess() {
        std::unordered_map<uint32_t, std::vector<std::string>> map;
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
        if (!manager) return map;
        DWORD needed = 0, count = 0, resume = 0;
        EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, nullptr, 0, &needed, &count, &resume, nullptr);
        std::vector<BYTE> buffer(needed + 4096);
        resume = 0;
        if (EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, buffer.data(), static_cast<DWORD>(buffer.size()),
                &needed, &count, &resume, nullptr)) {
            const auto* services = reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
            for (DWORD i = 0; i < count; ++i)
                map[services[i].ServiceStatusProcess.dwProcessId].push_back(Text::ToUtf8(services[i].lpDisplayName));
        }
        CloseServiceHandle(manager);
        return map;
    }

    void Collect() {
        using namespace ProcessMonitor;
        static std::mutex s_collecting; // замер может прийти из фона, после очистки и при открытии вкладки — по очереди
        std::lock_guard collecting(s_collecting);
        // все процессы одним вызовом
        std::vector<BYTE> buffer(1 << 20);
        ULONG needed = 0;
        NTSTATUS status;
        while ((status = NtQuerySystemInformation(SystemProcessInformation, buffer.data(), static_cast<ULONG>(buffer.size()), &needed)) ==
               static_cast<NTSTATUS>(0xC0000004L)) // STATUS_INFO_LENGTH_MISMATCH
            buffer.resize(needed + (1 << 16));
        if (status < 0) return;

        const ULONGLONG tick = GetTickCount64();
        const double interval = g_lastTick ? (tick - g_lastTick) * 10000.0 : 0.0; // в единицах по 100 нс
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        const auto services = ServicesByProcess();
        const uint32_t self = GetCurrentProcessId();

        const auto limits = LoadLimits();
        std::map<std::wstring, Group> groups; // ключ — exe, у служб — отдельная группа на каждый svchost
        std::unordered_map<uint32_t, uint64_t> cpuNow;
        Breakdown mem;
        int count = 0;
        for (ULONG offset = 0;;) {
            const auto* e = reinterpret_cast<const ProcessEntry*>(buffer.data() + offset);
            const uint32_t pid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(e->UniqueProcessId));
            std::wstring image = e->ImageName.Buffer ? std::wstring(e->ImageName.Buffer, e->ImageName.Length / sizeof(wchar_t)) : L"System Idle";
            if (pid != 0) {
                ++count;
                const std::wstring lower = Text::LowerRu(image);
                const uint64_t cpuTime = static_cast<uint64_t>(e->UserTime.QuadPart + e->KernelTime.QuadPart);
                cpuNow[pid] = cpuTime;
                float cpu = 0;
                if (const auto it = g_lastCpu.find(pid); it != g_lastCpu.end() && interval > 0 && cpuTime >= it->second)
                    cpu = static_cast<float>((cpuTime - it->second) / interval / info.dwNumberOfProcessors * 100.0);

                Process p;
                p.pid = pid;
                p.name = Text::ToUtf8(image);
                p.memory = static_cast<uint64_t>(e->WorkingSetPrivateSize.QuadPart);
                p.shared = e->WorkingSetSize > static_cast<SIZE_T>(p.memory) ? e->WorkingSetSize - static_cast<SIZE_T>(p.memory) : 0;
                mem.sharedTotal += p.shared;
                p.cpu = std::clamp(cpu, 0.0f, 100.0f);
                if (const auto s = services.find(pid); s != services.end()) p.services = s->second;

                if (const auto limit = limits.find(Text::ToUtf8(lower)); limit != limits.end() && pid != self) ApplyLimit(pid, limit->second);

                Kind kind = Kind::App;
                if (IsSystemProcess(lower, pid)) kind = Kind::System;
                else if (!p.services.empty() || e->SessionId == 0) kind = Kind::Service;
                if (lower == L"memory compression") mem.compressed = p.memory = static_cast<uint64_t>(e->WorkingSetSize);

                std::wstring key = lower;
                std::string title;
                if (kind == Kind::Service && lower == L"svchost.exe") { // svchost — по службе внутри, иначе 80 одинаковых «svchost»
                    key += L"#" + std::to_wstring(pid);
                    title = p.services.empty() ? "Служба Windows (svchost)" : p.services.front();
                } else if (kind == Kind::System) {
                    title = p.name + (lower == L"memory compression" ? " — сжатая память" : "");
                } else {
                    const std::wstring path = pid == self ? L"" : ExePath(pid);
                    const std::wstring description = path.empty() ? L"" : FileDescription(path);
                    title = description.empty() ? p.name : Text::ToUtf8(description);
                    if (pid == self) title = Config::Get().assistant;
                }
                Group& g = groups[key];
                if (g.processes.empty()) { g.title = title; g.exe = p.name; g.kind = kind; }
                g.memory += p.memory;
                g.cpu += p.cpu;
                g.processes.push_back(std::move(p));
                (kind == Kind::App ? mem.apps : kind == Kind::Service ? mem.services : mem.system) += g.processes.back().memory;
            }
            if (!e->NextEntryOffset) break;
            offset += e->NextEntryOffset;
        }
        g_lastCpu.swap(cpuNow);
        g_lastTick = tick;

        // общая картина: всего, свободно, кэш ожидания, ядро
        PERFORMANCE_INFORMATION perf{ sizeof(perf) };
        GetPerformanceInfo(&perf, sizeof(perf));
        const uint64_t page = perf.PageSize;
        mem.total = perf.PhysicalTotal * page;
        mem.available = perf.PhysicalAvailable * page;
        static MemoryCounters s_counters;
        if (!s_counters.query) s_counters.Open();
        if (s_counters.ok && PdhCollectQueryData(s_counters.query) == ERROR_SUCCESS) {
            mem.free = std::min(MemoryCounters::Value(s_counters.free), mem.available);
            mem.standbyNormal = MemoryCounters::Value(s_counters.standby[0]);
            mem.standbyReserve = MemoryCounters::Value(s_counters.standby[1]);
            mem.standbyCore = MemoryCounters::Value(s_counters.standby[2]);
            mem.cache = std::min(mem.standbyNormal + mem.standbyReserve + mem.standbyCore, mem.available - mem.free);
            mem.kernelPaged = MemoryCounters::Value(s_counters.paged);
            mem.kernelNonpaged = MemoryCounters::Value(s_counters.nonpaged);
            mem.kernel = mem.kernelPaged + mem.kernelNonpaged;
            mem.modified = MemoryCounters::Value(s_counters.modified);
        } else { // счётчики недоступны — приблизительно
            mem.kernel = perf.KernelNonpaged * page;
            mem.cache = std::min<uint64_t>(perf.SystemCache * page, mem.available);
            mem.free = mem.available - mem.cache;
        }
        const uint64_t used = mem.total > mem.available ? mem.total - mem.available : 0;
        const uint64_t counted = mem.apps + mem.services + mem.system + mem.kernel;
        mem.other = used > counted ? used - counted : 0; // общие библиотеки, встроенная видеокарта, изменённые страницы

        Snapshot s;
        s.ready = true;
        s.memory = mem;
        s.processCount = count;
        s.pool = PoolTags();
        for (auto& [key, g] : groups) {
            std::sort(g.processes.begin(), g.processes.end(), [](const Process& a, const Process& b) { return a.memory > b.memory; });
            s.groups.push_back(std::move(g));
        }
        std::sort(s.groups.begin(), s.groups.end(), [](const Group& a, const Group& b) { return a.memory > b.memory; });
        std::lock_guard lock(g_mutex);
        g_snapshot = std::move(s);
    }

    void Loop() {
        for (;;) {
            if (g_active.load()) Collect();
            Sleep(g_active.load() ? 2000 : 500);
        }
    }

    bool EnablePrivilege(const wchar_t* name) {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
        TOKEN_PRIVILEGES tp{ 1 };
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        const bool ok = LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid) && AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr) &&
                        GetLastError() == ERROR_SUCCESS;
        CloseHandle(token);
        return ok;
    }

    bool IsElevated() {
        HANDLE token = nullptr;
        TOKEN_ELEVATION elevation{};
        DWORD size = 0;
        const bool ok = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
                        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
        if (token) CloseHandle(token);
        return ok && elevation.TokenIsElevated;
    }
}

void ProcessMonitor::SetActive(bool active) {
    static bool s_started = false;
    if (!s_started) { s_started = true; std::thread(Loop).detach(); }
    if (active && !g_active.load()) std::thread(Collect).detach(); // сразу, не ждать 2 секунды
    g_active = active;
}

ProcessMonitor::Snapshot ProcessMonitor::Get() {
    std::lock_guard lock(g_mutex);
    return g_snapshot;
}

namespace {
    // Списки памяти Windows (нужны права администратора): 2 — рабочие наборы всех процессов, 3 — записать изменённые,
    // 4 — весь кэш ожидания, 5 — кэш малой важности
    bool MemoryCommand(int command) {
        using SetInfo = LONG(WINAPI*)(int, PVOID, ULONG);
        static auto set = reinterpret_cast<SetInfo>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetSystemInformation"));
        return set && set(80 /* SystemMemoryListInformation */, &command, sizeof(command)) >= 0;
    }
}

int ProcessMonitor::CleanAsAdmin() {
    if (!IsElevated()) return 2;
    EnablePrivilege(L"SeProfileSingleProcessPrivilege");
    EnablePrivilege(L"SeIncreaseQuotaPrivilege");
    MemoryCommand(2); // рабочие наборы всех процессов, в том числе системных
    MemoryCommand(3); // изменённые страницы — на диск
    return MemoryCommand(4) ? 0 : 1; // и весь кэш ожидания
}

ProcessMonitor::CleanResult ProcessMonitor::CleanMemory(bool deep) {
    CleanResult r;
    r.elevated = IsElevated();
    MEMORYSTATUSEX before{ sizeof(before) };
    GlobalMemoryStatusEx(&before);

    if (deep && !r.elevated) { // права администратора — только на очистку: отдельным процессом через окно UAC
        wchar_t self[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        SHELLEXECUTEINFOW sei{ sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
        sei.lpVerb = L"runas";
        sei.lpFile = self;
        sei.lpParameters = L"--clean-memory";
        sei.nShow = SW_HIDE;
        if (ShellExecuteExW(&sei) && sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 60000);
            DWORD code = 1;
            GetExitCodeProcess(sei.hProcess, &code);
            CloseHandle(sei.hProcess);
            r.standbyPurged = code == 0;
        } else {
            r.declined = true; // нажали «Нет» в окне UAC
        }
    }

    // 1. Рабочие наборы процессов: неиспользуемые страницы уходят из ОЗУ (вернутся с диска, если понадобятся)
    DWORD pids[4096], bytes = 0;
    if (EnumProcesses(pids, sizeof(pids), &bytes))
        for (DWORD i = 0; i < bytes / sizeof(DWORD); ++i) {
            HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
            if (!process) continue;
            if (EmptyWorkingSet(process)) ++r.trimmed;
            CloseHandle(process);
        }

    // 2. Уже с правами администратора — сразу и списки памяти Windows
    if (deep && r.elevated) r.standbyPurged = CleanAsAdmin() == 0;

    Sleep(800);
    MEMORYSTATUSEX after{ sizeof(after) };
    GlobalMemoryStatusEx(&after);
    r.freed = after.ullAvailPhys > before.ullAvailPhys ? after.ullAvailPhys - before.ullAvailPhys : 0;
    if (g_active.load()) Collect();
    return r;
}

void ProcessMonitor::SetLimit(const std::string& exe, uint64_t bytes) {
    const std::wstring ini = Text::ExecutableDirectory() + L"\\anya.ini";
    const std::wstring key = Text::LowerRu(Text::ToWide(exe));
    WritePrivateProfileStringW(L"limits", key.c_str(), bytes ? std::to_wstring(bytes).c_str() : nullptr, ini.c_str());
    if (bytes) return;
    // сняли ограничение — вернуть процессам обычный режим
    DWORD pids[4096], size = 0;
    if (!EnumProcesses(pids, sizeof(pids), &size)) return;
    for (DWORD i = 0; i < size / sizeof(DWORD); ++i) {
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!process) continue;
        wchar_t path[MAX_PATH] = {};
        DWORD length = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, path, &length)) {
            std::wstring name = Text::LowerRu(path);
            name = name.substr(name.find_last_of(L'\\') + 1);
            if (name == key) SetProcessWorkingSetSizeEx(process, static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), QUOTA_LIMITS_HARDWS_MAX_DISABLE);
        }
        CloseHandle(process);
    }
}

uint64_t ProcessMonitor::Limit(const std::string& exe) {
    const std::wstring ini = Text::ExecutableDirectory() + L"\\anya.ini";
    wchar_t value[32] = {};
    GetPrivateProfileStringW(L"limits", Text::LowerRu(Text::ToWide(exe)).c_str(), L"0", value, 32, ini.c_str());
    return _wcstoui64(value, nullptr, 10);
}

bool ProcessMonitor::EndProcess(uint32_t pid) {
    if (pid == GetCurrentProcessId()) return false;
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!process) return false;
    const bool ok = TerminateProcess(process, 1) != FALSE;
    CloseHandle(process);
    return ok;
}

uint32_t ProcessMonitor::SelfPid() { return GetCurrentProcessId(); }
