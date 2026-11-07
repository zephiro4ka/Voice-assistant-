#pragma once
// Всплывающий вопрос внизу экрана с кнопками «Да» / «Нет» — выезжает снизу и плавно уходит.

#include "../ImGui/imgui.h"

#include <functional>
#include <string>

namespace Toast {
    void Ask(const std::string& text, std::function<void()> onYes, const std::string& yes = "Да", const std::string& no = "Нет");
    void Draw(ImVec2 screen, float dt);
}
