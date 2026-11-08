// Красивое окно установщика ASTRIX. Сами файлы, ярлыки и удаление делает встроенный Inno Setup (ресурс ENGINE),
// который запускается без своего окна и пишет ход работы в файл.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define IMGUI_DEFINE_MATH_OPERATORS
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
    constexpr const char* kVersion = "1.4";
    constexpr float kW = 820, kH = 720;

    HWND g_hwnd = nullptr;
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    IDXGISwapChain* g_swap = nullptr;
    ID3D11RenderTargetView* g_target = nullptr;
    ID3D11ShaderResourceView* g_logo = nullptr;
    ImFont* g_body = nullptr, *g_semi = nullptr, *g_bold = nullptr;
    float g_dpi = 1, g_alpha = 1;

    enum Page { Welcome, Folder, Model, Installing, Done };
    int g_page = Welcome, g_next = Welcome, g_direction = 1;
    float g_fade = 1;

    char g_dir[1024] = {};
    bool g_startMenu = true, g_desktop = true;
    int g_model = 2;
    std::string g_error;

    // ход установки
    HANDLE g_job = nullptr, g_process = nullptr;
    std::wstring g_tempDir, g_progressFile;
    int g_percent = 0, g_item = 0;
    float g_shownPercent = 0;
    std::string g_stage = "Готовлюсь…";
    bool g_failed = false;
    std::string g_warnings;
    double g_lastRead = 0;

    struct ModelInfo { const char* name; const char* size; const char* about; double gb; };
    const ModelInfo kModels[] = {
        { "Самая лёгкая", "0,7 ГБ", "Для слабых компьютеров. Быстрая, но простоватая.", 0.7 },
        { "Лёгкая", "1,8 ГБ", "Если видеокарта на 4 ГБ или её нет.", 1.8 },
        { "Обычная", "3,2 ГБ", "Для видеокарты от 6 ГБ. Хороший баланс.", 3.2 },
        { "Умная", "6,1 ГБ", "Для видеокарты от 8 ГБ. Умнее, но медленнее.", 6.1 },
        { "Пока не скачивать", "0 ГБ", "Выберешь потом в настройках программы.", 0 },
    };

    std::wstring Wide(const std::string& s) {
        if (s.empty()) return {};
        std::wstring out(MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), static_cast<int>(out.size()));
        return out;
    }
    std::string Utf8(const std::wstring& s) {
        if (s.empty()) return {};
        std::string out(WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr), '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), static_cast<int>(out.size()), nullptr, nullptr);
        return out;
    }

    float P(float v) { return v * g_dpi; }
    ImU32 C(int r, int g, int b, float a = 1) { return IM_COL32(r, g, b, static_cast<int>(std::clamp(a * g_alpha, 0.0f, 1.0f) * 255)); }
    ImU32 Ink(float a = 1) { return C(246, 240, 255, a); }
    ImU32 Muted(float a = 1) { return C(178, 162, 212, a); }
    ImU32 Violet(float a = 1) { return C(150, 88, 255, a); }

    float Anim(const char* id, bool on, float speed = 12) {
        static std::unordered_map<ImGuiID, float> values;
        float& v = values[ImGui::GetID(id)];
        v += ((on ? 1.0f : 0.0f) - v) * (1 - std::exp(-ImGui::GetIO().DeltaTime * speed));
        return v;
    }

    ImVec2 Measure(ImFont* f, float size, const char* text, float wrap = 0) { return f->CalcTextSizeA(P(size), FLT_MAX, wrap, text); }
    float Text(ImFont* f, float size, ImVec2 pos, ImU32 col, const char* text, float wrap = 0) {
        ImGui::GetWindowDrawList()->AddText(f, P(size), pos, col, text, nullptr, wrap);
        return Measure(f, size, text, wrap).y;
    }

    // Скруглённый прямоугольник с плавным переходом цвета
    void GradientRect(ImVec2 a, ImVec2 b, float rounding, ImU32 c1, ImU32 c2, bool diagonal = true) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const int start = dl->VtxBuffer.Size;
        dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, static_cast<int>(g_alpha * 255)), rounding);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, start, dl->VtxBuffer.Size, a, diagonal ? b : ImVec2(a.x, b.y), c1, c2);
    }

    void Neon(ImVec2 a, ImVec2 b, float rounding, float strength) {
        if (strength < 0.01f) return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int ring = 3; ring >= 1; --ring) {
            const float g = P(1.6f * ring);
            dl->AddRect(a - ImVec2(g, g), b + ImVec2(g, g), Violet(0.11f * (4 - ring) * strength), rounding + g, 0, P(2));
        }
    }

    bool Button(const char* id, const char* label, ImVec2 a, ImVec2 b, bool primary, bool enabled = true) {
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton(id, b - a) && enabled;
        const float hover = Anim(id, ImGui::IsItemHovered() && enabled);
        const float r = (b.y - a.y) * 0.42f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (primary) {
            if (enabled) { GradientRect(a, b, r, IM_COL32(164, 104, 255, 255), IM_COL32(86, 34, 176, 255)); Neon(a, b, r, 0.45f + hover * 0.55f); }
            else dl->AddRectFilled(a, b, C(70, 58, 98, 0.8f), r);
        } else {
            dl->AddRectFilled(a, b, C(255, 255, 255, 0.05f + hover * 0.06f), r);
            dl->AddRect(a, b, C(190, 160, 255, 0.22f + hover * 0.35f), r, 0, P(1.2f));
        }
        const ImVec2 size = Measure(g_semi, 16, label);
        Text(g_semi, 16, ImVec2((a.x + b.x - size.x) * 0.5f, (a.y + b.y - size.y) * 0.5f), enabled ? Ink() : Muted(0.7f), label);
        return clicked;
    }

    bool Toggle(const char* id, ImVec2 pos, bool* value) {
        const ImVec2 size(P(50), P(28));
        ImGui::SetCursorScreenPos(pos);
        const bool clicked = ImGui::InvisibleButton(id, size);
        if (clicked) *value = !*value;
        const float t = Anim(id, *value, 14);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 end = pos + size;
        if (t > 0.01f) GradientRect(pos, end, size.y * 0.5f, IM_COL32(164, 104, 255, static_cast<int>(255 * t)), IM_COL32(86, 34, 176, static_cast<int>(255 * t)));
        dl->AddRectFilled(pos, end, C(255, 255, 255, 0.09f * (1 - t)), size.y * 0.5f);
        dl->AddRect(pos, end, C(190, 160, 255, 0.28f), size.y * 0.5f, 0, P(1));
        dl->AddCircleFilled(ImVec2(pos.x + size.y * 0.5f + (size.x - size.y) * t, pos.y + size.y * 0.5f), P(10), Ink(), 32);
        return clicked;
    }

    void DrawBackground(ImVec2 size) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(12, 7, 26, 255), IM_COL32(26, 13, 56, 255), IM_COL32(40, 18, 86, 255), IM_COL32(12, 7, 26, 255));
        const auto glow = [&](ImVec2 c, float radius, ImU32 col) {
            for (int i = 16; i >= 1; --i) dl->AddCircleFilled(c, radius * i / 16.0f, col, 64);
        };
        glow(ImVec2(size.x * 0.96f, 0), size.x * 0.5f, IM_COL32(112, 52, 230, 6));
        glow(ImVec2(0, size.y), size.x * 0.46f, IM_COL32(74, 28, 170, 7));

        const float step = P(30);
        for (float x = step; x < size.x; x += step) dl->AddLine(ImVec2(x, 0), ImVec2(x, size.y), IM_COL32(170, 130, 255, 10));
        for (float y = step; y < size.y; y += step) dl->AddLine(ImVec2(0, y), ImVec2(size.x, y), IM_COL32(170, 130, 255, 10));
    }

    // Каракули на карточке — как в программе, только очень тихо
    void DrawScribbles(ImVec2 a, ImVec2 b) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 col = IM_COL32(170, 120, 255, 26);
        const float line = P(1.6f);
        std::vector<ImVec2> points;
        const auto flush = [&](bool closed = false) { dl->AddPolyline(points.data(), static_cast<int>(points.size()), col, closed ? ImDrawFlags_Closed : 0, line); points.clear(); };
        ImVec2 c(b.x - P(92), b.y - P(120)); // спираль
        for (int i = 0; i < 180; ++i) { const float t = i * 0.11f; points.push_back(c + ImVec2(std::cos(t), std::sin(t)) * (t * P(3.6f))); }
        flush();
        c = ImVec2(a.x + P(46), b.y - P(150)); // сердечко
        for (int i = 0; i < 90; ++i) {
            const float t = i * 6.2832f / 90;
            points.push_back(c + ImVec2(16 * std::pow(std::sin(t), 3.0f), -(13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t))) * P(1.5f));
        }
        flush(true);
        c = ImVec2(b.x - P(210), a.y + P(120)); // звёздочка
        for (int i = 0; i < 10; ++i) { const float t = -1.5708f + i * 0.62832f, r = P(i % 2 ? 7.0f : 19.0f); points.push_back(c + ImVec2(std::cos(t), std::sin(t)) * r); }
        flush(true);
    }

    void DrawCheck(ImVec2 c, float r) {
        GradientRect(c - ImVec2(r, r), c + ImVec2(r, r), r, IM_COL32(164, 104, 255, 255), IM_COL32(86, 34, 176, 255));
        Neon(c - ImVec2(r, r), c + ImVec2(r, r), r, 0.8f);
        ImVec2 tick[3] = { c + ImVec2(-r * 0.42f, 0), c + ImVec2(-r * 0.1f, r * 0.34f), c + ImVec2(r * 0.46f, -r * 0.32f) };
        ImGui::GetWindowDrawList()->AddPolyline(tick, 3, Ink(), 0, P(4));
    }

    // ------------------------------------------------------------ установка
    std::wstring TrimDir() {
        std::wstring dir = Wide(g_dir);
        while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/' || dir.back() == L' ')) dir.pop_back();
        return dir;
    }

    bool ReadableFile(const std::wstring& path, std::string& out) {
        const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        char buffer[8192]; DWORD got = 0;
        const BOOL ok = ReadFile(f, buffer, sizeof(buffer) - 1, &got, nullptr);
        CloseHandle(f);
        if (!ok || got == 0) return false;
        out.assign(buffer, got);
        if (out.size() >= 3 && out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);
        return true;
    }

    bool ExtractEngine(const std::wstring& target) {
        const HRSRC res = FindResourceW(nullptr, L"ENGINE", MAKEINTRESOURCEW(10));
        if (!res) return false;
        const HGLOBAL data = LoadResource(nullptr, res);
        const DWORD size = SizeofResource(nullptr, res);
        const HANDLE f = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        const BOOL ok = WriteFile(f, LockResource(data), size, &written, nullptr);
        CloseHandle(f);
        return ok && written == size;
    }

    void Go(int page) { g_direction = page >= g_page ? 1 : -1; g_next = page; }

    void CleanupTemp() {
        if (g_tempDir.empty()) return;
        DeleteFileW((g_tempDir + L"\\ASTRIX-Engine.exe").c_str());
        DeleteFileW(g_progressFile.c_str());
        DeleteFileW((g_progressFile + L".failed").c_str());
        DeleteFileW((g_tempDir + L"\\engine.log").c_str());
        RemoveDirectoryW(g_tempDir.c_str());
    }

    void StartInstall() {
        g_error.clear();
        const std::wstring dir = TrimDir();
        if (dir.size() < 4 || dir[1] != L':' || dir[2] != L'\\') { g_error = "Укажи полный путь, например C:\\Games\\ASTRIX."; return; }
        const int made = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        if (made != ERROR_SUCCESS && made != ERROR_ALREADY_EXISTS && made != ERROR_FILE_EXISTS) { g_error = "Не получилось создать эту папку. Выбери другую."; return; }
        const HANDLE test = CreateFileW((dir + L"\\.astrix-test").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (test == INVALID_HANDLE_VALUE) { g_error = "В эту папку нельзя записывать. Выбери другую."; return; }
        CloseHandle(test);

        wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
        g_tempDir = std::wstring(temp) + L"astrix-setup-" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(g_tempDir.c_str(), nullptr);
        g_progressFile = g_tempDir + L"\\progress.txt";
        const std::wstring engine = g_tempDir + L"\\ASTRIX-Engine.exe";
        if (!ExtractEngine(engine)) { g_error = "Не получилось подготовить установку. Проверь, есть ли место на диске."; return; }

        std::wstring tasks;
        if (g_startMenu) tasks = L"startmenu";
        if (g_desktop) tasks += std::wstring(tasks.empty() ? L"" : L",") + L"desktopicon";
        std::wstring cmd = L"\"" + engine + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR=\"" + dir + L"\" /TASKS=\"" + tasks + L"\" /MODEL=" + std::to_wstring(g_model)
            + L" /PROGRESS=\"" + g_progressFile + L"\" /LOG=\"" + g_tempDir + L"\\engine.log\"";

        g_job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, g_tempDir.c_str(), &si, &pi)) { g_error = "Не получилось запустить установку."; return; }
        AssignProcessToJobObject(g_job, pi.hProcess);
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        g_process = pi.hProcess;
        g_percent = 0; g_item = 0; g_shownPercent = 0; g_stage = "Готовлюсь…"; g_failed = false; g_warnings.clear();
        Go(Installing);
    }

    void CancelInstall() {
        if (g_job) { TerminateJobObject(g_job, 1); CloseHandle(g_job); g_job = nullptr; }
        if (g_process) { CloseHandle(g_process); g_process = nullptr; }
    }

    void PollInstall() {
        if (!g_process) return;
        const double now = ImGui::GetTime();
        if (now - g_lastRead > 0.2) {
            g_lastRead = now;
            std::string text;
            if (ReadableFile(g_progressFile, text)) {
                std::istringstream in(text);
                std::string a, b;
                if (std::getline(in, a) && std::getline(in, b)) {
                    if (!b.empty() && b.back() == '\r') b.pop_back();
                    int item = g_item; // пункт списка узнаём по подписи, назад он не откатывается
                    if (b.find("распознавание") != std::string::npos) item = 0;
                    else if (b.find("голос") != std::string::npos) item = 1;
                    else if (b.find("пульт") != std::string::npos) item = 2;
                    else if (b.find("нейросеть") != std::string::npos) item = 3;
                    else if (b.find("Копирую") != std::string::npos) item = 4;
                    if (item >= g_item) { g_item = item; g_percent = std::clamp(std::atoi(a.c_str()), 0, 100); }
                    if (!b.empty()) g_stage = b;
                }
            }
        }
        if (WaitForSingleObject(g_process, 0) != WAIT_OBJECT_0) return;
        DWORD code = 1;
        GetExitCodeProcess(g_process, &code);
        CloseHandle(g_process); g_process = nullptr;
        if (g_job) { CloseHandle(g_job); g_job = nullptr; }
        g_failed = code != 0 || GetFileAttributesW((TrimDir() + L"\\ASTRIX.exe").c_str()) == INVALID_FILE_ATTRIBUTES;
        std::string failed;
        if (!g_failed && ReadableFile(g_progressFile + L".failed", failed)) g_warnings = failed;
        if (g_failed) g_error = "Установка не завершилась (код " + std::to_string(code) + "). Попробуй ещё раз.";
        Go(g_failed ? Folder : Done);
    }

    void BrowseFolder() {
        IFileOpenDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(L"Куда установить ASTRIX");
        if (SUCCEEDED(dialog->Show(g_hwnd))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    std::wstring chosen = path;
                    CoTaskMemFree(path);
                    while (!chosen.empty() && chosen.back() == L'\\') chosen.pop_back();
                    if (chosen.size() < 7 || _wcsicmp(chosen.c_str() + chosen.size() - 7, L"\\ASTRIX") != 0) chosen += L"\\ASTRIX";
                    strncpy_s(g_dir, Utf8(chosen).c_str(), _TRUNCATE);
                    g_error.clear();
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    double FreeGb() {
        const std::wstring dir = TrimDir();
        if (dir.size() < 3) return -1;
        ULARGE_INTEGER free{};
        if (!GetDiskFreeSpaceExW(dir.substr(0, 3).c_str(), &free, nullptr, nullptr)) return -1;
        return static_cast<double>(free.QuadPart) / (1024.0 * 1024.0 * 1024.0);
    }
    double NeedGb() { return 1.2 + kModels[g_model].gb; }

    // ------------------------------------------------------------ страницы
    struct Area { float x0, x1, y0, y1; };

    void PageWelcome(const Area& r) {
        float y = r.y0 + P(26);
        y += Text(g_bold, 25, ImVec2(r.x0, y), Ink(), "Привет! Сейчас поставим твою помощницу") + P(12);
        y += Text(g_body, 17, ImVec2(r.x0, y), Muted(), "Ничего настраивать не нужно. Нажимай «Далее», всё нужное скачается само. Понадобится интернет.", r.x1 - r.x0) + P(26);
        struct Row { const char* title; const char* text; };
        const Row rows[] = {
            { "Слышит и отвечает", "Говори с ней голосом, она поймёт и ответит вслух." },
            { "Управляет компьютером", "Откроет программу, включит музыку, найдёт в интернете." },
            { "Работает у тебя", "Нейросеть и распознавание речи стоят на твоём компьютере." },
        };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (const Row& row : rows) {
            const ImVec2 a(r.x0, y), b(r.x1, y + P(76));
            dl->AddRectFilled(a, b, C(255, 255, 255, 0.045f), P(16));
            dl->AddRect(a, b, C(190, 160, 255, 0.14f), P(16), 0, P(1));
            const ImVec2 dot(a.x + P(30), a.y + P(38));
            GradientRect(dot - ImVec2(P(7), P(7)), dot + ImVec2(P(7), P(7)), P(7), IM_COL32(164, 104, 255, 255), IM_COL32(86, 34, 176, 255));
            Text(g_semi, 17, ImVec2(a.x + P(54), a.y + P(14)), Ink(), row.title);
            Text(g_body, 15, ImVec2(a.x + P(54), a.y + P(42)), Muted(), row.text);
            y += P(86);
        }
    }

    void PageFolder(const Area& r) {
        float y = r.y0 + P(18);
        y += Text(g_semi, 18, ImVec2(r.x0, y), Ink(), "Папка установки") + P(12);
        const float browseW = P(120), h = P(50), inputW = r.x1 - r.x0 - browseW - P(12);
        ImGui::SetCursorScreenPos(ImVec2(r.x0, y));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, P(16));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(16), (h - P(16)) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, P(1));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(10, 6, 22, 190));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(14, 9, 30, 210));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(14, 9, 30, 230));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(190, 160, 255, 60));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(246, 240, 255, 255));
        ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, IM_COL32(130, 70, 240, 150));
        ImGui::SetNextItemWidth(inputW);
        ImGui::InputText("##dir", g_dir, sizeof(g_dir));
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(3);
        if (Button("browse", "Обзор…", ImVec2(r.x1 - browseW, y), ImVec2(r.x1, y + h), false)) BrowseFolder();
        y += h + P(10);
        y += Text(g_body, 14, ImVec2(r.x0, y), Muted(), "Лучше оставить как есть. Программа хранит настройки рядом с собой, поэтому «Program Files» не подойдёт.", r.x1 - r.x0) + P(14);
        const double free = FreeGb();
        if (free >= 0) {
            char text[120];
            sprintf_s(text, "Свободно на диске: %.1f ГБ", free);
            y += Text(g_body, 14, ImVec2(r.x0, y), free < NeedGb() ? C(255, 150, 170) : Muted(0.9f), text);
        }
        y += P(26);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(r.x0, y), ImVec2(r.x1, y), C(190, 160, 255, 0.14f), P(1));
        y += P(22);
        const auto row = [&](const char* id, const char* title, const char* hint, bool* value) {
            Text(g_semi, 17, ImVec2(r.x0, y), Ink(), title);
            Text(g_body, 14, ImVec2(r.x0, y + P(26)), Muted(), hint);
            Toggle(id, ImVec2(r.x1 - P(50), y + P(8)), value);
            y += P(76);
        };
        row("start", "Добавить ASTRIX в меню «Пуск»", "Будет в списке всех приложений.", &g_startMenu);
        row("desk", "Создать ярлык на рабочем столе", "Чтобы открывать одним двойным нажатием.", &g_desktop);
        if (!g_error.empty()) Text(g_semi, 15, ImVec2(r.x0, y), C(255, 150, 170), g_error.c_str(), r.x1 - r.x0);
    }

    void PageModel(const Area& r) {
        float y = r.y0 + P(14);
        y += Text(g_semi, 18, ImVec2(r.x0, y), Ink(), "Какую нейросеть поставить?") + P(6);
        y += Text(g_body, 15, ImVec2(r.x0, y), Muted(), "Это её мозги. Не знаешь — оставь как есть, поменять можно потом.") + P(14);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < 5; ++i) {
            const ImVec2 a(r.x0, y), b(r.x1, y + P(58));
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("card", b - a)) g_model = i;
            const float hover = Anim("hover", ImGui::IsItemHovered()), on = Anim("on", g_model == i, 14);
            ImGui::PopID();
            dl->AddRectFilled(a, b, C(255, 255, 255, 0.04f + hover * 0.04f), P(15));
            if (on > 0.01f) { dl->AddRectFilled(a, b, Violet(0.16f * on), P(15)); Neon(a, b, P(15), on * 0.6f); }
            dl->AddRect(a, b, C(190, 160, 255, 0.16f + on * 0.6f), P(15), 0, P(1.2f));
            const ImVec2 c(a.x + P(28), a.y + P(29));
            dl->AddCircle(c, P(10), C(200, 175, 255, 0.55f + on * 0.45f), 32, P(1.6f));
            if (on > 0.01f) dl->AddCircleFilled(c, P(6) * on, C(190, 150, 255), 32);
            const float titleW = Text(g_semi, 16, ImVec2(a.x + P(52), a.y + P(8)), Ink(), kModels[i].name), x = a.x + P(52) + Measure(g_semi, 16, kModels[i].name).x; (void)titleW;
            if (i == 2) {
                const ImVec2 size = Measure(g_semi, 12, "советую"), p0(x + P(10), a.y + P(9)), p1 = p0 + ImVec2(size.x + P(16), P(20));
                GradientRect(p0, p1, P(10), IM_COL32(164, 104, 255, 255), IM_COL32(86, 34, 176, 255));
                Text(g_semi, 12, ImVec2(p0.x + P(8), p0.y + (P(20) - size.y) * 0.5f), Ink(), "советую");
            }
            Text(g_body, 14, ImVec2(a.x + P(52), a.y + P(32)), Muted(), kModels[i].about);
            const ImVec2 sz = Measure(g_semi, 15, kModels[i].size);
            Text(g_semi, 15, ImVec2(b.x - sz.x - P(20), a.y + (P(58) - sz.y) * 0.5f), Ink(0.9f), kModels[i].size);
            y += P(66);
        }
    }

    void PageInstalling(const Area& r) {
        float y = r.y0 + P(8);
        const char* title = "Устанавливаю ASTRIX";
        y += Text(g_bold, 24, ImVec2(r.x0, y), Ink(), title) + P(4);
        y += Text(g_body, 15, ImVec2(r.x0, y), Muted(), "Можно пойти налить чай. Когда всё будет готово, здесь появится кнопка запуска.") + P(16);

        struct Item { const char* name; double weight; };
        const Item items[] = { { "Распознавание речи", 0.2 }, { "Голос", 0.3 }, { "Пульт в Telegram", 0.05 }, { "Нейросеть", kModels[g_model].gb }, { "Установка файлов", 0.2 } };
        double total = 0, passed = 0;
        for (int i = 0; i < 5; ++i) {
            if (i == 3 && g_model == 4) continue;
            total += items[i].weight;
            if (i < g_item) passed += items[i].weight;
            else if (i == g_item) passed += items[i].weight * g_percent / 100.0;
        }
        const float overall = static_cast<float>(passed / total * 100.0);
        g_shownPercent += (overall - g_shownPercent) * (1 - std::exp(-ImGui::GetIO().DeltaTime * 8));

        const ImVec2 a(r.x0, y), b(r.x1, y + P(14));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, C(10, 6, 22, 0.75f), P(7));
        dl->AddRect(a, b, C(190, 160, 255, 0.18f), P(7), 0, P(1));
        const float w = (b.x - a.x) * g_shownPercent / 100.0f;
        if (w > P(14)) { GradientRect(a, ImVec2(a.x + w, b.y), P(7), IM_COL32(86, 34, 176, 255), IM_COL32(176, 120, 255, 255)); Neon(a, ImVec2(a.x + w, b.y), P(7), 0.55f); }
        y += P(22);
        char pct[32];
        sprintf_s(pct, "Всего %d%%", static_cast<int>(g_shownPercent + 0.5f));
        y += Text(g_semi, 14, ImVec2(r.x1 - Measure(g_semi, 14, pct).x, y), Muted(), pct) + P(10);

        for (int i = 0; i < 5; ++i) {
            if (i == 3 && g_model == 4) continue;
            const bool done = i < g_item, current = i == g_item;
            const ImVec2 p0(r.x0, y), p1(r.x1, y + P(46));
            dl->AddRectFilled(p0, p1, current ? Violet(0.14f) : C(255, 255, 255, 0.04f), P(13));
            dl->AddRect(p0, p1, C(190, 160, 255, current ? 0.5f : 0.13f), P(13), 0, P(1));
            const ImVec2 c(p0.x + P(26), p0.y + P(23));
            if (done) {
                GradientRect(c - ImVec2(P(10), P(10)), c + ImVec2(P(10), P(10)), P(10), IM_COL32(164, 104, 255, 255), IM_COL32(86, 34, 176, 255));
                ImVec2 tick[3] = { c + ImVec2(P(-4.5f), 0), c + ImVec2(P(-1), P(3.5f)), c + ImVec2(P(5), P(-3.5f)) };
                dl->AddPolyline(tick, 3, Ink(), 0, P(2));
            } else if (current) {
                const float t = static_cast<float>(ImGui::GetTime()) * 5;
                dl->PathArcTo(c, P(9), t, t + 4.4f, 28);
                dl->PathStroke(C(190, 150, 255), 0, P(2.4f));
            } else dl->AddCircle(c, P(9), C(200, 175, 255, 0.35f), 28, P(1.5f));
            const float textY = p0.y + (P(46) - Measure(g_semi, 15, "А").y) * 0.5f;
            Text(g_semi, 15, ImVec2(p0.x + P(50), textY), current || done ? Ink() : Muted(), items[i].name);
            char status[24];
            if (done) strcpy_s(status, "готово");
            else if (current) sprintf_s(status, "%d%%", g_percent);
            else strcpy_s(status, "ждёт");
            Text(g_semi, 14, ImVec2(p1.x - Measure(g_semi, 14, status).x - P(18), p0.y + (P(46) - Measure(g_semi, 14, status).y) * 0.5f), current ? Ink() : Muted(0.85f), status);
            y += P(52);
        }
    }

    void PageDone(const Area& r) {
        float y = r.y0 + P(42);
        DrawCheck(ImVec2((r.x0 + r.x1) * 0.5f, y + P(42)), P(42));
        y += P(112);
        const char* title = "ASTRIX установлена!";
        y += Text(g_bold, 30, ImVec2((r.x0 + r.x1 - Measure(g_bold, 30, title).x) * 0.5f, y), Ink(), title) + P(12);
        std::string note = "При первом запуске она сама покажет, что и где находится.";
        if (g_startMenu && g_desktop) note += " Ярлыки добавлены в «Пуск» и на рабочий стол.";
        else if (g_startMenu) note += " Ярлык добавлен в «Пуск».";
        else if (g_desktop) note += " Ярлык добавлен на рабочий стол.";
        const float wrap = r.x1 - r.x0 - P(60);
        y += Text(g_body, 16, ImVec2(r.x0 + P(30), y), Muted(), note.c_str(), wrap) + P(20);
        if (!g_warnings.empty()) {
            std::string warn = "Не удалось скачать:\n" + g_warnings + "\nЭти функции заработают, если запустить установщик ещё раз с интернетом.";
            Text(g_body, 14, ImVec2(r.x0 + P(30), y), C(214, 186, 255), warn.c_str(), wrap);
        }
    }

    void LaunchApp() {
        const std::wstring dir = TrimDir(), exe = dir + L"\\ASTRIX.exe";
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
    }

    void Frame() {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 screen = io.DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(screen);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##setup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        g_alpha = 1;
        PollInstall();
        DrawBackground(screen);

        const ImVec2 card0(P(16), P(16)), card1(screen.x - P(16), screen.y - P(16));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        GradientRect(card0, card1, P(26), IM_COL32(32, 20, 62, 236), IM_COL32(18, 11, 36, 236));
        DrawScribbles(card0, card1);
        dl->AddRect(card0, card1, C(190, 160, 255, 0.22f), P(26), 0, P(1.2f));

        // шапка
        const float headY = card0.y + P(26), left = card0.x + P(34), right = card1.x - P(34);
        const float logoSize = P(66);
        dl->AddImage(reinterpret_cast<ImTextureID>(g_logo), ImVec2(left, headY), ImVec2(left + logoSize, headY + logoSize));
        Text(g_bold, 30, ImVec2(left + logoSize + P(16), headY + P(2)), Ink(), "Установка ASTRIX");
        const std::string version = std::string("Версия ") + kVersion;
        Text(g_body, 15, ImVec2(left + logoSize + P(17), headY + P(44)), Muted(), version.c_str());

        // свернуть и закрыть
        const float bx = screen.x - P(30), by = P(28), bs = P(32);
        ImGui::SetCursorScreenPos(ImVec2(bx - bs, by));
        if (ImGui::InvisibleButton("close", ImVec2(bs, bs))) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
        { const float h = Anim("close", ImGui::IsItemHovered()); const ImVec2 c(bx - bs * 0.5f, by + bs * 0.5f); const float s = P(6);
          dl->AddCircleFilled(c, bs * 0.5f, C(255, 255, 255, 0.10f * h), 32);
          dl->AddLine(c - ImVec2(s, s), c + ImVec2(s, s), Ink(0.8f), P(1.6f)); dl->AddLine(c + ImVec2(s, -s), c + ImVec2(-s, s), Ink(0.8f), P(1.6f)); }
        ImGui::SetCursorScreenPos(ImVec2(bx - bs * 2 - P(4), by));
        if (ImGui::InvisibleButton("min", ImVec2(bs, bs))) ShowWindow(g_hwnd, SW_MINIMIZE);
        { const float h = Anim("min", ImGui::IsItemHovered()); const ImVec2 c(bx - bs * 1.5f - P(4), by + bs * 0.5f);
          dl->AddCircleFilled(c, bs * 0.5f, C(255, 255, 255, 0.10f * h), 32);
          dl->AddLine(c - ImVec2(P(6), 0), c + ImVec2(P(6), 0), Ink(0.8f), P(1.6f)); }

        // плавная смена страниц
        const float dt = io.DeltaTime;
        if (g_next != g_page) { g_fade -= dt * 9; if (g_fade <= 0) { g_fade = 0; g_page = g_next; } }
        else g_fade = std::min(1.0f, g_fade + dt * 7);
        const float eased = g_fade * g_fade * (3 - 2 * g_fade), shift = (1 - eased) * P(26) * g_direction * (g_next != g_page ? -1.0f : 1.0f);
        const bool canClick = g_next == g_page;
        Area area{ left + shift, right + shift, headY + logoSize + P(10), card1.y - P(96) };

        g_alpha = eased;
        ImGui::GetStyle().Alpha = std::max(eased, 0.01f);
        ImGui::BeginDisabled(!canClick);
        switch (g_page) {
        case Welcome: PageWelcome(area); break;
        case Folder: PageFolder(area); break;
        case Model: PageModel(area); break;
        case Installing: PageInstalling(area); break;
        case Done: PageDone(area); break;
        }

        // нижние кнопки
        const float bottom0 = card1.y - P(78), bottom1 = card1.y - P(26), bw = P(170), a0 = area.x1 - bw;
        const ImVec2 nextA(a0, bottom0), nextB(area.x1, bottom1), backA(area.x0, bottom0), backB(area.x0 + P(130), bottom1);
        dl = ImGui::GetWindowDrawList();
        switch (g_page) {
        case Welcome: {
            const std::string where = "Установка в " + Utf8(TrimDir());
            Text(g_body, 13, ImVec2(area.x0, (bottom0 + bottom1 - P(18)) * 0.5f), Muted(0.8f), where.c_str(), a0 - area.x0 - P(20));
            if (Button("next", "Далее", nextA, nextB, true) && canClick) Go(Folder);
            break; }
        case Folder:
            if (Button("back", "Назад", backA, backB, false) && canClick) Go(Welcome);
            if (Button("next", "Далее", nextA, nextB, true, Utf8(TrimDir()).size() > 3) && canClick) { g_error.clear(); Go(Model); }
            break;
        case Model: {
            if (Button("back", "Назад", backA, backB, false) && canClick) Go(Folder);
            const double free = FreeGb();
            const bool enough = free < 0 || free >= NeedGb();
            if (!enough) { char t[120]; sprintf_s(t, "Нужно около %.1f ГБ, свободно %.1f ГБ", NeedGb(), free); Text(g_semi, 14, ImVec2(backB.x + P(16), (bottom0 + bottom1 - P(18)) * 0.5f), C(255, 150, 170), t); }
            if (Button("next", "Установить", nextA, nextB, true, enough) && canClick) { StartInstall(); if (!g_error.empty()) Go(Folder); }
            break; }
        case Installing:
            if (Button("cancel", "Отменить", ImVec2((area.x0 + area.x1 - bw) * 0.5f, bottom0), ImVec2((area.x0 + area.x1 + bw) * 0.5f, bottom1), false) && canClick)
                PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
            break;
        case Done:
            if (Button("closeAll", "Закрыть", backA, ImVec2(backA.x + P(150), bottom1), false) && canClick) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
            if (Button("run", "Запустить", nextA, nextB, true) && canClick) { LaunchApp(); PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }
            break;
        }
        ImGui::EndDisabled();
        ImGui::GetStyle().Alpha = 1;
        g_alpha = 1;
        ImGui::End();
    }

    // ------------------------------------------------------------ Windows и видеокарта
    void LoadLogo() {
        const HRSRC res = FindResourceW(nullptr, L"LOGO", MAKEINTRESOURCEW(10));
        if (!res) return;
        const auto* bytes = static_cast<BYTE*>(LockResource(LoadResource(nullptr, res)));
        IWICImagingFactory* factory = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return;
        IWICStream* stream = nullptr; IWICBitmapDecoder* decoder = nullptr; IWICBitmapFrameDecode* frame = nullptr; IWICFormatConverter* conv = nullptr;
        factory->CreateStream(&stream);
        stream->InitializeFromMemory(const_cast<BYTE*>(bytes), SizeofResource(nullptr, res));
        if (SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) && SUCCEEDED(decoder->GetFrame(0, &frame))) {
            factory->CreateFormatConverter(&conv);
            conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
            UINT w = 0, h = 0; conv->GetSize(&w, &h);
            std::vector<BYTE> pixels(static_cast<size_t>(w) * h * 4);
            conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data());
            D3D11_TEXTURE2D_DESC desc{}; desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init{ pixels.data(), w * 4, 0 };
            ID3D11Texture2D* texture = nullptr;
            if (SUCCEEDED(g_device->CreateTexture2D(&desc, &init, &texture))) { g_device->CreateShaderResourceView(texture, nullptr, &g_logo); texture->Release(); }
            conv->Release();
        }
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        stream->Release();
        factory->Release();
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return TRUE;
        switch (msg) {
        case WM_NCHITTEST: {
            POINT p{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            ScreenToClient(hwnd, &p);
            RECT rc; GetClientRect(hwnd, &rc);
            return p.y < static_cast<LONG>(P(100)) && p.x < rc.right - static_cast<LONG>(P(110)) ? HTCAPTION : HTCLIENT;
        }
        case WM_CLOSE:
            if (g_process) {
                if (MessageBoxW(hwnd, L"Установка ещё идёт. Прервать её?", L"ASTRIX", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;
                CancelInstall();
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        strncpy_s(g_dir, Utf8(std::wstring(local) + L"\\ASTRIX").c_str(), _TRUNCATE);
        CoTaskMemFree(local);
    }

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ASTRIX.Setup";
    RegisterClassExW(&wc);

    POINT cursor{}; GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    UINT dpiX = 96, dpiY = 96;
    using GetDpiFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    if (const auto fn = reinterpret_cast<GetDpiFn>(GetProcAddress(LoadLibraryW(L"Shcore.dll"), "GetDpiForMonitor"))) fn(monitor, 0, &dpiX, &dpiY);
    g_dpi = dpiX / 96.0f;
    MONITORINFO info{ sizeof(info) }; GetMonitorInfoW(monitor, &info);
    const int w = static_cast<int>(kW * g_dpi), h = static_cast<int>(kH * g_dpi);
    const int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - w) / 2, y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - h) / 2;
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Установка ASTRIX", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU, x, y, w, h, nullptr, nullptr, instance, nullptr);
    BOOL dark = TRUE; int round = 2;
    DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(g_hwnd, 33, &round, sizeof(round));

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_swap, &g_device, nullptr, &g_context)) &&
        FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_swap, &g_device, nullptr, &g_context))) {
        MessageBoxW(nullptr, L"Не получилось запустить окно установки: видеокарта недоступна.", L"ASTRIX", MB_ICONERROR);
        return 1;
    }
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_target);
    back->Release();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    const auto font = [&](const char* name, const char* fallback) {
        const std::string path = std::string("C:\\Windows\\Fonts\\") + name;
        ImFont* f = GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES ? io.Fonts->AddFontFromFileTTF(path.c_str(), 16 * g_dpi) : nullptr;
        return f ? f : io.Fonts->AddFontFromFileTTF((std::string("C:\\Windows\\Fonts\\") + fallback).c_str(), 16 * g_dpi);
    };
    g_body = font("segoeui.ttf", "arial.ttf");
    g_semi = font("seguisb.ttf", "arial.ttf");
    g_bold = font("segoeuib.ttf", "arialbd.ttf");
    io.FontDefault = g_body;
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    LoadLogo();

    // Служебные ключи для проверки: --page N показывает страницу, --autorun ставит без нейросети
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool autorun = false;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--page") && i + 1 < argc) {
            g_page = g_next = std::clamp(_wtoi(argv[++i]), 0, 4);
            if (g_page == Installing) { g_percent = 66; g_item = 1; g_shownPercent = 8; g_stage = "Скачиваю: голос"; }
        }
        if (!wcscmp(argv[i], L"--autorun")) autorun = true;
    }
    LocalFree(argv);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    if (autorun) { g_model = 4; StartInstall(); }

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Frame();
        ImGui::Render();
        const float clear[4] = { 0.05f, 0.03f, 0.1f, 1 };
        g_context->OMSetRenderTargets(1, &g_target, nullptr);
        g_context->ClearRenderTargetView(g_target, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    CancelInstall();
    CleanupTemp();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
