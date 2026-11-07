#pragma once
// «Открой видео „Всё проще, чем кажется“» — нажать именно на ту ссылку, что видна сейчас в браузере.
// Никакого поиска в интернете: Аня читает страницу через специальные возможности Windows (UI Automation),
// находит ссылку с самым похожим названием и нажимает её. Работает на любом сайте, не только на YouTube.

#include <string>

namespace ScreenLinks {
    struct Result {
        bool found = false;
        std::string title;   // название ссылки, на которую нажала
    };

    Result Find(const std::string& spokenTitle);   // только найти, ничего не нажимая
    Result Open(const std::string& spokenTitle);   // найти и нажать

    // «Перейди по первой ссылке», «открой второе видео»: n-й результат на странице (с 1).
    // Результат — ссылка-заголовок (так устроены Google, Яндекс, Bing, YouTube); если таких нет — длинные видимые ссылки.
    Result OpenNth(int n);

    // Поставить курсор в поле поиска на странице (Google, YouTube, любой сайт). false — поля нет.
    bool FocusSearchField();
}
