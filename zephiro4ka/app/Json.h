#pragma once
// Минимальный JSON без зависимостей: ровно то, что нужно для общения с нейросетью.

#include <string>
#include <vector>

namespace Json {
    std::string Escape(const std::string& value);

    // Читает строку JSON, начиная с кавычки в позиции pos. Сдвигает pos за конец строки.
    bool ReadString(const std::string& json, size_t& pos, std::string& value);

    // Все строковые значения ключа key (в порядке появления)
    std::vector<std::string> FindStrings(const std::string& json, const char* key, size_t limit = 1000);
}
