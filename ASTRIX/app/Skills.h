#pragma once
// Навыки Ани — всё, что она делает сама, без ИИ:
// программы, медиа, громкость, окна, горячие клавиши, печать под диктовку, таймеры, счёт, время.

#include <optional>
#include <string>

namespace Skills {
    // Если фраза — команда, выполнить её и вернуть, что сказать в ответ (пустая строка — молча).
    std::optional<std::string> Handle(const std::string& command);

    // Режим диктовки: всё сказанное печатается в активное окно
    bool IsDictating();
    void HandleDictation(const std::string& spoken);

    // Короткая справка «что ты умеешь»
    std::string Help();
}
