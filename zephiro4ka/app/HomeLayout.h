#pragma once
// «Полная кастомизация»: любой блок главного экрана (сфера, погода, карточки справа, поле ввода, шторка разговора)
// можно перетащить мышью, а у некоторых — потянуть за уголок и поменять размер. Места хранятся в [layout] anya.ini
// в долях окна, поэтому при другом размере окна блоки остаются на своих местах.
// Только ImGui — переносится на Linux вместе с интерфейсом.

#include "../ImGui/imgui.h"

namespace HomeLayout {
    bool Editing();              // галочка «Полная кастомизация» включена
    void SetEditing(bool on);

    ImVec2 Shift(const char* id, ImVec2 screen);   // на сколько пикселей сдвинут блок
    float Scale(const char* id);                    // во сколько раз увеличен (1 — как задумано)
    bool Moved(const char* id);                     // блок двигали — показывать его, даже если «по умолчанию» ему тесно

    // Блок нарисован здесь (до сдвига уже применённого — то есть где он сейчас на экране); canResize — уголок для размера
    void Report(const char* id, const char* label, ImVec2 min, ImVec2 max, bool canResize);
    // Раз в кадр, поверх главного экрана: рамки, перетаскивание и панель «Готово / Сбросить»
    void DrawEditor(ImVec2 screen, float top, bool pageOpen);

    void Reset();                // все блоки — на места по умолчанию
}
