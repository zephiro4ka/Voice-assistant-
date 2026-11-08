#pragma once
// Пульт в Telegram Mini App: быстрые команды, быстрый запуск и настройки.
// Вызывается только после проверки ключа сеанса. Настройки — pult.json рядом с программой.

#include <string>

namespace Pult {
    struct Reply {
        int status = 200;
        std::string type = "application/json", body;
    };
    Reply Handle(const std::string& method, const std::string& path, const std::string& body);
}
