#pragma once
// Мини-диспетчер задач для главного экрана: загрузка и температура железа, память, диск, сеть.
// Замеры раз в секунду в фоновом потоке; Snapshot() мгновенный — его можно звать каждый кадр.

#include <array>
#include <string>

namespace SystemMonitor {
    constexpr int kHistory = 60; // секунд истории для графиков

    struct Stats {
        bool ready = false;
        float cpu = 0;             // % загрузки процессора
        float cpuTemp = -1;        // °C (датчик ACPI; -1 — Windows его не отдаёт)
        float ram = 0;             // % занятой памяти
        float ramUsedGb = 0, ramTotalGb = 0;
        bool hasGpu = false;
        std::string gpuName;
        float gpu = 0;             // % загрузки видеокарты
        float gpuTemp = -1;        // °C
        float vramUsedGb = 0, vramTotalGb = 0;
        float diskFreeGb = 0, diskTotalGb = 0; // диск C:
        float netDownKbps = 0, netUpKbps = 0;  // КБ/с
        std::array<float, kHistory> cpuHistory{}, gpuHistory{}; // от старых к новым

        // для вкладки «Процессы» → «Показатели»
        float cpuFreqGhz = 0;          // текущая частота процессора
        int cores = 0;
        float diskActive = 0;          // % времени, когда диск занят
        float diskReadMBs = 0, diskWriteMBs = 0;
        float gpuPowerW = -1, gpuFan = -1, gpuClockMHz = -1; // -1 — видеокарта не сообщает
        bool hasIgpu = false;          // встроенная видеокарта (Intel / AMD)
        std::string igpuName;
        float igpu = 0;                // % загрузки встроенной
        bool hasBattery = false;
        int battery = 0;
        bool charging = false;
        std::array<float, kHistory> ramHistory{}, diskHistory{}, netHistory{}, igpuHistory{}, cpuTempHistory{}, gpuTempHistory{};
    };

    void Start();
    Stats Snapshot();
    std::string Report(); // «Процессор 12%, 54°; видеокарта 3%, 41°…» — для телеграма и голоса
}
