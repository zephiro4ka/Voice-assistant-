#include "Overlay.h"

#include "Platform.h"
#include "Log.h"
#include "MediaNow.h"
#include "Settings.h"
#include "State.h"
#include "SystemMonitor.h"
#include "TextUtils.h"
#include "Theme.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <array>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace {
    std::atomic<bool> g_dirty{ true };
    std::mutex g_dumpMutex;
    std::string g_dumpFolder;

    struct Options { bool res = false, pinned = false, island = false; int opacity = 75, place = 0, x = -1, y = -1, w = 0, h = 0; } g_opt;

    float S(float v) { return v * Theme::dpi; }

    Color Argb(int a, const Theme::Color& c) { return Color(static_cast<BYTE>(std::clamp(a, 0, 255)), static_cast<BYTE>(c.r), static_cast<BYTE>(c.g), static_cast<BYTE>(c.b)); }

    void RoundedPath(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL r) {
        const REAL d = r * 2;
        path.AddArc(x, y, d, d, 180, 90);
        path.AddArc(x + w - d, y, d, d, 270, 90);
        path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
        path.AddArc(x, y + h - d, d, d, 90, 90);
        path.CloseFigure();
    }

    struct Surface {
        HWND hwnd = nullptr;
        int width = 0, height = 0;
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        void* bits = nullptr;

        void Resize(int w, int h) {
            if (w == width && h == height && bits) return;
            if (bitmap) DeleteObject(bitmap);
            if (!dc) dc = CreateCompatibleDC(nullptr);
            BITMAPINFO info{};
            info.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
            bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            SelectObject(dc, bitmap);
            width = w;
            height = h;
        }
        void Present(int x, int y, int alpha) const {
            POINT source{ 0, 0 }, target{ x, y };
            SIZE size{ width, height };
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, static_cast<BYTE>(std::clamp(alpha, 0, 255)), AC_SRC_ALPHA };
            UpdateLayeredWindow(hwnd, nullptr, &target, &size, dc, &source, 0, &blend, ULW_ALPHA);
        }
    };

    Surface g_res, g_island;
    float g_islandT = 0, g_islandE = 0, g_resAlpha = 1;
    struct Resize { int edges = 0; POINT start{}; RECT rect{}; } g_resize; // 1 слева, 2 справа, 4 сверху, 8 снизу
    ULONGLONG g_resDrawn = 0, g_islandLeft = 0;
    bool g_resShown = false, g_islandShown = false;

    // ---------------------------------------------------------------- окно нагрузки
    constexpr float kResW = 262, kHeader = 36, kRow = 62, kMinW = 190, kMinRow = 40;

    void Text2(Graphics& g, const WCHAR* text, REAL x, REAL y, REAL size, const Color& color, bool bold, REAL maxWidth = 0) {
        FontFamily family(L"Segoe UI");
        Font font(&family, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
        SolidBrush brush(color);
        StringFormat format;
        format.SetTrimming(StringTrimmingEllipsisCharacter);
        format.SetFormatFlags(StringFormatFlagsNoWrap);
        g.DrawString(text, -1, &font, RectF(x, y, maxWidth > 0 ? maxWidth : 2000, size * 1.5f), &format, &brush);
    }

    void Graph(Graphics& g, REAL x, REAL y, REAL w, REAL h, const std::array<float, SystemMonitor::kHistory>& data, const Theme::Color& color) {
        SolidBrush back(Color(50, 255, 255, 255));
        g.FillRectangle(&back, x, y, w, h);
        PointF points[SystemMonitor::kHistory + 2];
        for (int i = 0; i < SystemMonitor::kHistory; ++i)
            points[i] = PointF(x + w * i / (SystemMonitor::kHistory - 1), y + h - h * std::clamp(data[i], 0.0f, 100.0f) / 100.0f);
        Pen pen(Argb(255, color), S(1.4f));
        g.DrawLines(&pen, points, SystemMonitor::kHistory);
        points[SystemMonitor::kHistory] = PointF(x + w, y + h);
        points[SystemMonitor::kHistory + 1] = PointF(x, y + h);
        SolidBrush fill(Argb(70, color));
        g.FillPolygon(&fill, points, SystemMonitor::kHistory + 2);
    }

    int ResHeight(const SystemMonitor::Stats& s) {
        const int rows = 4 + (s.hasGpu ? 1 : 0) + (s.hasIgpu ? 1 : 0);
        return static_cast<int>(S(kHeader + kRow * rows + 8));
    }

    void DrawRes(Graphics& g, int W, int H, const SystemMonitor::Stats& s) {
        g.Clear(Color(0, 0, 0, 0));
        GraphicsPath panel;
        RoundedPath(panel, 0.5f, 0.5f, W - 1.0f, H - 1.0f, S(16));
        SolidBrush bg(Color(200, 14, 11, 24));
        g.FillPath(&bg, &panel);
        Pen edge(Color(46, 255, 255, 255), 1.0f);
        g.DrawPath(&edge, &panel);

        const Color ink(255, 244, 239, 250), soft(190, 244, 239, 250);
        Text2(g, L"Производительность", S(12), S(9), S(14), ink, true);
        // кнопки: закрепить и закрыть
        const REAL by = S(18);
        Pen button(soft, S(1.8f));
        const REAL pinX = W - S(50), closeX = W - S(22);
        if (g_opt.pinned) { SolidBrush on(Argb(255, Theme::Accent())); g.FillEllipse(&on, pinX - S(6), by - S(6), S(12), S(12)); }
        else g.DrawEllipse(&button, pinX - S(6), by - S(6), S(12), S(12));
        g.DrawLine(&button, closeX - S(5), by - S(5), closeX + S(5), by + S(5));
        g.DrawLine(&button, closeX + S(5), by - S(5), closeX - S(5), by + S(5));

        REAL y = S(kHeader);
        wchar_t line[96];
        const int rows = 4 + (s.hasGpu ? 1 : 0) + (s.hasIgpu ? 1 : 0);
        const REAL rowH = (H - S(kHeader) - S(8)) / rows, k = std::clamp(rowH / S(kRow), 0.7f, 1.7f), graphW = W * 0.27f;
        auto row = [&](const WCHAR* name, const WCHAR* value, const std::array<float, SystemMonitor::kHistory>& data, const Theme::Color& color) {
            Graph(g, S(12), y + S(5), graphW, rowH - S(10), data, color);
            const REAL textX = S(12) + graphW + S(10);
            Text2(g, name, textX, y + rowH * 0.1f, S(14) * k, ink, true, W - textX - S(8));
            Text2(g, value, textX, y + rowH * 0.47f, S(13) * k, soft, false, W - textX - S(8));
            y += rowH;
        };
        const Theme::Color cyan{ 90, 200, 220 }, blue{ 110, 160, 245 }, green{ 130, 210, 90 }, pink{ 235, 110, 150 }, violet{ 190, 110, 235 };
        swprintf_s(line, L"%d%%  %.2f ГГц%s", static_cast<int>(s.cpu), s.cpuFreqGhz, s.cpuTemp > 0 ? (L"  " + std::to_wstring(static_cast<int>(s.cpuTemp)) + L"°").c_str() : L"");
        row(L"Процессор", line, s.cpuHistory, cyan);
        swprintf_s(line, L"%.1f / %.1f ГБ  (%d%%)", s.ramUsedGb, s.ramTotalGb, static_cast<int>(s.ram));
        row(L"Память", line, s.ramHistory, blue);
        swprintf_s(line, L"%d%%  чтение %.1f МБ/с", static_cast<int>(s.diskActive), s.diskReadMBs);
        row(L"Диск", line, s.diskHistory, green);
        swprintf_s(line, L"↓ %.0f  ↑ %.0f КБ/с", s.netDownKbps, s.netUpKbps);
        row(L"Сеть", line, s.netHistory, pink);
        if (s.hasGpu) {
            swprintf_s(line, L"%d%%  %.1f / %.0f ГБ%s", static_cast<int>(s.gpu), s.vramUsedGb, s.vramTotalGb, s.gpuTemp > 0 ? (L"  " + std::to_wstring(static_cast<int>(s.gpuTemp)) + L"°").c_str() : L"");
            row(L"Видеокарта", line, s.gpuHistory, violet);
        }
        if (s.hasIgpu) {
            swprintf_s(line, L"%d%%", static_cast<int>(s.igpu));
            row(L"Встроенная графика", line, s.igpuHistory, violet);
        }
    }

    // ---------------------------------------------------------------- островок
    constexpr float kIslandW = 320, kIslandH = 138, kPillW = 230, kPillH = 44;

    void Icon(Graphics& g, int kind, REAL cx, REAL cy, REAL r, const Color& color) { // 0 назад, 1 пауза, 2 играть, 3 вперёд
        SolidBrush brush(color);
        if (kind == 1) { g.FillRectangle(&brush, cx - r * 0.7f, cy - r, r * 0.5f, r * 2); g.FillRectangle(&brush, cx + r * 0.2f, cy - r, r * 0.5f, r * 2); return; }
        if (kind == 2) { PointF p[3] = { {cx - r * 0.7f, cy - r}, {cx + r, cy}, {cx - r * 0.7f, cy + r} }; g.FillPolygon(&brush, p, 3); return; }
        const REAL side = kind == 3 ? 1.0f : -1.0f;
        PointF p[3] = { {cx - side * r * 0.8f, cy - r}, {cx + side * r * 0.5f, cy}, {cx - side * r * 0.8f, cy + r} };
        g.FillPolygon(&brush, p, 3);
        g.FillRectangle(&brush, cx + (side > 0 ? r * 0.5f : -r * 0.75f), cy - r, r * 0.25f, r * 2);
    }

    PointF IslandButton(int index, int W, int H, float e) { // 0 назад, 1 пауза, 2 вперёд
        if (e > 0.5f) return PointF(W * 0.5f + (index - 1) * S(54), H - S(34));
        return PointF(W - S(82) + index * S(30), H * 0.5f);
    }

    Color Fade(const Color& c, float a) { return Color(static_cast<BYTE>(c.GetA() * std::clamp(a, 0.0f, 1.0f)), c.GetR(), c.GetG(), c.GetB()); }

    // Волна пройденной части: бежит, пока играет звук, на паузе замирает
    void Wave(Graphics& g, REAL x, REAL y, REAL w, REAL amplitude, const Color& color, REAL thick, bool moving) {
        if (w < 2) return;
        const float phase = moving ? static_cast<float>(GetTickCount64() % 100000) / 1000.0f * 5.0f : 0.0f;
        const int steps = std::clamp(static_cast<int>(w), 2, 500);
        std::vector<PointF> points(steps + 1);
        for (int i = 0; i <= steps; ++i) {
            const REAL px = x + w * i / steps, edge = std::min(px - x, x + w - px) / S(14); // к концам волна плавно сходит на нет
            const REAL fade = std::clamp(edge, 0.0f, 1.0f);
            points[i] = PointF(px, y + std::sin((px - x) * 0.06f / Theme::dpi - phase) * amplitude * fade * fade * (3 - 2 * fade));
        }
        Pen pen(color, thick);
        pen.SetLineJoin(LineJoinRound);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        g.DrawLines(&pen, points.data(), steps + 1);
    }

    std::wstring Clock2(double seconds) {
        wchar_t t[16];
        swprintf_s(t, L"%d:%02d", static_cast<int>(seconds) / 60, static_cast<int>(seconds) % 60);
        return t;
    }

    void DrawIsland(Graphics& g, int W, int H, const MediaNow::Track& t, float e) {
        g.Clear(Color(0, 0, 0, 0));
        GraphicsPath panel;
        RoundedPath(panel, 0.5f, 0.5f, W - 1.0f, H - 1.0f, S(24 + 4 * e));
        // Матовое стекло: тёмная основа, цветной отблеск из угла и светлая кромка сверху
        const auto light = Theme::Accent().Mix({ 255, 255, 255 }, 0.35f);
        LinearGradientBrush bg(PointF(0, 0), PointF(0, static_cast<REAL>(H)), Color(150, 30, 24, 48), Color(120, 12, 10, 24));
        g.FillPath(&bg, &panel);
        LinearGradientBrush tint(PointF(0, 0), PointF(static_cast<REAL>(W), static_cast<REAL>(H)), Argb(64, Theme::Accent()), Argb(0, Theme::Accent()));
        g.FillPath(&tint, &panel);
        LinearGradientBrush rim(PointF(0, 0), PointF(0, static_cast<REAL>(H)), Color(110, 255, 255, 255), Argb(70, light));
        Pen edge(&rim, S(1));
        g.DrawPath(&edge, &panel);
        const Color ink(255, 244, 239, 250), soft(185, 244, 239, 250);
        const std::wstring title = Text::ToWide(t.title.empty() ? "Сейчас играет" : t.title), sub = Text::ToWide(t.artist.empty() ? t.app : t.artist);
        const float pillShow = 1.0f - std::clamp(e * 2.2f, 0.0f, 1.0f), big = std::clamp(e * 2.2f - 1.2f, 0.0f, 1.0f);
        if (pillShow > 0.01f) Text2(g, title.c_str(), S(16), H * 0.5f - S(10), S(13), Fade(ink, pillShow), true, W - S(122));
        if (big > 0.01f) {
            Text2(g, title.c_str(), S(18), S(12), S(16), Fade(ink, big), true, W - S(36));
            Text2(g, sub.c_str(), S(18), S(36), S(13), Fade(soft, big), false, W - S(36));
            if (t.duration > 1) {
                const REAL k = static_cast<REAL>(std::clamp(t.position / t.duration, 0.0, 1.0)), barX = S(56), barW = W - S(112), barY = S(70);
                Wave(g, barX, barY, barW * k, S(3.5f), Fade(Argb(255, light), big), S(2.2f), t.playing);
                Text2(g, Clock2(t.position).c_str(), S(16), barY - S(8), S(11), Fade(soft, big), false);
                Text2(g, Clock2(t.duration).c_str(), W - S(48), barY - S(8), S(11), Fade(soft, big), false);
            }
        }
        const float show = e > 0.5f ? big : pillShow;
        const REAL iconR = e > 0.5f ? S(9) : S(6), circleR = e > 0.5f ? S(20) : S(13);
        const PointF prev = IslandButton(0, W, H, e), play = IslandButton(1, W, H, e), next = IslandButton(2, W, H, e);
        Icon(g, 0, prev.X, prev.Y, iconR, Fade(ink, show));
        SolidBrush circle(Fade(Argb(210, light), show));
        g.FillEllipse(&circle, play.X - circleR, play.Y - circleR, circleR * 2, circleR * 2);
        Icon(g, t.playing ? 1 : 2, play.X, play.Y, iconR, Fade(Color(255, 255, 255, 255), show));
        Icon(g, 3, next.X, next.Y, iconR, Fade(ink, show));
    }

    void Render(Surface& surface, const std::function<void(Graphics&)>& paint) {
        Bitmap bitmap(surface.width, surface.height, surface.width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(surface.bits));
        Graphics g(&bitmap);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        paint(g);
    }

    void ReadOptions() {
        g_opt.res = Config::Value("overlay", "res", "0") == "1";
        g_opt.pinned = Config::Value("overlay", "pinned", "0") == "1";
        g_opt.island = Config::Value("overlay", "island", "1") == "1";
        g_opt.opacity = std::clamp(std::atoi(Config::Value("overlay", "opacity", "85").c_str()), 20, 100);
        g_opt.place = std::clamp(std::atoi(Config::Value("overlay", "place", "0").c_str()), 0, 3);
        g_opt.x = std::atoi(Config::Value("overlay", "x", "-1").c_str());
        g_opt.y = std::atoi(Config::Value("overlay", "y", "-1").c_str());
        g_opt.w = std::atoi(Config::Value("overlay", "w", "0").c_str());
        g_opt.h = std::atoi(Config::Value("overlay", "h", "0").c_str());
        LONG_PTR ex = GetWindowLongPtrW(g_res.hwnd, GWL_EXSTYLE);
        ex = g_opt.pinned ? ex | WS_EX_TRANSPARENT : ex & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT); // закреплённое окно не ловит мышь
        SetWindowLongPtrW(g_res.hwnd, GWL_EXSTYLE, ex);
        g_resDrawn = 0;
    }

    RECT ResRect() {
        RECT r{};
        GetWindowRect(g_res.hwnd, &r);
        return r;
    }

    int EdgesAt(HWND hwnd, LPARAM lParam) { // у какого края мышь (только у незакреплённого окна)
        RECT r{};
        GetClientRect(hwnd, &r);
        const int x = static_cast<short>(LOWORD(lParam)), y = static_cast<short>(HIWORD(lParam)), m = static_cast<int>(S(7));
        return (x < m ? 1 : 0) | (x >= r.right - m ? 2 : 0) | (y < m ? 4 : 0) | (y >= r.bottom - m ? 8 : 0);
    }

    void SaveResRect() {
        const RECT r = ResRect();
        g_opt.x = r.left; g_opt.y = r.top; g_opt.w = r.right - r.left; g_opt.h = r.bottom - r.top;
        Config::SetValue("overlay", "x", std::to_string(g_opt.x));
        Config::SetValue("overlay", "y", std::to_string(g_opt.y));
        Config::SetValue("overlay", "w", std::to_string(g_opt.w));
        Config::SetValue("overlay", "h", std::to_string(g_opt.h));
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
            if (hwnd == g_res.hwnd) {
                if (const int edges = EdgesAt(hwnd, lParam)) { // тянем за край — меняем размер
                    g_resize.edges = edges;
                    GetCursorPos(&g_resize.start);
                    g_resize.rect = ResRect();
                    SetCapture(hwnd);
                    return 0;
                }
                const float x = static_cast<float>(static_cast<short>(LOWORD(lParam))), y = static_cast<float>(static_cast<short>(HIWORD(lParam)));
                if (y < S(kHeader) && x > g_res.width - S(36)) { Config::SetValue("overlay", "res", "0"); g_dirty = true; return 0; }
                if (y < S(kHeader) && x > g_res.width - S(66)) { Config::SetValue("overlay", "pinned", "1"); g_dirty = true; return 0; }
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                return 0;
            }
            break;
        case WM_SETCURSOR:
            if (hwnd == g_res.hwnd) {
                POINT at{};
                GetCursorPos(&at);
                ScreenToClient(hwnd, &at);
                const int e = g_resize.edges ? g_resize.edges : EdgesAt(hwnd, MAKELPARAM(at.x, at.y));
                if (e) {
                    const bool nwse = e == 5 || e == 10, nesw = e == 6 || e == 9;
                    SetCursor(LoadCursorW(nullptr, nwse ? IDC_SIZENWSE : nesw ? IDC_SIZENESW : (e & 3) ? IDC_SIZEWE : IDC_SIZENS));
                    return TRUE;
                }
            }
            break;
        case WM_MOUSEMOVE:
            if (hwnd == g_res.hwnd && g_resize.edges) {
                POINT now{};
                GetCursorPos(&now);
                const int dx = now.x - g_resize.start.x, dy = now.y - g_resize.start.y;
                RECT r = g_resize.rect;
                const auto stats = SystemMonitor::Snapshot();
                const int minW = static_cast<int>(S(kMinW)), minH = static_cast<int>(S(kHeader + kMinRow * (4 + (stats.hasGpu ? 1 : 0) + (stats.hasIgpu ? 1 : 0)) + 8));
                if (g_resize.edges & 1) r.left = std::min(r.left + dx, r.right - minW);
                if (g_resize.edges & 2) r.right = std::max(r.right + dx, r.left + minW);
                if (g_resize.edges & 4) r.top = std::min(r.top + dy, r.bottom - minH);
                if (g_resize.edges & 8) r.bottom = std::max(r.bottom + dy, r.top + minH);
                g_opt.x = r.left; g_opt.y = r.top; g_opt.w = r.right - r.left; g_opt.h = r.bottom - r.top;
                g_resDrawn = 0;
                return 0;
            }
            break;
        case WM_EXITSIZEMOVE:
            if (hwnd == g_res.hwnd) SaveResRect();
            return 0;
        case WM_LBUTTONUP:
            if (hwnd == g_res.hwnd && g_resize.edges) {
                g_resize.edges = 0;
                ReleaseCapture();
                SaveResRect();
                return 0;
            }
            if (hwnd == g_island.hwnd) {
                const float x = static_cast<float>(static_cast<short>(LOWORD(lParam))), y = static_cast<float>(static_cast<short>(HIWORD(lParam)));
                for (int i = 0; i < 3; ++i)
                    if (const PointF c = IslandButton(i, g_island.width, g_island.height, g_islandE); std::hypot(x - c.X, y - c.Y) < (i == 1 && g_islandE > 0.5f ? S(30) : S(22)))
                        MediaNow::Control(i == 0 ? MediaNow::Action::Previous : i == 1 ? MediaNow::Action::PlayPause : MediaNow::Action::Next);
                return 0;
            }
            break;
        default: break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    HWND MakeWindow(const wchar_t* title) {
        return CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"AstrixOverlay", title, WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    void UpdateRes(ULONGLONG now, POINT cursor) {
        if (!g_opt.res) {
            if (g_resShown) { ShowWindow(g_res.hwnd, SW_HIDE); g_resShown = false; }
            return;
        }
        const auto stats = SystemMonitor::Snapshot();
        if (!stats.ready) return;
        const int W = g_opt.w > 0 ? g_opt.w : static_cast<int>(S(kResW)), H = g_opt.h > 0 ? g_opt.h : ResHeight(stats);
        const bool redraw = now - g_resDrawn > 1000 || g_res.width != W || g_res.height != H;
        if (redraw) {
            g_res.Resize(W, H);
            Render(g_res, [&](Graphics& g) { DrawRes(g, W, H, stats); });
            g_resDrawn = now;
        }
        int x = g_opt.x, y = g_opt.y;
        if (x < -30000 || (x == -1 && y == -1)) {
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            x = work.right - W - static_cast<int>(S(16));
            y = work.top + static_cast<int>(S(16));
        }
        // закреплённое окно под мышкой становится почти прозрачным, чтобы не мешать
        const bool over = g_opt.pinned && cursor.x >= x && cursor.x < x + W && cursor.y >= y && cursor.y < y + H;
        g_resAlpha += ((over ? 0.15f : 1.0f) - g_resAlpha) * 0.18f;
        g_res.Present(x, y, static_cast<int>(255 * g_resAlpha * g_opt.opacity / 100.0f));
        if (!g_resShown) { ShowWindow(g_res.hwnd, SW_SHOWNOACTIVATE); g_resShown = true; }
    }

    void UpdateIsland(ULONGLONG now, POINT cursor) {
        const auto track = MediaNow::Snapshot();
        MONITORINFO info{ sizeof(info) };
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &info);
        const RECT mon = info.rcMonitor;
        static ULONGLONG s_last = now;
        const float dt = std::min(static_cast<float>(now - s_last) / 1000.0f, 0.05f);
        s_last = now;
        const float smooth = g_islandE * g_islandE * (3.0f - 2.0f * g_islandE);
        const int W = static_cast<int>(S(kPillW + (kIslandW - kPillW) * smooth)), H = static_cast<int>(S(kPillH + (kIslandH - kPillH) * smooth)), gap = static_cast<int>(S(10));
        const bool bottom = g_opt.place == 3;
        const int restX = g_opt.place == 1 ? mon.left + gap : g_opt.place == 2 ? mon.right - W - gap : (mon.left + mon.right - W) / 2;
        const int shownY = bottom ? info.rcWork.bottom - H - gap : mon.top + gap, hiddenY = bottom ? mon.bottom + 4 : mon.top - H - 4;

        const bool atEdge = (bottom ? cursor.y >= mon.bottom - 2 : cursor.y <= mon.top + 1) && cursor.x >= restX - W / 3 && cursor.x <= restX + W + W / 3;
        const bool onIsland = g_islandT > 0.4f && cursor.x >= restX && cursor.x <= restX + W && cursor.y >= shownY && cursor.y <= shownY + H;
        const bool near2 = g_islandT > 0.05f && cursor.x >= restX - gap * 3 && cursor.x <= restX + W + gap * 3 && (bottom ? cursor.y >= shownY - gap * 3 : cursor.y <= shownY + H + gap * 3);
        bool want = g_opt.island && track.present && (atEdge || near2);
        if (want) g_islandLeft = now;
        else if (g_opt.island && track.present && now - g_islandLeft < 500 && g_islandT > 0.05f) want = true; // мышь успеет вернуться

        g_islandT += ((want ? 1.0f : 0.0f) - g_islandT) * (1.0f - std::exp(-dt * 5.5f)); // медленнее и мягче
        g_islandE += ((onIsland ? 1.0f : 0.0f) - g_islandE) * (1.0f - std::exp(-dt * 5.0f));
        if (g_islandT < 0.01f && !want) {
            g_islandT = 0;
            g_islandE = 0;
            if (g_islandShown) { ShowWindow(g_island.hwnd, SW_HIDE); g_islandShown = false; }
            return;
        }
        g_island.Resize(W, H);
        Render(g_island, [&](Graphics& g) { DrawIsland(g, W, H, track, smooth); });
        const float ease = g_islandT * g_islandT * (3.0f - 2.0f * g_islandT);
        g_island.Present(restX, hiddenY + static_cast<int>((shownY - hiddenY) * ease), static_cast<int>(255 * std::min(1.0f, g_islandT * 1.6f)));
        if (!g_islandShown) { ShowWindow(g_island.hwnd, SW_SHOWNOACTIVATE); g_islandShown = true; }
    }

    void SaveDump(const std::string& folder) {
        CLSID png;
        CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &png);
        SystemMonitor::Stats stats = SystemMonitor::Snapshot();
        MediaNow::Track track = MediaNow::Snapshot();
        if (!track.present) { track.present = true; track.title = "Пример песни"; track.artist = "Исполнитель"; track.playing = true; track.position = 40; track.duration = 120; }
        Surface a, b, c;
        a.Resize(static_cast<int>(S(kResW)), ResHeight(stats));
        b.Resize(static_cast<int>(S(kIslandW)), static_cast<int>(S(kIslandH)));
        c.Resize(static_cast<int>(S(kPillW)), static_cast<int>(S(kPillH)));
        Render(a, [&](Graphics& g) { DrawRes(g, a.width, a.height, stats); });
        Render(b, [&](Graphics& g) { DrawIsland(g, b.width, b.height, track, 1.0f); });
        Render(c, [&](Graphics& g) { DrawIsland(g, c.width, c.height, track, 0.0f); });
        Bitmap one(a.width, a.height, a.width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(a.bits));
        Bitmap two(b.width, b.height, b.width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(b.bits));
        Bitmap three(c.width, c.height, c.width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(c.bits));
        three.Save(Text::ToWide(folder + "\\overlay_pill.png").c_str(), &png);
        one.Save(Text::ToWide(folder + "\\overlay_res.png").c_str(), &png);
        two.Save(Text::ToWide(folder + "\\overlay_island.png").c_str(), &png);
    }

    void Loop() {
        ULONG_PTR token = 0;
        GdiplusStartupInput input;
        GdiplusStartup(&token, &input, nullptr);
        WNDCLASSW wc{};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AstrixOverlay";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        g_res.hwnd = MakeWindow(L"ASTRIX: нагрузка");
        g_island.hwnd = MakeWindow(L"ASTRIX: сейчас играет");

        while (g_app.running) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            if (g_dirty.exchange(false)) ReadOptions();
            {
                std::lock_guard lock(g_dumpMutex);
                if (!g_dumpFolder.empty()) { SaveDump(g_dumpFolder); g_dumpFolder.clear(); }
            }
            POINT cursor{};
            GetCursorPos(&cursor);
            const ULONGLONG now = GetTickCount64();
            UpdateRes(now, cursor);
            UpdateIsland(now, cursor);
            MsgWaitForMultipleObjects(0, nullptr, FALSE, g_islandShown ? 16 : 40, QS_ALLINPUT);
        }
        ShowWindow(g_res.hwnd, SW_HIDE);
        ShowWindow(g_island.hwnd, SW_HIDE);
    }
}

void Overlay::Start() { std::thread(Loop).detach(); }
void Overlay::Reload() { g_dirty = true; }

void Overlay::Dump(const std::string& folder) {
    std::lock_guard lock(g_dumpMutex);
    g_dumpFolder = folder;
}
