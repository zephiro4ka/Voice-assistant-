#pragma once
// Телеграм-бот Ани: управление компьютером с телефона, снимки экрана и решение задач с экрана.
// Слушается только владельца — его ID в anya.ini, [telegram] owner; всем остальным бот отвечает отказом.
// Токен — [telegram] token (его выдаёт @BotFather). Работает, пока запущена Аня, в том числе в режиме «Аня на связи».

#include <cstdint>
#include <string>
#include <vector>

namespace Telegram {
    void Start();
    void Reload();                 // после смены токена, владельца или выключателя в настройках
    bool Ready();                  // бот на связи
    std::string Status();          // для настроек: «на связи как @anya_bot», «нет токена», «ошибка: …»

    bool SendText(const std::string& text);                                   // владельцу
    bool SendPhoto(const std::vector<uint8_t>& jpeg, const std::string& caption);

    // «Аня, реши задачу на экране» — снимок → нейросеть → ответ владельцу в телеграм (в фоне)
    void SolveScreenAsync();
    void OnLockChanged(bool locked); // Windows заблокирована/разблокирована — обновить кнопки у владельца
    void SendScreenAsync();
}
