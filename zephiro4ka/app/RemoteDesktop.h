#pragma once
// Пульт и удалённый рабочий стол в Telegram Mini App.
// Локальный сервер (только 127.0.0.1) + туннель cloudflared; работает, пока включён пульт, адрес при запуске новый.
// Вход — только с подписью Telegram (initData) и только для владельца. На заблокированном компьютере не работает.

#include <functional>
#include <string>

namespace RemoteDesktop {
    struct Result {
        std::string url;   // https-адрес Mini App
        std::string error; // понятная причина, если не получилось
    };

    // Долгая (ждёт туннель, до минуты) — звать не из потока интерфейса. Уже запущен — сразу вернёт адрес.
    Result Start(const std::string& botToken, long long owner, std::function<void(const std::string&)> onClosed);
    void Stop();
}
