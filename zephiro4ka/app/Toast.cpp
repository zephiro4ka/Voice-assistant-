#include "Toast.h"

#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>

using Theme::Px;

namespace {
    std::string g_text, g_yes, g_no;
    std::function<void()> g_onYes;
    bool g_shown = false;
    float g_anim = 0.0f;
}

void Toast::Ask(const std::string& text, std::function<void()> onYes, const std::string& yes, const std::string& no) {
    g_text = text;
    g_yes = yes;
    g_no = no;
    g_onYes = std::move(onYes);
    g_shown = true;
}

void Toast::Draw(ImVec2 screen, float dt) {
    g_anim += ((g_shown ? 1.0f : 0.0f) - g_anim) * std::min(1.0f, dt * 10.0f);
    if (g_anim < 0.01f) return;
    const float ease = 1.0f - std::pow(1.0f - g_anim, 3.0f);

    const float buttons = Px(96) * 2 + Px(10);
    const float textWidth = std::min(Widgets::TextSize(Theme::body, g_text.c_str()).x, screen.x - Px(120) - buttons);
    const ImVec2 size(textWidth + buttons + Px(64), Px(64));
    const ImVec2 pos(std::round((screen.x - size.x) * 0.5f), screen.y - Px(24) - size.y + (1.0f - ease) * Px(60)); // выезжает снизу

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Px(22));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Surface(1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Accent().WithAlpha(0.45f));
    ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // мягкое свечение любимого цвета слева
    dl->AddCircleFilled(ImVec2(pos.x + Px(32), pos.y + size.y * 0.5f), Px(14), Theme::Accent().WithAlpha(0.22f), 24);
    Widgets::DrawIcon(dl, Widgets::Icon::Sun, ImVec2(pos.x + Px(32), pos.y + size.y * 0.5f), Px(15), Theme::Accent().WithAlpha(1.0f));
    ImGui::PushClipRect(pos, ImVec2(pos.x + Px(56) + textWidth, pos.y + size.y), true);
    Widgets::Text(dl, Theme::body, ImVec2(pos.x + Px(56), pos.y + (size.y - Theme::body->LegacySize) * 0.5f), Theme::kInk, g_text.c_str());
    ImGui::PopClipRect();

    const float y = (size.y - Px(38)) * 0.5f;
    ImGui::SetCursorPos(ImVec2(size.x - buttons - Px(14), y));
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::Accent().WithAlpha(0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Accent().Mix({ 255, 255, 255 }, 0.2f).WithAlpha(1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(18, 11, 29, 255));
    if (ImGui::Button(g_yes.c_str(), ImVec2(Px(96), Px(38))) && g_shown) {
        g_shown = false;
        if (g_onYes) g_onYes();
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, Px(10));
    if (ImGui::Button(g_no.c_str(), ImVec2(Px(96), Px(38)))) g_shown = false;

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}
