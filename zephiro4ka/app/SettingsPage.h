#pragma once
// Страница настроек на всё окно: разделы слева, поиск по настройкам, карточки с пояснениями.
// Без API Windows — только ImGui и настройки, чтобы переехать на Linux как есть.

#include "../ImGui/imgui.h"

#include <optional>
#include <string>

namespace SettingsPage {
    void Open(int section = -1); // section — номер раздела слева (для проверки и переходов)
    void Close();
    bool IsOpen();
    bool Covers(); // страница открыта полностью — главный экран под ней не видно, его можно не рисовать
    float Visibility(); // 0…1 — насколько страница открыта (фон Ани под ней становится плотным)

    // top — где кончается шапка окна
    void Draw(ImVec2 screen, float top, float dt);

    // «Где настройка погоды?» — найти пункт (сначала по словам, не нашла — спросить нейросеть),
    // открыть его раздел и подсветить оранжевым. Можно звать из любого потока. Пусто — не нашла.
    struct Found { std::string section, title; };
    std::optional<Found> Reveal(const std::string& question, bool askAi = true);
    bool PollReveal(); // из потока интерфейса, раз в кадр: открыть страницу, если просили (true — открыла)
}
