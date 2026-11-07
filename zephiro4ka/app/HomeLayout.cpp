#include "HomeLayout.h"

#include "Settings.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

using Theme::Px;

namespace {
    struct Entry {
        float x = 0, y = 0, s = 1;     // куда хотят (x, y — доли окна)
        float shownX = 0, shownY = 0, shownS = 1; // где нарисован сейчас — плавно едет к цели
    };
    std::unordered_map<std::string, Entry> g_entries;
    int g_editing = -1; // -1 — ещё не читали из файла

    struct Box { std::string id, label; ImVec2 min, max; bool canResize; };
    std::vector<Box> g_boxes;

    struct Drag { std::string id; bool resize = false; ImVec2 mouse; float x = 0, y = 0, s = 1; ImVec2 min, max; };
    Drag g_drag;

    std::string Key(const std::string& id, const char* what) { return id + "_" + what; }

    Entry& Get(const std::string& id) {
        auto [it, fresh] = g_entries.try_emplace(id);
        if (fresh) {
            Entry& e = it->second;
            e.x = std::atoi(Config::Value("layout", Key(id, "x").c_str(), "0").c_str()) / 10000.0f;
            e.y = std::atoi(Config::Value("layout", Key(id, "y").c_str(), "0").c_str()) / 10000.0f;
            e.s = std::clamp(std::atoi(Config::Value("layout", Key(id, "s").c_str(), "1000").c_str()) / 1000.0f, 0.4f, 2.5f);
            e.shownX = e.x, e.shownY = e.y, e.shownS = e.s;
        }
        return it->second;
    }

    void Store(const std::string& id, const Entry& e) {
        Config::SetValue("layout", Key(id, "x").c_str(), std::to_string(std::lround(e.x * 10000.0f)));
        Config::SetValue("layout", Key(id, "y").c_str(), std::to_string(std::lround(e.y * 10000.0f)));
        Config::SetValue("layout", Key(id, "s").c_str(), std::to_string(std::lround(e.s * 1000.0f)));
    }

    void Smooth(Entry& e) {
        const float k = std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
        e.shownX += (e.x - e.shownX) * k, e.shownY += (e.y - e.shownY) * k, e.shownS += (e.s - e.shownS) * k;
        if (std::fabs(e.x - e.shownX) < 1e-4f) e.shownX = e.x;
        if (std::fabs(e.y - e.shownY) < 1e-4f) e.shownY = e.y;
        if (std::fabs(e.s - e.shownS) < 1e-4f) e.shownS = e.s;
    }
}

bool HomeLayout::Editing() {
    if (g_editing < 0) g_editing = Config::Value("layout", "edit", "0") == "1";
    return g_editing == 1;
}

void HomeLayout::SetEditing(bool on) {
    g_editing = on ? 1 : 0;
    Config::SetValue("layout", "edit", on ? "1" : "0");
    g_drag = {};
}

ImVec2 HomeLayout::Shift(const char* id, ImVec2 screen) {
    Entry& e = Get(id);
    Smooth(e);
    return ImVec2(e.shownX * screen.x, e.shownY * screen.y);
}

float HomeLayout::Scale(const char* id) { return Get(id).shownS; }

bool HomeLayout::Moved(const char* id) {
    const Entry& e = Get(id);
    return e.x != 0.0f || e.y != 0.0f || e.s != 1.0f;
}

void HomeLayout::Report(const char* id, const char* label, ImVec2 min, ImVec2 max, bool canResize) {
    if (Editing()) g_boxes.push_back({ id, label, min, max, canResize });
}

void HomeLayout::Reset() {
    for (auto& [id, e] : g_entries) { e.x = e.y = 0.0f; e.s = 1.0f; Store(id, e); }
    g_drag = {};
}

void HomeLayout::DrawEditor(ImVec2 screen, float top, bool pageOpen) {
    std::vector<Box> boxes;
    boxes.swap(g_boxes);
    static float s_anim = 0.0f;
    s_anim += ((Editing() && !pageOpen ? 1.0f : 0.0f) - s_anim) * std::min(1.0f, ImGui::GetIO().DeltaTime * 10.0f);
    if (!Editing() || pageOpen || s_anim < 0.01f) { if (!Editing()) g_drag = {}; return; }
    const float ease = 1.0f - std::pow(1.0f - s_anim, 3.0f);

    ImGui::SetNextWindowPos(ImVec2(0, top));
    ImGui::SetNextWindowSize(ImVec2(screen.x, screen.y - top));
    ImGui::SetNextWindowFocus(); // поверх всего главного экрана, пока правим
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##layoutEditor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    dl->AddRectFilled(ImVec2(0, top), screen, IM_COL32(0, 0, 0, static_cast<int>(70 * ease))); // лёгкое затемнение: видно, что это режим правки

    bool snappedCenter = false;
    for (const Box& box : boxes) {
        Entry& e = Get(box.id);
        const bool dragged = g_drag.id == box.id;
        ImGui::PushID(box.id.c_str());
        ImGui::SetCursorScreenPos(box.min);
        ImGui::InvisibleButton("##box", ImVec2(std::max(1.0f, box.max.x - box.min.x), std::max(1.0f, box.max.y - box.min.y)));
        const bool hovered = ImGui::IsItemHovered();
        const float handle = Px(18);
        const bool nearCorner = box.canResize && io.MousePos.x > box.max.x - handle && io.MousePos.y > box.max.y - handle;
        if (ImGui::IsItemActivated()) {
            g_drag = { box.id, nearCorner, io.MousePos, e.x, e.y, e.s, box.min, box.max };
        }
        if (dragged && ImGui::IsItemActive()) {
            ImVec2 d(io.MousePos.x - g_drag.mouse.x, io.MousePos.y - g_drag.mouse.y);
            if (g_drag.resize) {
                const float w = g_drag.max.x - g_drag.min.x, h = g_drag.max.y - g_drag.min.y;
                e.s = std::clamp(g_drag.s * (w + h + d.x + d.y) / std::max(1.0f, w + h), 0.5f, 2.2f);
            } else {
                d.x = std::clamp(d.x, -g_drag.min.x, screen.x - g_drag.max.x); // не уводить за край окна
                d.y = std::clamp(d.y, top - g_drag.min.y, screen.y - g_drag.max.y);
                const float center = (g_drag.min.x + g_drag.max.x) * 0.5f + d.x;
                if (std::fabs(center - screen.x * 0.5f) < Px(10)) { d.x += screen.x * 0.5f - center; snappedCenter = true; } // прилипает к середине
                e.x = g_drag.x + d.x / screen.x;
                e.y = g_drag.y + d.y / screen.y;
            }
            e.shownX = e.x, e.shownY = e.y, e.shownS = e.s; // пока тянешь — без задержки
        }
        if (dragged && ImGui::IsItemDeactivated()) { Store(box.id, e); g_drag = {}; }
        if (hovered || dragged)
            ImGui::SetMouseCursor(nearCorner || (dragged && g_drag.resize) ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeAll);
        ImGui::PopID();

        const ImU32 accent = Theme::Accent().WithAlpha((dragged ? 0.95f : hovered ? 0.8f : 0.5f) * ease);
        dl->AddRectFilled(box.min, box.max, Theme::Accent().WithAlpha((dragged ? 0.16f : hovered ? 0.1f : 0.05f) * ease), Px(16));
        dl->AddRect(box.min, box.max, accent, Px(16), 0, Px(dragged ? 2.0f : 1.4f));
        // подпись блока
        const ImVec2 label = Widgets::TextSize(Theme::semibold, box.label.c_str());
        const ImVec2 chip(box.min.x + Px(10), box.min.y + Px(8));
        dl->AddRectFilled(chip, ImVec2(chip.x + label.x + Px(16), chip.y + label.y + Px(8)), Theme::Accent().WithAlpha(0.85f * ease), Px(8));
        Widgets::Text(dl, Theme::semibold, ImVec2(chip.x + Px(8), chip.y + Px(4)), IM_COL32(255, 255, 255, static_cast<int>(255 * ease)), box.label.c_str());
        if (box.canResize) { // уголок для размера
            for (int i = 1; i <= 3; ++i)
                dl->AddLine(ImVec2(box.max.x - Px(5.0f * i) - Px(3), box.max.y - Px(3)), ImVec2(box.max.x - Px(3), box.max.y - Px(5.0f * i) - Px(3)), accent, Px(1.6f));
            if (dragged && g_drag.resize) {
                char text[32];
                snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::lround(e.s * 100.0f)));
                Widgets::Text(dl, Theme::semibold, ImVec2(box.max.x - Px(54), box.max.y - Px(30)), IM_COL32(255, 255, 255, 230), text);
            }
        }
    }
    if (snappedCenter) dl->AddLine(ImVec2(screen.x * 0.5f, top), ImVec2(screen.x * 0.5f, screen.y), Theme::Accent().WithAlpha(0.5f), Px(1));

    // панель сверху: что происходит и как выйти
    const char* hint = "Режим правки: тяни блок мышью, уголок — размер";
    const float hintW = Widgets::TextSize(Theme::semibold, hint).x;
    const float barW = hintW + Px(250), barH = Px(46);
    const ImVec2 bar(screen.x * 0.5f - barW * 0.5f, top + Px(10) - (1.0f - ease) * Px(16));
    dl->AddRectFilled(bar, ImVec2(bar.x + barW, bar.y + barH), Theme::Surface(0.96f), barH * 0.5f);
    dl->AddRect(bar, ImVec2(bar.x + barW, bar.y + barH), Theme::Accent().WithAlpha(0.5f * ease), barH * 0.5f, 0, Px(1.2f));
    Widgets::Text(dl, Theme::semibold, ImVec2(bar.x + Px(20), bar.y + (barH - Theme::semibold->LegacySize) * 0.5f), Theme::kInk, hint);
    ImGui::SetCursorScreenPos(ImVec2(bar.x + Px(32) + hintW, bar.y + (barH - ImGui::GetFrameHeight()) * 0.5f));
    if (ImGui::Button("сбросить")) Reset();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::Accent().WithAlpha(0.9f));
    if (ImGui::Button("  готово  ")) SetEditing(false);
    ImGui::PopStyleColor();
    ImGui::End();
}
