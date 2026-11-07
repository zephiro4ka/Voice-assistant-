#pragma once
// Экран Ани: шапка, сфера, приветствие, поле ввода, история и настройки.

#include "Platform.h"

namespace Ui {
    void Init(HWND window);
    // active — окно в фокусе (иначе фон может замереть, см. «Анимировать в неактивном окне»)
    void Frame(float dt, bool active);

    // Для оконной процедуры: можно ли тянуть окно за эту точку шапки (не по меню и не по кнопкам)
    void SetHistory(bool open);
    float TitleHeight();
    bool IsCaption(float x, float y);
}
