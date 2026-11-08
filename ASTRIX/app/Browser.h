#pragma once
// Браузер (по умолчанию Edge, иначе любой открытый): окно, вкладки и плавная прокрутка.
// Перед каждым действием Аня сама выводит браузер на передний план — клавиши уходят именно ему.

#include "Platform.h"

#include <initializer_list>
#include <string>
#include <vector>

namespace Browser {
    // Окно браузера, которое видит пользователь. restore — развернуть, если свёрнуто.
    HWND Window(bool restore);
    bool Focus();                                  // вывести на передний план; false — браузер не открыт

    bool Hide();                                   // свернуть все окна браузера
    bool Show();                                   // развернуть и показать
    void Open();                                   // показать открытый браузер, а если закрыт — запустить (без новой вкладки)
    bool Close();                                  // закрыть

    bool Keys(std::initializer_list<WORD> chord);  // нажать сочетание в браузере (например Ctrl+Tab)

    // Вкладки: названия открытых вкладок (по порядку) и переход на вкладку по названию
    std::vector<std::string> Tabs();
    bool SwitchToTab(const std::string& spokenTitle, std::string& title);

    // Плавная прокрутка страницы: direction +1 — вниз, -1 — вверх. Идёт, пока не позовут StopScroll.
    bool StartScroll(int direction);
    bool StopScroll();                             // true — прокрутка шла и остановлена
    bool IsScrolling();
    void ChangeScrollSpeed(float factor);          // «быстрее» ×1.6, «медленнее» ×0.6
}
