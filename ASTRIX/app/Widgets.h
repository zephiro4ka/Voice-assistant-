#pragma once
// Свои виджеты в стиле Ани: иконки, «стеклянные» кнопки, переключатель, пузыри чата, текст.

#include "State.h"
#include "Theme.h"

#include <string>

namespace Widgets {
    enum class Icon { Chat, Gear, Minimize, Maximize, Close, Mic, Send, Stop,
                      User, Wave, Spark, Search, Layers, Grid, Sun, Home, Play, Pause, Next, Previous, Phone, Flow, Plus, Trash, Chevron, Lock, Book, Download }; // разделы, меню, плеер, сценарии

    void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

    // plate — рисовать подложку и без наведения (у кнопок окна её можно выключить в настройках)
    bool IconButton(const char* id, ImVec2 pos, float size, Icon icon, bool danger = false, bool plate = true);
    bool Toggle(const char* id, ImVec2 pos, bool* value, ImU32 accent);
    bool Chip(const char* id, ImVec2 pos, const char* label, ImVec2* outSize = nullptr);
    // Кружок цвета (любимый цвет); выбранный — с кольцом
    bool Swatch(const char* id, ImVec2 center, float radius, ImU32 color, bool selected);
    // Плитка темы: превью фона и панели, подпись снизу
    bool ThemeTile(const char* id, ImVec2 pos, ImVec2 size, ImU32 top, ImU32 bottom, ImU32 panel, const char* label, bool selected);

    // Выпадающий список — тёмная «таблетка» со стрелкой ⌄ (как BeginCombo/EndCombo, но в стиле Ани)
    bool BeginCombo(const char* id, const char* preview, float width, int flags = 0);
    void EndCombo();
    bool ComboItem(const char* label, bool selected); // пункт списка: крупный, у выбранного — ✓
    bool Combo(const char* id, int* current, const char* const items[], int count, float width);
    // Тонкий ползунок с круглой ручкой и значением справа; true — значение меняется
    bool Slider(const char* id, int* value, int min, int max, const char* format, float width);

    // Плавная прокрутка колесом: звать сразу после BeginChild(..., ImGuiWindowFlags_NoScrollWithMouse)
    void SmoothScroll(float* scrollTo = nullptr); // scrollTo >= 0 — плавно доехать туда (потом станет -1)
    // Меню, которое появляется плавно (BeginPopup + плавная прозрачность); закрывать EndPopupAnimated
    bool BeginPopupAnimated(const char* id);
    void EndPopupAnimated();

    void StatusDot(ImDrawList* dl, ImVec2 pos, const char* label, int state); // 0 — ждём, 1 — ок, 2 — ошибка
    void ChatBubble(const ChatMessage& message, float width);

    ImVec2 TextSize(ImFont* font, const char* text);
    void Text(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 color, const char* text);
    // По центру, с переносом по словам; если не влезает — показывает последние строки
    void TextWrappedCentered(ImDrawList* dl, ImFont* font, float centerX, float top, float maxWidth, ImU32 color, const std::string& text, int maxLines);
}
