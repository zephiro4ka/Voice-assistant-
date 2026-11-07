#pragma once
// Что делать с услышанным: обращение «Аня», быстрые команды, вопросы к ИИ.

#include "BodySounds.h"

#include <string>

namespace Brain {
    void Start();
    void Shutdown();

    void Submit(const std::string& command);    // команда/вопрос (текстом или голосом)
    void OnHeard(const std::string& text);      // распознанная речь — решает, обращались ли к Ане
    void OnBodySound(BodySounds::Kind kind);    // кашель — «Всё в порядке? Не заболел?», чих — «Будь здоров!»
    void Interrupt();                            // перебить: замолчать и бросить текущий ответ
    void ToggleListening();                      // кнопка «говорить» / пробел / клик по сфере
}
