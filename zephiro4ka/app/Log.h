#pragma once
// Журнал anya.log рядом с exe: что Аня услышала и что решила сделать.
// Помогает понять, почему команда не сработала («Они откроетим» вместо «Аня, открой стим»).

#include <string>

namespace Log {
    void Write(const std::string& line);
}
