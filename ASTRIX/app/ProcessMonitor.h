#pragma once
// Подробный диспетчер задач: куда уходит оперативная память (программы, службы Windows, система, кэш),
// процессы, собранные по программам, службы внутри svchost, загрузка процессора, очистка ОЗУ.
// Данные собираются в фоне раз в 2 секунды, пока открыта вкладка «Процессы».

#include <cstdint>
#include <string>
#include <vector>

namespace ProcessMonitor {
    enum class Kind { App, Service, System };

    struct Process {
        uint32_t pid = 0;
        std::string name;              // имя exe
        std::vector<std::string> services; // для svchost и других служб — понятные названия служб
        uint64_t memory = 0;           // частный рабочий набор, байт (как «Память» в диспетчере задач)
        uint64_t shared = 0;           // общие страницы (библиотеки, отображённые файлы) в рабочем наборе
        float cpu = 0;                 // %
    };

    struct Group {
        std::string title;             // «Google Chrome», «Службы Windows: Сеть»…
        std::string exe;
        Kind kind = Kind::App;
        uint64_t memory = 0;
        float cpu = 0;
        std::vector<Process> processes;
    };

    struct Breakdown {                 // байт
        uint64_t total = 0, apps = 0, services = 0, system = 0, kernel = 0, cache = 0, other = 0, available = 0, free = 0;
        uint64_t compressed = 0;       // сжатая память Windows (процесс Memory Compression)
        uint64_t kernelPaged = 0, kernelNonpaged = 0;
        uint64_t standbyCore = 0, standbyNormal = 0, standbyReserve = 0; // кэш: важный, обычный, малой важности
        uint64_t modified = 0;         // изменённые страницы — ждут записи на диск
        uint64_t sharedTotal = 0;      // общие страницы всех процессов (с повторами)
    };

    struct PoolTag { std::string tag, description; uint64_t bytes = 0; bool paged = false; };

    struct Snapshot {
        bool ready = false;
        Breakdown memory;
        std::vector<Group> groups;     // по убыванию памяти
        std::vector<PoolTag> pool;     // кто держит память ядра — метки драйверов (как poolmon)
        int processCount = 0;
    };

    void SetActive(bool active);       // собирать данные (вкладка открыта) или спать
    Snapshot Get();

    struct CleanResult { uint64_t freed = 0; int trimmed = 0; bool standbyPurged = false; bool elevated = false; bool declined = false; };
    // deep — «сильная»: ещё кэш ожидания, изменённые страницы и рабочие наборы всей системы.
    // Без прав администратора Аня запускает себя с флагом --clean-memory через окно UAC. Долго — не из UI-потока.
    CleanResult CleanMemory(bool deep = false);
    int CleanAsAdmin();                // вызывается в процессе с --clean-memory: чистит и возвращает код выхода

    // Ограничение памяти программы (по имени exe): байт, 0 — без ограничения. Хранится в anya.ini, [limits]
    void SetLimit(const std::string& exe, uint64_t bytes);
    uint64_t Limit(const std::string& exe);
    bool EndProcess(uint32_t pid);     // завершить процесс (только свои программы)
    uint32_t SelfPid();                // pid Ани — себя не завершаем
}
