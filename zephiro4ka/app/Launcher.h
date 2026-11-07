#pragma once
// Запуск и закрытие программ голосом — без ИИ.
// Список берётся из «Пуска» Windows (включая приложения из Store) и ярлыков на рабочем столе.
// Название ищется нечётко и с транслитерацией: «стим» → Steam, «дискорд» → Discord.

#include <string>
#include <vector>

namespace Launcher {
    void Start();                    // собрать список программ в фоне

    struct Result {
        bool found = false;
        std::string name;            // как программа называется в Windows
    };

    Result Find(const std::string& spokenName);   // только найти, не запуская
    Result Open(const std::string& spokenName);      // уже запущена (даже свёрнута в трей) — показать; иначе запустить
    Result Close(const std::string& spokenName);
    Result Minimize(const std::string& spokenName);  // «скрой стим»
    Result Show(const std::string& spokenName);      // «покажи дискорд» — развернуть и вывести вперёд

    void* MainWindowOf(const std::string& spokenName); // HWND главного окна запущенной программы (nullptr — не запущена)
    std::vector<std::string> AppNames();              // все найденные программы — для выбора в сценариях
}
