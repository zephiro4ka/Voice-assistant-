#pragma once
// Первый запуск: «Давай познакомимся» — имя помощницы, как зовут тебя, город для погоды, автозапуск,
// по желанию — телеграм-бот и Discord. Всё это потом можно поменять в настройках.
// Только ImGui — переносится на Linux вместе с интерфейсом.

#include "../ImGui/imgui.h"

namespace SetupWizard {
    bool Needed();     // ещё не настраивали ([anya] setup нет)
    void Open();
    bool IsOpen();
    void Draw(ImVec2 screen, float top, float dt);
}
