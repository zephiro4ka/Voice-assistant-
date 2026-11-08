#include "ModelStore.h"

#include "Platform.h"
#include "Ears.h"
#include "LocalAi.h"
#include "Log.h"
#include "Settings.h"
#include "TextUtils.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")

namespace fs = std::filesystem;

namespace {
    const ModelStore::Model kModels[] = {
#include "ModelCatalog.inc"
    };
    constexpr int kCount = static_cast<int>(sizeof(kModels) / sizeof(kModels[0]));

    std::mutex g_mutex;
    ModelStore::Progress g_now;
    std::atomic<bool> g_cancel{ false }, g_busy{ false };

    fs::path Folder(int index) { return fs::path(Text::ExecutableDirectory()) / "models" / kModels[index].id; }

    void Status(const std::string& text) {
        std::lock_guard lock(g_mutex);
        g_now.status = text;
    }

    struct Handle {
        HINTERNET h = nullptr;
        ~Handle() { if (h) WinHttpCloseHandle(h); }
    };

    // Скачать один файл, сверить SHA-256; true — готово
    bool Fetch(const ModelStore::File& file, const fs::path& target) {
        std::error_code ec;
        if (fs::exists(target, ec) && fs::file_size(target, ec) == file.size) {
            std::lock_guard lock(g_mutex);
            g_now.done += file.size;
            return true;
        }
        std::wstring url = Text::ToWide(file.url), host(256, L'\0'), path(2048, L'\0');
        URL_COMPONENTS parts{ sizeof(parts) };
        parts.lpszHostName = host.data();
        parts.dwHostNameLength = 256;
        parts.lpszUrlPath = path.data();
        parts.dwUrlPathLength = 2048;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return false;
        Handle session{ WinHttpOpen(L"ASTRIX/1.3", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
        if (!session.h) return false;
        WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 60000);
        Handle connect{ WinHttpConnect(session.h, host.c_str(), parts.nPort, 0) };
        Handle request{ connect.h ? WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr };
        if (!request.h || !WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.h, nullptr)) return false;
        DWORD code = 0, codeSize = sizeof(code);
        WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &codeSize, nullptr);
        if (code != 200) return false;

        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
        BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
        fs::path part = target;
        part += ".part";
        bool ok = true;
        uint64_t received = 0;
        {
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            std::vector<char> buffer(1 << 18);
            for (;;) {
                if (g_cancel.load()) { ok = false; break; }
                DWORD got = 0;
                if (!WinHttpReadData(request.h, buffer.data(), static_cast<DWORD>(buffer.size()), &got)) { ok = false; break; }
                if (got == 0) break;
                out.write(buffer.data(), got);
                BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), got, 0);
                received += got;
                std::lock_guard lock(g_mutex);
                g_now.done += got;
            }
            if (!out) ok = false;
        }
        unsigned char digest[32] = {};
        BCryptFinishHash(hash, digest, 32, 0);
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        char hex[65] = {};
        for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
        if (ok && (received != file.size || std::string(hex) != file.sha256)) { ok = false; Log::Write("нейросеть: контрольная сумма не сошлась"); }
        if (ok) fs::rename(part, target, ec);
        else fs::remove(part, ec);
        return ok && !ec;
    }

    void Worker(int index, bool activate) {
        const auto& model = kModels[index];
        std::error_code ec;
        fs::create_directories(Folder(index), ec);
        bool ok = !ec;
        const auto space = fs::space(Folder(index), ec);
        if (!ec && space.available < ModelStore::SizeOf(index) + (1ull << 28)) {
            Status("Мало места на диске");
            ok = false;
        }
        for (const auto& file : model.files) {
            if (!ok || !file.url) break;
            Status(std::string("Качаю ") + file.name);
            if (!Fetch(file, Folder(index) / fs::u8path(file.name))) {
                ok = false;
                Status(g_cancel.load() ? "Отменено" : "Не получилось скачать. Проверь интернет и нажми ещё раз");
            }
        }
        if (ok) {
            Status("Готово");
            Log::Write(std::string("нейросеть: скачана ") + model.tech);
            if (activate && std::string(model.id) == "speech") {
                Config::SetValue("ears", "model", model.files[0].name);
                Ears::ReloadModel();
            } else if (activate) {
                Config::Update([&](Settings& s) { s.model = ModelStore::Path(index); });
                Ai::Restart();
            }
        }
        {
            std::lock_guard lock(g_mutex);
            g_now.running = false;
        }
        g_busy = false;
    }
}

int ModelStore::Count() { return kCount - 3; }
int ModelStore::SpeechFirst() { return kCount - 3; }
int ModelStore::SpeechCount() { return 3; }
const ModelStore::Model& ModelStore::Get(int index) { return kModels[std::clamp(index, 0, kCount - 1)]; }

uint64_t ModelStore::SizeOf(int index) {
    uint64_t total = 0;
    for (const auto& file : Get(index).files) if (file.url) total += file.size;
    return total;
}

std::string ModelStore::Path(int index) { return Text::ToUtf8((Folder(index) / fs::u8path(Get(index).files[0].name)).wstring()); }

bool ModelStore::Installed(int index) {
    if (std::string(Get(index).id) == "speech") return Ears::ModelPresent(Get(index).files[0].name);
    std::error_code ec;
    return fs::exists(Folder(index) / fs::u8path(Get(index).files[0].name), ec);
}

ModelStore::Progress ModelStore::Now() {
    std::lock_guard lock(g_mutex);
    return g_now;
}

void ModelStore::Download(int index, bool activate) {
    if (g_busy.exchange(true)) return;
    g_cancel = false;
    {
        std::lock_guard lock(g_mutex);
        g_now = {};
        g_now.running = true;
        g_now.index = index;
        g_now.total = SizeOf(index);
    }
    std::thread(Worker, index, activate).detach();
}

void ModelStore::Cancel() { g_cancel = true; }
