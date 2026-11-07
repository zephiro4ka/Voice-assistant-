#pragma once
// Сценарии: «Аня, уютный вечер» → цепочка блоков: открыть кино на втором мониторе, запустить игру, убавить звук…
// Каждый сценарий = фраза («Слушать») + блоки действий по порядку + «Конец». Хранятся в scenarios.json рядом с Аней.

#include <optional>
#include <string>
#include <vector>

namespace Scenarios {
    enum class Step {
        Launch,     // запустить программу (text — название, как в «Пуске»)
        OpenLink,   // открыть ссылку в браузере (text — адрес)
        Place,      // поставить окно: text — программа (пусто — из прошлого блока), monitor, position
        Volume,     // громкость number %
        Media,      // number: 0 — пауза/плей, 1 — следующий трек
        Wait,       // подождать number секунд
        Say,        // сказать text вслух
        kCount
    };
    // Положение окна для блока Place
    enum class Position { Maximize = 0, LeftHalf = 1, RightHalf = 2, Center = 3, FullScreen = 4 /* развернуть и F11 — для видео в браузере */ };

    struct Block {
        Step step = Step::Launch;
        std::string text;
        int monitor = 1;   // 1 — основной (слева направо по порядку)
        int position = 0;
        int number = 0;
        // холст: место блока, куда идёт дальше (номера блоков), свой цвет (0xRRGGBB, -1 — по типу) и своё название
        int id = 0;
        float x = 0, y = 0;
        std::vector<int> next;
        int color = -1;
        std::string title;
    };

    struct Scenario {
        std::string name = "Новый сценарий";
        std::string phrase;        // что сказать после «Аня»
        bool enabled = true;
        std::vector<Block> blocks;
        // блок «Слушать» на холсте и куда он ведёт; linkStyle: 0 — кривые, 1 — косые, 2 — прямые
        float startX = 0, startY = 0;
        std::vector<int> startNext;
        int linkStyle = 0;
        bool hasLayout = false; // старый сценарий (цепочкой) — расставим на холсте сами
    };

    // Раздать блокам номера и места на холсте (старые сценарии и собранные голосом — цепочкой слева направо)
    void Normalize(Scenario& scenario);
    // В каком порядке выполнять: от «Слушать» по стрелкам; развилки — по очереди, сверху вниз
    std::vector<Block> Order(const Scenario& scenario);

    std::vector<Scenario> Load();
    void Save(const std::vector<Scenario>& scenarios);

    // Фраза совпала с каким-то сценарием — запустить его в фоне и вернуть, что ответить
    std::optional<std::string> TryRun(const std::string& command);
    void Run(const Scenario& scenario); // в фоне

    const char* StepName(Step step);
    int MonitorCount();
}
