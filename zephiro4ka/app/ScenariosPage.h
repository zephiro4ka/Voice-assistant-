#pragma once
// Вкладка «Сценарии»: список сценариев слева, справа — цепочка блоков «Слушать → действия → Конец».
// Только ImGui — переносится вместе с интерфейсом.

#include "../ImGui/imgui.h"
#include "Scenarios.h"

namespace ScenariosPage {
    void Open();
    void Close();
    bool IsOpen();
    bool Covers();
    float Visibility();
    void Draw(ImVec2 screen, float top, float dt);

    // Новый сценарий, который собрала Аня (из любого потока): сохранить и показать во вкладке
    void Add(const Scenarios::Scenario& scenario);
    bool PollAdded(); // из потока интерфейса, раз в кадр (true — открыла вкладку)
}
