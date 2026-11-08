#pragma once
// Свои обои на фоне Ани и цвета, подобранные по картинке: любимый цвет — самый яркий заметный оттенок,
// тема — затемнённый средний цвет. Картинку читает Windows (WIC), рисует ImGui.

#include "Theme.h"

#include <string>

namespace Wallpaper {
    bool Load(const std::string& path);  // false — файл не открылся (тогда обоев нет)
    void Unload();
    bool Loaded();

    // Нарисовать «по размеру экрана с обрезкой» (как обои Windows); alpha — для плавного появления
    void Draw(ImDrawList* dl, ImVec2 size, float alpha);

    struct Colors { Theme::Color accent, base; };
    Colors Suggested();                   // цвета по текущей картинке

    // Включить/выключить цвета по обоям: включение запоминает прежние цвета, выключение их возвращает
    void ApplyAutoColors(bool enable);

    // Выбор картинки в окне Windows — в фоне; Picked() вернёт путь один раз, когда выбор сделан
    void PickAsync();
    bool Picked(std::string& path);
}
