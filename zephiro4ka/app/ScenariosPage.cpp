#include "ScenariosPage.h"

#include "Launcher.h"
#include "Log.h"
#include "Settings.h"
#include "Scenarios.h"
#include "TextUtils.h"
#include "Theme.h"
#include "Widgets.h"
#include "../ImGui/imgui_internal.h"

#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using Theme::Px;
using Widgets::Icon;
using Scenarios::Step;

namespace {
    bool g_open = false;
    float g_anim = 0.0f;
    float g_chainAnim = 1.0f; // появление блоков при выборе сценария
    std::vector<Scenarios::Scenario> g_list;
    int g_selected = 0;
    bool g_dirty = false;
    std::mutex g_addMutex;
    std::vector<Scenarios::Scenario> g_added; // собрала Аня — добавим в потоке интерфейса

    ImU32 PageColor(float alpha) { return Theme::Background(1).Mix(Theme::Background(0), 0.5f).WithAlpha(alpha); }

    // Цвет блока по типу — чтобы цепочка читалась с одного взгляда
    Theme::Color StepColor(Step step) {
        switch (step) {
        case Step::Launch: return { 125, 175, 245 };
        case Step::OpenLink: return { 110, 210, 180 };
        case Step::Place: return { 250, 165, 125 };
        case Step::Volume: case Step::Media: return { 245, 205, 95 };
        case Step::Wait: return { 192, 196, 206 };
        default: return { 244, 114, 182 };
        }
    }

    // InputText со std::string
    bool InputString(const char* id, std::string& value, const char* hint, float width) {
        ImGui::SetNextItemWidth(width);
        auto resize = [](ImGuiInputTextCallbackData* data) {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* s = static_cast<std::string*>(data->UserData);
                s->resize(data->BufTextLen);
                data->Buf = s->data();
            }
            return 0;
        };
        const bool changed = ImGui::InputTextWithHint(id, hint, value.data(), value.capacity() + 1, ImGuiInputTextFlags_CallbackResize, resize, &value);
        if (ImGui::IsItemDeactivatedAfterEdit()) g_dirty = true;
        return changed;
    }

    void MonitorAndPosition(Scenarios::Block& b, float width) {
        static const char* kPositions[] = { "развернуть", "левая половина", "правая половина", "по центру", "во весь экран (F11)" };
        const int monitors = std::max(1, Scenarios::MonitorCount());
        char preview[32];
        snprintf(preview, sizeof(preview), "монитор %d", b.monitor);
                if (Widgets::BeginCombo("##monitor", preview, std::min(width * 0.4f, Px(150)))) {
            for (int m = 1; m <= std::max(monitors, b.monitor); ++m) {
                snprintf(preview, sizeof(preview), m == 1 ? "монитор %d (основной)" : "монитор %d", m);
                if (Widgets::ComboItem(preview, b.monitor == m)) { b.monitor = m; g_dirty = true; }
            }
            Widgets::EndCombo();
        }
        ImGui::SameLine();
                if (Widgets::Combo("##position", &b.position, kPositions, IM_ARRAYSIZE(kPositions), std::min(width * 0.55f, Px(210)))) g_dirty = true;
    }

    // Поле программы + список из «Пуска», отфильтрованный по набранному
    void AppField(std::string& app, const char* hint, float width) {
        InputString("##app", app, hint, width - Px(96));
        ImGui::SameLine();
                if (Widgets::BeginCombo("##pick", "из Пуска", Px(86), ImGuiComboFlags_HeightLarge)) {
            const std::wstring filter = Text::LowerRu(Text::ToWide(app));
            int shown = 0;
            for (const auto& name : Launcher::AppNames()) {
                if (!filter.empty() && Text::LowerRu(Text::ToWide(name)).find(filter) == std::wstring::npos) continue;
                if (Widgets::ComboItem(name.c_str(), name == app)) { app = name; g_dirty = true; }
                if (++shown >= 60) break;
            }
            if (shown == 0) ImGui::TextDisabled("ничего не нашла");
            Widgets::EndCombo();
        }
    }

    void BlockFields(Scenarios::Block& b, float width) {
        switch (b.step) {
        case Step::Launch: AppField(b.text, "например, NTE или Steam", width); break;
        case Step::OpenLink:
            InputString("##url", b.text, "адрес, например kinopoisk.ru", width);
            MonitorAndPosition(b, width);
            break;
        case Step::Place:
            AppField(b.text, "пусто — окно из прошлого блока", width);
            MonitorAndPosition(b, width);
            break;
        case Step::Volume:
                        Widgets::Slider("##volume", &b.number, 0, 100, "%d%%", std::min(width, Px(320)));
            if (ImGui::IsItemDeactivatedAfterEdit()) g_dirty = true;
            break;
        case Step::Media: {
            static const char* kMedia[] = { "пауза / продолжить", "следующий трек" };
                        if (Widgets::Combo("##media", &b.number, kMedia, 2, std::min(width, Px(240)))) g_dirty = true;
            break;
        }
        case Step::Wait:
                        Widgets::Slider("##wait", &b.number, 1, 60, "%d с", std::min(width, Px(320)));
            if (ImGui::IsItemDeactivatedAfterEdit()) g_dirty = true;
            break;
        case Step::Say: InputString("##say", b.text, "что сказать", width); break;
        default: break;
        }
    }

    Icon StepIcon(Step step) {
        switch (step) {
        case Step::Launch: return Icon::Play;
        case Step::OpenLink: return Icon::Search;
        case Step::Place: return Icon::Layers;
        case Step::Volume: return Icon::Wave;
        case Step::Media: return Icon::Next;
        case Step::Wait: return Icon::Sun;
        default: return Icon::Chat;
        }
    }

    void DrawList(ImDrawList* dl, ImVec2 pos, float width, float height) {
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), Theme::Overlay(8), Px(18));
        dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), Theme::Overlay(20), Px(18));
        float y = pos.y + Px(8);
        for (int i = 0; i < static_cast<int>(g_list.size()); ++i) {
            const ImVec2 a(pos.x + Px(8), y), b(pos.x + width - Px(8), y + Px(52));
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##scenario", ImVec2(b.x - a.x, b.y - a.y)) && g_selected != i) { g_selected = i; g_chainAnim = 0.0f; }
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool on = i == g_selected;
            if (on || hovered) dl->AddRectFilled(a, b, Theme::Overlay(on ? 22 : 12), Px(12));
            if (on) dl->AddRectFilled(ImVec2(a.x, a.y + Px(12)), ImVec2(a.x + Px(3), b.y - Px(12)), Theme::Accent().WithAlpha(1.0f), Px(2));
            const auto& s = g_list[i];
            Widgets::Text(dl, Theme::body, ImVec2(a.x + Px(14), a.y + Px(7)), s.enabled ? Theme::kInk : Theme::kInkMuted, s.name.c_str());
            const std::string phrase = s.phrase.empty() ? "фраза не задана" : "«Аня, " + s.phrase + "»";
            Widgets::Text(dl, Theme::semibold, ImVec2(a.x + Px(14), a.y + Px(29)), Theme::kInkMuted, phrase.c_str());
            y += Px(56);
        }
        const ImVec2 a(pos.x + Px(8), y + Px(4));
        ImGui::SetCursorScreenPos(a);
        if (ImGui::Button("+ новый сценарий", ImVec2(width - Px(16), Px(38)))) {
            g_list.push_back({});
            g_list.back().hasLayout = true; // пустой холст — только «Слушать»
            g_selected = static_cast<int>(g_list.size()) - 1;
            g_chainAnim = 0.0f;
            g_dirty = true;
        }
    }

    // ================================================================ холст сценария
    // Блоки лежат где угодно (координаты холста), стрелки идут от кружка справа к кружку слева у следующего блока.
    // Колесо — масштаб, тянешь пустое место — двигаешь холст. Внизу — палитра: тянешь блок оттуда на холст.
    constexpr int kStartId = 0;          // «Слушать» — у него номер 0
    constexpr float kNodeWidth = 300.0f; // ширина блока на холсте (до масштаба)

    struct Canvas {
        ImVec2 pan{ 60, 40 };
        float zoom = 1.0f, zoomTarget = 1.0f;   // колесо меняет цель — масштаб плавно доезжает
        ImVec2 anchorScreen, anchorCanvas;      // точка под мышью, вокруг которой масштаб
        int selected = -1;               // выделенный блок (новые из палитры подключаются к нему)
        int linkFrom = -1;               // тянем стрелку от этого блока (-1 — не тянем)
        int dragNode = -1;               // двигаем этот блок
        ImVec2 dragOffset;
        int newStep = -1;                // тянем новый блок из палитры
        int lastScenario = -1;
        bool panning = false;
        std::unordered_map<int, float> heights; // высота блоков в прошлом кадре
        std::unordered_map<int, float> appear;  // появление новых блоков
    };
    Canvas g_canvas;

    ImU32 NodeColor(const Scenarios::Block& b, float a) { return (b.color >= 0 ? Theme::FromRgb(b.color) : StepColor(b.step)).WithAlpha(a); }

    Scenarios::Block* FindBlock(Scenarios::Scenario& s, int id) {
        for (auto& b : s.blocks) if (b.id == id) return &b;
        return nullptr;
    }

    std::vector<int>& NextOf(Scenarios::Scenario& s, int id) {
        if (id == kStartId) return s.startNext;
        return FindBlock(s, id)->next;
    }

    ImVec2 NodePos(Scenarios::Scenario& s, int id) {
        if (id == kStartId) return ImVec2(s.startX, s.startY);
        const auto* b = FindBlock(s, id);
        return b ? ImVec2(b->x, b->y) : ImVec2(0, 0);
    }

    // точки стрелки: выход — справа у заголовка, вход — слева у заголовка
    ImVec2 ToScreen(ImVec2 origin, ImVec2 p) { return ImVec2(origin.x + g_canvas.pan.x + p.x * g_canvas.zoom, origin.y + g_canvas.pan.y + p.y * g_canvas.zoom); }
    ImVec2 ToCanvas(ImVec2 origin, ImVec2 p) { return ImVec2((p.x - origin.x - g_canvas.pan.x) / g_canvas.zoom, (p.y - origin.y - g_canvas.pan.y) / g_canvas.zoom); }
    ImVec2 OutPort(ImVec2 origin, ImVec2 node) { return ToScreen(origin, ImVec2(node.x + kNodeWidth, node.y + 24.0f)); }
    ImVec2 InPort(ImVec2 origin, ImVec2 node) { return ToScreen(origin, ImVec2(node.x, node.y + 24.0f)); }

    // Куда крепится стрелка: к ближайшей стороне блока (сбоку или сверху/снизу) — тогда линии короткие и мягкие.
    // Возвращает точки на экране: начало, конец и направления «наружу» от сторон
    struct LinkEnds { ImVec2 a, b, na, nb; };
    float NodeHeight(int id) { return (g_canvas.heights.count(id) ? g_canvas.heights[id] : Px(90) * g_canvas.zoom) / g_canvas.zoom; }

    LinkEnds Ends(ImVec2 origin, ImVec2 from, float fromH, ImVec2 to, float toH) {
        const ImVec2 ca(from.x + kNodeWidth * 0.5f, from.y + fromH * 0.5f), cb(to.x + kNodeWidth * 0.5f, to.y + toH * 0.5f);
        const float dx = cb.x - ca.x, dy = cb.y - ca.y;
        LinkEnds e;
        const bool sideways = std::fabs(dx) > kNodeWidth * 0.75f || std::fabs(dx) * 0.6f > std::fabs(dy);
        if (sideways) { // рядом по горизонтали — из боковой стороны, на уровне заголовка
            const bool right = dx > 0;
            e.a = ToScreen(origin, ImVec2(right ? from.x + kNodeWidth : from.x, from.y + 24.0f));
            e.b = ToScreen(origin, ImVec2(right ? to.x : to.x + kNodeWidth, to.y + 24.0f));
            e.na = ImVec2(right ? 1.0f : -1.0f, 0), e.nb = ImVec2(right ? -1.0f : 1.0f, 0);
        } else { // друг под другом — снизу вверх (или сверху вниз)
            const bool down = dy > 0;
            e.a = ToScreen(origin, ImVec2(from.x + kNodeWidth * 0.5f, down ? from.y + fromH : from.y));
            e.b = ToScreen(origin, ImVec2(to.x + kNodeWidth * 0.5f, down ? to.y : to.y + toH));
            e.na = ImVec2(0, down ? 1.0f : -1.0f), e.nb = ImVec2(0, down ? -1.0f : 1.0f);
        }
        return e;
    }

    // кривая (0), косая (1) или прямая (2) — точки линии, чтобы и нарисовать, и попасть мышью
    std::vector<ImVec2> LinkPoints(const LinkEnds& e, int style) {
        const float dist = std::hypot(e.b.x - e.a.x, e.b.y - e.a.y);
        if (style == 2) return { e.a, e.b };
        std::vector<ImVec2> pts;
        if (style == 1) { // косая: короткий отвод, наискосок, короткий подвод — со скруглёнными изгибами
            const float stub = std::min(Px(28) * g_canvas.zoom, dist * 0.25f);
            const ImVec2 p1(e.a.x + e.na.x * stub, e.a.y + e.na.y * stub), p2(e.b.x + e.nb.x * stub, e.b.y + e.nb.y * stub);
            const ImVec2 corners[] = { e.a, p1, p2, e.b };
            pts.push_back(e.a);
            for (int c = 1; c <= 2; ++c) { // мягкий угол: короткая кривая вокруг каждой точки излома
                const ImVec2 prev = corners[c - 1], cur = corners[c], next = corners[c + 1];
                const ImVec2 in = ImLerp(cur, prev, std::min(0.45f, Px(14) / std::max(1.0f, std::hypot(cur.x - prev.x, cur.y - prev.y))));
                const ImVec2 out = ImLerp(cur, next, std::min(0.45f, Px(14) / std::max(1.0f, std::hypot(next.x - cur.x, next.y - cur.y))));
                for (int i = 0; i <= 8; ++i) pts.push_back(ImBezierQuadraticCalc(in, cur, out, i / 8.0f));
            }
            pts.push_back(e.b);
            return pts;
        }
        // кривая: изгиб пропорционален длине — короткие стрелки почти прямые, длинные мягко «провисают»
        const float k = std::clamp(dist * 0.42f, Px(24) * g_canvas.zoom, Px(110) * g_canvas.zoom);
        const ImVec2 c1(e.a.x + e.na.x * k, e.a.y + e.na.y * k), c2(e.b.x + e.nb.x * k, e.b.y + e.nb.y * k);
        for (int i = 0; i <= 40; ++i) pts.push_back(ImBezierCubicCalc(e.a, c1, c2, e.b, i / 40.0f));
        return pts;
    }

    // к мыши (тянем новую стрелку) — сторона по направлению
    LinkEnds EndsToMouse(ImVec2 origin, ImVec2 from, float fromH, ImVec2 mouse) {
        const ImVec2 m = ToCanvas(origin, mouse);
        LinkEnds e = Ends(origin, from, fromH, ImVec2(m.x - kNodeWidth * 0.5f, m.y - 20.0f), 40.0f);
        e.b = mouse;
        return e;
    }

    float DistanceToPolyline(const std::vector<ImVec2>& pts, ImVec2 p) {
        float best = 1e9f;
        for (size_t i = 1; i < pts.size(); ++i) {
            const ImVec2 q = ImLineClosestPoint(pts[i - 1], pts[i], p);
            best = std::min(best, std::sqrt((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y)));
        }
        return best;
    }

    ImVec2 PointAt(const std::vector<ImVec2>& pts, float t) { // точка на ломаной по доле длины — для «бегущих» огоньков
        float total = 0;
        for (size_t i = 1; i < pts.size(); ++i) total += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        float want = total * t;
        for (size_t i = 1; i < pts.size(); ++i) {
            const float len = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
            if (want <= len && len > 0) return ImLerp(pts[i - 1], pts[i], want / len);
            want -= len;
        }
        return pts.back();
    }

    // Стрелка: мягкое свечение снизу, цвет плавно переходит от блока к блоку, по ней бегут огоньки
    void DrawLink(ImDrawList* dl, const std::vector<ImVec2>& pts, Theme::Color from, Theme::Color to, float alpha, float thickness, bool flow) {
        const int n = static_cast<int>(pts.size());
        dl->AddPolyline(pts.data(), n, from.Mix(to, 0.5f).WithAlpha(0.10f * alpha), ImDrawFlags_None, thickness * 4.5f); // свечение
        dl->AddPolyline(pts.data(), n, from.Mix(to, 0.5f).WithAlpha(0.16f * alpha), ImDrawFlags_None, thickness * 2.2f);
        for (int i = 1; i < n; ++i) {
            const float t = (i - 0.5f) / (n - 1);
            dl->AddLine(pts[i - 1], pts[i], from.Mix(to, t).WithAlpha(0.9f * alpha), thickness);
        }
        dl->AddCircleFilled(pts.front(), thickness * 1.6f, from.WithAlpha(alpha), 12);
        dl->AddCircleFilled(pts.back(), thickness * 2.0f, to.WithAlpha(alpha), 12);
        dl->AddCircleFilled(pts.back(), thickness * 0.9f, Theme::Surface(1.0f), 10);
        if (!flow) return;
        const float time = static_cast<float>(ImGui::GetTime());
        for (int i = 0; i < 2; ++i) { // огоньки — по стрелке, от блока к блоку
            const float k = std::fmod(time * 0.35f + i * 0.5f, 1.0f);
            const float glow = std::sin(k * 3.1416f);
            const ImVec2 p = PointAt(pts, k);
            dl->AddCircleFilled(p, thickness * 2.6f, Theme::Overlay(static_cast<int>(28 * glow * alpha)), 12);
            dl->AddCircleFilled(p, thickness * 1.1f, Theme::Overlay(static_cast<int>(220 * glow * alpha)), 10);
        }
    }

    Theme::Color ColorOf(Scenarios::Scenario& s, int id) {
        if (id == kStartId) return Theme::Color{ 167, 139, 250 };
        const auto* b = FindBlock(s, id);
        return !b ? Theme::Color{ 167, 139, 250 } : b->color >= 0 ? Theme::FromRgb(b->color) : StepColor(b->step);
    }

    Scenarios::Block NewBlock(Step step) {
        Scenarios::Block block;
        block.step = step;
        if (step == Step::Volume) block.number = 50;
        if (step == Step::Wait) block.number = 3;
        return block;
    }

    void RemoveLinksTo(Scenarios::Scenario& s, int id) {
        auto drop = [id](std::vector<int>& v) { v.erase(std::remove(v.begin(), v.end(), id), v.end()); };
        drop(s.startNext);
        for (auto& b : s.blocks) drop(b.next);
    }

    // Строка меню: значок, текст, подсветка при наведении (danger — красная)
    bool MenuRow(const char* id, Icon icon, const char* label, bool danger = false) {
        const float w = ImGui::GetContentRegionAvail().x, h = Px(34);
        const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + w, a.y + h);
        const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 tint = danger ? IM_COL32(255, 110, 110, 255) : Theme::kInk;
        if (hovered) dl->AddRectFilled(a, b, danger ? IM_COL32(255, 90, 90, 26) : Theme::Overlay(14), Px(10));
        Widgets::DrawIcon(dl, icon, ImVec2(a.x + Px(16), (a.y + b.y) * 0.5f), Px(13), danger ? tint : (hovered ? Theme::kInk : Theme::kInkSoft));
        Widgets::Text(dl, Theme::semibold, ImVec2(a.x + Px(34), (a.y + b.y) * 0.5f - Theme::semibold->LegacySize * 0.55f), hovered || danger ? tint : Theme::kInkSoft, label);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        return pressed;
    }

    void MenuDivider() {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(0, Px(6)));
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + Px(3)), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + Px(3)), Theme::Overlay(16), Px(1));
        ImGui::Dummy(ImVec2(0, Px(4)));
    }

    // Меню блока по правому клику: шапка с типом, название, цвет, копия, убрать стрелки, удалить
    void NodeMenu(Scenarios::Scenario& s, int id) {
        Scenarios::Block* b = FindBlock(s, id);
        if (!b) return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Theme::Color color = b->color >= 0 ? Theme::FromRgb(b->color) : StepColor(b->step);
        // шапка
        const ImVec2 head = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(head.x + Px(16), head.y + Px(16)), Px(16), color.WithAlpha(0.18f), 24);
        Widgets::DrawIcon(dl, StepIcon(b->step), ImVec2(head.x + Px(16), head.y + Px(16)), Px(14), color.WithAlpha(1.0f));
        Widgets::Text(dl, Theme::semibold, ImVec2(head.x + Px(42), head.y + Px(1)), Theme::kInk, b->title.empty() ? Scenarios::StepName(b->step) : b->title.c_str());
        Widgets::Text(dl, Theme::semibold, ImVec2(head.x + Px(42), head.y + Px(18)), Theme::kInkMuted,
                      b->title.empty() ? "настройка блока" : Scenarios::StepName(b->step)); // своё название — ниже тип блока
        ImGui::Dummy(ImVec2(0, Px(40)));

        ImGui::TextColored(ImColor(Theme::kInkMuted), "название");
        InputString("##title", b->title, Scenarios::StepName(b->step), ImGui::GetContentRegionAvail().x);
        ImGui::Dummy(ImVec2(0, Px(4)));
        ImGui::TextColored(ImColor(Theme::kInkMuted), "цвет");
        static const int kColors[] = { -1, 0x7DAFF5, 0x6ED2B4, 0xFAA57D, 0xF5CD5F, 0xF472B6, 0xA78BFA, 0xC0C4CE };
        static float s_hover[IM_ARRAYSIZE(kColors)] = {};
        for (int i = 0; i < IM_ARRAYSIZE(kColors); ++i) {
            if (i) ImGui::SameLine(0, Px(6));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##color", ImVec2(Px(24), Px(24)))) { b->color = kColors[i]; g_dirty = true; }
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            s_hover[i] += ((hovered ? 1.0f : 0.0f) - s_hover[i]) * Theme::Ease(ImGui::GetIO().DeltaTime, 8.7f);
            const ImVec2 c(p.x + Px(12), p.y + Px(12));
            const ImU32 fill = kColors[i] < 0 ? StepColor(b->step).WithAlpha(1.0f) : Theme::FromRgb(kColors[i]).WithAlpha(1.0f);
            dl->AddCircleFilled(c, Px(8) + Px(2) * s_hover[i], fill, 24);
            if (kColors[i] < 0) dl->AddCircle(c, Px(4), Theme::Overlay(160), 16, Px(1.2f)); // «по типу блока»
            if (b->color == kColors[i]) dl->AddCircle(c, Px(12), Theme::Overlay(230), 24, Px(1.6f));
            if (hovered) ImGui::SetTooltip(kColors[i] < 0 ? "цвет по типу блока" : "свой цвет");
        }
        MenuDivider();
        if (MenuRow("##copy", Icon::Plus, "Копия блока")) {
            Scenarios::Block copy = *b;
            int maxId = 0;
            for (const auto& o : s.blocks) maxId = std::max(maxId, o.id);
            copy.id = maxId + 1;
            copy.x += 40, copy.y += 40;
            copy.next.clear();
            s.blocks.push_back(copy);
            g_canvas.selected = copy.id;
            g_dirty = true;
            ImGui::CloseCurrentPopup();
        }
        if (MenuRow("##unlink", Icon::Close, "Убрать стрелки из блока")) { b->next.clear(); g_dirty = true; ImGui::CloseCurrentPopup(); }
        if (MenuRow("##delete", Icon::Trash, "Удалить блок", true)) {
            RemoveLinksTo(s, id);
            s.blocks.erase(std::remove_if(s.blocks.begin(), s.blocks.end(), [id](const Scenarios::Block& x) { return x.id == id; }), s.blocks.end());
            g_canvas.selected = -1;
            g_dirty = true;
            ImGui::CloseCurrentPopup();
        }
    }

    // Показать все блоки: масштаб и сдвиг, чтобы схема поместилась
    void FitCanvas(Scenarios::Scenario& s, ImVec2 size) {
        ImVec2 mn(s.startX, s.startY), mx(s.startX + kNodeWidth, s.startY + 120);
        for (const auto& b : s.blocks) {
            mn = ImVec2(std::min(mn.x, b.x), std::min(mn.y, b.y));
            mx = ImVec2(std::max(mx.x, b.x + kNodeWidth), std::max(mx.y, b.y + 160));
        }
        const float w = mx.x - mn.x, h = mx.y - mn.y;
        g_canvas.zoom = std::clamp(std::min((size.x - Px(150)) / std::max(1.0f, w), (size.y - Px(120)) / std::max(1.0f, h)), 0.5f, 1.0f);
        g_canvas.pan = ImVec2(Px(75) - mn.x * g_canvas.zoom, Px(50) - mn.y * g_canvas.zoom); // поля — чтобы петли стрелок не уходили за край
        g_canvas.zoomTarget = g_canvas.zoom;
    }

    // Один блок на холсте. Возвращает true, если блок удалили
    bool DrawNode(ImDrawList* dl, ImVec2 origin, Scenarios::Scenario& s, int id) {
        const bool start = id == kStartId;
        Scenarios::Block* b = start ? nullptr : FindBlock(s, id);
        if (!start && !b) return false;
        const float z = g_canvas.zoom;
        float& shown = g_canvas.appear.try_emplace(id, 0.0f).first->second;
        shown += (1.0f - shown) * Theme::Ease(ImGui::GetIO().DeltaTime, 6.2f);
        const float appear = 1.0f - std::pow(1.0f - shown, 3.0f);

        const ImVec2 pos = NodePos(s, id);
        const ImVec2 a = ToScreen(origin, pos);
        const float width = kNodeWidth * z, pad = Px(14) * z;
        const float height = g_canvas.heights.count(id) ? g_canvas.heights[id] : Px(90) * z;
        const ImVec2 bmax(a.x + width, a.y + height);
        const bool selected = g_canvas.selected == id;
        const Theme::Color color = start ? Theme::Color{ 167, 139, 250 } : (b->color >= 0 ? Theme::FromRgb(b->color) : StepColor(b->step));
        ImGui::PushID(id);

        // кружки-порты (раньше остального — чтобы мышь доставалась им)
        const ImVec2 outP = OutPort(origin, pos), inP = InPort(origin, pos);
        const float portR = Px(7) * z;
        ImGui::SetCursorScreenPos(ImVec2(outP.x - portR * 1.6f, outP.y - portR * 1.6f));
        ImGui::InvisibleButton("##out", ImVec2(portR * 3.2f, portR * 3.2f));
        const bool outHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemActivated()) g_canvas.linkFrom = id;
        bool inHovered = false;
        if (!start) {
            ImGui::SetCursorScreenPos(ImVec2(inP.x - portR * 1.6f, inP.y - portR * 1.6f));
            ImGui::InvisibleButton("##in", ImVec2(portR * 3.2f, portR * 3.2f));
            inHovered = ImGui::IsItemHovered() || (g_canvas.linkFrom >= 0 && ImGui::IsMouseHoveringRect(a, bmax, false));
        }

        // содержимое
        dl->ChannelsSetCurrent(2);
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * z);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x * z, ImGui::GetStyle().FramePadding.y * z));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x * z, ImGui::GetStyle().ItemSpacing.y * z));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * appear);
        const ImVec2 head(a.x + pad, a.y + Px(12) * z);
        dl->AddCircleFilled(ImVec2(head.x + Px(11) * z, head.y + Px(11) * z), Px(13) * z, color.WithAlpha(0.2f * appear), 24);
        Widgets::DrawIcon(dl, start ? Icon::Mic : StepIcon(b->step), ImVec2(head.x + Px(11) * z, head.y + Px(11) * z), Px(13) * z, color.WithAlpha(appear));
        const std::string title = start ? "Слушать" : (b->title.empty() ? Scenarios::StepName(b->step) : b->title);
        dl->AddText(Theme::semibold, Theme::semibold->LegacySize * z, ImVec2(head.x + Px(32) * z, head.y + Px(4) * z), Theme::kInk, title.c_str());
        bool removed = false;
        if (!start && Widgets::IconButton("##remove", ImVec2(a.x + width - pad - Px(24) * z, head.y - Px(1) * z), Px(24) * z, Icon::Trash, true)) {
            RemoveLinksTo(s, id);
            s.blocks.erase(std::remove_if(s.blocks.begin(), s.blocks.end(), [id](const Scenarios::Block& x) { return x.id == id; }), s.blocks.end());
            g_dirty = true;
            removed = true;
        }
        float bottom = head.y + Px(30) * z;
        if (!removed) {
            ImGui::SetCursorScreenPos(ImVec2(head.x, head.y + Px(32) * z));
            ImGui::BeginGroup();
            const float inner = width - pad * 2;
            if (start) {
                ImGui::TextDisabled("%s, …", Config::Get().assistant.c_str());
                InputString("##phrase", s.phrase, "уютный вечер", inner);
            } else {
                BlockFields(*b, inner);
            }
            ImGui::EndGroup();
            bottom = ImGui::GetItemRectMax().y;
        }
        ImGui::PopStyleVar(3);
        ImGui::PopFont();
        g_canvas.heights[id] = bottom + pad - a.y;

        // весь блок — тянуть (после полей: поля важнее)
        bool menu = false;
        if (!removed) {
            ImGui::SetCursorScreenPos(a);
            ImGui::InvisibleButton("##node", ImVec2(width, std::max(1.0f, height)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                g_canvas.dragNode = id;
                g_canvas.selected = id;
                const ImVec2 m = ToCanvas(origin, ImGui::GetIO().MousePos);
                g_canvas.dragOffset = ImVec2(m.x - pos.x, m.y - pos.y);
            }
            if (ImGui::IsItemHovered() && g_canvas.dragNode < 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (!start && ImGui::IsItemClicked(ImGuiMouseButton_Right)) { g_canvas.selected = id; menu = true; }
        }
        ImGui::PopID();
        if (menu) ImGui::OpenPopup(("##nodeMenu" + std::to_string(id)).c_str());
        if (!start && !removed && ImGui::IsPopupOpen(("##nodeMenu" + std::to_string(id)).c_str())) {
            ImGui::SetNextWindowSize(ImVec2(Px(280), 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(14), Px(14)));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, Px(16));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, Px(1));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Px(6), Px(4)));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::SurfaceSolid());
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::Overlay(26));
            if (Widgets::BeginPopupAnimated(("##nodeMenu" + std::to_string(id)).c_str())) {
                NodeMenu(s, id);
                Widgets::EndPopupAnimated();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);
        }
        if (removed) return true;

        // подложка блока
        dl->ChannelsSetCurrent(1);
        const ImVec2 b2(a.x + width, a.y + g_canvas.heights[id]);
        if (selected) dl->AddRect(ImVec2(a.x - Px(3), a.y - Px(3)), ImVec2(b2.x + Px(3), b2.y + Px(3)), Theme::Accent().WithAlpha(0.6f * appear), Px(18) * z, 0, Px(2));
        dl->AddRectFilled(a, b2, Theme::Surface(0.93f * appear), Px(16) * z);
        dl->AddRectFilledMultiColor(ImVec2(a.x + Px(1), a.y + Px(1)), ImVec2(b2.x - Px(1), a.y + Px(40) * z),
            color.WithAlpha(0.14f * appear), color.WithAlpha(0.06f * appear), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
        dl->AddRect(a, b2, inHovered && g_canvas.linkFrom >= 0 && g_canvas.linkFrom != id ? Theme::Accent().WithAlpha(0.9f) : Theme::Overlay(26), Px(16) * z, 0, Px(1.2f));
        // порты
        dl->ChannelsSetCurrent(2);
        const ImU32 portColor = color.WithAlpha(appear);
        dl->AddCircleFilled(outP, portR * (outHovered ? 1.3f : 1.0f), Theme::Surface(1.0f), 16);
        dl->AddCircle(outP, portR * (outHovered ? 1.3f : 1.0f), portColor, 16, Px(2) * z);
        if (!NextOf(s, id).empty() || outHovered) dl->AddCircleFilled(outP, portR * 0.45f, portColor, 12);
        if (!start && g_canvas.linkFrom >= 0 && g_canvas.linkFrom != id) { // тянем стрелку — подсказать, куда её можно отпустить
            dl->AddCircleFilled(inP, portR, Theme::Surface(1.0f), 16);
            dl->AddCircle(inP, portR * (inHovered ? 1.3f : 1.0f), inHovered ? Theme::Accent().WithAlpha(1.0f) : Theme::Overlay(90), 16, Px(2) * z);
        }
        return false;
    }

    void DrawPalette(ImDrawList* dl, ImVec2 pos, float width, float height) {
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), Theme::Surface(0.92f), Px(18));
        dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), Theme::Overlay(22), Px(18));
        Widgets::Text(dl, Theme::semibold, ImVec2(pos.x + Px(16), pos.y + Px(8)), Theme::kInkMuted, "блоки — зажми и перетащи на холст");
        float x = pos.x + Px(12);
        const float y = pos.y + Px(30), h = height - Px(40);
        for (int i = 0; i < static_cast<int>(Step::kCount); ++i) {
            const Step step = static_cast<Step>(i);
            static const char* kShort[] = { "Программа", "Ссылка", "Окно", "Громкость", "Музыка", "Подождать", "Сказать" };
            const char* name = i < IM_ARRAYSIZE(kShort) ? kShort[i] : Scenarios::StepName(step);
            const float w = Widgets::TextSize(Theme::semibold, name).x + Px(44);
            if (x + w > pos.x + width - Px(12)) break;
            const ImVec2 a(x, y), b(x + w, y + h);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            ImGui::InvisibleButton("##palette", ImVec2(w, h));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, Px(4))) g_canvas.newStep = i;
            ImGui::PopID();
            const Theme::Color color = StepColor(step);
            dl->AddRectFilled(a, b, hovered ? Theme::Overlay(18) : Theme::Overlay(8), h * 0.5f);
            dl->AddRect(a, b, color.WithAlpha(hovered ? 0.8f : 0.4f), h * 0.5f, 0, Px(1.2f));
            Widgets::DrawIcon(dl, StepIcon(step), ImVec2(a.x + Px(18), (a.y + b.y) * 0.5f), Px(12), color.WithAlpha(1.0f));
            Widgets::Text(dl, Theme::semibold, ImVec2(a.x + Px(32), (a.y + b.y) * 0.5f - Theme::semibold->LegacySize * 0.55f), Theme::kInk, name);
            if (hovered) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s — зажми и перетащи на холст", Scenarios::StepName(step)); }
            x += w + Px(6);
        }
    }

    void DrawEditor(ImVec2 areaMin, ImVec2 areaMax) {
        if (g_list.empty() || g_selected < 0 || g_selected >= static_cast<int>(g_list.size())) {
            ImGui::SetCursorScreenPos(areaMin);
            ImGui::TextDisabled("Создай первый сценарий слева");
            return;
        }
        auto& s = g_list[g_selected];
        Scenarios::Normalize(s);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();

        // строка сверху: название, включён, проверить, вид стрелок, показать всё, удалить
        ImGui::SetCursorScreenPos(areaMin);
        const float width = areaMax.x - areaMin.x;
        InputString("##name", s.name, "название сценария", std::min(width - Px(560), Px(320)));
        ImGui::SameLine();
        if (Widgets::Toggle("##enabled", ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + Px(10)), &s.enabled, Theme::Accent().WithAlpha(1.0f))) g_dirty = true;
        ImGui::SameLine();
        if (ImGui::Button("▶ проверить")) Scenarios::Run(s);
        ImGui::SameLine();
        static const char* kStyles[] = { "стрелки: кривые", "стрелки: косые", "стрелки: прямые" };
        if (Widgets::Combo("##linkStyle", &s.linkStyle, kStyles, 3, Px(170))) g_dirty = true;
        ImGui::SameLine();
        const float toolbarH = ImGui::GetFrameHeight() + Px(12);
        const ImVec2 canvasMin(areaMin.x, areaMin.y + toolbarH), canvasMax(areaMax.x, areaMax.y - Px(92));
        const ImVec2 canvasSize(canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y);
        if (ImGui::Button("показать всё")) FitCanvas(s, canvasSize);
        ImGui::SameLine();
        if (ImGui::Button("удалить")) {
            g_list.erase(g_list.begin() + g_selected);
            g_selected = std::max(0, g_selected - 1);
            g_dirty = true;
            return;
        }
        if (g_canvas.lastScenario != g_selected) { // другой сценарий — показать его целиком
            g_canvas.lastScenario = g_selected;
            g_canvas.selected = g_canvas.linkFrom = g_canvas.dragNode = -1;
            g_canvas.heights.clear();
            g_canvas.appear.clear();
            FitCanvas(s, canvasSize);
        }

        // фон холста: точки сетки
        dl->AddRectFilled(canvasMin, canvasMax, IM_COL32(0, 0, 0, 40), Px(18));
        dl->AddRect(canvasMin, canvasMax, Theme::Overlay(18), Px(18));
        ImGui::PushClipRect(canvasMin, canvasMax, true); // одна обрезка — и для рисунка, и для мыши
        const float step = Px(24) * g_canvas.zoom;
        for (float gx = std::fmod(g_canvas.pan.x, step); gx < canvasSize.x; gx += step)
            for (float gy = std::fmod(g_canvas.pan.y, step); gy < canvasSize.y; gy += step)
                dl->AddCircleFilled(ImVec2(canvasMin.x + gx, canvasMin.y + gy), Px(1.1f), Theme::Overlay(22), 4);
        if (s.blocks.empty())
            Widgets::Text(dl, Theme::semibold, ImVec2(canvasMin.x + Px(380), canvasMin.y + Px(60)), Theme::kInkMuted,
                          "перетащи блок снизу и соедини: тяни от кружка справа к следующему блоку");

        dl->ChannelsSplit(3);
        const ImVec2 origin = canvasMin;
        const bool canvasHovered = ImGui::IsMouseHoveringRect(canvasMin, canvasMax) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

        // блоки
        std::vector<int> ids{ kStartId };
        for (const auto& b : s.blocks) ids.push_back(b.id);
        for (int id : ids)
            if (DrawNode(dl, origin, s, id)) break; // удалили — список поменялся, дорисуем в следующем кадре

        // стрелки
        dl->ChannelsSetCurrent(0);
        int removeFrom = -1, removeTo = -1;
        for (int from : ids) {
            if (from != kStartId && !FindBlock(s, from)) continue;
            for (int to : NextOf(s, from)) {
                if (!FindBlock(s, to)) continue;
                const auto pts = LinkPoints(Ends(origin, NodePos(s, from), NodeHeight(from), NodePos(s, to), NodeHeight(to)), s.linkStyle);
                const bool hovered = canvasHovered && g_canvas.linkFrom < 0 && g_canvas.dragNode < 0 && DistanceToPolyline(pts, io.MousePos) < Px(7);
                const Theme::Color red{ 255, 120, 120 };
                DrawLink(dl, pts, hovered ? red : ColorOf(s, from), hovered ? red : ColorOf(s, to), 1.0f, Px(hovered ? 2.6f : 1.9f) * std::max(0.7f, g_canvas.zoom), !hovered);
                if (hovered) {
                    ImGui::SetTooltip("правый клик — убрать стрелку");
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) removeFrom = from, removeTo = to;
                }
            }
        }
        if (removeFrom >= 0 || removeTo >= 0) {
            auto& v = NextOf(s, removeFrom);
            v.erase(std::remove(v.begin(), v.end(), removeTo), v.end());
            g_dirty = true;
        }

        // тянем новую стрелку
        if (g_canvas.linkFrom >= 0) {
            const Theme::Color c = ColorOf(s, g_canvas.linkFrom);
            DrawLink(dl, LinkPoints(EndsToMouse(origin, NodePos(s, g_canvas.linkFrom), NodeHeight(g_canvas.linkFrom), io.MousePos), s.linkStyle),
                     c, c, 0.9f, Px(2.0f) * std::max(0.7f, g_canvas.zoom), false);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { // отпустили — над каким блоком?
                for (const auto& b : s.blocks) {
                    const ImVec2 a = ToScreen(origin, ImVec2(b.x, b.y));
                    const float h = g_canvas.heights.count(b.id) ? g_canvas.heights[b.id] : Px(90);
                    if (b.id != g_canvas.linkFrom && io.MousePos.x >= a.x - Px(14) && io.MousePos.x <= a.x + kNodeWidth * g_canvas.zoom && io.MousePos.y >= a.y && io.MousePos.y <= a.y + h) {
                        auto& next = NextOf(s, g_canvas.linkFrom);
                        if (std::find(next.begin(), next.end(), b.id) == next.end()) next.push_back(b.id);
                        g_dirty = true;
                        break;
                    }
                }
                g_canvas.linkFrom = -1;
            }
        }
        dl->ChannelsMerge();
        ImGui::PopClipRect();

        // двигаем блок
        if (g_canvas.dragNode >= 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const ImVec2 m = ToCanvas(origin, io.MousePos);
                ImVec2 p(m.x - g_canvas.dragOffset.x, m.y - g_canvas.dragOffset.y);
                if (!io.KeyAlt) p = ImVec2(std::round(p.x / 20.0f) * 20.0f, std::round(p.y / 20.0f) * 20.0f); // по сетке; Alt — свободно
                if (g_canvas.dragNode == kStartId) s.startX = p.x, s.startY = p.y;
                else if (auto* b = FindBlock(s, g_canvas.dragNode)) b->x = p.x, b->y = p.y;
            } else {
                g_canvas.dragNode = -1;
                g_dirty = true;
            }
        }

        // пустое место холста: тянуть — двигать, колесо — масштаб (последним — блоки важнее)
        ImGui::SetCursorScreenPos(canvasMin);
        ImGui::InvisibleButton("##canvasBg", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        if (ImGui::IsItemActivated()) g_canvas.selected = -1;
        if (ImGui::IsItemActive()) { g_canvas.pan.x += io.MouseDelta.x; g_canvas.pan.y += io.MouseDelta.y; ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll); }
        if (canvasHovered && io.MouseWheel != 0.0f && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            g_canvas.anchorScreen = io.MousePos; // масштаб — вокруг мыши
            g_canvas.anchorCanvas = ToCanvas(origin, io.MousePos);
            g_canvas.zoomTarget = std::clamp(g_canvas.zoomTarget * (io.MouseWheel > 0 ? 1.12f : 1.0f / 1.12f), 0.5f, 1.6f);
        }
        if (std::fabs(g_canvas.zoomTarget - g_canvas.zoom) > 0.0005f) { // плавно доезжаем до нового масштаба
            g_canvas.zoom += (g_canvas.zoomTarget - g_canvas.zoom) * Theme::Ease(io.DeltaTime, 7.4f);
            g_canvas.pan = ImVec2(g_canvas.anchorScreen.x - origin.x - g_canvas.anchorCanvas.x * g_canvas.zoom,
                                  g_canvas.anchorScreen.y - origin.y - g_canvas.anchorCanvas.y * g_canvas.zoom);
        }
        if (g_canvas.selected > 0 && ImGui::IsKeyPressed(ImGuiKey_Delete) && !io.WantTextInput) {
            const int id = g_canvas.selected;
            RemoveLinksTo(s, id);
            s.blocks.erase(std::remove_if(s.blocks.begin(), s.blocks.end(), [id](const Scenarios::Block& x) { return x.id == id; }), s.blocks.end());
            g_canvas.selected = -1;
            g_dirty = true;
        }
        Widgets::Text(dl, Theme::semibold, ImVec2(canvasMin.x + Px(14), canvasMax.y - Px(24)), Theme::kInkMuted,
                      "колесо — масштаб · тяни пустое место — двигать · от кружка справа — стрелка · правый клик по блоку — настройки");

        // палитра снизу
        DrawPalette(dl, ImVec2(areaMin.x, areaMax.y - Px(80)), width, Px(76));
        if (g_canvas.newStep >= 0) {
            const Step step = static_cast<Step>(g_canvas.newStep);
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            const ImVec2 m = io.MousePos;
            const float w = kNodeWidth * g_canvas.zoom, h = Px(70) * g_canvas.zoom;
            fg->AddRectFilled(ImVec2(m.x - Px(20), m.y - Px(16)), ImVec2(m.x - Px(20) + w, m.y - Px(16) + h), Theme::Surface(0.85f), Px(16) * g_canvas.zoom);
            fg->AddRect(ImVec2(m.x - Px(20), m.y - Px(16)), ImVec2(m.x - Px(20) + w, m.y - Px(16) + h), StepColor(step).WithAlpha(0.9f), Px(16) * g_canvas.zoom, 0, Px(1.5f));
            fg->AddText(Theme::semibold, Theme::semibold->LegacySize * g_canvas.zoom, ImVec2(m.x + Px(12), m.y - Px(4)), Theme::kInk, Scenarios::StepName(step));
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (m.x >= canvasMin.x && m.x <= canvasMax.x && m.y >= canvasMin.y && m.y <= canvasMax.y) {
                    Scenarios::Block block = NewBlock(step);
                    int maxId = 0;
                    for (const auto& o : s.blocks) maxId = std::max(maxId, o.id);
                    block.id = maxId + 1;
                    const ImVec2 p = ToCanvas(origin, ImVec2(m.x - Px(20), m.y - Px(16)));
                    block.x = std::round(p.x / 20.0f) * 20.0f, block.y = std::round(p.y / 20.0f) * 20.0f;
                    // «и делаешь дальше»: новый блок сразу продолжает выделенный (или «Слушать», если стрелок ещё нет)
                    const int from = g_canvas.selected >= 0 ? g_canvas.selected : (s.startNext.empty() ? kStartId : -1);
                    if (from >= 0 && (from == kStartId || FindBlock(s, from))) NextOf(s, from).push_back(block.id);
                    s.blocks.push_back(block);
                    g_canvas.selected = block.id;
                    g_dirty = true;
                }
                g_canvas.newStep = -1;
            }
        }
    }
}

void ScenariosPage::Open() {
    if (g_open) return;
    g_list = Scenarios::Load();
    g_selected = std::clamp(g_selected, 0, std::max(0, static_cast<int>(g_list.size()) - 1));
    g_open = true;
    g_chainAnim = 0.0f;
}

void ScenariosPage::Close() {
    if (g_open && g_dirty) Scenarios::Save(g_list);
    g_dirty = false;
    g_open = false;
}

void ScenariosPage::Add(const Scenarios::Scenario& scenario) {
    std::lock_guard lock(g_addMutex);
    g_added.push_back(scenario);
}

bool ScenariosPage::PollAdded() {
    std::vector<Scenarios::Scenario> added;
    {
        std::lock_guard lock(g_addMutex);
        added.swap(g_added);
    }
    if (added.empty()) return false;
    if (!g_open) g_list = Scenarios::Load(); // открытая вкладка — дописываем к тому, что в ней (с несохранёнными правками)
    for (auto& s : added) g_list.push_back(std::move(s));
    Scenarios::Save(g_list);
    g_dirty = false;
    g_selected = static_cast<int>(g_list.size()) - 1;
    if (!g_open) Open();
    g_selected = static_cast<int>(g_list.size()) - 1;
    g_chainAnim = 0.0f;
    g_canvas.lastScenario = -1; // показать новый сценарий целиком
    return true;
}

bool ScenariosPage::IsOpen() { return g_open; }
bool ScenariosPage::Covers() { return g_open && g_anim > 0.97f; }
float ScenariosPage::Visibility() { return g_anim; }

void ScenariosPage::Draw(ImVec2 screen, float top, float dt) {
    g_anim += ((g_open ? 1.0f : 0.0f) - g_anim) * Theme::Ease(dt, 8.7f);
    if (g_anim < 0.01f) return;
    const float ease = 1.0f - std::pow(1.0f - g_anim, 3.0f);

    ImGui::SetNextWindowPos(ImVec2(0, top));
    ImGui::SetNextWindowSize(ImVec2(screen.x, screen.y - top));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, PageColor(0.86f)); // сквозь страницу — живой фон Ани
    ImGui::Begin("##scenariosPage", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + (1.0f - ease) * Px(14));
    const ImVec2 size = ImGui::GetWindowSize();
    const float margin = Px(16), listWidth = size.x < Px(820) ? Px(200) : Px(260);

    Widgets::Text(dl, Theme::hero, ImVec2(origin.x + margin + Px(4), origin.y + margin), Theme::kInk, "Сценарии");
    Widgets::Text(dl, Theme::semibold, ImVec2(origin.x + margin + Px(4), origin.y + margin + Px(40)), Theme::kInkMuted,
        "скажи фразу — и Аня выполнит блоки по стрелкам");
    if (Widgets::IconButton("##scenariosClose", ImVec2(origin.x + size.x - margin - Px(36), origin.y + margin), Px(36), Icon::Close)) Close();

    const float top2 = origin.y + margin + Px(72);
    const float listHeight = std::min(size.y - (top2 - origin.y) - margin, Px(16) + (g_list.size() + 1) * Px(56) + Px(16));
    DrawList(dl, ImVec2(origin.x + margin, top2), listWidth, listHeight);

    const float left = origin.x + margin + listWidth + Px(24);
    DrawEditor(ImVec2(left, top2), ImVec2(origin.x + size.x - margin, origin.y + size.y - margin)); // холст до правого и нижнего края

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    // изменения сохраняем, когда закончили редактировать поле или поменяли цепочку
    if (g_dirty && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        Scenarios::Save(g_list);
        g_dirty = false;
    }
}
