#pragma once
// Статус в Discord: «Играет в Anya», а когда играет музыка или видео — «Слушает …» / «Смотрит …» (по выбору в настройках).
// Нужен ID приложения из discord.com/developers (название приложения Discord и покажет: назови его «Anya»);
// картинка — ассет «anya» в разделе Rich Presence → Art Assets. Настройки — anya.ini, раздел [discord].

#include <string>

namespace DiscordRpc {
    void Start();
    void Reload();          // после изменения настроек
    std::string Status();   // для настроек: «показываю статус», «Discord не запущен»…
}
