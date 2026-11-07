#include "Widgets.h"

#include "Visuals.h"

#include "../ImGui/imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

using Theme::Px;

void Widgets::DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 color) {
    const float w = Px(1.7f);
    auto line = [&](float x1, float y1, float x2, float y2, float width) {
        dl->AddLine(ImVec2(c.x + s * x1, c.y + s * y1), ImVec2(c.x + s * x2, c.y + s * y2), color, width);
    };
    switch (icon) {
    case Icon::Chat:
        dl->AddRect(ImVec2(c.x - s * 0.5f, c.y - s * 0.4f), ImVec2(c.x + s * 0.5f, c.y + s * 0.25f), color, Px(3), 0, w);
        line(-0.3f, 0.25f, -0.38f, 0.5f, w);
        line(-0.38f, 0.5f, -0.08f, 0.25f, w);
        break;
    case Icon::Gear:
        dl->AddCircle(c, s * 0.22f, color, 16, w);
        dl->AddCircle(c, s * 0.36f, color, 20, w);
        for (int i = 0; i < 8; ++i) {
            const float a = i * IM_PI / 4.0f;
            line(std::cos(a) * 0.34f, std::sin(a) * 0.34f, std::cos(a) * 0.5f, std::sin(a) * 0.5f, w * 1.3f);
        }
        break;
    case Icon::Minimize:
        line(-0.35f, 0, 0.35f, 0, w);
        break;
    case Icon::Maximize:
        dl->AddRect(ImVec2(c.x - s * 0.3f, c.y - s * 0.3f), ImVec2(c.x + s * 0.3f, c.y + s * 0.3f), color, Px(2), 0, w);
        break;
    case Icon::Close:
        line(-0.3f, -0.3f, 0.3f, 0.3f, w);
        line(0.3f, -0.3f, -0.3f, 0.3f, w);
        break;
    case Icon::Mic:
        dl->AddRectFilled(ImVec2(c.x - s * 0.17f, c.y - s * 0.45f), ImVec2(c.x + s * 0.17f, c.y + s * 0.12f), color, s * 0.17f);
        dl->PathArcTo(ImVec2(c.x, c.y - s * 0.05f), s * 0.32f, 0.0f, IM_PI, 16);
        dl->PathStroke(color, 0, w * 1.1f);
        line(0, 0.27f, 0, 0.45f, w * 1.1f);
        break;
    case Icon::Send:
        line(-0.35f, 0, 0.35f, 0, w);
        line(0.05f, -0.3f, 0.35f, 0, w);
        line(0.05f, 0.3f, 0.35f, 0, w);
        break;
    case Icon::Stop:
        dl->AddRectFilled(ImVec2(c.x - s * 0.22f, c.y - s * 0.22f), ImVec2(c.x + s * 0.22f, c.y + s * 0.22f), color, Px(3));
        break;
    case Icon::User: // голова и плечи
        dl->AddCircle(ImVec2(c.x, c.y - s * 0.18f), s * 0.2f, color, 20, w);
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.48f), s * 0.4f, IM_PI * 1.1f, IM_PI * 1.9f, 16);
        dl->PathStroke(color, 0, w);
        break;
    case Icon::Wave: // звуковая волна
        for (int i = 0; i < 5; ++i) {
            static const float kHeight[] = { 0.18f, 0.34f, 0.48f, 0.3f, 0.16f };
            const float x = (i - 2) * 0.2f;
            line(x, -kHeight[i], x, kHeight[i], w * 1.2f);
        }
        break;
    case Icon::Spark: { // четырёхлучевая звёздочка ✦
        const ImVec2 p[] = { { c.x, c.y - s * 0.5f }, { c.x + s * 0.12f, c.y - s * 0.12f }, { c.x + s * 0.5f, c.y }, { c.x + s * 0.12f, c.y + s * 0.12f },
                             { c.x, c.y + s * 0.5f }, { c.x - s * 0.12f, c.y + s * 0.12f }, { c.x - s * 0.5f, c.y }, { c.x - s * 0.12f, c.y - s * 0.12f } };
        dl->AddPolyline(p, 8, color, ImDrawFlags_Closed, w);
        break;
    }
    case Icon::Search: // лупа
        dl->AddCircle(ImVec2(c.x - s * 0.08f, c.y - s * 0.08f), s * 0.28f, color, 20, w);
        line(0.13f, 0.13f, 0.42f, 0.42f, w * 1.2f);
        break;
    case Icon::Layers: // окно поверх окна
        dl->AddRect(ImVec2(c.x - s * 0.45f, c.y - s * 0.2f), ImVec2(c.x + s * 0.2f, c.y + s * 0.4f), color, Px(2), 0, w);
        dl->AddRectFilled(ImVec2(c.x - s * 0.2f, c.y - s * 0.45f), ImVec2(c.x + s * 0.45f, c.y + s * 0.15f), color, Px(2));
        break;
    case Icon::Sun: // солнышко — «Внешний вид»
        dl->AddCircle(c, s * 0.2f, color, 16, w);
        for (int i = 0; i < 8; ++i) {
            const float a = i * IM_PI / 4.0f;
            line(std::cos(a) * 0.32f, std::sin(a) * 0.32f, std::cos(a) * 0.46f, std::sin(a) * 0.46f, w);
        }
        break;
    case Icon::Home: // домик
        line(-0.42f, -0.02f, 0.0f, -0.42f, w);
        line(0.0f, -0.42f, 0.42f, -0.02f, w);
        dl->AddRect(ImVec2(c.x - s * 0.3f, c.y - s * 0.12f), ImVec2(c.x + s * 0.3f, c.y + s * 0.4f), color, Px(2), ImDrawFlags_RoundCornersBottom, w);
        break;
    case Icon::Play: {
        const ImVec2 p[] = { { c.x - s * 0.22f, c.y - s * 0.32f }, { c.x + s * 0.32f, c.y }, { c.x - s * 0.22f, c.y + s * 0.32f } };
        dl->AddTriangleFilled(p[0], p[1], p[2], color);
        break;
    }
    case Icon::Pause:
        dl->AddRectFilled(ImVec2(c.x - s * 0.26f, c.y - s * 0.3f), ImVec2(c.x - s * 0.08f, c.y + s * 0.3f), color, Px(1.5f));
        dl->AddRectFilled(ImVec2(c.x + s * 0.08f, c.y - s * 0.3f), ImVec2(c.x + s * 0.26f, c.y + s * 0.3f), color, Px(1.5f));
        break;
    case Icon::Next:
    case Icon::Previous: {
        const float d = icon == Icon::Next ? 1.0f : -1.0f;
        dl->AddTriangleFilled(ImVec2(c.x - d * s * 0.3f, c.y - s * 0.28f), ImVec2(c.x + d * s * 0.16f, c.y), ImVec2(c.x - d * s * 0.3f, c.y + s * 0.28f), color);
        line(d * 0.26f, -0.28f, d * 0.26f, 0.28f, w * 1.3f);
        break;
    }
    case Icon::Phone:
        dl->AddRect(ImVec2(c.x - s * 0.24f, c.y - s * 0.42f), ImVec2(c.x + s * 0.24f, c.y + s * 0.42f), color, Px(3), 0, w);
        line(-0.06f, 0.3f, 0.06f, 0.3f, w);
        break;
    case Icon::Flow: // блоки, соединённые стрелкой — «Сценарии»
        dl->AddRect(ImVec2(c.x - s * 0.46f, c.y - s * 0.38f), ImVec2(c.x - s * 0.1f, c.y - s * 0.06f), color, Px(2), 0, w);
        dl->AddRect(ImVec2(c.x + s * 0.1f, c.y + s * 0.06f), ImVec2(c.x + s * 0.46f, c.y + s * 0.38f), color, Px(2), 0, w);
        line(-0.28f, -0.06f, -0.28f, 0.22f, w);
        line(-0.28f, 0.22f, 0.1f, 0.22f, w);
        break;
    case Icon::Plus:
        line(-0.3f, 0, 0.3f, 0, w * 1.2f);
        line(0, -0.3f, 0, 0.3f, w * 1.2f);
        break;
    case Icon::Trash:
        line(-0.32f, -0.26f, 0.32f, -0.26f, w);
        line(-0.1f, -0.38f, 0.1f, -0.38f, w);
        dl->AddRect(ImVec2(c.x - s * 0.24f, c.y - s * 0.2f), ImVec2(c.x + s * 0.24f, c.y + s * 0.4f), color, Px(2), ImDrawFlags_RoundCornersBottom, w);
        break;
    case Icon::Chevron: // ⌄
        line(-0.4f, -0.18f, 0.0f, 0.22f, w);
        line(0.0f, 0.22f, 0.4f, -0.18f, w);
        break;
    case Icon::Grid: // четыре плитки
        for (int i = 0; i < 4; ++i) {
            const float x = (i % 2) ? 0.06f : -0.42f, y = (i / 2) ? 0.06f : -0.42f;
            dl->AddRect(ImVec2(c.x + s * x, c.y + s * y), ImVec2(c.x + s * (x + 0.36f), c.y + s * (y + 0.36f)), color, Px(2), 0, w);
        }
        break;
    }
}

namespace {
    // Плавная анимация наведения для любого элемента: 0 — нет, 1 — мышь на нём
    float HoverAnim(ImGuiID id, bool hovered, float speed = 16.0f) {
        static std::unordered_map<ImGuiID, float> s_hover;
        float& t = s_hover[id];
        t += ((hovered ? 1.0f : 0.0f) - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * speed);
        return t;
    }

    ImU32 Lerp(ImU32 a, ImU32 b, float t) {
        const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
    }
}

bool Widgets::IconButton(const char* id, ImVec2 pos, float size, Icon icon, bool danger, bool plate) {
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const float t = HoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size, pos.y + size);
    const ImU32 idle = plate ? IM_COL32(255, 255, 255, 10) : IM_COL32(255, 255, 255, 0);
    const ImU32 hover = danger ? IM_COL32(232, 72, 104, 210) : IM_COL32(255, 255, 255, 26);
    dl->AddRectFilled(pos, max, Lerp(idle, hover, t), Px(12));
    if (plate || t > 0.01f) dl->AddRect(pos, max, IM_COL32(255, 255, 255, static_cast<int>(22 * (plate ? 1.0f : t))), Px(12));
    DrawIcon(dl, icon, ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f), size * (0.42f + 0.03f * t), Lerp(Theme::kInkSoft, IM_COL32(255, 255, 255, 240), t));
    return pressed;
}

bool Widgets::BeginCombo(const char* id, const char* preview, float width, int flags) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 70)); // поле — тёмная «таблетка»
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 95));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 110));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 12));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::Background(1).Mix(Theme::Background(0), 0.6f).WithAlpha(0.98f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Px(12));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, Px(14));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(6), Px(6)));
    ImGui::PushFont(Theme::semibold, Theme::semibold->LegacySize);
    ImGui::SetNextItemWidth(width);
    const bool open = ImGui::BeginCombo(id, preview, flags | ImGuiComboFlags_NoArrowButton);
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const float t = HoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered() || open);
    ImDrawList* dl = open ? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
    if (open) dl->AddRect(a, b, Theme::Accent().WithAlpha(0.9f), Px(12), 0, Px(1.6f)); // открытый список — рамка любимого цвета
    // стрелка: вниз, а у открытого — вверх
    const ImVec2 c(b.x - Px(16), (a.y + b.y) * 0.5f + Px(1));
    const float s = Px(10) * 0.4f;
    const float flip = open ? -1.0f : 1.0f;
    const ImU32 color = Lerp(Theme::kInkMuted, Theme::kInk, t);
    dl->AddLine(ImVec2(c.x - s, c.y - s * 0.45f * flip), ImVec2(c.x, c.y + s * 0.55f * flip), color, Px(1.7f));
    dl->AddLine(ImVec2(c.x, c.y + s * 0.55f * flip), ImVec2(c.x + s, c.y - s * 0.45f * flip), color, Px(1.7f));
    if (!open) { // закрыт — стиль снимаем сразу; открыт — в EndCombo, после того как список закроется
        ImGui::PopFont();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);
        return false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Px(4), Px(3))); // только внутри списка
    return true;
}

void Widgets::EndCombo() {
    ImGui::PopStyleVar(); // отступы пунктов — внутри списка
    ImGui::EndCombo();
    ImGui::PopFont();     // и стиль поля — снаружи, где его ставили
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
}

// Пункт списка: высокий, у выбранного — галочка справа
bool Widgets::ComboItem(const char* label, bool selected) {
    const bool clicked = ImGui::Selectable(label, selected, 0, ImVec2(0, Px(30)));
    if (selected) {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(b.x - Px(16), (a.y + b.y) * 0.5f);
        dl->AddLine(ImVec2(c.x - Px(5), c.y), ImVec2(c.x - Px(1.5f), c.y + Px(3.5f)), Theme::kInkSoft, Px(1.6f));
        dl->AddLine(ImVec2(c.x - Px(1.5f), c.y + Px(3.5f)), ImVec2(c.x + Px(5), c.y - Px(3.5f)), Theme::kInkSoft, Px(1.6f));
    }
    return clicked;
}

bool Widgets::Combo(const char* id, int* current, const char* const items[], int count, float width) {
    bool changed = false;
    if (BeginCombo(id, *current >= 0 && *current < count ? items[*current] : "", width)) {
        for (int i = 0; i < count; ++i)
            if (ComboItem(items[i], i == *current)) { *current = i; changed = true; }
        EndCombo();
    }
    return changed;
}

bool Widgets::Slider(const char* id, int* value, int min, int max, const char* format, float width) {
    char text[32];
    snprintf(text, sizeof(text), format, *value);
    const float valueWidth = Px(56), height = Px(24);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float trackWidth = std::max(Px(60), width - valueWidth);
    ImGui::InvisibleButton(id, ImVec2(width, height));
    const ImGuiID item = ImGui::GetItemID();
    const bool active = ImGui::IsItemActive();
    bool changed = false;
    if (active && max > min) {
        const float k = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / trackWidth, 0.0f, 1.0f);
        const int next = min + static_cast<int>(std::lround(k * (max - min)));
        if (next != *value) { *value = next; changed = true; ImGui::MarkItemEdited(item); }
    }
    const float hover = HoverAnim(item, ImGui::IsItemHovered() || active);
    const float k = max > min ? static_cast<float>(*value - min) / (max - min) : 0.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = pos.y + height * 0.5f, track = Px(4);
    dl->AddRectFilled(ImVec2(pos.x, cy - track * 0.5f), ImVec2(pos.x + trackWidth, cy + track * 0.5f), IM_COL32(0, 0, 0, 90), track);
    dl->AddRectFilled(ImVec2(pos.x, cy - track * 0.5f), ImVec2(pos.x + trackWidth * k, cy + track * 0.5f), Theme::Accent().WithAlpha(0.85f), track);
    const ImVec2 knob(pos.x + trackWidth * k, cy);
    dl->AddCircleFilled(knob, Px(8) + Px(1.5f) * hover, IM_COL32(0, 0, 0, 60), 20); // тень ручки
    dl->AddCircleFilled(knob, Px(7) + Px(1) * hover, IM_COL32(255, 255, 255, 240), 20);
    const float tw = TextSize(Theme::semibold, text).x;
    Text(dl, Theme::semibold, ImVec2(pos.x + width - tw, cy - Theme::semibold->LegacySize * 0.5f), Theme::kInkSoft, text);
    return changed;
}

void Widgets::SmoothScroll(float* scrollTo) {
    static std::unordered_map<ImGuiID, float> s_target, s_last;
    const ImGuiID id = ImGui::GetID("##smoothScroll");
    const float now = ImGui::GetScrollY(), max = ImGui::GetScrollMaxY();
    auto [target, fresh] = s_target.try_emplace(id, now);
    float& last = s_last.try_emplace(id, now).first->second;
    if (std::fabs(now - last) > 1.0f) target->second = now; // полосу прокрутки потянули мышью — догоняем её
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel != 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        target->second = std::clamp(target->second - io.MouseWheel * Px(140), 0.0f, max);
    if (scrollTo && *scrollTo >= 0.0f) { target->second = *scrollTo; *scrollTo = -1.0f; }
    target->second = std::clamp(target->second, 0.0f, max);
    const float next = now + (target->second - now) * std::min(1.0f, io.DeltaTime * 12.0f); // мягкое торможение
    ImGui::SetScrollY(std::fabs(target->second - next) < 0.5f ? target->second : next);
    last = std::fabs(target->second - next) < 0.5f ? target->second : next;
}

bool Widgets::BeginPopupAnimated(const char* id) {
    static std::unordered_map<ImGuiID, float> s_open;
    const ImGuiID key = ImGui::GetID(id);
    float& t = s_open[key];
    if (!ImGui::IsPopupOpen(id)) { t = 0.0f; return false; }
    t = std::min(1.0f, t + ImGui::GetIO().DeltaTime * 7.0f);
    const float ease = 1.0f - std::pow(1.0f - t, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (0.15f + 0.85f * ease));
    if (ImGui::BeginPopup(id)) return true;
    ImGui::PopStyleVar();
    return false;
}

void Widgets::EndPopupAnimated() {
    ImGui::EndPopup();
    ImGui::PopStyleVar();
}

bool Widgets::Swatch(const char* id, ImVec2 center, float radius, ImU32 color, bool selected) {
    ImGui::SetCursorScreenPos(ImVec2(center.x - radius - Px(4), center.y - radius - Px(4)));
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(radius * 2 + Px(8), radius * 2 + Px(8)));
    const ImGuiID item = ImGui::GetItemID();
    const float hover = HoverAnim(item, ImGui::IsItemHovered());
    const float pick = HoverAnim(item + 1, selected, 12.0f); // кольцо выбора появляется плавно
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(center, radius * (1.0f + 0.08f * hover), color, 32);
    if (pick > 0.01f) dl->AddCircle(center, radius + Px(4), IM_COL32(255, 255, 255, static_cast<int>(200 * pick)), 32, Px(1.8f));
    return pressed;
}

bool Widgets::ThemeTile(const char* id, ImVec2 pos, ImVec2 size, ImU32 top, ImU32 bottom, ImU32 panel, const char* label, bool selected) {
    const float labelHeight = Theme::semibold->LegacySize + Px(8);
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size.x, size.y + labelHeight));
    const ImGuiID item = ImGui::GetItemID();
    const float hover = HoverAnim(item, ImGui::IsItemHovered());
    const float pick = HoverAnim(item + 1, selected, 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lift = Px(2) * hover;
    const ImVec2 a(pos.x, pos.y - lift), b(pos.x + size.x, pos.y + size.y - lift);
    const float rounding = Px(14);
    dl->AddRectFilled(a, b, top, rounding);
    dl->AddRectFilledMultiColor(ImVec2(a.x, a.y + size.y * 0.45f), ImVec2(b.x, b.y - rounding * 0.5f), Lerp(top, bottom, 0.3f), Lerp(top, bottom, 0.3f), bottom, bottom);
    dl->AddRectFilled(ImVec2(a.x, b.y - rounding), b, bottom, rounding, ImDrawFlags_RoundCornersBottom);
    // «панель» внутри превью
    dl->AddRectFilled(ImVec2(a.x + size.x * 0.18f, a.y + size.y * 0.42f), ImVec2(b.x - size.x * 0.18f, b.y + Px(1)), panel, Px(8), ImDrawFlags_RoundCornersTop);
    dl->AddRect(a, b, Lerp(IM_COL32(255, 255, 255, 30), Theme::Accent().WithAlpha(1.0f), pick), rounding, 0, Px(1.0f + 1.2f * pick));
    const float textWidth = TextSize(Theme::semibold, label).x;
    Text(dl, Theme::semibold, ImVec2(pos.x + (size.x - textWidth) * 0.5f, pos.y + size.y + Px(6)), selected ? Theme::kInk : Theme::kInkMuted, label);
    return pressed;
}

bool Widgets::Toggle(const char* id, ImVec2 pos, bool* value, ImU32 accent) {
    const float h = Px(18), w = Px(32);
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
    if (pressed) *value = !*value;

    static std::unordered_map<ImGuiID, float> s_anim; // плавное движение «шарика»
    float& t = s_anim[ImGui::GetItemID()];
    t += ((*value ? 1.0f : 0.0f) - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);

    const ImVec4 on = ImGui::ColorConvertU32ToFloat4(accent);
    const ImVec4 off = ImGui::ColorConvertU32ToFloat4(IM_COL32(255, 255, 255, 30));
    const ImVec4 mix(off.x + (on.x - off.x) * t, off.y + (on.y - off.y) * t, off.z + (on.z - off.z) * t, off.w + (on.w - off.w) * t);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), ImGui::ColorConvertFloat4ToU32(mix), h * 0.5f);
    dl->AddCircleFilled(ImVec2(pos.x + h * 0.5f + t * (w - h), pos.y + h * 0.5f), h * 0.36f, IM_COL32(255, 255, 255, 235), 16);
    return pressed;
}

bool Widgets::Chip(const char* id, ImVec2 pos, const char* label, ImVec2* outSize) {
    const ImVec2 text = TextSize(Theme::semibold, label);
    const ImVec2 size(text.x + Px(28), text.y + Px(14));
    if (outSize) *outSize = size;

    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    if (hovered) dl->AddRectFilled(pos, max, IM_COL32(255, 255, 255, 18), size.y * 0.5f);
    dl->AddRect(pos, max, hovered ? IM_COL32(255, 255, 255, 55) : IM_COL32(255, 255, 255, 26), size.y * 0.5f);
    Text(dl, Theme::semibold, ImVec2(pos.x + Px(14), pos.y + Px(7)), hovered ? Theme::kInk : Theme::kInkMuted, label);
    return pressed;
}

void Widgets::StatusDot(ImDrawList* dl, ImVec2 pos, const char* label, int state) {
    const ImU32 color = state == 1 ? Theme::kOk : state == 2 ? Theme::kBad : IM_COL32(255, 255, 255, 80);
    const float r = Px(3.2f);
    const ImVec2 dot(pos.x + r, pos.y + Theme::semibold->LegacySize * 0.5f);
    if (state == 1) dl->AddCircleFilled(dot, r * 2.4f, IM_COL32(110, 231, 183, 40), 16);
    dl->AddCircleFilled(dot, r, color, 12);
    Text(dl, Theme::semibold, ImVec2(pos.x + r * 2 + Px(7), pos.y), IM_COL32(244, 239, 250, 110), label);
}

void Widgets::ChatBubble(const ChatMessage& message, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pad = Px(12);
    ImGui::PushFont(Theme::body, Theme::body->LegacySize);

    if (message.role == Role::System) {
        const ImVec2 size = ImGui::CalcTextSize(message.text.c_str(), nullptr, false, width - pad * 2);
        ImGui::SetCursorPosX(std::max(0.0f, (width - size.x) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(244, 239, 250, 100));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size.x + 1);
        ImGui::TextUnformatted(message.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, Px(4)));
        ImGui::PopFont();
        return;
    }

    const float maxWidth = width * 0.82f;
    const ImVec2 textSize = ImGui::CalcTextSize(message.text.c_str(), nullptr, false, maxWidth - pad * 2);
    const ImVec2 size(textSize.x + pad * 2, textSize.y + pad * 1.6f);
    const bool fromUser = message.role == Role::User;

    ImGui::SetCursorPosX(fromUser ? width - size.x : 0.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 max(p.x + size.x, p.y + size.y);
    const float rounding = Px(16);
    if (fromUser) {
        Visuals::GradientRoundRect(dl, p, max, rounding, ImVec2(p.x, p.y + size.y * 0.5f), IM_COL32(140, 100, 235, 150), IM_COL32(220, 95, 165, 120));
    } else {
        dl->AddRectFilled(p, max, IM_COL32(255, 255, 255, 16), rounding);
        dl->AddRect(p, max, IM_COL32(255, 255, 255, 18), rounding);
    }

    ImGui::SetCursorScreenPos(ImVec2(p.x + pad, p.y + pad * 0.8f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maxWidth - pad * 2);
    ImGui::TextUnformatted(message.text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x, max.y + Px(8)));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::PopFont();
}

ImVec2 Widgets::TextSize(ImFont* font, const char* text) {
    return font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, text);
}

void Widgets::Text(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 color, const char* text) {
    dl->AddText(font, font->LegacySize, pos, color, text);
}

void Widgets::TextWrappedCentered(ImDrawList* dl, ImFont* font, float centerX, float top, float maxWidth, ImU32 color, const std::string& text, int maxLines) {
    std::vector<std::string> lines;
    std::string line, word;
    auto commitWord = [&] {
        if (word.empty()) return;
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && TextSize(font, candidate.c_str()).x > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char ch : text) {
        if (ch == ' ' || ch == '\n') commitWord();
        else word.push_back(ch);
    }
    commitWord();
    if (!line.empty()) lines.push_back(line);

    if (static_cast<int>(lines.size()) > maxLines) { // показываем хвост — то, что звучит прямо сейчас
        lines.erase(lines.begin(), lines.end() - maxLines);
        lines.front() = "… " + lines.front();
    }
    const float lineHeight = font->LegacySize * 1.35f;
    for (size_t i = 0; i < lines.size(); ++i) {
        const float w = TextSize(font, lines[i].c_str()).x;
        Text(dl, font, ImVec2(centerX - w * 0.5f, top + i * lineHeight), color, lines[i].c_str());
    }
}
