#pragma once
// Курсы валют для главного экрана. Валюта — по стране города из настроек погоды (или выбранная вручную, [rates] currency).
// Узбекистан — официальные курсы ЦБ, остальные страны — open.er-api.com. Обновляется раз в час, в фоне.

#include <string>
#include <vector>

namespace Rates {
    struct Item {
        std::string code;      // USD
        double value = 0;      // сколько местной валюты за 1 единицу
        double change = 0;     // изменение за день (0 — источник не сообщает)
    };
    struct Data {
        bool ready = false;
        std::string base;      // UZS
        std::string date;      // на какую дату, если источник её даёт
        std::vector<Item> items;
    };

    Data Snapshot();           // мгновенно; первый вызов запускает обновление
    void Reload();             // сменили город или валюту
    bool Enabled();            // карточка на главной включена
}
