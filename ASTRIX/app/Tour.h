#pragma once
// Обучение при первом запуске: затемняет экран и по шагам подсвечивает части программы.

#include "../ImGui/imgui.h"

namespace Tour {
    void Start();
    void GoTo(int step);                          // для проверки
    bool Active();
    void Mark(const char* id, ImVec2 min, ImVec2 max);   // где сейчас находится блок (каждый кадр)
    void Draw(ImVec2 screen, float dt, bool wizardOpen);
}
