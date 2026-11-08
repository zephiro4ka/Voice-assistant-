#pragma once
// Мессенджеры: «зайди в чат с Зефирочником» — открыть Discord и перейти в личку к человеку.
// Как произносится имя и какой у человека ник — в anya.ini, раздел [contacts]:  зефирочник=zephiro4nik

#include <string>

namespace Messenger {
    // Открыть Discord (если закрыт) и перейти в чат через быстрый поиск Ctrl+K.
    // Возвращает, что сказать в ответ. Само переключение идёт в фоне — Discord может стартовать несколько секунд.
    std::string OpenDiscordChat(const std::string& spokenName);
}
