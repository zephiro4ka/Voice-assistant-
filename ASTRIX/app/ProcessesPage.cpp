#include "ProcessesPage.h"

#include "ProcessMonitor.h"
#include "Settings.h"
#include "SystemMonitor.h"
#include "TextUtils.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <thread>

using Theme::Px;
using Widgets::Icon;
using ProcessMonitor::Kind;

namespace {
    bool g_open = false;
    float g_anim = 0.0f;
    char g_query[96] = "";
    int g_filter = 0; // 0 — все, 1 — программы, 2 — службы, 3 — система
    std::set<std::string> g_expanded;
    uint32_t g_confirmPid = 0; // «завершить» нажали один раз — ждём подтверждения
    double g_confirmUntil = 0;

    std::mutex g_cleanMutex;
    std::atomic<bool> g_cleaning{ false };
    std::string g_cleanText;
    double g_cleanShownAt = -100;

    float g_bar[7] = {}; // плавные доли полосы памяти
    int g_detail = -1;   // какой пункт полосы раскрыт: кто съедает эту память
    float g_detailAnim = 0.0f;

    std::string Gb(uint64_t bytes); // ниже

    // Строка «название — полоска — объём» для панели подробностей
    void DetailRow(ImDrawList* dl, float inner, const std::string& name, const std::string& note, uint64_t bytes, uint64_t biggest, ImU32 color) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float barX = inner * 0.55f, barW = inner * 0.3f;
        ImGui::PushClipRect(p, ImVec2(p.x + barX - Px(12), p.y + Px(34)), true);
        Widgets::Text(dl, Theme::semibold, ImVec2(p.x + Px(4), p.y + Px(2)), Theme::kInk, name.c_str());
        if (!note.empty()) Widgets::Text(dl, Theme::semibold, ImVec2(p.x + Px(4), p.y + Px(18)), Theme::kInkMuted, note.c_str());
        ImGui::PopClipRect();
        const float k = biggest ? static_cast<float>(bytes) / biggest : 0.0f;
        dl->AddRectFilled(ImVec2(p.x + barX, p.y + Px(9)), ImVec2(p.x + barX + barW, p.y + Px(13)), IM_COL32(0, 0, 0, 80), Px(2));
        dl->AddRectFilled(ImVec2(p.x + barX, p.y + Px(9)), ImVec2(p.x + barX + barW * std::clamp(k, 0.0f, 1.0f), p.y + Px(13)), color, Px(2));
        const std::string value = Gb(bytes);
        const float vw = Widgets::TextSize(Theme::semibold, value.c_str()).x;
        Widgets::Text(dl, Theme::semibold, ImVec2(p.x + inner - vw - Px(4), p.y + Px(3)), Theme::kInkSoft, value.c_str());
        ImGui::Dummy(ImVec2(inner, note.empty() ? Px(24) : Px(38)));
    }

    void DetailPanel(const ProcessMonitor::Snapshot& s, int part, float inner, ImU32 color) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const auto& m = s.memory;
        auto groupsOf = [&](Kind kind) {
            std::vector<const ProcessMonitor::Group*> list;
            for (const auto& g : s.groups) if (g.kind == kind) list.push_back(&g);
            return list;
        };
        auto text = [&](const char* line) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kInkSoft);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
            ImGui::TextUnformatted(line);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, Px(4)));
        };
        if (part <= 2) { // программы, службы, система — кто больше всех
            const Kind kind = part == 0 ? Kind::App : part == 1 ? Kind::Service : Kind::System;
            const auto list = groupsOf(kind);
            const uint64_t biggest = list.empty() ? 1 : list.front()->memory;
            for (size_t i = 0; i < list.size() && i < 12; ++i) {
                const auto& g = *list[i];
                std::string note = g.processes.size() > 1 ? std::to_string(g.processes.size()) + " процессов" : g.exe;
                if (kind == Kind::Service && !g.processes.empty() && g.processes.front().services.size() > 1)
                    note = "ещё " + std::to_string(g.processes.front().services.size() - 1) + " служб в этом процессе";
                DetailRow(dl, inner, g.title, note, g.memory, biggest, color);
            }
            if (list.size() > 12) ImGui::TextDisabled("и ещё %d — в списке ниже", static_cast<int>(list.size() - 12));
        } else if (part == 3) { // ядро и драйверы — по меткам выделений
            DetailRow(dl, inner, "Выгружаемая память ядра", "может уходить на диск", m.kernelPaged, m.kernel, color);
            DetailRow(dl, inner, "Невыгружаемая память ядра", "всегда в ОЗУ: драйверы, сеть, файловая система", m.kernelNonpaged, m.kernel, color);
            ImGui::Dummy(ImVec2(0, Px(6)));
            ImGui::TextDisabled("Кто больше всех — по меткам драйверов:");
            ImGui::Dummy(ImVec2(0, Px(4)));
            const uint64_t biggest = s.pool.empty() ? 1 : s.pool.front().bytes;
            for (const auto& tag : s.pool)
                DetailRow(dl, inner, "«" + tag.tag + "»  " + (tag.description.empty() ? std::string("драйвер или служба Windows") : tag.description),
                          tag.paged ? "выгружаемая" : "невыгружаемая", tag.bytes, biggest, color);
            if (s.pool.empty()) text("Windows не отдала список меток — его показывают только с правами администратора.");
        } else if (part == 4) { // общее и прочее
            const uint64_t rest = m.other > m.modified ? m.other - m.modified : 0;
            DetailRow(dl, inner, "Ждёт записи на диск", "изменённые страницы — Windows запишет их и освободит", m.modified, m.other, color);
            DetailRow(dl, inner, "Встроенная видеокарта, драйверы и общие файлы", "всё, что Windows не приписывает одной программе", rest, m.other, color);
            ImGui::Dummy(ImVec2(0, Px(6)));
            ImGui::TextDisabled("Больше всех общих библиотек и файлов держат:");
            ImGui::Dummy(ImVec2(0, Px(4)));
            std::vector<std::pair<uint64_t, std::string>> shared;
            for (const auto& g : s.groups) {
                uint64_t sum = 0;
                for (const auto& p : g.processes) sum += p.shared;
                if (sum > (16u << 20)) shared.push_back({ sum, g.title });
            }
            std::sort(shared.begin(), shared.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            const uint64_t biggest = shared.empty() ? 1 : shared.front().first;
            for (size_t i = 0; i < shared.size() && i < 8; ++i) DetailRow(dl, inner, shared[i].second, "", shared[i].first, biggest, color);
            text("Общие страницы видны сразу в нескольких программах, поэтому их сумма больше реального объёма.");
        } else if (part == 5) { // кэш
            DetailRow(dl, inner, "Важный кэш", "Windows отдаёт его последним", m.standbyCore, m.cache, color);
            DetailRow(dl, inner, "Обычный кэш", "недавно открытые файлы и программы", m.standbyNormal, m.cache, color);
            DetailRow(dl, inner, "Кэш малой важности", "заранее загружено «на всякий случай» (SuperFetch)", m.standbyReserve, m.cache, color);
            text("Это не занятая память: как только программе нужно место, Windows мгновенно отдаёт его из кэша. "
                 "«Сильная очистка» сбрасывает кэш, но первые запуски программ после неё будут медленнее.");
        } else { // свободно
            text("Совсем пустая память. Windows старается держать её немного — пустая память ничего не ускоряет, "
                 "поэтому остальное она занимает кэшем, который отдаёт по первому требованию.");
        }
    }

    std::string Gb(uint64_t bytes) {
        char text[32];
        const double gb = bytes / 1073741824.0;
        if (gb >= 0.1) snprintf(text, sizeof(text), "%.1f ГБ", gb);
        else snprintf(text, sizeof(text), "%.0f МБ", bytes / 1048576.0);
        return text;
    }

    ImU32 PageColor(float alpha) { return Theme::Background(1).Mix(Theme::Background(0), 0.5f).WithAlpha(alpha); }

    // Карточка с заголовком; содержимое — fields(внутренняя ширина)
    template <class Body>
    void Card(float width, Body&& body) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float pad = Px(24);
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + pad));
        ImGui::BeginGroup();
        body(width - pad * 2);
        ImGui::EndGroup();
        const ImVec2 end(start.x + width, ImGui::GetItemRectMax().y + pad);
        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled(start, end, Theme::Surface(0.9f), Px(24));
        dl->AddRectFilledMultiColor(ImVec2(start.x + Px(1), start.y + Px(1)), ImVec2(end.x - Px(1), start.y + Px(50)), Theme::Overlay(12),
            Theme::Overlay(12), Theme::Overlay(0), Theme::Overlay(0));
        dl->AddRect(start, end, Theme::Overlay(16), Px(24));
        dl->ChannelsMerge();
        ImGui::SetCursorScreenPos(ImVec2(start.x, end.y + Px(16)));
        ImGui::Dummy(ImVec2(width, 0));
    }

    // ------------------------------------------------------------ «Показатели»: настраиваемые плитки
    // Раскладка хранится в anya.ini, [metrics]: order = cpu,ram,… ; columns ; для каждой плитки key_span / _height / _graph / _color / _sub / _temp / _title
    struct Metric { const char* key; const char* name; bool byDefault; };
    const Metric kMetrics[] = {
        { "cpu", "Процессор", true }, { "ram", "Оперативная память", true }, { "disk", "Диск C", true }, { "net", "Интернет", true },
        { "gpu", "Видеокарта", true }, { "igpu", "Встроенная видеокарта", true }, { "cpu_temp", "Температура процессора", false },
        { "gpu_temp", "Температура видеокарты", false }, { "gpu_extra", "Частота видеокарты", false }, { "battery", "Батарея", false },
    };

    const Metric* FindMetric(const std::string& key) {
        for (const Metric& m : kMetrics) if (key == m.key) return &m;
        return nullptr;
    }

    struct TileConfig { std::string key, title; int span = 1, height = 1, graph = 2, color = -1; bool sub = true, temp = true; int x = -1, y = -1, w = -1, h = -1; };

    std::string Key(const std::string& tile, const char* field) { return tile + "_" + field; }

    // настройки плиток — в памяти: из anya.ini читаем один раз, а не 60 раз в секунду
    std::unordered_map<std::string, std::string> g_values;
    std::string CachedValue(const std::string& key, const std::string& fallback) {
        const auto it = g_values.find(key);
        if (it != g_values.end()) return it->second;
        return g_values[key] = Config::Value("metrics", key.c_str(), fallback.c_str());
    }
    void StoreValue(const std::string& key, const std::string& value) {
        g_values[key] = value;
        Config::SetValue("metrics", key.c_str(), value);
    }
    int IntValue(const std::string& key, int fallback) {
        const std::string v = CachedValue(key, std::to_string(fallback));
        return v.empty() ? fallback : std::atoi(v.c_str());
    }

    std::vector<std::string> Order() {
        std::string fallback; // раньше показатели включались по одному — переносим и их
        for (const Metric& m : kMetrics)
            if (CachedValue(m.key, m.byDefault ? "1" : "0") == "1") fallback += (fallback.empty() ? "" : ",") + std::string(m.key);
        const std::string order = CachedValue("order", fallback);
        std::vector<std::string> keys;
        for (size_t start = 0; start <= order.size();) {
            const size_t comma = std::min(order.find(',', start), order.size());
            const std::string key = Text::Trim(order.substr(start, comma - start));
            if (!key.empty() && FindMetric(key) && std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
            start = comma + 1;
        }
        return keys;
    }

    void SaveOrder(const std::vector<std::string>& keys) {
        std::string order;
        for (const auto& k : keys) order += (order.empty() ? "" : ",") + k;
        StoreValue("order", order);
    }

    TileConfig LoadTile(const std::string& key) {
        TileConfig t;
        t.key = key;
        t.span = std::clamp(IntValue(Key(key, "span"), 1), 1, 4);
        t.height = std::clamp(IntValue(Key(key, "height"), 1), 0, 2);
        t.graph = std::clamp(IntValue(Key(key, "graph"), key == "gpu_extra" || key == "battery" ? 0 : 2), 0, 3);
        t.color = IntValue(Key(key, "color"), -1);
        t.sub = IntValue(Key(key, "sub"), 1) != 0;
        t.temp = IntValue(Key(key, "temp"), 1) != 0;
        t.title = CachedValue(Key(key, "title"), "");
        t.x = IntValue(Key(key, "x"), -1);
        t.y = IntValue(Key(key, "y"), -1);
        t.w = IntValue(Key(key, "w"), -1);
        t.h = IntValue(Key(key, "h"), -1);
        if (t.w <= 0 || t.h <= 0) t.x = -1; // места ещё нет — расставим
        return t;
    }

    void SaveTile(const TileConfig& t) {
        StoreValue(Key(t.key, "span"), std::to_string(t.span));
        StoreValue(Key(t.key, "height"), std::to_string(t.height));
        StoreValue(Key(t.key, "graph"), std::to_string(t.graph));
        StoreValue(Key(t.key, "color"), std::to_string(t.color));
        StoreValue(Key(t.key, "sub"), t.sub ? "1" : "0");
        StoreValue(Key(t.key, "temp"), t.temp ? "1" : "0");
        StoreValue(Key(t.key, "title"), t.title);
        StoreValue(Key(t.key, "x"), std::to_string(t.x));
        StoreValue(Key(t.key, "y"), std::to_string(t.y));
        StoreValue(Key(t.key, "w"), std::to_string(t.w));
        StoreValue(Key(t.key, "h"), std::to_string(t.h));
    }

    ImU32 TempColor(float t) {
        if (t < 60) return IM_COL32(110, 231, 183, 255);
        if (t < 80) return IM_COL32(250, 204, 21, 255);
        return IM_COL32(251, 113, 133, 255);
    }

    // График: 1 — линия, 2 — линия с заливкой, 3 — столбики
    void Graph(ImDrawList* dl, ImVec2 a, ImVec2 b, const std::array<float, SystemMonitor::kHistory>& data, float scale, ImU32 color, int style) {
        constexpr int n = SystemMonitor::kHistory;
        auto y = [&](int i) { return b.y - (b.y - a.y) * std::clamp(scale > 0 ? data[i] / scale : 0.0f, 0.0f, 1.0f); };
        const ImU32 fill = (color & ~IM_COL32_A_MASK) | (IM_COL32(0, 0, 0, 38) & IM_COL32_A_MASK);
        if (style == 3) {
            const float w = (b.x - a.x) / n;
            for (int i = 0; i < n; ++i)
                dl->AddRectFilled(ImVec2(a.x + i * w + w * 0.15f, y(i)), ImVec2(a.x + (i + 1) * w - w * 0.15f, b.y), i == n - 1 ? color : fill, Px(1));
            return;
        }
        ImVec2 points[n];
        for (int i = 0; i < n; ++i) points[i] = ImVec2(a.x + (b.x - a.x) * i / (n - 1), y(i));
        if (style == 2)
            for (int i = 0; i + 1 < n; ++i) dl->AddQuadFilled(points[i], points[i + 1], ImVec2(points[i + 1].x, b.y), ImVec2(points[i].x, b.y), fill);
        dl->AddPolyline(points, n, color, 0, Px(1.6f));
    }

    std::string Speed(float kbps) {
        char text[32];
        if (kbps >= 1024) snprintf(text, sizeof(text), "%.1f МБ/с", kbps / 1024);
        else snprintf(text, sizeof(text), "%.0f КБ/с", kbps);
        return text;
    }

    // Что показать в плитке сейчас
    struct TileData { std::string title, value, sub; ImU32 color = 0; const std::array<float, SystemMonitor::kHistory>* history = nullptr; float scale = 100; float temp = -1; bool available = true; };

    TileData Data(const std::string& key, const SystemMonitor::Stats& s) {
        const ImU32 accent = Theme::Accent().WithAlpha(1.0f);
        TileData d;
        char v[64], sub[160];
        if (key == "cpu") {
            snprintf(v, sizeof(v), "%.0f%%", s.cpu);
            snprintf(sub, sizeof(sub), "%.2f ГГц · %d потоков", s.cpuFreqGhz, s.cores);
            d = { "Процессор", v, sub, accent, &s.cpuHistory, 100, s.cpuTemp };
        } else if (key == "ram") {
            snprintf(v, sizeof(v), "%.0f%%", s.ram);
            snprintf(sub, sizeof(sub), "%.1f из %.1f ГБ", s.ramUsedGb, s.ramTotalGb);
            d = { "Оперативная память", v, sub, IM_COL32(125, 175, 245, 255), &s.ramHistory, 100, -1 };
        } else if (key == "disk") {
            snprintf(v, sizeof(v), "%.0f%%", s.diskActive);
            snprintf(sub, sizeof(sub), "чтение %.1f · запись %.1f МБ/с · свободно %.0f ГБ", s.diskReadMBs, s.diskWriteMBs, s.diskFreeGb);
            d = { "Диск C", v, sub, IM_COL32(110, 210, 180, 255), &s.diskHistory, 100, -1 };
        } else if (key == "net") {
            float netMax = 64;
            for (float k : s.netHistory) netMax = std::max(netMax, k);
            d = { "Интернет", "↓ " + Speed(s.netDownKbps), "↑ " + Speed(s.netUpKbps), IM_COL32(250, 165, 125, 255), &s.netHistory, netMax, -1 };
        } else if (key == "gpu") {
            snprintf(v, sizeof(v), "%.0f%%", s.gpu);
            snprintf(sub, sizeof(sub), "%.1f из %.0f ГБ видеопамяти%s", s.vramUsedGb, s.vramTotalGb,
                s.gpuPowerW > 0 ? (" · " + std::to_string(static_cast<int>(s.gpuPowerW)) + " Вт").c_str() : "");
            d = { s.gpuName.empty() ? "Видеокарта" : s.gpuName, v, sub, accent, &s.gpuHistory, 100, s.gpuTemp };
            d.available = s.hasGpu;
        } else if (key == "igpu") {
            snprintf(v, sizeof(v), "%.0f%%", s.igpu);
            d = { s.igpuName.empty() ? "Встроенная видеокарта" : s.igpuName, v, "встроенная — память берёт из ОЗУ", IM_COL32(245, 205, 95, 255), &s.igpuHistory, 100, -1 };
            d.available = s.hasIgpu;
        } else if (key == "cpu_temp") {
            if (s.cpuTemp > 0) snprintf(v, sizeof(v), "%.0f°C", s.cpuTemp); else snprintf(v, sizeof(v), "—");
            d = { "Температура процессора", v, s.cpuTemp > 0 ? "датчик ACPI" : "датчик недоступен", s.cpuTemp > 0 ? TempColor(s.cpuTemp) : IM_COL32(150, 140, 170, 255),
                  &s.cpuTempHistory, 100, -1 };
        } else if (key == "gpu_temp") {
            snprintf(v, sizeof(v), "%.0f°C", s.gpuTemp);
            d = { "Температура видеокарты", v, s.gpuName, TempColor(s.gpuTemp), &s.gpuTempHistory, 100, -1 };
            d.available = s.hasGpu;
        } else if (key == "gpu_extra") {
            snprintf(v, sizeof(v), "%.0f МГц", std::max(0.0f, s.gpuClockMHz));
            snprintf(sub, sizeof(sub), "вентилятор %s · %s", s.gpuFan >= 0 ? (std::to_string(static_cast<int>(s.gpuFan)) + "%").c_str() : "—",
                s.gpuPowerW >= 0 ? (std::to_string(static_cast<int>(s.gpuPowerW)) + " Вт").c_str() : "—");
            d = { "Частота видеокарты", v, sub, accent, &s.gpuHistory, 100, -1 };
            d.available = s.hasGpu;
        } else if (key == "battery") {
            snprintf(v, sizeof(v), "%d%%", s.battery);
            d = { "Батарея", v, s.charging ? "заряжается" : "от батареи", IM_COL32(110, 210, 180, 255), nullptr, 0, -1 };
            d.available = s.hasBattery;
        }
        return d;
    }

    // ------------------------------------------------------------ расстановка плиток: как окна на рабочем столе
    // x и w — в тысячных долях ширины панели, y и h — в пикселях (до масштаба экрана). «Сетка» — прилипание к колонкам и шагу 20,
    // «свобода» — любая позиция. Тянешь за край или угол — размер, за середину — перемещение.
    constexpr int kGridStep = 20, kMinW = 120, kMinH = 70;

    struct Rect { float x, y, w, h; }; // x, w — доли (0…1000), y, h — пиксели до масштаба
    bool Overlap(const Rect& a, const Rect& b) { return a.x < b.x + b.w - 1 && b.x < a.x + a.w - 1 && a.y < b.y + b.h - 1 && b.y < a.y + a.h - 1; }

    bool GridMode() { return CachedValue("layout", "grid") != "free"; }

    Rect Snap(Rect r, int columns) {
        const float unit = 1000.0f / columns;
        r.x = std::round(r.x / unit) * unit;
        r.w = std::max(unit, std::round(r.w / unit) * unit);
        r.y = std::round(r.y / kGridStep) * kGridStep;
        r.h = std::max(static_cast<float>(kMinH), std::round(r.h / kGridStep) * kGridStep);
        r.x = std::clamp(r.x, 0.0f, 1000.0f - r.w);
        return r;
    }

    Rect TileRect(const TileConfig& t) { return { static_cast<float>(t.x), static_cast<float>(t.y), static_cast<float>(t.w), static_cast<float>(t.h) }; }

    void StoreRect(TileConfig& t, const Rect& r) {
        t.x = static_cast<int>(std::lround(r.x)), t.y = static_cast<int>(std::lround(r.y));
        t.w = static_cast<int>(std::lround(r.w)), t.h = static_cast<int>(std::lround(r.h));
        SaveTile(t);
    }

    // Раскладка по умолчанию — строками по колонкам (и для плиток, у которых места ещё нет)
    void PlaceMissing(const std::vector<std::string>& order, int columns) {
        static const int kHeights[] = { 86, 118, 176 };
        float bottom = 0;
        for (const auto& key : order) { const TileConfig t = LoadTile(key); if (t.x >= 0) bottom = std::max(bottom, static_cast<float>(t.y + t.h)); }
        int col = 0;
        float rowY = bottom, rowH = 0;
        for (const auto& key : order) {
            TileConfig t = LoadTile(key);
            if (t.x >= 0) continue;
            const int span = std::min(t.span, columns);
            if (col + span > columns) { col = 0; rowY += rowH; rowH = 0; }
            const float h = static_cast<float>(kHeights[t.height] + 12);
            StoreRect(t, { 1000.0f * col / columns, rowY, 1000.0f * span / columns, h });
            col += span;
            rowH = std::max(rowH, h);
        }
    }

    // Сетка: плитки не налезают — те, что мешают, съезжают вниз
    void ResolveOverlaps(const std::vector<std::string>& order, const std::string& fixed) {
        std::vector<TileConfig> tiles;
        for (const auto& key : order) tiles.push_back(LoadTile(key));
        std::sort(tiles.begin(), tiles.end(), [&](const TileConfig& a, const TileConfig& b) {
            if (a.key == fixed) return true;
            if (b.key == fixed) return false;
            return a.y < b.y || (a.y == b.y && a.x < b.x);
        });
        std::vector<Rect> placed;
        for (auto& t : tiles) {
            Rect r = TileRect(t);
            for (bool moved = true; moved;) {
                moved = false;
                for (const Rect& p : placed)
                    if (Overlap(r, p)) { r.y = p.y + p.h; moved = true; }
            }
            placed.push_back(r);
            if (r.y != t.y) StoreRect(t, r);
        }
    }

    struct Drag { std::string key; bool left = false, right = false, top = false, bottom = false, move = false; ImVec2 mouse; Rect start; Rect now; };
    Drag g_drag;
    std::unordered_map<std::string, Rect> g_shown; // где плитка нарисована сейчас — плавно едет к своему месту

    std::string g_menuTile;       // для какой плитки открыто меню
    char g_titleEdit[64] = "";

    // Меню плитки: название, размер, график, цвет, подписи, порядок
    void TileMenu(std::vector<std::string>& order) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(14), Px(12)));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, Px(14));
        if (Widgets::BeginPopupAnimated("##tileMenu")) {
            TileConfig t = LoadTile(g_menuTile);
            const Metric* m = FindMetric(g_menuTile);
            bool changed = false;
            ImGui::PushFont(Theme::title, Theme::title->LegacySize);
            ImGui::TextUnformatted(m ? m->name : "");
            ImGui::PopFont();
            ImGui::TextDisabled("название");
            ImGui::SetNextItemWidth(Px(240));
            if (ImGui::InputTextWithHint("##tileTitle", "как обычно", g_titleEdit, sizeof(g_titleEdit))) { t.title = Text::Trim(g_titleEdit); changed = true; }
            ImGui::TextDisabled("график");
            static const char* kGraphs[] = { "нет", "линия", "заливка", "столбики" };
            for (int i = 0; i < 4; ++i) {
                if (i) ImGui::SameLine();
                if (ImGui::RadioButton(kGraphs[i], t.graph == i)) { t.graph = i; changed = true; }
            }
            ImGui::TextDisabled("цвет");
            const float r = Px(10);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            for (int i = 0; i <= Theme::kAccentPresets; ++i) { // первый — «как обычно»
                const ImVec2 c(start.x + r + Px(2) + i * Px(28), start.y + r + Px(4));
                ImGui::PushID(i);
                const int rgb = i == 0 ? -1 : Theme::ToRgb(Theme::AccentPreset(i - 1));
                const ImU32 color = i == 0 ? IM_COL32(120, 110, 140, 255) : Theme::AccentPreset(i - 1).WithAlpha(1.0f);
                if (Widgets::Swatch("##c", c, r, color, t.color == rgb)) { t.color = rgb; changed = true; }
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + r * 2 + Px(10)));
            if (ImGui::Button("свой цвет…")) ImGui::OpenPopup("##tileColor");
            if (ImGui::BeginPopup("##tileColor")) {
                const Theme::Color now = Theme::FromRgb(t.color >= 0 ? t.color : 0xB4A0FA);
                float rgbf[3] = { now.r / 255.0f, now.g / 255.0f, now.b / 255.0f };
                if (ImGui::ColorPicker3("##picker", rgbf, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel)) {
                    t.color = Theme::ToRgb({ rgbf[0] * 255, rgbf[1] * 255, rgbf[2] * 255 });
                    changed = true;
                }
                ImGui::EndPopup();
            }
            if (ImGui::Checkbox("подпись снизу", &t.sub)) changed = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("значок температуры", &t.temp)) changed = true;
            ImGui::Separator();
            if (ImGui::Button("обычный размер")) { t.w = 1000 / std::clamp(IntValue("columns", 3), 2, 4); t.h = 130; changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("скрыть")) {
                order.erase(std::remove(order.begin(), order.end(), g_menuTile), order.end());
                SaveOrder(order);
                ImGui::CloseCurrentPopup();
            }
            if (changed) SaveTile(t);
            Widgets::EndPopupAnimated();
        }
        ImGui::PopStyleVar(2);
    }

    // Меню панели: колонки, какие показатели есть, сброс
    void PanelMenu(std::vector<std::string>& order) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(14), Px(12)));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, Px(14));
        if (Widgets::BeginPopupAnimated("##metricsMenu")) {
            ImGui::TextDisabled("расстановка");
            const bool grid = GridMode();
            if (ImGui::RadioButton("сетка — плитки прилипают", grid)) StoreValue("layout", "grid");
            ImGui::SameLine();
            if (ImGui::RadioButton("полная свобода", !grid)) StoreValue("layout", "free");
            ImGui::TextDisabled("колонок в сетке");
            const int columns = std::clamp(IntValue("columns", 3), 2, 4);
            for (int c = 2; c <= 4; ++c) {
                if (c > 2) ImGui::SameLine();
                if (ImGui::RadioButton(std::to_string(c).c_str(), columns == c)) StoreValue("columns", std::to_string(c));
            }
            ImGui::Separator();
            ImGui::TextDisabled("что показывать");
            for (const Metric& m : kMetrics) {
                bool on = std::find(order.begin(), order.end(), m.key) != order.end();
                if (ImGui::Checkbox(m.name, &on)) {
                    if (on) order.push_back(m.key);
                    else order.erase(std::remove(order.begin(), order.end(), std::string(m.key)), order.end());
                    SaveOrder(order);
                }
            }
            ImGui::Separator();
            if (ImGui::Button("сбросить как было")) {
                std::string fallback;
                for (const Metric& m : kMetrics) {
                    if (m.byDefault) fallback += (fallback.empty() ? "" : ",") + std::string(m.key);
                    TileConfig t;
                    t.key = m.key;
                    t.graph = t.key == "gpu_extra" || t.key == "battery" ? 0 : 2;
                    SaveTile(t); // x = -1 — место расставится заново
                    g_shown.erase(t.key);
                }
                StoreValue("order", fallback);
                StoreValue("columns", "3");
                StoreValue("layout", "grid");
                order = Order();
            }
            Widgets::EndPopupAnimated();
        }
        ImGui::PopStyleVar(2);
    }

    void MetricsCard(float width) {
        const auto s = SystemMonitor::Snapshot();
        std::vector<std::string> order = Order();
        Card(width, [&](float inner) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 head = ImGui::GetCursorScreenPos();
            Widgets::Text(dl, Theme::display, head, Theme::kInk, "Показатели");
            const bool grid = GridMode();
            const char* hint = grid ? "тяни за край — размер, за середину — место · сетка · правый клик — настроить"
                                    : "тяни за край — размер, за середину — место · свобода · правый клик — настроить";
            const float hw = Widgets::TextSize(Theme::semibold, hint).x;
            Widgets::Text(dl, Theme::semibold, ImVec2(head.x + inner - hw, head.y + Px(5)), Theme::kInkMuted, hint);
            ImGui::Dummy(ImVec2(inner, Theme::display->LegacySize + Px(14)));

            const int columns = std::clamp(IntValue("columns", 3), 2, 4);
            PlaceMissing(order, columns);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float gap = Px(12);
            auto toScreen = [&](const Rect& r) { // доли и пиксели → экран; зазор между плитками — внутри прямоугольника
                return std::pair{ ImVec2(origin.x + inner * r.x / 1000.0f + gap * 0.5f, origin.y + Px(r.y) + gap * 0.5f),
                                  ImVec2(origin.x + inner * (r.x + r.w) / 1000.0f - gap * 0.5f, origin.y + Px(r.y + r.h) - gap * 0.5f) };
            };
            const ImGuiIO& io = ImGui::GetIO();
            const float dt = io.DeltaTime;
            float bottom = 0;
            bool tileClicked = false;

            // перетаскивание: обновить прямоугольник по движению мыши
            if (!g_drag.key.empty()) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    const float dx = (io.MousePos.x - g_drag.mouse.x) / inner * 1000.0f, dy = (io.MousePos.y - g_drag.mouse.y) / Theme::dpi;
                    Rect r = g_drag.start;
                    const float minW = kMinW / std::max(1.0f, inner / Theme::dpi) * 1000.0f;
                    if (g_drag.move) { r.x += dx; r.y += dy; }
                    if (g_drag.left) { r.x = std::min(r.x + dx, r.x + r.w - minW); r.w = g_drag.start.x + g_drag.start.w - r.x; }
                    if (g_drag.right) r.w = std::max(minW, r.w + dx);
                    if (g_drag.top) { r.y = std::min(r.y + dy, r.y + r.h - kMinH); r.h = g_drag.start.y + g_drag.start.h - r.y; }
                    if (g_drag.bottom) r.h = std::max(static_cast<float>(kMinH), r.h + dy);
                    r.x = std::clamp(r.x, 0.0f, 1000.0f - std::min(r.w, 1000.0f));
                    r.w = std::min(r.w, 1000.0f - r.x);
                    r.y = std::max(0.0f, r.y);
                    g_drag.now = r;
                } else { // отпустили — сохранить (в сетке — прилипнуть и раздвинуть соседей)
                    TileConfig t = LoadTile(g_drag.key);
                    StoreRect(t, grid ? Snap(g_drag.now, columns) : g_drag.now);
                    if (grid) ResolveOverlaps(order, g_drag.key);
                    g_drag = {};
                }
            }

            // сначала все, потом та, которую тащим, — поверх
            std::vector<std::string> drawOrder = order;
            if (!g_drag.key.empty()) {
                drawOrder.erase(std::remove(drawOrder.begin(), drawOrder.end(), g_drag.key), drawOrder.end());
                drawOrder.push_back(g_drag.key);
            }
            for (const auto& key : drawOrder) {
                const TileConfig cfg = LoadTile(key);
                TileData d = Data(key, s);
                if (!d.available) continue;
                if (!cfg.title.empty()) d.title = cfg.title;
                if (cfg.color >= 0) d.color = Theme::FromRgb(cfg.color).WithAlpha(1.0f);
                const bool dragged = g_drag.key == key;
                Rect target = dragged ? g_drag.now : TileRect(cfg);
                // плитка плавно едет на место (после прилипания к сетке и когда её сдвинули соседи)
                Rect& shown = g_shown.try_emplace(key, target).first->second;
                const float k = dragged ? 1.0f : Theme::Ease(dt, 8.7f);
                shown = { shown.x + (target.x - shown.x) * k, shown.y + (target.y - shown.y) * k, shown.w + (target.w - shown.w) * k, shown.h + (target.h - shown.h) * k };
                bottom = std::max(bottom, std::max(shown.y + shown.h, target.y + target.h));
                const auto [a, b] = toScreen(shown);
                const float w = b.x - a.x, h = b.y - a.y;

                // тень места, куда прилипнет плитка в сетке
                if (dragged && grid) {
                    const auto [sa, sb] = toScreen(Snap(g_drag.now, columns));
                    dl->AddRectFilled(sa, sb, Theme::Accent().WithAlpha(0.10f), Px(16));
                    dl->AddRect(sa, sb, Theme::Accent().WithAlpha(0.6f), Px(16), 0, Px(1.5f));
                }

                // края и углы: зона 8 пикселей — изменение размера; середина — перемещение
                ImGui::SetCursorScreenPos(a);
                ImGui::PushID(key.c_str());
                ImGui::InvisibleButton("##tile", ImVec2(std::max(1.0f, w), std::max(1.0f, h)));
                const bool hovered = ImGui::IsItemHovered();
                ImGui::PopID();
                const float zone = Px(8);
                const ImVec2 mp = io.MousePos;
                const bool nearL = mp.x < a.x + zone, nearR = mp.x > b.x - zone, nearT = mp.y < a.y + zone, nearB = mp.y > b.y - zone;
                if (hovered && g_drag.key.empty()) {
                    if ((nearL && nearT) || (nearR && nearB)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                    else if ((nearR && nearT) || (nearL && nearB)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
                    else if (nearL || nearR) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    else if (nearT || nearB) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                    else ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        g_drag.key = key;
                        g_drag.left = nearL, g_drag.right = nearR, g_drag.top = nearT, g_drag.bottom = nearB;
                        g_drag.move = !(nearL || nearR || nearT || nearB);
                        g_drag.mouse = mp;
                        g_drag.start = g_drag.now = TileRect(cfg);
                    }
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        g_menuTile = key;
                        snprintf(g_titleEdit, sizeof(g_titleEdit), "%s", cfg.title.c_str());
                        ImGui::OpenPopup("##tileMenu");
                        tileClicked = true;
                    }
                }
                if (dragged) ImGui::SetMouseCursor(g_drag.move ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Arrow);

                // сама плитка
                const float lift = dragged ? Px(3) : 0.0f;
                if (dragged) dl->AddRectFilled(ImVec2(a.x, a.y + Px(10)), ImVec2(b.x, b.y + Px(14)), IM_COL32(0, 0, 0, 70), Px(18)); // тень, пока несём
                const ImVec2 ta(a.x, a.y - lift), tb(b.x, b.y - lift);
                dl->AddRectFilled(ta, tb, dragged ? Theme::Surface(1.0f) : IM_COL32(0, 0, 0, hovered ? 72 : 50), Px(16));
                dl->AddRect(ta, tb, dragged ? Theme::Accent().WithAlpha(0.8f) : Theme::Overlay(hovered ? 28 : 12), Px(16), 0, dragged ? Px(1.6f) : Px(1));
                if (hovered && !dragged) { // уголок-«ручка» внизу справа
                    dl->AddLine(ImVec2(tb.x - Px(12), tb.y - Px(5)), ImVec2(tb.x - Px(5), tb.y - Px(12)), Theme::Overlay(60), Px(1.4f));
                    dl->AddLine(ImVec2(tb.x - Px(8), tb.y - Px(5)), ImVec2(tb.x - Px(5), tb.y - Px(8)), Theme::Overlay(60), Px(1.4f));
                }
                ImGui::PushClipRect(ta, tb, true);
                const bool compact = h < Px(100), tall = h >= Px(160);
                Widgets::Text(dl, Theme::semibold, ImVec2(ta.x + Px(14), ta.y + Px(12)), Theme::kInkSoft, d.title.c_str());
                ImFont* big = compact ? Theme::display : Theme::hero;
                const float valueY = ta.y + Px(30);
                Widgets::Text(dl, big, ImVec2(ta.x + Px(14), valueY), Theme::kInk, d.value.c_str());
                if (cfg.temp && d.temp > 0) {
                    char temp[16];
                    snprintf(temp, sizeof(temp), "%.0f°C", d.temp);
                    const float vw = Widgets::TextSize(big, d.value.c_str()).x;
                    const ImVec2 tp(ta.x + Px(24) + vw, valueY + (big->LegacySize - Px(20)) * 0.5f);
                    const float tw = Widgets::TextSize(Theme::semibold, temp).x;
                    dl->AddRectFilled(tp, ImVec2(tp.x + tw + Px(14), tp.y + Px(20)), (TempColor(d.temp) & ~IM_COL32_A_MASK) | (IM_COL32(0, 0, 0, 40) & IM_COL32_A_MASK), Px(10));
                    Widgets::Text(dl, Theme::semibold, ImVec2(tp.x + Px(7), tp.y + Px(3)), TempColor(d.temp), temp);
                }
                if (cfg.sub && !compact) Widgets::Text(dl, Theme::semibold, ImVec2(ta.x + Px(14), ta.y + Px(72)), Theme::kInkMuted, d.sub.c_str());
                if (cfg.graph > 0 && d.history) { // высокая — график снизу во всю ширину, иначе справа
                    const ImVec2 ga = tall ? ImVec2(ta.x + Px(14), ta.y + Px(96)) : ImVec2(ta.x + w * (w > Px(420) ? 0.4f : 0.55f), ta.y + Px(14));
                    const ImVec2 gb = tall ? ImVec2(tb.x - Px(14), tb.y - Px(22)) : ImVec2(tb.x - Px(12), compact ? tb.y - Px(14) : ta.y + Px(62));
                    if (gb.x > ga.x + Px(20) && gb.y > ga.y + Px(8)) Graph(dl, ga, gb, *d.history, d.scale, d.color, cfg.graph);
                }
                dl->AddRectFilled(ImVec2(ta.x + Px(14), tb.y - Px(12)), ImVec2(ta.x + Px(40), tb.y - Px(9)), d.color, Px(2));
                if (dragged && !g_drag.move) { // размер, пока тянешь: в сетке — колонки × шаги
                    char size[32];
                    const Rect r = grid ? Snap(g_drag.now, columns) : g_drag.now;
                    if (grid) snprintf(size, sizeof(size), "%d × %d", static_cast<int>(std::lround(r.w * columns / 1000.0f)), static_cast<int>(r.h / kGridStep));
                    else snprintf(size, sizeof(size), "%d × %d", static_cast<int>(w / Theme::dpi), static_cast<int>(h / Theme::dpi));
                    const float sw = Widgets::TextSize(Theme::semibold, size).x;
                    dl->AddRectFilled(ImVec2(tb.x - sw - Px(26), tb.y - Px(32)), ImVec2(tb.x - Px(10), tb.y - Px(10)), IM_COL32(0, 0, 0, 140), Px(10));
                    Widgets::Text(dl, Theme::semibold, ImVec2(tb.x - sw - Px(18), tb.y - Px(28)), Theme::kInk, size);
                }
                ImGui::PopClipRect();
            }
            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + Px(bottom)));
            ImGui::Dummy(ImVec2(inner, 0));
            if (order.empty()) ImGui::TextDisabled("пусто — правый клик, чтобы добавить показатели");

            // правый клик мимо плиток — меню панели
            const ImVec2 cardMin(head.x - Px(24), head.y - Px(24)), cardMax(head.x + inner + Px(24), ImGui::GetCursorScreenPos().y + Px(24));
            if (!tileClicked && g_drag.key.empty() && ImGui::IsMouseHoveringRect(cardMin, cardMax, false) && ImGui::IsWindowHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##metricsMenu");
            TileMenu(order);
            PanelMenu(order);
        });
    }

    // ------------------------------------------------------------ полоса «куда уходит память»
    void MemoryCard(const ProcessMonitor::Snapshot& s, float width, float dt) {
        Card(width, [&](float inner) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto& m = s.memory;
            const uint64_t freeOnly = m.free;
            struct Part { const char* name; const char* hint; uint64_t bytes; Theme::Color color; };
            const Part parts[] = {
                { "Программы", "то, что ты запустил", m.apps, Theme::Accent() },
                { "Службы Windows", "фоновые службы и svchost", m.services, { 125, 175, 245 } },
                { "Система", "ядро процессов Windows, сжатая память", m.system, { 250, 165, 125 } },
                { "Ядро и драйверы", "память ядра Windows и драйверов", m.kernel, { 245, 205, 95 } },
                { "Общее и прочее", "общие библиотеки, встроенная видеокарта, ждёт записи на диск", m.other, { 170, 165, 190 } },
                { "Кэш (ожидание)", "файлы «про запас» — отдаются программам сразу", m.cache, { 110, 210, 180 } },
                { "Свободно", "совсем пустая память", freeOnly, { 70, 60, 90 } },
            };
            const double total = std::max<double>(1.0, static_cast<double>(m.total));
            const ImVec2 head = ImGui::GetCursorScreenPos();
            Widgets::Text(dl, Theme::display, head, Theme::kInk, "Оперативная память");
            const std::string used = Gb(m.total - m.available) + " из " + Gb(m.total) + " занято";
            const float uw = Widgets::TextSize(Theme::title, used.c_str()).x;
            Widgets::Text(dl, Theme::title, ImVec2(head.x + inner - uw, head.y + Px(3)), Theme::kInkSoft, used.c_str());
            ImGui::Dummy(ImVec2(inner, Theme::display->LegacySize + Px(14)));

            // полоса из частей, ширины плавно перетекают
            const ImVec2 bar = ImGui::GetCursorScreenPos();
            const float h = Px(16);
            dl->AddRectFilled(bar, ImVec2(bar.x + inner, bar.y + h), IM_COL32(0, 0, 0, 80), h * 0.5f);
            float x = bar.x;
            int hoveredPart = -1;
            static float s_lift[7] = {};
            for (int i = 0; i < 7; ++i) {
                g_bar[i] += (static_cast<float>(parts[i].bytes / total) - g_bar[i]) * std::min(1.0f, dt * 5.0f);
                const float w = inner * g_bar[i];
                if (w < 0.5f) continue;
                // наведение: сегмент приподнимается и светлеет
                const bool over = ImGui::IsMouseHoveringRect(ImVec2(x, bar.y - Px(4)), ImVec2(x + w, bar.y + h + Px(4)), false) && ImGui::IsWindowHovered();
                if (over) hoveredPart = i;
                s_lift[i] += ((over ? 1.0f : 0.0f) - s_lift[i]) * Theme::Ease(dt, 8.7f);
                const float grow = Px(3) * s_lift[i];
                const ImDrawFlags corners = (x == bar.x ? ImDrawFlags_RoundCornersLeft : 0) | (i == 6 || x + w >= bar.x + inner - 0.5f ? ImDrawFlags_RoundCornersRight : 0);
                const Theme::Color color = parts[i].color.Mix({ 255, 255, 255 }, 0.25f * s_lift[i]);
                dl->AddRectFilled(ImVec2(x, bar.y - grow), ImVec2(x + w, bar.y + h + grow), color.WithAlpha(i == 6 ? 0.5f : 0.95f), h * 0.5f,
                    corners ? corners : ImDrawFlags_RoundCornersNone);
                x += w;
            }
            if (hoveredPart >= 0) { // подсказка: что это, сколько и кто больше всех
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { g_detail = g_detail == hoveredPart ? -1 : hoveredPart; g_detailAnim = 0.0f; }
                const Part& part = parts[hoveredPart];
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(14), Px(12)));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Px(14));
                ImGui::BeginTooltip();
                ImGui::PushFont(Theme::title, Theme::title->LegacySize);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(part.color.Mix({ 255, 255, 255 }, 0.35f).WithAlpha(1.0f)), "%s", part.name);
                ImGui::PopFont();
                char line[96];
                snprintf(line, sizeof(line), "%s · %.0f%% памяти", Gb(part.bytes).c_str(), part.bytes * 100.0 / total);
                ImGui::TextUnformatted(line);
                ImGui::TextDisabled("%s", part.hint);
                // три главных «едока»
                std::vector<std::pair<std::string, uint64_t>> top;
                if (hoveredPart <= 2) {
                    const Kind kind = hoveredPart == 0 ? Kind::App : hoveredPart == 1 ? Kind::Service : Kind::System;
                    for (const auto& g : s.groups) if (g.kind == kind && top.size() < 3) top.push_back({ g.title, g.memory });
                } else if (hoveredPart == 3) {
                    for (const auto& t : s.pool) if (top.size() < 3) top.push_back({ "«" + t.tag + "» " + (t.description.empty() ? "драйвер" : t.description), t.bytes });
                } else if (hoveredPart == 4) {
                    top.push_back({ "ждёт записи на диск", m.modified });
                    top.push_back({ "видеокарта, драйверы, общие файлы", m.other > m.modified ? m.other - m.modified : 0 });
                } else if (hoveredPart == 5) {
                    top.push_back({ "обычный кэш", m.standbyNormal });
                    top.push_back({ "важный кэш", m.standbyCore });
                    top.push_back({ "малой важности", m.standbyReserve });
                }
                if (!top.empty()) {
                    ImGui::Dummy(ImVec2(0, Px(4)));
                    for (const auto& [name, bytes] : top) ImGui::Text("•  %s — %s", name.c_str(), Gb(bytes).c_str());
                }
                ImGui::Dummy(ImVec2(0, Px(4)));
                ImGui::TextDisabled(g_detail == hoveredPart ? "нажми — свернуть" : "нажми — подробнее");
                ImGui::EndTooltip();
                ImGui::PopStyleVar(2);
            }
            ImGui::Dummy(ImVec2(inner, h + Px(16)));

            // подписи в две колонки; нажмёшь — под полосой появится, кто именно ест эту память
            const float column = inner * 0.5f;
            for (int i = 0; i < 7; ++i) {
                const ImVec2 p(bar.x + (i % 2) * column, ImGui::GetCursorScreenPos().y);
                const ImVec2 cellMin(p.x - Px(8), p.y - Px(6)), cellMax(p.x + column - Px(12), p.y + Px(38));
                ImGui::SetCursorScreenPos(cellMin);
                ImGui::PushID(i);
                if (ImGui::InvisibleButton("##part", ImVec2(cellMax.x - cellMin.x, cellMax.y - cellMin.y))) {
                    g_detail = g_detail == i ? -1 : i;
                    g_detailAnim = 0.0f;
                }
                const bool hovered = ImGui::IsItemHovered();
                ImGui::PopID();
                ImGui::SetCursorScreenPos(ImVec2(bar.x, p.y));
                if (hovered || g_detail == i)
                    dl->AddRectFilled(cellMin, cellMax, g_detail == i ? parts[i].color.WithAlpha(0.14f) : Theme::Overlay(8), Px(10));
                if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                dl->AddCircleFilled(ImVec2(p.x + Px(6), p.y + Px(9)), Px(5), parts[i].color.WithAlpha(1.0f), 16);
                Widgets::Text(dl, Theme::semibold, ImVec2(p.x + Px(20), p.y + Px(1)), Theme::kInk, parts[i].name);
                const std::string value = Gb(parts[i].bytes);
                const float vw = Widgets::TextSize(Theme::semibold, value.c_str()).x;
                Widgets::Text(dl, Theme::semibold, ImVec2(p.x + column - vw - Px(20), p.y + Px(1)), Theme::kInkSoft, value.c_str());
                Widgets::Text(dl, Theme::semibold, ImVec2(p.x + Px(20), p.y + Px(19)), Theme::kInkMuted, parts[i].hint);
                if (i % 2 == 1 || i == 6) ImGui::Dummy(ImVec2(inner, Px(44)));
            }

            if (g_detail >= 0 && g_detail < 7) { // панель «кто ест»: выезжает под полосой
                g_detailAnim += (1.0f - g_detailAnim) * Theme::Ease(dt, 6.2f);
                ImGui::Dummy(ImVec2(0, Px(6)));
                const ImVec2 at = ImGui::GetCursorScreenPos();
                dl->AddLine(ImVec2(at.x, at.y), ImVec2(at.x + inner, at.y), Theme::Overlay(14), Px(1));
                ImGui::Dummy(ImVec2(0, Px(10) + (1.0f - g_detailAnim) * Px(10)));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * g_detailAnim);
                Widgets::Text(dl, Theme::title, ImGui::GetCursorScreenPos(), parts[g_detail].color.Mix({ 255, 255, 255 }, 0.4f).WithAlpha(g_detailAnim),
                    (std::string(parts[g_detail].name) + " — выбранное потребление: " + Gb(parts[g_detail].bytes)).c_str());
                ImGui::Dummy(ImVec2(inner, Theme::title->LegacySize + Px(10)));
                DetailPanel(s, g_detail, inner, parts[g_detail].color.WithAlpha(0.9f));
                ImGui::PopStyleVar();
            }
        });
    }

    // ------------------------------------------------------------ список программ и служб
    bool Matches(const ProcessMonitor::Group& g, const std::wstring& query) {
        if (g_filter == 1 && g.kind != Kind::App) return false;
        if (g_filter == 2 && g.kind != Kind::Service) return false;
        if (g_filter == 3 && g.kind != Kind::System) return false;
        if (query.empty()) return true;
        auto has = [&](const std::string& s) { return Text::LowerRu(Text::ToWide(s)).find(query) != std::wstring::npos; };
        if (has(g.title) || has(g.exe)) return true;
        for (const auto& p : g.processes)
            for (const auto& svc : p.services)
                if (has(svc)) return true;
        return false;
    }

    void ListCard(const ProcessMonitor::Snapshot& s, float width) {
        const std::wstring query = Text::LowerRu(Text::ToWide(Text::Trim(g_query)));
        Card(width, [&](float inner) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            // фильтр «Все · Программы · Службы · Система»
            static const char* kFilters[] = { "Все", "Программы", "Службы", "Система" };
            float fx = ImGui::GetCursorScreenPos().x;
            const float fy = ImGui::GetCursorScreenPos().y;
            for (int i = 0; i < 4; ++i) {
                ImGui::PushID(i);
                ImVec2 size;
                if (Widgets::Chip("##filter", ImVec2(fx, fy), kFilters[i], &size)) g_filter = i;
                if (g_filter == i) dl->AddRect(ImVec2(fx, fy), ImVec2(fx + size.x, fy + size.y), Theme::Accent().WithAlpha(0.8f), size.y * 0.5f, 0, Px(1.4f));
                ImGui::PopID();
                fx += size.x + Px(8);
            }
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, fy + Px(40)));
            ImGui::Dummy(ImVec2(0, 0));

            // шапка таблицы
            const float memX = inner - Px(90), cpuX = inner - Px(200);
            const ImVec2 hp = ImGui::GetCursorScreenPos();
            Widgets::Text(dl, Theme::semibold, hp, Theme::kInkMuted, "ПРИЛОЖЕНИЕ");
            Widgets::Text(dl, Theme::semibold, ImVec2(hp.x + cpuX, hp.y), Theme::kInkMuted, "ЦП");
            Widgets::Text(dl, Theme::semibold, ImVec2(hp.x + memX, hp.y), Theme::kInkMuted, "ПАМЯТЬ");
            ImGui::Dummy(ImVec2(inner, Px(22)));

            const uint64_t biggest = s.groups.empty() ? 1 : std::max<uint64_t>(1, s.groups.front().memory);
            int shown = 0;
            for (const auto& g : s.groups) {
                if (!Matches(g, query)) continue;
                if (++shown > 150) break;
                const std::string key = g.exe + "|" + g.title;
                const bool open = g_expanded.count(key) > 0;
                const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + inner, a.y + Px(40));
                ImGui::PushID(key.c_str());
                if (ImGui::InvisibleButton("##row", ImVec2(inner, Px(40)))) open ? (void)g_expanded.erase(key) : (void)g_expanded.insert(key);
                const bool hovered = ImGui::IsItemHovered();
                if (hovered || open) dl->AddRectFilled(a, b, Theme::Overlay(open ? 14 : 9), Px(10));

                // стрелка раскрытия, название, число процессов
                const float cy = (a.y + b.y) * 0.5f;
                const float t = open ? 1.0f : 0.0f;
                const ImVec2 c(a.x + Px(12), cy);
                const float s2 = Px(4);
                if (t > 0.5f) { dl->AddLine(ImVec2(c.x - s2, c.y - s2 * 0.5f), ImVec2(c.x, c.y + s2 * 0.5f), Theme::kInkSoft, Px(1.6f)); dl->AddLine(ImVec2(c.x, c.y + s2 * 0.5f), ImVec2(c.x + s2, c.y - s2 * 0.5f), Theme::kInkSoft, Px(1.6f)); }
                else { dl->AddLine(ImVec2(c.x - s2 * 0.5f, c.y - s2), ImVec2(c.x + s2 * 0.5f, c.y), Theme::kInkMuted, Px(1.6f)); dl->AddLine(ImVec2(c.x + s2 * 0.5f, c.y), ImVec2(c.x - s2 * 0.5f, c.y + s2), Theme::kInkMuted, Px(1.6f)); }
                const ImU32 kindColor = g.kind == Kind::App ? Theme::Accent().WithAlpha(1.0f) : g.kind == Kind::Service ? IM_COL32(125, 175, 245, 255) : IM_COL32(250, 165, 125, 255);
                dl->AddCircleFilled(ImVec2(a.x + Px(30), cy), Px(4), kindColor, 12);
                std::string title = g.title;
                if (g.processes.size() > 1) title += "  ×" + std::to_string(g.processes.size());
                ImGui::PushClipRect(a, ImVec2(a.x + cpuX - Px(12), b.y), true);
                Widgets::Text(dl, Theme::title, ImVec2(a.x + Px(42), cy - Theme::title->LegacySize * 0.55f), Theme::kInk, title.c_str());
                ImGui::PopClipRect();

                char cpu[16];
                snprintf(cpu, sizeof(cpu), "%.1f%%", g.cpu);
                Widgets::Text(dl, Theme::semibold, ImVec2(a.x + cpuX, cy - Theme::semibold->LegacySize * 0.5f), g.cpu > 20 ? Theme::kInk : Theme::kInkSoft, cpu);
                // память: число и тонкая полоска относительно самой «тяжёлой» программы
                const std::string memory = Gb(g.memory);
                Widgets::Text(dl, Theme::semibold, ImVec2(a.x + memX, cy - Theme::semibold->LegacySize * 0.5f - Px(5)), Theme::kInk, memory.c_str());
                const float bw = Px(80) * static_cast<float>(g.memory) / biggest;
                dl->AddRectFilled(ImVec2(a.x + memX, cy + Px(8)), ImVec2(a.x + memX + Px(80), cy + Px(11)), IM_COL32(0, 0, 0, 80), Px(2));
                dl->AddRectFilled(ImVec2(a.x + memX, cy + Px(8)), ImVec2(a.x + memX + bw, cy + Px(11)), kindColor, Px(2));
                ImGui::PopID();
                ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + Px(2)));
                ImGui::Dummy(ImVec2(0, 0)); // ImGui: окно растёт только от элементов, не от курсора

                if (!open) continue;
                for (const auto& p : g.processes) { // процессы внутри программы
                    const ImVec2 pa = ImGui::GetCursorScreenPos();
                    ImGui::PushID(static_cast<int>(p.pid));
                    std::string line = p.name + "  ·  PID " + std::to_string(p.pid);
                    if (!p.services.empty()) {
                        line += "  ·  ";
                        for (size_t i = 0; i < p.services.size() && i < 4; ++i) line += (i ? ", " : "") + p.services[i];
                        if (p.services.size() > 4) line += " и ещё " + std::to_string(p.services.size() - 4);
                    }
                    ImGui::PushClipRect(pa, ImVec2(pa.x + cpuX - Px(12) - (g.kind == Kind::App ? Px(90) : 0), pa.y + Px(28)), true);
                    Widgets::Text(dl, Theme::semibold, ImVec2(pa.x + Px(42), pa.y + Px(6)), Theme::kInkMuted, line.c_str());
                    ImGui::PopClipRect();
                    snprintf(cpu, sizeof(cpu), "%.1f%%", p.cpu);
                    Widgets::Text(dl, Theme::semibold, ImVec2(pa.x + cpuX, pa.y + Px(6)), Theme::kInkMuted, cpu);
                    Widgets::Text(dl, Theme::semibold, ImVec2(pa.x + memX, pa.y + Px(6)), Theme::kInkSoft, Gb(p.memory).c_str());
                    if (g.kind == Kind::App && p.pid != ProcessMonitor::SelfPid()) {
                        const bool confirming = g_confirmPid == p.pid && ImGui::GetTime() < g_confirmUntil;
                        ImGui::SetCursorScreenPos(ImVec2(pa.x + cpuX - Px(100), pa.y + Px(2)));
                        ImGui::PushStyleColor(ImGuiCol_Button, confirming ? IM_COL32(232, 72, 104, 200) : IM_COL32(0, 0, 0, 60));
                        if (ImGui::SmallButton(confirming ? "точно?" : "завершить")) {
                            if (confirming) { ProcessMonitor::EndProcess(p.pid); g_confirmPid = 0; }
                            else { g_confirmPid = p.pid; g_confirmUntil = ImGui::GetTime() + 3.0; }
                        }
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopID();
                    ImGui::SetCursorScreenPos(ImVec2(pa.x, pa.y + Px(28)));
                    ImGui::Dummy(ImVec2(0, 0));
                }
                if (g.kind == Kind::App) { // потолок памяти для этой программы
                    static const char* kLimits[] = { "без ограничения", "256 МБ", "512 МБ", "1 ГБ", "2 ГБ" };
                    static const uint64_t kBytes[] = { 0, 256ull << 20, 512ull << 20, 1ull << 30, 2ull << 30 };
                    const uint64_t now = ProcessMonitor::Limit(g.exe);
                    int current = 0;
                    for (int i = 0; i < 5; ++i) if (kBytes[i] == now) current = i;
                    const ImVec2 la = ImGui::GetCursorScreenPos();
                    Widgets::Text(dl, Theme::semibold, ImVec2(la.x + Px(42), la.y + Px(7)), Theme::kInkSoft, "ограничить память:");
                    ImGui::SetCursorScreenPos(ImVec2(la.x + Px(42) + Widgets::TextSize(Theme::semibold, "ограничить память:").x + Px(10), la.y));
                    ImGui::PushID("limit");
                    if (Widgets::Combo("##limit", &current, kLimits, 5, Px(170))) ProcessMonitor::SetLimit(g.exe, kBytes[current]);
                    ImGui::PopID();
                    if (current > 0) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("лишнее Windows держит на диске — программа может подтормаживать");
                    }
                }
                ImGui::Dummy(ImVec2(inner, Px(6)));
            }
            if (shown == 0) ImGui::TextDisabled(s.ready ? "ничего не нашла" : "собираю данные…");
        });
    }
}

void ProcessesPage::Open() {
    g_open = true;
    ProcessMonitor::SetActive(true);
}

void ProcessesPage::Close() {
    g_open = false;
    ProcessMonitor::SetActive(false);
}

bool ProcessesPage::IsOpen() { return g_open; }
bool ProcessesPage::Covers() { return g_open && g_anim > 0.97f; }
float ProcessesPage::Visibility() { return g_anim; }

void ProcessesPage::Draw(ImVec2 screen, float top, float dt) {
    g_anim += ((g_open ? 1.0f : 0.0f) - g_anim) * Theme::Ease(dt, 8.7f);
    if (g_anim < 0.01f) return;
    const float ease = 1.0f - std::pow(1.0f - g_anim, 3.0f);
    const ProcessMonitor::Snapshot s = ProcessMonitor::Get();

    ImGui::SetNextWindowPos(ImVec2(0, top));
    ImGui::SetNextWindowSize(ImVec2(screen.x, screen.y - top));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, PageColor(0.22f));
    ImGui::Begin("##processesPage", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + (1.0f - ease) * Px(14));
    const ImVec2 size = ImGui::GetWindowSize();

    const float width = std::min(Px(1000), size.x - Px(48));
    const float left = origin.x + (size.x - width) * 0.5f;
    const float headerY = origin.y + Px(24);
    Widgets::Text(dl, Theme::hero, ImVec2(left, headerY), Theme::kInk, "Процессы");
    char sub[96];
    snprintf(sub, sizeof(sub), "%d процессов · обновляется каждые 2 секунды", s.processCount);
    Widgets::Text(dl, Theme::semibold, ImVec2(left, headerY + Px(42)), Theme::kInkMuted, sub);

    // кнопка «Очистить ОЗУ» и поиск справа
    const float buttonW = Px(150), searchW = std::min(Px(240), width - Px(600));
    const ImVec2 bp(left + width - buttonW, headerY + Px(2));
    ImGui::SetCursorScreenPos(bp);
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::Accent().WithAlpha(0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Accent().Mix({ 255, 255, 255 }, 0.2f).WithAlpha(1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(18, 11, 29, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Px(14));
    auto startClean = [](bool deep) {
        if (g_cleaning.exchange(true)) return;
        std::thread([deep] {
            const auto r = ProcessMonitor::CleanMemory(deep);
            std::string text = "Освободила " + Gb(r.freed);
            if (r.standbyPurged) text += " — процессы, кэш и вся система";
            else if (r.declined) text += " — без кэша: в окне Windows выбрали «Нет»";
            else text += " — " + std::to_string(r.trimmed) + " процессов";
            std::lock_guard lock(g_cleanMutex);
            g_cleanText = text;
            g_cleanShownAt = -1; // покажем с текущего кадра
            g_cleaning = false;
        }).detach();
    };
    if (ImGui::Button(g_cleaning.load() ? "чищу…" : "Очистить ОЗУ", ImVec2(buttonW, Px(36)))) startClean(false);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    // «сильная» — ещё кэш и вся система; Windows спросит права администратора
    ImGui::SetCursorScreenPos(ImVec2(bp.x - Px(10) - Px(170), bp.y));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Px(14));
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Accent().WithAlpha(0.7f));
    if (ImGui::Button("Сильная очистка", ImVec2(Px(170), Px(36)))) startClean(true);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (searchW > Px(120)) {
        const ImVec2 sp(bp.x - Px(192) - searchW, headerY + Px(2));
        dl->AddRectFilled(sp, ImVec2(sp.x + searchW, sp.y + Px(36)), IM_COL32(0, 0, 0, 70), Px(18));
        Widgets::DrawIcon(dl, Icon::Search, ImVec2(sp.x + Px(18), sp.y + Px(18)), Px(14), Theme::kInkMuted);
        ImGui::SetCursorScreenPos(ImVec2(sp.x + Px(32), sp.y + (Px(36) - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::SetNextItemWidth(searchW - Px(40));
        for (ImGuiCol col : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_Border }) ImGui::PushStyleColor(col, IM_COL32(0, 0, 0, 0));
        ImGui::InputTextWithHint("##processSearch", "поиск программы или службы…", g_query, sizeof(g_query));
        ImGui::PopStyleColor(4);
    }
    { // итог очистки — под кнопкой, 7 секунд
        std::lock_guard lock(g_cleanMutex);
        if (g_cleanShownAt < 0) g_cleanShownAt = ImGui::GetTime();
        if (!g_cleanText.empty() && ImGui::GetTime() - g_cleanShownAt < 7.0) {
            const float tw = Widgets::TextSize(Theme::semibold, g_cleanText.c_str()).x;
            Widgets::Text(dl, Theme::semibold, ImVec2(left + width - tw, headerY + Px(44)), Theme::kOk, g_cleanText.c_str());
        }
    }

    const float contentTop = headerY + Px(76);
    ImGui::SetCursorScreenPos(ImVec2(left - Px(4), contentTop));
    ImGui::BeginChild("##processesBody", ImVec2(origin.x + size.x - Px(6) - (left - Px(4)), origin.y + size.y - contentTop), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    Widgets::SmoothScroll();
    ImGui::SetCursorPos(ImVec2(Px(4), Px(2)));
    MetricsCard(width);
    MemoryCard(s, width, dt);
    ListCard(s, width);
    ImGui::Dummy(ImVec2(0, Px(12)));
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}
