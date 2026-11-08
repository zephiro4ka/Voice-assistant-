#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h> // раньше windows.h
#include <ws2tcpip.h>

#include "RemoteDesktop.h"

#include "Http.h"
#include "Input.h"
#include "Log.h"
#include "PowerMode.h"
#include "Pult.h"
#include "RemoteDesktopPage.h"
#include "Screen.h"
#include "TextUtils.h"

#include "../third_party/nlohmann/json.hpp"

#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

using nlohmann::json;

namespace {
    constexpr ULONGLONG kSessionIdleMs = 30 * 60 * 1000; // ключ сеанса без действий гаснет; страница получит новый по подписи Telegram
    constexpr long long kInitDataMaxAge = 3600;       // подпись Telegram старше часа не принимаем
    constexpr ULONGLONG kStreamFrameMs = 25;          // не чаще 40 кадров в секунду
    constexpr int kStreamInFlight = 2;                // кадров в пути: медленная сеть сама сбавляет скорость

    std::mutex g_startMutex; // запуски идут по одному
    std::mutex g_mutex;      // всё, что ниже
    SOCKET g_listen = INVALID_SOCKET;
    HANDLE g_process = nullptr, g_job = nullptr;
    int g_port = 0;
    std::string g_url, g_tunnelError, g_botToken, g_session;
    long long g_owner = 0;
    size_t g_lastFrame = 0;
    std::function<void(const std::string&)> g_onClosed;

    std::atomic<int> g_generation{ 0 };
    std::atomic<bool> g_running{ false };
    std::atomic<int> g_monitor{ 0 };
    std::atomic<bool> g_hd{ false };
    std::atomic<bool> g_buttonDown[3];
    std::mutex g_keysMutex;
    std::vector<WORD> g_keysDown; // что нажато с телефона — отпустить при обрыве
    std::atomic<ULONGLONG> g_activeInput{ 0 };

    std::string Hmac(const std::string& key, const std::string& message) {
        BCRYPT_ALG_HANDLE alg = nullptr;
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) return {};
        std::string out(32, '\0');
        const NTSTATUS status = BCryptHash(alg, reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())), static_cast<ULONG>(key.size()),
            reinterpret_cast<PUCHAR>(const_cast<char*>(message.data())), static_cast<ULONG>(message.size()), reinterpret_cast<PUCHAR>(out.data()), 32);
        BCryptCloseAlgorithmProvider(alg, 0);
        return status == 0 ? out : std::string();
    }

    std::string Hex(const std::string& bytes) {
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (unsigned char c : bytes) { out += digits[c >> 4]; out += digits[c & 15]; }
        return out;
    }

    std::string RandomToken() {
        std::string bytes(32, '\0');
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()), 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return {};
        return Hex(bytes);
    }

    bool SameSecret(const std::string& a, const std::string& b) { // сравнение за одинаковое время
        if (a.empty() || a.size() != b.size()) return false;
        unsigned char diff = 0;
        for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
        return diff == 0;
    }

    std::string UrlDecode(const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size() && isxdigit(static_cast<unsigned char>(s[i + 1])) && isxdigit(static_cast<unsigned char>(s[i + 2]))) {
                out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else out += s[i] == '+' ? ' ' : s[i];
        }
        return out;
    }

    // Проверка по документации Telegram: https://core.telegram.org/bots/webapps#validating-data-received-via-the-mini-app
    bool ValidInitData(const std::string& initData, const std::string& botToken, long long owner) {
        std::map<std::string, std::string> fields;
        size_t at = 0;
        while (at <= initData.size()) {
            size_t end = initData.find('&', at);
            if (end == std::string::npos) end = initData.size();
            const std::string pair = initData.substr(at, end - at);
            const size_t eq = pair.find('=');
            if (eq != std::string::npos) fields[pair.substr(0, eq)] = UrlDecode(pair.substr(eq + 1));
            at = end + 1;
        }
        const auto hash = fields.find("hash");
        if (hash == fields.end() || !fields.count("user") || !fields.count("auth_date")) return false;
        const std::string received = hash->second;
        fields.erase(hash);
        std::string check;
        for (const auto& [key, value] : fields) check += (check.empty() ? "" : "\n") + key + "=" + value;
        if (!SameSecret(Hex(Hmac(Hmac("WebAppData", botToken), check)), received)) return false;

        const long long age = static_cast<long long>(std::time(nullptr)) - std::atoll(fields["auth_date"].c_str());
        if (age > kInitDataMaxAge || age < -300) return false;
        const json user = json::parse(fields["user"], nullptr, false);
        return !user.is_discarded() && user.is_object() && user.value("id", 0LL) == owner;
    }

    Screen::MonitorArea Area() {
        Screen::MonitorArea area;
        Screen::Monitor(g_monitor.load(), area);
        return area;
    }

    void MouseTo(double x, double y) {
        const auto area = Area();
        if (area.width <= 0) return;
        const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vw = std::max(2, GetSystemMetrics(SM_CXVIRTUALSCREEN)), vh = std::max(2, GetSystemMetrics(SM_CYVIRTUALSCREEN));
        const double px = area.left + std::clamp(x, 0.0, 1.0) * (area.width - 1), py = area.top + std::clamp(y, 0.0, 1.0) * (area.height - 1);
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = static_cast<LONG>((px - vx) * 65535.0 / (vw - 1));
        in.mi.dy = static_cast<LONG>((py - vy) * 65535.0 / (vh - 1));
        in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        SendInput(1, &in, sizeof(in));
    }

    void MouseButton(int button, bool down) { // 0 левая, 1 средняя, 2 правая
        if (button < 0 || button > 2) return;
        INPUT in{};
        in.type = INPUT_MOUSE;
        static const DWORD kFlags[3][2] = { { MOUSEEVENTF_LEFTUP, MOUSEEVENTF_LEFTDOWN }, { MOUSEEVENTF_MIDDLEUP, MOUSEEVENTF_MIDDLEDOWN }, { MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_RIGHTDOWN } };
        in.mi.dwFlags = kFlags[button][down ? 1 : 0];
        SendInput(1, &in, sizeof(in));
        g_buttonDown[button] = down;
    }

    void MouseRelative(int dx, int dy) { // для игр с захваченной мышью и правого стика
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = std::clamp(dx, -400, 400);
        in.mi.dy = std::clamp(dy, -400, 400);
        in.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &in, sizeof(in));
    }

    // KeyboardEvent.code → клавиша Windows
    WORD VirtualKey(const std::string& code) {
        if (code.size() == 4 && code.rfind("Key", 0) == 0 && code[3] >= 'A' && code[3] <= 'Z') return static_cast<WORD>(code[3]);
        if (code.size() == 6 && code.rfind("Digit", 0) == 0 && code[5] >= '0' && code[5] <= '9') return static_cast<WORD>(code[5]);
        if (code.size() >= 2 && code.size() <= 3 && code[0] == 'F') {
            const int n = std::atoi(code.c_str() + 1);
            if (n >= 1 && n <= 12) return static_cast<WORD>(VK_F1 + n - 1);
        }
        static const std::map<std::string, WORD> kKeys = {
            { "Enter", VK_RETURN }, { "Escape", VK_ESCAPE }, { "Backspace", VK_BACK }, { "Tab", VK_TAB }, { "Space", VK_SPACE }, { "CapsLock", VK_CAPITAL },
            { "ShiftLeft", VK_LSHIFT }, { "ShiftRight", VK_RSHIFT }, { "ControlLeft", VK_LCONTROL }, { "ControlRight", VK_RCONTROL },
            { "AltLeft", VK_LMENU }, { "AltRight", VK_RMENU }, { "MetaLeft", VK_LWIN }, { "MetaRight", VK_RWIN },
            { "ArrowUp", VK_UP }, { "ArrowDown", VK_DOWN }, { "ArrowLeft", VK_LEFT }, { "ArrowRight", VK_RIGHT },
            { "Delete", VK_DELETE }, { "Insert", VK_INSERT }, { "Home", VK_HOME }, { "End", VK_END }, { "PageUp", VK_PRIOR }, { "PageDown", VK_NEXT },
            { "Minus", VK_OEM_MINUS }, { "Equal", VK_OEM_PLUS }, { "BracketLeft", VK_OEM_4 }, { "BracketRight", VK_OEM_6 }, { "Backslash", VK_OEM_5 },
            { "Semicolon", VK_OEM_1 }, { "Quote", VK_OEM_7 }, { "Comma", VK_OEM_COMMA }, { "Period", VK_OEM_PERIOD }, { "Slash", VK_OEM_2 }, { "Backquote", VK_OEM_3 } };
        const auto it = kKeys.find(code);
        return it == kKeys.end() ? 0 : it->second;
    }

    void SendKey(WORD vk, bool down) { // сканкодами: так нажатия видят и игры
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
        switch (vk) {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_DELETE: case VK_INSERT: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT: case VK_RCONTROL: case VK_RMENU: case VK_LWIN: case VK_RWIN:
            in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            break;
        default: break;
        }
        SendInput(1, &in, sizeof(in));
        std::lock_guard lock(g_keysMutex);
        g_keysDown.erase(std::remove(g_keysDown.begin(), g_keysDown.end(), vk), g_keysDown.end());
        if (down) g_keysDown.push_back(vk);
    }

    void Wheel(int notches) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = static_cast<DWORD>(std::clamp(notches, -20, 20) * WHEEL_DELTA);
        SendInput(1, &in, sizeof(in));
    }

    void ReleaseButtons() {
        for (int button = 0; button < 3; ++button) if (g_buttonDown[button].load()) MouseButton(button, false);
        std::vector<WORD> keys;
        { std::lock_guard lock(g_keysMutex); keys = g_keysDown; }
        for (WORD vk : keys) SendKey(vk, false);
    }

    void Key(const std::string& k) { // только известные кнопки панели
        if (k == "enter") Input::Tap(VK_RETURN);
        else if (k == "back") Input::Tap(VK_BACK);
        else if (k == "tab") Input::Tap(VK_TAB);
        else if (k == "esc") Input::Tap(VK_ESCAPE);
        else if (k == "del") Input::Tap(VK_DELETE);
        else if (k == "win") Input::Tap(VK_LWIN);
        else if (k == "alttab") Input::Chord({ VK_MENU, VK_TAB });
        else if (k == "copy") Input::Chord({ VK_CONTROL, 'C' });
        else if (k == "paste") Input::Chord({ VK_CONTROL, 'V' });
        else if (k == "desktop") Input::Chord({ VK_LWIN, 'D' });
    }

    struct Response {
        int status = 200;
        std::string type = "text/plain; charset=utf-8", body, extra;
    };

    void HandleInput(const json& e) {
        const std::string type = e.value("t", "");
        const double x = e.value("x", 0.0), y = e.value("y", 0.0);
        const int button = e.value("b", 0);
        g_activeInput = GetTickCount64();
        if (type == "move") MouseTo(x, y);
        else if (type == "down") { MouseTo(x, y); MouseButton(button, true); }
        else if (type == "up") { MouseTo(x, y); MouseButton(button, false); }
        else if (type == "click") { MouseTo(x, y); MouseButton(button, true); Sleep(25); MouseButton(button, false); }
        else if (type == "btn") MouseButton(button, e.value("d", 0) != 0);
        else if (type == "rel") MouseRelative(e.value("x", 0), e.value("y", 0));
        else if (type == "vk") { if (const WORD vk = VirtualKey(e.value("c", ""))) SendKey(vk, e.value("d", 0) != 0); }
        else if (type == "scroll") Wheel(e.value("d", 0));
        else if (type == "text") Input::TypeText(e.value("s", ""));
        else if (type == "key") Key(e.value("k", ""));
        else if (type == "mon") {
            const int count = std::max(1, Screen::MonitorCount());
            g_monitor = (g_monitor.load() + 1) % count;
            std::lock_guard lock(g_mutex);
            g_lastFrame = 0;
        } else if (type == "hd") {
            g_hd = e.value("v", false);
            std::lock_guard lock(g_mutex);
            g_lastFrame = 0;
        }
    }

    Response Route(const std::string& method, const std::string& path, const std::map<std::string, std::string>& headers, const std::string& body) {
        Response r;
        if (method == "GET" && (path == "/" || path == "/index.html")) {
            r.type = "text/html; charset=utf-8";
            r.body = kRemoteDesktopPage;
            r.extra = "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline' https://telegram.org; style-src 'unsafe-inline'; img-src blob:; connect-src 'self' wss:; base-uri 'none'; form-action 'none'\r\n";
            return r;
        }
        if (method == "GET" && path == "/ping") { r.body = "ok"; return r; }
        if (method == "POST" && path == "/auth") {
            std::string token;
            long long owner = 0;
            { std::lock_guard lock(g_mutex); token = g_botToken; owner = g_owner; }
            if (!ValidInitData(body, token, owner)) {
                Log::Write("удалённый стол: отказ — подпись Telegram не подошла");
                r.status = 403;
                r.body = "forbidden";
                return r;
            }
            const std::string session = RandomToken();
            if (session.empty()) { r.status = 500; return r; }
            { std::lock_guard lock(g_mutex); g_session = session; g_lastFrame = 0; }
            g_activeInput = GetTickCount64();
            Log::Write("пульт: владелец подключился");
            r.type = "application/json";
            r.body = json{ { "session", session }, { "monitors", Screen::MonitorCount() } }.dump();
            return r;
        }

        // дальше — только с ключом сеанса
        const auto given = headers.find("x-session");
        std::string session;
        { std::lock_guard lock(g_mutex); session = g_session; }
        if (given == headers.end() || !SameSecret(given->second, session)) { r.status = 401; r.body = "unauthorized"; return r; }

        if (path.rfind("/api/", 0) == 0) {
            if (method == "POST") g_activeInput = GetTickCount64();
            auto reply = Pult::Handle(method, path, body);
            r.status = reply.status;
            r.type = std::move(reply.type);
            r.body = std::move(reply.body);
            return r;
        }
        if (PowerMode::IsLocked()) { r.status = 423; r.body = "locked"; return r; } // экран блокировки не показываем и не трогаем

        if (method == "GET" && path == "/frame") {
            const auto jpeg = Screen::CaptureMonitorJpeg(g_monitor.load(), g_hd.load() ? 1920 : 1280, g_hd.load() ? 72 : 55);
            if (jpeg.empty()) { r.status = 503; return r; }
            const size_t hash = std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(jpeg.data()), jpeg.size()));
            std::lock_guard lock(g_mutex);
            if (hash == g_lastFrame) { r.status = 204; return r; }
            g_lastFrame = hash;
            r.type = "image/jpeg";
            r.body.assign(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
            return r;
        }
        if (method == "POST" && path == "/input") {
            const json event = json::parse(body, nullptr, false);
            if (event.is_object()) HandleInput(event);
            r.status = 204;
            return r;
        }
        r.status = 404;
        return r;
    }

    bool SendAll(SOCKET s, const char* data, size_t size) {
        while (size > 0) {
            const int sent = send(s, data, static_cast<int>(std::min<size_t>(size, 1 << 20)), 0);
            if (sent <= 0) return false;
            data += sent;
            size -= sent;
        }
        return true;
    }

    std::string WebSocketAccept(const std::string& key) {
        const std::string text = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        BCRYPT_ALG_HANDLE alg = nullptr;
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) return {};
        std::vector<uint8_t> digest(20);
        const NTSTATUS status = BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())), static_cast<ULONG>(text.size()), digest.data(), 20);
        BCryptCloseAlgorithmProvider(alg, 0);
        return status == 0 ? Screen::Base64(digest) : std::string();
    }

    bool WsSend(SOCKET s, unsigned char opcode, const char* data, size_t size) {
        std::string head(1, static_cast<char>(0x80 | opcode));
        if (size < 126) head += static_cast<char>(size);
        else if (size < 65536) { head += static_cast<char>(126); head += static_cast<char>(size >> 8); head += static_cast<char>(size & 255); }
        else { head += static_cast<char>(127); for (int shift = 56; shift >= 0; shift -= 8) head += static_cast<char>((static_cast<unsigned long long>(size) >> shift) & 255); }
        return SendAll(s, head.data(), head.size()) && SendAll(s, data, size);
    }

    bool WsText(SOCKET s, const std::string& text) { return WsSend(s, 1, text.data(), text.size()); }

    // Кадры одним соединением: без отдельного запроса через туннель на каждый кадр
    void RunStream(SOCKET client, const std::string& key) {
        const std::string accept = WebSocketAccept(key);
        if (accept.empty()) return;
        const std::string head = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n";
        if (!SendAll(client, head.data(), head.size())) return;
        BOOL noDelay = TRUE;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

        const int generation = g_generation.load();
        const ULONGLONG started = GetTickCount64();
        ULONGLONG nextFrame = 0, nextLocked = 0;
        std::string input;
        bool authed = false, open = true;
        int inFlight = 0, sent = 0;
        size_t last = 0;

        while (open && generation == g_generation.load()) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(client, &readable);
            timeval wait{ 0, authed && inFlight < kStreamInFlight ? 0 : 10000 };
            const int ready = select(0, &readable, nullptr, nullptr, &wait);
            if (ready < 0) break;
            if (ready > 0) {
                char buffer[2048];
                const int got = recv(client, buffer, sizeof(buffer), 0);
                if (got <= 0) break;
                input.append(buffer, got);
            }

            while (input.size() >= 2) { // сообщения браузера: всегда с маской и короткие
                const unsigned char opcode = input[0] & 15, length = input[1] & 127;
                if (!(input[1] & 0x80) || length == 127) { open = false; break; }
                const size_t maskAt = length == 126 ? 4 : 2;
                if (input.size() < maskAt) break;
                const size_t size = length == 126 ? (static_cast<size_t>(static_cast<unsigned char>(input[2])) << 8) | static_cast<unsigned char>(input[3]) : length;
                if (size > 16384) { open = false; break; }
                const size_t total = maskAt + 4 + size;
                if (input.size() < total) break;
                std::string message = input.substr(maskAt + 4, size);
                for (size_t i = 0; i < message.size(); ++i) message[i] ^= input[maskAt + i % 4];
                input.erase(0, total);
                if (opcode == 8) { open = false; break; }
                if (opcode == 9) { WsSend(client, 10, message.data(), message.size()); continue; }
                if (!authed) {
                    std::string session;
                    { std::lock_guard lock(g_mutex); session = g_session; }
                    if (!SameSecret(message, session)) { WsText(client, "denied"); open = false; break; }
                    authed = true;
                    WsText(client, "ok");
                } else if (message == "a") inFlight = std::max(0, inFlight - 1);
                else if (message == "r") inFlight = 0; // страницу снова показали
                else if (!message.empty() && message[0] == '{' && !PowerMode::IsLocked()) {
                    const json event = json::parse(message, nullptr, false);
                    if (event.is_object()) HandleInput(event);
                }
            }
            if (!open) break;
            if (!authed) {
                if (GetTickCount64() - started > 8000) break;
                continue;
            }
            bool current = false;
            { std::lock_guard lock(g_mutex); current = !g_session.empty(); }
            if (!current) break;

            const ULONGLONG now = GetTickCount64();
            if (PowerMode::IsLocked()) { // экран блокировки не показываем
                if (now >= nextLocked) { nextLocked = now + 2000; if (!WsText(client, "locked")) break; }
                Sleep(50);
                continue;
            }
            if (inFlight >= kStreamInFlight) continue;
            if (now < nextFrame) { Sleep(1); continue; }
            const auto jpeg = Screen::CaptureMonitorJpeg(g_monitor.load(), g_hd.load() ? 1920 : 1280, g_hd.load() ? 72 : 55);
            if (jpeg.empty()) { nextFrame = now + 300; continue; }
            const size_t hash = std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(jpeg.data()), jpeg.size()));
            if (hash == last) { nextFrame = now + 60; continue; } // картинка не изменилась
            last = hash;
            if (!WsSend(client, 2, reinterpret_cast<const char*>(jpeg.data()), jpeg.size())) break;
            ++inFlight;
            ++sent;
            nextFrame = now + kStreamFrameMs;
        }
        ReleaseButtons(); // ни кнопка, ни клавиша не должны «залипнуть» при обрыве
        const ULONGLONG seconds = std::max<ULONGLONG>(1, (GetTickCount64() - started) / 1000);
        Log::Write("удалённый стол: поток закрыт, в среднем " + std::to_string(sent / seconds) + " к/с");
    }

    void Serve(SOCKET client) {
        DWORD timeout = 10000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

        std::string data;
        char buffer[4096];
        size_t headerEnd = std::string::npos;
        while (headerEnd == std::string::npos && data.size() < 32768) {
            const int got = recv(client, buffer, sizeof(buffer), 0);
            if (got <= 0) { closesocket(client); return; }
            data.append(buffer, got);
            headerEnd = data.find("\r\n\r\n");
        }
        if (headerEnd == std::string::npos) { closesocket(client); return; }

        std::map<std::string, std::string> headers;
        size_t lineEnd = data.find("\r\n");
        const std::string first = data.substr(0, lineEnd);
        for (size_t at = lineEnd + 2; at < headerEnd;) {
            size_t end = data.find("\r\n", at);
            if (end == std::string::npos || end > headerEnd) end = headerEnd;
            const std::string line = data.substr(at, end - at);
            if (const size_t colon = line.find(':'); colon != std::string::npos) {
                std::string name = line.substr(0, colon);
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
                headers[name] = Text::Trim(line.substr(colon + 1));
            }
            at = end + 2;
        }

        const size_t length = headers.count("content-length") ? std::strtoull(headers["content-length"].c_str(), nullptr, 10) : 0;
        if (length > 65536) { closesocket(client); return; } // ввод и подпись — короткие
        std::string body = data.substr(headerEnd + 4);
        while (body.size() < length) {
            const int got = recv(client, buffer, sizeof(buffer), 0);
            if (got <= 0) { closesocket(client); return; }
            body.append(buffer, got);
        }

        const size_t space1 = first.find(' '), space2 = first.find(' ', space1 + 1);
        Response r;
        if (space1 == std::string::npos || space2 == std::string::npos) r.status = 400;
        else {
            std::string path = first.substr(space1 + 1, space2 - space1 - 1);
            path = path.substr(0, path.find('?'));
            if (path == "/stream" && first.rfind("GET ", 0) == 0 && headers.count("sec-websocket-key")) {
                RunStream(client, headers["sec-websocket-key"]);
                closesocket(client);
                return;
            }
            try { r = Route(first.substr(0, space1), path, headers, body); } catch (...) { r = Response{}; r.status = 500; }
        }
        const char* reason = r.status == 200 ? "OK" : r.status == 204 ? "No Content" : "Error";
        const std::string head = "HTTP/1.1 " + std::to_string(r.status) + " " + reason + "\r\nContent-Type: " + r.type +
            "\r\nContent-Length: " + std::to_string(r.body.size()) + "\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\n"
            "X-Content-Type-Options: nosniff\r\n" + r.extra + "Connection: close\r\n\r\n";
        if (SendAll(client, head.data(), head.size())) SendAll(client, r.body.data(), r.body.size());
        shutdown(client, SD_SEND);
        closesocket(client);
    }

    void AcceptLoop(SOCKET listener) {
        for (;;) {
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) return;
            std::thread(Serve, client).detach();
        }
    }

    std::wstring FindCloudflared() {
        std::vector<std::wstring> places = { Text::ExecutableDirectory() + L"\\cloudflared.exe" };
        wchar_t path[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"ProgramFiles(x86)", path, MAX_PATH)) places.push_back(std::wstring(path) + L"\\cloudflared\\cloudflared.exe");
        if (GetEnvironmentVariableW(L"ProgramFiles", path, MAX_PATH)) places.push_back(std::wstring(path) + L"\\cloudflared\\cloudflared.exe");
        for (const auto& place : places)
            if (GetFileAttributesW(place.c_str()) != INVALID_FILE_ATTRIBUTES) return place;
        if (SearchPathW(nullptr, L"cloudflared.exe", nullptr, MAX_PATH, path, nullptr)) return path;
        return {};
    }

    std::string FindTunnelUrl(const std::string& text) {
        for (size_t at = text.find("https://"); at != std::string::npos; at = text.find("https://", at + 8)) {
            size_t end = at + 8;
            while (end < text.size() && (isalnum(static_cast<unsigned char>(text[end])) || text[end] == '-' || text[end] == '.')) ++end;
            const std::string url = text.substr(at, end - at);
            const std::string suffix = ".trycloudflare.com";
            if (url.size() > suffix.size() && url.compare(url.size() - suffix.size(), suffix.size(), suffix) == 0 && url != "https://api.trycloudflare.com") return url;
        }
        return {};
    }

    void ReadTunnelLog(HANDLE pipe, int generation) {
        std::string text;
        char buffer[1024];
        DWORD got = 0;
        while (ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
            if (text.size() < 65536) text.append(buffer, got);
            if (generation != g_generation.load()) continue;
            std::lock_guard lock(g_mutex);
            if (g_url.empty()) g_url = FindTunnelUrl(text);
        }
        CloseHandle(pipe);
    }

    bool StartTunnel(int port, int generation, const std::wstring& exe) {
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE read = nullptr, write = nullptr;
        if (!CreatePipe(&read, &write, &sa, 0)) return false;
        SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{ sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = si.hStdError = write;
        PROCESS_INFORMATION pi{};
        std::wstring command = L"\"" + exe + L"\" tunnel --no-autoupdate --url http://127.0.0.1:" + std::to_wstring(port);
        const BOOL created = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
        CloseHandle(write);
        if (!created) { CloseHandle(read); return false; }

        // туннель не должен пережить Аню, даже если она упадёт
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
            AssignProcessToJobObject(job, pi.hProcess);
        }
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        {
            std::lock_guard lock(g_mutex);
            g_process = pi.hProcess;
            g_job = job;
        }
        std::thread(ReadTunnelLog, read, generation).detach();
        return true;
    }

    void Close(const std::string& reason) {
        std::function<void(const std::string&)> notify;
        {
            std::lock_guard lock(g_mutex);
            if (!g_running.exchange(false)) return;
            ++g_generation;
            if (g_listen != INVALID_SOCKET) closesocket(g_listen);
            g_listen = INVALID_SOCKET;
            if (g_process) { TerminateProcess(g_process, 0); CloseHandle(g_process); g_process = nullptr; }
            if (g_job) { CloseHandle(g_job); g_job = nullptr; }
            g_url.clear();
            g_session.clear();
            g_botToken.clear();
            notify = std::move(g_onClosed);
            g_onClosed = nullptr;
        }
        ReleaseButtons();
        Log::Write("удалённый стол: закрыт");
        if (notify && !reason.empty()) notify(reason);
    }

    void Watchdog(int generation) {
        int failures = 0;
        for (int tick = 1; generation == g_generation.load(); ++tick) {
            Sleep(3000);
            if (generation != g_generation.load()) return;
            std::string url;
            bool died = false;
            {
                std::lock_guard lock(g_mutex);
                died = g_process && WaitForSingleObject(g_process, 0) == WAIT_OBJECT_0;
                url = g_url;
                if (!g_session.empty() && GetTickCount64() - g_activeInput.load() > kSessionIdleMs) g_session.clear();
            }
            if (died) { Close(""); return; }
            if (tick % 20 != 0) continue; // раз в минуту проверяем, что адрес живой
            const std::wstring host = Text::ToWide(url.substr(std::min<size_t>(8, url.size())));
            failures = Http::HttpsGet(host.c_str(), L"/ping", 8000) == "ok" ? 0 : failures + 1;
            if (failures >= 3) { Log::Write("пульт: адрес перестал отвечать, поднимаю новый"); Close(""); return; }
        }
    }
}

RemoteDesktop::Result RemoteDesktop::Start(const std::string& botToken, long long owner, std::function<void(const std::string&)> onClosed) {
    std::lock_guard start(g_startMutex);
    {
        std::lock_guard lock(g_mutex);
        if (g_running.load() && !g_url.empty()) return { g_url, "" };
    }
    if (botToken.empty() || owner == 0) return { "", "Не настроен бот или ID владельца." };
    const std::wstring exe = FindCloudflared();
    if (exe.empty()) return { "", "Для рабочего стола нужна программа cloudflared. Установи её: winget install Cloudflare.cloudflared" };

    static const bool network = [] { WSADATA data; return WSAStartup(MAKEWORD(2, 2), &data) == 0; }();
    if (!network) return { "", "Не запустилась сеть Windows." };

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // снаружи — только через туннель
    int size = sizeof(address);
    if (listener == INVALID_SOCKET || bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) != 0 || listen(listener, 32) != 0) {
        if (listener != INVALID_SOCKET) closesocket(listener);
        return { "", "Не получилось открыть локальный порт." };
    }

    const int port = ntohs(address.sin_port);
    int generation = 0;
    {
        std::lock_guard lock(g_mutex);
        generation = ++g_generation;
        g_listen = listener;
        g_port = port;
        g_botToken = botToken;
        g_owner = owner;
        g_url.clear();
        g_session.clear();
        g_lastFrame = 0;
        g_onClosed = std::move(onClosed);
        g_monitor = 0;
        g_hd = false;
        g_running = true;
    }
    g_activeInput = GetTickCount64();
    std::thread(AcceptLoop, listener).detach();
    Log::Write("удалённый стол: локальный порт " + std::to_string(port));

    if (!StartTunnel(port, generation, exe)) {
        Close("");
        return { "", "Не получилось запустить cloudflared." };
    }

    std::string url;
    for (int waited = 0; waited < 45000 && generation == g_generation.load(); waited += 250) {
        { std::lock_guard lock(g_mutex); url = g_url; }
        if (!url.empty()) break;
        Sleep(250);
    }
    if (url.empty()) {
        Close("");
        return { "", "Cloudflare не выдал адрес — проверь интернет и попробуй ещё раз." };
    }

    // новому адресу нужно несколько секунд, чтобы появиться в DNS
    const std::wstring host = Text::ToWide(url.substr(8));
    for (int attempt = 0; attempt < 12 && generation == g_generation.load(); ++attempt) {
        if (Http::HttpsGet(host.c_str(), L"/ping", 5000) == "ok") break;
        Sleep(1500);
    }
    g_activeInput = GetTickCount64();
    std::thread(Watchdog, generation).detach();
    Log::Write("удалённый стол: туннель готов");
    return { url, "" };
}

void RemoteDesktop::Stop() { Close(""); }
