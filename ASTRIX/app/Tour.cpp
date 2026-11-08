#include "Tour.h"

#include "Settings.h"
#include "SettingsPage.h"
#include "TextUtils.h"
#include "Theme.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <string>

using Theme::Px;

namespace {
    constexpr int kClose = -2, kKeep = -1, kConnections = 8; // номер раздела «Связь»

    struct Step { const char* target; const char* title; const char* text; int section; };
    const Step kSteps[] = {
        { nullptr, "Привет! Это ASTRIX", "Сейчас за минуту покажу, что здесь где. Просто нажимай «Далее».", kClose },
        { "orb", "Это твоя помощница", "Нажми на шар или на пробел и говори, как с человеком: «открой Steam», «какая завтра погода».", kKeep },
        { "dock", "Можно и писать", "Не хочется говорить — напиши сюда и нажми Enter.", kKeep },
        { "nav", "Вкладки", "Главная — этот экран. Процессы — что грузит компьютер. Сценарии — несколько действий одной фразой. Настройки — всё остальное.", kKeep },
        { "weather", "Погода", "Наведи мышку — покажу весь день. Город меняется в настройках.", kKeep },
        { "widgets", "Компьютер и курсы валют", "Здесь видно, насколько загружен компьютер, что играет и сколько стоит доллар.", kKeep },
        { "sidebar", "Настройки", "Слева разделы. Тот, в котором ты сейчас, выделен цветом.", 0 },
        { "search", "Не можешь что-то найти?", "Напиши сюда слово, например «голос». Или спроси вслух: «где настройка голоса?» — покажу и подсвечу оранжевым.", kKeep },
        { "content", "Телефон и Discord", "Здесь инструкция по шагам: как сделать бота и подключить Discord. Все ссылки уже готовы, гуглить ничего не надо.", kConnections },
        { nullptr, "Готово!", "Если что-то забудешь, это обучение можно запустить снова: Настройки → Общие.", kClose },
    };
    constexpr int kCount = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));

    struct Box { ImVec2 min, max; int frame = -10; };
    std::map<std::string, Box> g_marks;
    bool g_active = false, g_checked = false;
    int g_step = 0;
    float g_fade = 0, g_wait = 0;
    ImVec2 g_a(0, 0), g_b(0, 0);

    void Enter(int step) {
        g_step = std::clamp(step, 0, kCount - 1);
        const int section = kSteps[g_step].section;
        if (section == kClose) SettingsPage::Close();
        else if (section >= 0) SettingsPage::Open(section);
    }

    void Finish() {
        g_active = false;
        SettingsPage::Close();
        Config::SetValue("anya", "tour", "1");
    }

    ImVec2 Lerp(ImVec2 from, ImVec2 to, float k) { return ImVec2(from.x + (to.x - from.x) * k, from.y + (to.y - from.y) * k); }
}

void Tour::Start() {
    g_active = true;
    g_checked = true;
    Enter(0);
}

void Tour::GoTo(int step) {
    g_active = true;
    g_checked = true;
    Enter(step);
}

bool Tour::Active() { return g_active; }

void Tour::Mark(const char* id, ImVec2 min, ImVec2 max) { g_marks[id] = { min, max, ImGui::GetFrameCount() }; }

void Tour::Draw(ImVec2 screen, float dt, bool wizardOpen) {
    if (!g_checked && !wizardOpen && (g_wait += dt) > 1.5f) { // первый запуск: сразу после знакомства
        g_checked = true;
        if (Config::Value("anya", "tour", "0") != "1") Start();
    }
    g_fade += ((g_active ? 1.0f : 0.0f) - g_fade) * Theme::Ease(dt, 8.0f);
    if (g_fade < 0.01f) return;

    const Step& step = kSteps[g_step];
    const ImVec2 center(screen.x * 0.5f, screen.y * 0.5f);
    ImVec2 wantA = center, wantB = center;
    bool hole = false;
    if (step.target) {
        const auto it = g_marks.find(step.target);
        if (it != g_marks.end() && ImGui::GetFrameCount() - it->second.frame <= 2) {
            wantA = ImVec2(it->second.min.x - Px(10), it->second.min.y - Px(10));
            wantB = ImVec2(it->second.max.x + Px(10), it->second.max.y + Px(10));
            hole = true;
        }
    }
    const float k = Theme::Ease(dt, 9.0f);
    g_a = Lerp(g_a, wantA, k);
    g_b = Lerp(g_b, wantB, k);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(screen);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##tour", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImU32 dim = IM_COL32(6, 4, 12, static_cast<int>(185 * g_fade));
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(screen.x, g_a.y), dim);
    dl->AddRectFilled(ImVec2(0, g_b.y), screen, dim);
    dl->AddRectFilled(ImVec2(0, g_a.y), ImVec2(g_a.x, g_b.y), dim);
    dl->AddRectFilled(ImVec2(g_b.x, g_a.y), ImVec2(screen.x, g_b.y), dim);
    if (hole) {
        const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.0f);
        for (int i = 3; i >= 1; --i)
            dl->AddRect(ImVec2(g_a.x - Px(2.0f * i), g_a.y - Px(2.0f * i)), ImVec2(g_b.x + Px(2.0f * i), g_b.y + Px(2.0f * i)), IM_COL32(255, 152, 48, static_cast<int>(36 * (4 - i) * pulse * g_fade)), Px(16), 0, Px(2));
        dl->AddRect(g_a, g_b, IM_COL32(255, 152, 48, static_cast<int>(240 * g_fade)), Px(14), 0, Px(2.2f));
    }

    // карточка с объяснением
    const float width = std::min(Px(430), screen.x - Px(32)), pad = Px(22), inner = width - pad * 2;
    const ImFont* titleFont = Theme::display;
    const ImFont* textFont = Theme::body;
    const ImVec2 titleSize = const_cast<ImFont*>(titleFont)->CalcTextSizeA(titleFont->LegacySize, FLT_MAX, inner, step.title);
    const ImVec2 textSize = const_cast<ImFont*>(textFont)->CalcTextSizeA(textFont->LegacySize, FLT_MAX, inner, step.text);
    const float height = pad * 2 + titleSize.y + Px(8) + textSize.y + Px(18) + Px(38);
    ImVec2 at(center.x - width * 0.5f, center.y - height * 0.5f);
    if (hole) {
        const float gap = Px(18);
        at.x = (g_a.x + g_b.x) * 0.5f - width * 0.5f;
        if (g_b.y + gap + height < screen.y - Px(12)) at.y = g_b.y + gap;                       // снизу
        else if (g_a.y - gap - height > Px(12)) at.y = g_a.y - gap - height;                     // сверху
        else if (g_b.x + gap + width < screen.x - Px(12)) { at.x = g_b.x + gap; at.y = (g_a.y + g_b.y) * 0.5f - height * 0.5f; } // справа
        else if (g_a.x - gap - width > Px(12)) { at.x = g_a.x - gap - width; at.y = (g_a.y + g_b.y) * 0.5f - height * 0.5f; }    // слева
    }
    at.x = std::clamp(at.x, Px(16), std::max(Px(16), screen.x - width - Px(16)));
    at.y = std::clamp(at.y, Px(16), std::max(Px(16), screen.y - height - Px(16)));
    const ImVec2 cardMax(at.x + width, at.y + height);
    dl->AddRectFilled(ImVec2(at.x + Px(3), at.y + Px(6)), ImVec2(cardMax.x + Px(3), cardMax.y + Px(6)), IM_COL32(0, 0, 0, static_cast<int>(90 * g_fade)), Px(20));
    dl->AddRectFilled(at, cardMax, Theme::SurfaceSolid(), Px(20));
    dl->AddRect(at, cardMax, Theme::Accent().WithAlpha(0.8f * g_fade), Px(20), 0, Px(1.4f));
    dl->AddText(Theme::display, titleFont->LegacySize, ImVec2(at.x + pad, at.y + pad), Theme::kInk, step.title, nullptr, inner);
    dl->AddText(Theme::body, textFont->LegacySize, ImVec2(at.x + pad, at.y + pad + titleSize.y + Px(8)), Theme::kInkSoft, step.text, nullptr, inner);

    const std::string counter = std::to_string(g_step + 1) + " из " + std::to_string(kCount);
    const float buttonY = cardMax.y - pad - Px(34);
    ImGui::SetCursorScreenPos(ImVec2(at.x + pad, buttonY));
    if (g_step > 0) {
        if (ImGui::Button(" Назад ")) Enter(g_step - 1);
    } else if (ImGui::Button(" Пропустить ")) Finish();
    if (g_step > 0 && g_step < kCount - 1) {
        ImGui::SameLine();
        if (ImGui::SmallButton("пропустить")) Finish();
    }
    const char* nextLabel = g_step == kCount - 1 ? "  Начать  " : "  Далее  ";
    const float nextWidth = ImGui::CalcTextSize(nextLabel).x + ImGui::GetStyle().FramePadding.x * 2;
    const float counterWidth = ImGui::CalcTextSize(counter.c_str()).x;
    dl->AddText(ImVec2(cardMax.x - pad - nextWidth - counterWidth - Px(12), buttonY + Px(7)), Theme::kInkMuted, counter.c_str());
    ImGui::SetCursorScreenPos(ImVec2(cardMax.x - pad - nextWidth, buttonY));
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::Accent().WithAlpha(0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Accent().WithAlpha(1.0f));
    if (g_active && (ImGui::Button(nextLabel) || ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        if (g_step == kCount - 1) Finish();
        else Enter(g_step + 1);
    }
    ImGui::PopStyleColor(2);
    ImGui::End();
    ImGui::PopStyleVar(2);
}
