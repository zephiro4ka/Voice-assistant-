#include "SystemMonitor.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h> // до windows.h — иначе старый winsock.h конфликтует с iphlpapi
#include <ws2ipdef.h>
#include "Platform.h"

#include <dxgi.h>
#include <iphlpapi.h>
#include <pdh.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "dxgi.lib")

namespace {
    std::mutex g_mutex;
    SystemMonitor::Stats g_stats;

    // ------------------------------------------------------------ видеокарта NVIDIA: nvml.dll ставится вместе с драйвером
    struct Nvml {
        using Init = int (*)();
        using GetHandle = int (*)(unsigned, void**);
        using GetName = int (*)(void*, char*, unsigned);
        using GetTemp = int (*)(void*, int, unsigned*);
        struct Utilization { unsigned gpu, memory; };
        using GetUtil = int (*)(void*, Utilization*);
        struct Memory { unsigned long long total, free, used; };
        using GetMemory = int (*)(void*, Memory*);

        using GetUint = int (*)(void*, unsigned*);
        using GetClock = int (*)(void*, int, unsigned*);
        GetUint power = nullptr, fan = nullptr;
        GetClock clock = nullptr;

        HMODULE dll = nullptr;
        void* device = nullptr;
        GetTemp temp = nullptr;
        GetUtil util = nullptr;
        GetMemory memory = nullptr;
        std::string name;

        bool Load() {
            dll = LoadLibraryW(L"nvml.dll");
            if (!dll) dll = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
            if (!dll) return false;
            auto init = reinterpret_cast<Init>(GetProcAddress(dll, "nvmlInit_v2"));
            auto handle = reinterpret_cast<GetHandle>(GetProcAddress(dll, "nvmlDeviceGetHandleByIndex_v2"));
            auto getName = reinterpret_cast<GetName>(GetProcAddress(dll, "nvmlDeviceGetName"));
            temp = reinterpret_cast<GetTemp>(GetProcAddress(dll, "nvmlDeviceGetTemperature"));
            util = reinterpret_cast<GetUtil>(GetProcAddress(dll, "nvmlDeviceGetUtilizationRates"));
            memory = reinterpret_cast<GetMemory>(GetProcAddress(dll, "nvmlDeviceGetMemoryInfo"));
            power = reinterpret_cast<GetUint>(GetProcAddress(dll, "nvmlDeviceGetPowerUsage"));
            fan = reinterpret_cast<GetUint>(GetProcAddress(dll, "nvmlDeviceGetFanSpeed"));
            clock = reinterpret_cast<GetClock>(GetProcAddress(dll, "nvmlDeviceGetClockInfo"));
            if (!init || !handle || !temp || !util || !memory || init() != 0 || handle(0, &device) != 0) return false;
            char buffer[96] = {};
            if (getName && getName(device, buffer, sizeof(buffer)) == 0) name = buffer;
            return true;
        }
    };

    // ------------------------------------------------------------ температура процессора: датчик ACPI из счётчиков Windows
    struct ThermalZone {
        PDH_HQUERY query = nullptr;
        PDH_HCOUNTER counter = nullptr;
        bool ok = false;

        bool Open() {
            if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return false;
            // «высокая точность» — в десятых долях кельвина; без админ-прав
            ok = PdhAddEnglishCounterW(query, L"\\Thermal Zone Information(*)\\High Precision Temperature", 0, &counter) == ERROR_SUCCESS;
            if (ok) PdhCollectQueryData(query);
            return ok;
        }

        float Read() {
            if (!ok || PdhCollectQueryData(query) != ERROR_SUCCESS) return -1;
            DWORD size = 0, count = 0;
            PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, nullptr);
            if (size == 0) return -1;
            std::vector<BYTE> buffer(size);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items) != ERROR_SUCCESS) return -1;
            double hottest = -1;
            for (DWORD i = 0; i < count; ++i)
                if (items[i].FmtValue.CStatus == ERROR_SUCCESS) hottest = std::max(hottest, items[i].FmtValue.doubleValue / 10.0 - 273.15);
            return hottest > 0 && hottest < 130 ? static_cast<float>(hottest) : -1;
        }
    };

    // ------------------------------------------------------------ диск, частота процессора, встроенная видеокарта — счётчики Windows
    struct Extras {
        PDH_HQUERY query = nullptr;
        PDH_HCOUNTER idle = nullptr, read = nullptr, write = nullptr, freq = nullptr, perf = nullptr, engines = nullptr;
        struct Adapter { std::wstring luid; std::string name; bool integrated; };
        std::vector<Adapter> adapters;

        void Open() {
            if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return;
            PdhAddEnglishCounterW(query, L"\\PhysicalDisk(_Total)\\% Idle Time", 0, &idle);
            PdhAddEnglishCounterW(query, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &read);
            PdhAddEnglishCounterW(query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &write);
            PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\Processor Frequency", 0, &freq);
            PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\% Processor Performance", 0, &perf);
            PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &engines);
            PdhCollectQueryData(query);
            // видеокарты: у каждой свой LUID — по нему счётчики «GPU Engine» делятся между встроенной и дискретной
            IDXGIFactory1* factory = nullptr;
            if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return;
            IDXGIAdapter1* adapter = nullptr;
            for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 d{};
                adapter->GetDesc1(&d);
                adapter->Release();
                if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                wchar_t luid[48];
                swprintf_s(luid, L"luid_0x%08x_0x%08x", static_cast<unsigned>(d.AdapterLuid.HighPart), static_cast<unsigned>(d.AdapterLuid.LowPart));
                const bool integrated = d.VendorId == 0x8086 || (d.VendorId == 0x1002 && d.DedicatedVideoMemory < (512ull << 20));
                char name[128];
                WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof(name), nullptr, nullptr);
                adapters.push_back({ luid, name, integrated });
            }
            factory->Release();
        }

        static double Value(PDH_HCOUNTER counter) {
            PDH_FMT_COUNTERVALUE v{};
            return counter && PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS && v.CStatus == ERROR_SUCCESS ? v.doubleValue : 0.0;
        }

        // Загрузка видеокарты как в диспетчере задач: самый занятый тип движков (3D, видео, копирование)
        float AdapterLoad(const std::wstring& luid) {
            if (!engines) return 0;
            DWORD size = 0, count = 0;
            PdhGetFormattedCounterArrayW(engines, PDH_FMT_DOUBLE, &size, &count, nullptr);
            if (!size) return 0;
            std::vector<BYTE> buffer(size);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            if (PdhGetFormattedCounterArrayW(engines, PDH_FMT_DOUBLE, &size, &count, items) != ERROR_SUCCESS) return 0;
            std::vector<std::pair<std::wstring, double>> byType;
            for (DWORD i = 0; i < count; ++i) {
                const std::wstring name = items[i].szName;
                if (name.find(luid) == std::wstring::npos || items[i].FmtValue.CStatus != ERROR_SUCCESS) continue;
                const size_t t = name.find(L"engtype_");
                const std::wstring type = t == std::wstring::npos ? L"" : name.substr(t);
                auto it = std::find_if(byType.begin(), byType.end(), [&](const auto& p) { return p.first == type; });
                if (it == byType.end()) byType.push_back({ type, items[i].FmtValue.doubleValue });
                else it->second += items[i].FmtValue.doubleValue;
            }
            double best = 0;
            for (const auto& [type, value] : byType) best = std::max(best, value);
            return static_cast<float>(std::min(100.0, best));
        }
    };

    ULONGLONG ToUll(const FILETIME& t) { return (static_cast<ULONGLONG>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }

    // Сумма байт по всем сетевым адаптерам
    void NetBytes(ULONG64& in, ULONG64& out) {
        in = out = 0;
        MIB_IF_TABLE2* table = nullptr;
        if (GetIfTable2(&table) != NO_ERROR) return;
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const MIB_IF_ROW2& row = table->Table[i];
            if (row.InterfaceAndOperStatusFlags.HardwareInterface && row.OperStatus == IfOperStatusUp) {
                in += row.InOctets;
                out += row.OutOctets;
            }
        }
        FreeMibTable(table);
    }

    void Loop() {
        Nvml nvml;
        const bool gpu = nvml.Load();
        ThermalZone thermal;
        thermal.Open();
        Extras extras;
        extras.Open();

        FILETIME idle0{}, kernel0{}, user0{};
        GetSystemTimes(&idle0, &kernel0, &user0);
        ULONG64 in0 = 0, out0 = 0;
        NetBytes(in0, out0);
        ULONGLONG t0 = GetTickCount64();

        for (;;) {
            Sleep(1000);
            SystemMonitor::Stats s;
            s.ready = true;

            FILETIME idle{}, kernel{}, user{};
            GetSystemTimes(&idle, &kernel, &user);
            const ULONGLONG busy = (ToUll(kernel) - ToUll(kernel0)) + (ToUll(user) - ToUll(user0));
            const ULONGLONG idleTime = ToUll(idle) - ToUll(idle0);
            s.cpu = busy ? std::clamp(100.0f * (busy - idleTime) / busy, 0.0f, 100.0f) : 0.0f;
            idle0 = idle, kernel0 = kernel, user0 = user;
            s.cpuTemp = thermal.Read();

            MEMORYSTATUSEX memory{ sizeof(memory) };
            GlobalMemoryStatusEx(&memory);
            s.ram = static_cast<float>(memory.dwMemoryLoad);
            s.ramTotalGb = memory.ullTotalPhys / 1073741824.0f;
            s.ramUsedGb = (memory.ullTotalPhys - memory.ullAvailPhys) / 1073741824.0f;

            if (gpu) {
                s.hasGpu = true;
                s.gpuName = nvml.name;
                unsigned temperature = 0;
                if (nvml.temp(nvml.device, 0, &temperature) == 0) s.gpuTemp = static_cast<float>(temperature);
                Nvml::Utilization u{};
                if (nvml.util(nvml.device, &u) == 0) s.gpu = static_cast<float>(u.gpu);
                Nvml::Memory m{};
                if (nvml.memory(nvml.device, &m) == 0) {
                    s.vramUsedGb = m.used / 1073741824.0f;
                    s.vramTotalGb = m.total / 1073741824.0f;
                }
            }

            // диск, частота процессора, встроенная видеокарта
            if (extras.query && PdhCollectQueryData(extras.query) == ERROR_SUCCESS) {
                s.diskActive = std::clamp(100.0f - static_cast<float>(Extras::Value(extras.idle)), 0.0f, 100.0f);
                s.diskReadMBs = static_cast<float>(Extras::Value(extras.read) / 1048576.0);
                s.diskWriteMBs = static_cast<float>(Extras::Value(extras.write) / 1048576.0);
                s.cpuFreqGhz = static_cast<float>(Extras::Value(extras.freq) * Extras::Value(extras.perf) / 100.0 / 1000.0);
                for (const auto& a : extras.adapters)
                    if (a.integrated) { s.hasIgpu = true; s.igpuName = a.name; s.igpu = extras.AdapterLoad(a.luid); break; }
            }
            SYSTEM_INFO info;
            GetSystemInfo(&info);
            s.cores = static_cast<int>(info.dwNumberOfProcessors);
            if (gpu) { // NVIDIA: потребление, вентилятор, частота
                unsigned value = 0;
                if (nvml.power && nvml.power(nvml.device, &value) == 0) s.gpuPowerW = value / 1000.0f;
                if (nvml.fan && nvml.fan(nvml.device, &value) == 0) s.gpuFan = static_cast<float>(value);
                if (nvml.clock && nvml.clock(nvml.device, 0, &value) == 0) s.gpuClockMHz = static_cast<float>(value);
            }
            SYSTEM_POWER_STATUS power{};
            if (GetSystemPowerStatus(&power) && power.BatteryFlag != 128 && power.BatteryLifePercent <= 100) {
                s.hasBattery = true;
                s.battery = power.BatteryLifePercent;
                s.charging = power.ACLineStatus == 1;
            }

            ULARGE_INTEGER freeBytes{}, totalBytes{};
            if (GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, nullptr)) {
                s.diskFreeGb = freeBytes.QuadPart / 1073741824.0f;
                s.diskTotalGb = totalBytes.QuadPart / 1073741824.0f;
            }

            ULONG64 in = 0, out = 0;
            NetBytes(in, out);
            const ULONGLONG t = GetTickCount64();
            const float seconds = std::max(0.001f, (t - t0) / 1000.0f);
            s.netDownKbps = in >= in0 ? (in - in0) / 1024.0f / seconds : 0.0f;
            s.netUpKbps = out >= out0 ? (out - out0) / 1024.0f / seconds : 0.0f;
            in0 = in, out0 = out, t0 = t;

            std::lock_guard lock(g_mutex);
            auto push = [](std::array<float, SystemMonitor::kHistory>& history, const std::array<float, SystemMonitor::kHistory>& old, float value) {
                history = old;
                std::rotate(history.begin(), history.begin() + 1, history.end());
                history.back() = value;
            };
            push(s.cpuHistory, g_stats.cpuHistory, s.cpu);
            push(s.gpuHistory, g_stats.gpuHistory, s.gpu);
            push(s.ramHistory, g_stats.ramHistory, s.ram);
            push(s.diskHistory, g_stats.diskHistory, s.diskActive);
            push(s.netHistory, g_stats.netHistory, s.netDownKbps);
            push(s.igpuHistory, g_stats.igpuHistory, s.igpu);
            push(s.cpuTempHistory, g_stats.cpuTempHistory, std::max(0.0f, s.cpuTemp));
            push(s.gpuTempHistory, g_stats.gpuTempHistory, std::max(0.0f, s.gpuTemp));
            g_stats = s;
        }
    }
}

void SystemMonitor::Start() { std::thread(Loop).detach(); }

SystemMonitor::Stats SystemMonitor::Snapshot() {
    std::lock_guard lock(g_mutex);
    return g_stats;
}

std::string SystemMonitor::Report() {
    const Stats s = Snapshot();
    if (!s.ready) return "Ещё собираю данные о компьютере";
    char text[512];
    std::string cpu = "Процессор " + std::to_string(static_cast<int>(s.cpu)) + "%";
    if (s.cpuTemp > 0) cpu += ", " + std::to_string(static_cast<int>(s.cpuTemp)) + "°";
    std::string gpu;
    if (s.hasGpu) {
        gpu = ". Видеокарта " + std::to_string(static_cast<int>(s.gpu)) + "%";
        if (s.gpuTemp > 0) gpu += ", " + std::to_string(static_cast<int>(s.gpuTemp)) + "°";
        snprintf(text, sizeof(text), ", видеопамять %.1f из %.1f ГБ", s.vramUsedGb, s.vramTotalGb);
        gpu += text;
    }
    snprintf(text, sizeof(text), ". Память %.1f из %.0f ГБ (%d%%). На диске C свободно %.0f ГБ", s.ramUsedGb, s.ramTotalGb,
        static_cast<int>(s.ram), s.diskFreeGb);
    return cpu + gpu + text + ".";
}
