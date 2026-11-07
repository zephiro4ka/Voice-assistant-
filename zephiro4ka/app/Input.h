#pragma once
// Имитация клавиатуры: печать текста, сочетания клавиш, медиа-кнопки.
// Всё уходит в окно, которое сейчас активно (то, где мигает курсор).

#include "Platform.h"

#include <initializer_list>
#include <string>

namespace Input {
    void TypeText(const std::string& utf8);               // напечатать текст (любой язык)
    void Chord(std::initializer_list<WORD> keys);         // сочетание: { VK_CONTROL, 'C' }
    void Tap(WORD key, int times = 1);                    // нажать клавишу N раз
}
