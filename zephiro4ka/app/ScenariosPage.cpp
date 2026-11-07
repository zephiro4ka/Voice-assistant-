#include "ScenariosPage.h"

#include "Launcher.h"
#include "Scenarios.h"
#include "TextUtils.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
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

    // Соединитель между блоками: линия с «бегущими» точками и кнопка «+» (вставить блок)
    bool Connector(ImDrawList* dl, float x, float width, int insertAt, std::vector<Scenarios::Block>& blocks) {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float h = Px(34), cx = x + Px(26);
        const float t = static_cast<float>(ImGui::GetTime());
        dl->AddLine(ImVec2(cx, start.y), ImVec2(cx, start.y + h), IM_COL32(255, 255, 255, 30), Px(2));
        for (int i = 0; i < 3; ++i) { // течение сверху вниз — цепочка «живая»
            const float k = std::fmod(t * 0.8f + i / 3.0f, 1.0f);
            dl->AddCircleFilled(ImVec2(cx, start.y + h * k), Px(2.2f), Theme::Accent().WithAlpha(0.8f * std::sin(k * 3.1416f)), 10);
        }
        ImGui::PushID(insertAt);
        const bool add = Widgets::IconButton("##add", ImVec2(cx + Px(16), start.y + h * 0.5f - Px(12)), Px(24), Icon::Plus);
        if (add) ImGui::OpenPopup("##stepMenu");
        bool changed = false;
        if (ImGui::BeginPopup("##stepMenu")) {
            ImGui::TextDisabled("добавить блок");
            for (int s = 0; s < static_cast<int>(Step::kCount); ++s)
                if (ImGui::Selectable(Scenarios::StepName(static_cast<Step>(s)))) {
                    Scenarios::Block block;
                    block.step = static_cast<Step>(s);
                    if (block.step == Step::Volume) block.number = 50;
                    if (block.step == Step::Wait) block.number = 3;
                    blocks.insert(blocks.begin() + insertAt, block);
                    changed = true;
                }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + h));
        ImGui::Dummy(ImVec2(width, 0));
        return changed;
    }

    // Карточка-блок с цветной полоской; содержимое рисует fields. Возвращает высоту
    template <class Fields>
    void BlockCard(ImDrawList* dl, float x, float width, Theme::Color color, Icon icon, const char* title, float appear, Fields&& fields,
                   bool* remove = nullptr, bool* up = nullptr, bool* down = nullptr) {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float pad = Px(16), slide = (1.0f - appear) * Px(16);
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        ImGui::SetCursorScreenPos(ImVec2(x + pad + Px(6), start.y + pad + slide));
        ImGui::BeginGroup();
        const ImVec2 head = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(head.x + Px(11), head.y + Px(11)), Px(13), color.WithAlpha(0.18f * appear), 24);
        Widgets::DrawIcon(dl, icon, ImVec2(head.x + Px(11), head.y + Px(11)), Px(13), color.WithAlpha(appear));
        Widgets::Text(dl, Theme::semibold, ImVec2(head.x + Px(32), head.y + Px(3)), Theme::kInk, title);
        float bx = x + width - pad - Px(26);
        if (remove && Widgets::IconButton("##remove", ImVec2(bx, head.y - Px(2)), Px(26), Icon::Trash, true)) *remove = true;
        bx -= Px(30);
        if (down && Widgets::IconButton("##down", ImVec2(bx, head.y - Px(2)), Px(26), Icon::Next)) *down = true; // ↓
        bx -= Px(30);
        if (up && Widgets::IconButton("##up", ImVec2(bx, head.y - Px(2)), Px(26), Icon::Previous)) *up = true; // ↑
        ImGui::SetCursorScreenPos(ImVec2(head.x, head.y + Px(30)));
        fields(width - pad * 2 - Px(6));
        ImGui::EndGroup();
        const ImVec2 end(x + width, ImGui::GetItemRectMax().y + pad);
        dl->ChannelsSetCurrent(0);
        const ImVec2 a(x, start.y + slide), b(end.x, end.y);
        const bool hovered = ImGui::IsMouseHoveringRect(a, b, false);
        dl->AddRectFilled(a, b, Theme::Surface(hovered ? 0.95f : 0.85f), Px(16));
        dl->AddRect(a, b, IM_COL32(255, 255, 255, hovered ? 40 : 22), Px(16));
        dl->AddRectFilled(ImVec2(a.x, a.y + Px(10)), ImVec2(a.x + Px(4), b.y - Px(10)), color.WithAlpha(appear), Px(2));
        dl->ChannelsMerge();
        ImGui::SetCursorScreenPos(ImVec2(start.x, b.y));
        ImGui::Dummy(ImVec2(width, 0));
    }

    void DrawList(ImDrawList* dl, ImVec2 pos, float width, float height) {
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(255, 255, 255, 8), Px(18));
        dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(255, 255, 255, 20), Px(18));
        float y = pos.y + Px(8);
        for (int i = 0; i < static_cast<int>(g_list.size()); ++i) {
            const ImVec2 a(pos.x + Px(8), y), b(pos.x + width - Px(8), y + Px(52));
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##scenario", ImVec2(b.x - a.x, b.y - a.y)) && g_selected != i) { g_selected = i; g_chainAnim = 0.0f; }
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool on = i == g_selected;
            if (on || hovered) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, on ? 22 : 12), Px(12));
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
            g_list.back().blocks.push_back({});
            g_selected = static_cast<int>(g_list.size()) - 1;
            g_chainAnim = 0.0f;
            g_dirty = true;
        }
    }

    void DrawEditor(float width) {
        if (g_list.empty() || g_selected < 0 || g_selected >= static_cast<int>(g_list.size())) {
            ImGui::TextDisabled("Создай первый сценарий слева");
            return;
        }
        auto& s = g_list[g_selected];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float x = ImGui::GetCursorScreenPos().x;
        g_chainAnim += (1.0f - g_chainAnim) * std::min(1.0f, ImGui::GetIO().DeltaTime * 8.0f);
        auto appear = [&](int index) { // блоки появляются по очереди
            const float t = std::clamp(g_chainAnim * 1.6f - index * 0.12f, 0.0f, 1.0f);
            return 1.0f - std::pow(1.0f - t, 3.0f);
        };

        // название, включён, проверить
        InputString("##name", s.name, "название сценария", std::min(width - Px(220), Px(360)));
        ImGui::SameLine();
        if (Widgets::Toggle("##enabled", ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + Px(10)), &s.enabled, Theme::Accent().WithAlpha(1.0f))) g_dirty = true;
        ImGui::SameLine();
        if (ImGui::Button("▶ проверить")) Scenarios::Run(s);
        ImGui::SameLine();
        if (ImGui::Button("удалить")) {
            g_list.erase(g_list.begin() + g_selected);
            g_selected = std::max(0, g_selected - 1);
            g_dirty = true;
            return;
        }
        ImGui::Dummy(ImVec2(0, Px(8)));

        int index = 0;
        BlockCard(dl, x, width, Theme::Accent(), Icon::Mic, "Слушать", appear(index++), [&](float w) {
            ImGui::TextDisabled("Аня, …");
            ImGui::SameLine();
            InputString("##phrase", s.phrase, "уютный вечер", w - Px(60));
        });
        if (Connector(dl, x, width, 0, s.blocks)) { g_dirty = true; g_chainAnim = 0.6f; }

        for (int i = 0; i < static_cast<int>(s.blocks.size()); ++i) {
            ImGui::PushID(i);
            bool remove = false, up = false, down = false;
            auto& b = s.blocks[i];
            BlockCard(dl, x, width, StepColor(b.step), StepIcon(b.step), Scenarios::StepName(b.step), appear(index++),
                [&](float w) { BlockFields(b, w); }, &remove, i > 0 ? &up : nullptr, i + 1 < static_cast<int>(s.blocks.size()) ? &down : nullptr);
            ImGui::PopID();
            if (remove) { s.blocks.erase(s.blocks.begin() + i); g_dirty = true; break; }
            if (up) { std::swap(s.blocks[i], s.blocks[i - 1]); g_dirty = true; break; }
            if (down) { std::swap(s.blocks[i], s.blocks[i + 1]); g_dirty = true; break; }
            if (Connector(dl, x, width, i + 1, s.blocks)) { g_dirty = true; break; }
        }

        // «Конец» — маленькая пилюля
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float a = appear(index);
        dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + Px(96), p.y + Px(34)), Theme::Surface(0.85f * a), Px(17));
        dl->AddRect(ImVec2(x, p.y), ImVec2(x + Px(96), p.y + Px(34)), IM_COL32(255, 255, 255, static_cast<int>(26 * a)), Px(17));
        dl->AddCircleFilled(ImVec2(x + Px(18), p.y + Px(17)), Px(4), Theme::kOk, 12);
        Widgets::Text(dl, Theme::semibold, ImVec2(x + Px(30), p.y + Px(9)), Theme::kInk, "Конец");
        ImGui::Dummy(ImVec2(width, Px(48)));
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
    return true;
}

bool ScenariosPage::IsOpen() { return g_open; }
bool ScenariosPage::Covers() { return g_open && g_anim > 0.97f; }
float ScenariosPage::Visibility() { return g_anim; }

void ScenariosPage::Draw(ImVec2 screen, float top, float dt) {
    g_anim += ((g_open ? 1.0f : 0.0f) - g_anim) * std::min(1.0f, dt * 14.0f);
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
        "скажи фразу — и Аня выполнит цепочку блоков по порядку");
    if (Widgets::IconButton("##scenariosClose", ImVec2(origin.x + size.x - margin - Px(36), origin.y + margin), Px(36), Icon::Close)) Close();

    const float top2 = origin.y + margin + Px(72);
    const float listHeight = std::min(size.y - (top2 - origin.y) - margin, Px(16) + (g_list.size() + 1) * Px(56) + Px(16));
    DrawList(dl, ImVec2(origin.x + margin, top2), listWidth, listHeight);

    const float left = origin.x + margin + listWidth + Px(24);
    const float width = std::min(origin.x + size.x - margin - left, Px(760));
    ImGui::SetCursorScreenPos(ImVec2(left, top2));
    ImGui::BeginChild("##chain", ImVec2(origin.x + size.x - margin - left, origin.y + size.y - top2 - Px(8)), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    Widgets::SmoothScroll();
    DrawEditor(width - Px(12));
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    // изменения сохраняем, когда закончили редактировать поле или поменяли цепочку
    if (g_dirty && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        Scenarios::Save(g_list);
        g_dirty = false;
    }
}
