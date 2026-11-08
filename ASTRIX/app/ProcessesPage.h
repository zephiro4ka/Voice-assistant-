#pragma once
// Вкладка «Процессы»: на что уходит память, программы и службы по убыванию памяти, очистка ОЗУ.

#include "../ImGui/imgui.h"

namespace ProcessesPage {
    void Open();
    void Close();
    bool IsOpen();
    bool Covers();
    float Visibility();
    void Draw(ImVec2 screen, float top, float dt);
}
