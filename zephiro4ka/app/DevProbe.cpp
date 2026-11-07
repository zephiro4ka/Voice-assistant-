#include "DevProbe.h"

#include "Brain.h"
#include "Graphics.h"
#include "Log.h"
#include "Settings.h"
#include "ProcessesPage.h"
#include "ScenariosPage.h"
#include "SettingsPage.h"
#include "TextUtils.h"
#include "Ui.h"

#include "../ImGui/imgui.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {
    ImVec2 g_mouse(0, 0);
    bool g_holdMouse = false; // сценарий двигал мышь — держим её там, а не где настоящий курсор
    bool g_enabled = false;
    bool g_perf = false;
    std::deque<std::string> g_queue;
    int g_wait = 0;
    int g_frame = 0;
    std::string g_shot; // снимок, который надо сделать после этого кадра

    // статистика кадров за окно в 5 секунд
    double g_workSum = 0, g_workMax = 0, g_intervalMax = 0, g_elapsed = 0;
    int g_frames = 0;

    std::filesystem::path Dir() { return std::filesystem::path(Text::ExecutableDirectory()); }

    void ReadScript() {
        const auto file = Dir() / "devprobe.txt";
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) return;
        std::ifstream in(file);
        for (std::string line; std::getline(in, line);)
            if (!Text::Trim(line).empty()) g_queue.push_back(Text::Trim(line));
        in.close();
        std::filesystem::remove(file, ec);
        Log::Write("devprobe: команд " + std::to_string(g_queue.size()));
    }

    void Run(const std::string& line) {
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        ImGuiIO& io = ImGui::GetIO();
        if (cmd == "wait") { in >> g_wait; return; }
        if (cmd == "settings") {
            std::string arg;
            in >> arg;
            if (arg == "close") SettingsPage::Close();
            else SettingsPage::Open(std::atoi(arg.c_str()));
            return;
        }
        if (cmd == "scenarios") { std::string arg; in >> arg; arg == "close" ? ScenariosPage::Close() : ScenariosPage::Open(); return; }
        if (cmd == "processes") { std::string arg; in >> arg; arg == "close" ? ProcessesPage::Close() : ProcessesPage::Open(); return; }
        if (cmd == "history") { std::string arg; in >> arg; Ui::SetHistory(arg == "on"); return; }
        if (cmd == "mouse") { float x = 0, y = 0; in >> x >> y; io.AddMousePosEvent(x, y); g_mouse = ImVec2(x, y); g_holdMouse = true; return; }
        if (cmd == "down") { io.AddMouseButtonEvent(0, true); return; }   // зажать левую кнопку
        if (cmd == "up") { io.AddMouseButtonEvent(0, false); return; }    // отпустить
        if (cmd == "rclick") { io.AddMouseButtonEvent(1, true); io.AddMouseButtonEvent(1, false); return; } // правый клик
        if (cmd == "scroll") { float n = 0; in >> n; io.AddMouseWheelEvent(0, -n); return; }
        if (cmd == "ini") {
            std::string key, value;
            in >> key >> value;
            Config::SetValue("anya", key.c_str(), value);
            Config::Load();
            return;
        }
        if (cmd == "shot") { in >> g_shot; return; }
        if (cmd == "perf") { g_perf = true; return; }
        if (cmd == "ask") { std::string rest; std::getline(in, rest); Brain::Submit(Text::Trim(rest)); return; } // как будто сказали Ане
    }
}

void DevProbe::Start() {
    g_enabled = Config::Value("debug", "probe", "0") == "1";
    g_perf = Config::Value("debug", "perf", "0") == "1";
}

void DevProbe::HoldMouse() {
    if (g_enabled && g_holdMouse) ImGui::GetIO().AddMousePosEvent(g_mouse.x, g_mouse.y);
}

void DevProbe::BeforeFrame() {
    if (!g_enabled) return;
    if (++g_frame % 30 == 0) ReadScript();
    if (g_wait > 0) { --g_wait; return; }
    while (!g_queue.empty() && g_wait == 0 && g_shot.empty()) {
        const std::string line = g_queue.front();
        g_queue.pop_front();
        Run(line);
    }
}

void DevProbe::AfterRender() {
    if (g_shot.empty()) return;
    const auto path = Dir() / (g_shot + ".bmp");
    Log::Write(std::string("devprobe: снимок ") + (Graphics::SaveScreenshot(path.wstring()) ? "" : "НЕ ") + "сохранён — " + g_shot);
    g_shot.clear();
}

void DevProbe::FrameTime(double workMs, double intervalMs) {
    if (!g_perf) return;
    g_workSum += workMs;
    g_workMax = std::max(g_workMax, workMs);
    g_intervalMax = std::max(g_intervalMax, intervalMs);
    g_elapsed += intervalMs;
    ++g_frames;
    if (g_elapsed < 5000.0) return;
    char text[160];
    snprintf(text, sizeof(text), "кадры: %.0f/с, работа на кадр: средняя %.2f мс, худшая %.2f мс; самый долгий кадр %.1f мс",
        g_frames * 1000.0 / g_elapsed, g_workSum / g_frames, g_workMax, g_intervalMax);
    Log::Write(text);
    g_workSum = g_workMax = g_intervalMax = g_elapsed = 0;
    g_frames = 0;
}
