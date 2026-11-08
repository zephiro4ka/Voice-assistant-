#include "SileroVoice.h"

#include "Http.h"
#include "Json.h"
#include "Log.h"
#include "Sidecar.h"
#include "State.h"
#include "TextUtils.h"

#include <atomic>
#include <cstring>
#include <thread>

namespace {
    constexpr unsigned short kPort = 18081;

    // Модель распознавания — в папке models рядом с проектом (models/huggingface)
    std::wstring AsrCache() {
        const std::wstring models = Sidecar::FindUpwards({ L"models\\silero\\v5_6_ru.pt", L"ASTRIX\\models\\silero\\v5_6_ru.pt" });
        std::wstring dir = Text::ExecutableDirectory() + L"\\models";
        if (!models.empty()) { // …\models\silero\v5_6_ru.pt → …\models
            dir = models.substr(0, models.find_last_of(L"\\/"));
            dir = dir.substr(0, dir.find_last_of(L"\\/"));
        }
        return dir + L"\\huggingface";
    }

    std::atomic<int> g_asr{ 0 };        // 0 — неизвестно, 1 — готово, -1 — нет
    std::atomic<Clock> g_asrChecked{ 0 };
    constexpr Clock kStartupTimeoutMs = 90000;

    std::atomic<bool> g_ready{ false };
    HANDLE g_process = nullptr;

    bool Healthy() {
        return Http::Local(kPort, L"GET", L"/health", "", 2000, [](const std::string&) { return true; }) == 200;
    }

    void StartBlocking() {
        const std::wstring python = Sidecar::FindUpwards({ L"third_party\\silero\\venv\\Scripts\\python.exe", L"ASTRIX\\third_party\\silero\\venv\\Scripts\\python.exe" });
        const std::wstring script = Sidecar::FindUpwards({ L"third_party\\silero\\silero_server.py", L"ASTRIX\\third_party\\silero\\silero_server.py" });
        const std::wstring model = Sidecar::FindUpwards({ L"models\\silero\\v5_6_ru.pt", L"ASTRIX\\models\\silero\\v5_6_ru.pt" });
        if (python.empty() || script.empty() || model.empty()) {
            Log::Write("Silero: не найден Python, сервер или модель — голос Silero недоступен");
            return;
        }
        if (Healthy()) { g_ready = true; return; } // уже запущен (например, после перезапуска Ани)

        g_process = Sidecar::Launch(python, L"\"" + script + L"\" --model \"" + model + L"\" --port " + std::to_wstring(kPort) + L" --threads 4 --asr-cache \"" + AsrCache() + L"\"",
                                    L"silero.log");
        const Clock deadline = NowMs() + kStartupTimeoutMs;
        while (g_app.running && NowMs() < deadline && !Sidecar::HasExited(g_process)) {
            if (Healthy()) {
                g_ready = true;
                Log::Write("Silero: голос готов");
                return;
            }
            Sleep(300);
        }
        Log::Write("Silero: сервер не запустился — подробности в silero.log");
    }
}

void SileroVoice::Start() { std::thread(StartBlocking).detach(); }

bool SileroVoice::IsReady() { return g_ready; }

std::vector<std::string> SileroVoice::Speakers() { return { "xenia", "baya", "aidar", "eugene" }; }

std::vector<float> SileroVoice::Synthesize(const std::string& text, const std::string& speaker) {
    if (!g_ready) return {};
    const std::string body = "{\"text\":\"" + Json::Escape(text) + "\",\"speaker\":\"" + Json::Escape(speaker) + "\",\"rate\":" + std::to_string(kSampleRate) + "}";
    std::string raw;
    const int code = Http::Local(kPort, L"POST", L"/tts", body, 30000, [&](const std::string& chunk) { raw += chunk; return true; });
    if (code != 200) {
        if (code == 0) g_ready = false; // сервер пропал
        Log::Write("Silero: ошибка " + std::to_string(code) + " " + raw.substr(0, 200));
        return {};
    }
    std::vector<float> samples(raw.size() / sizeof(float));
    std::memcpy(samples.data(), raw.data(), samples.size() * sizeof(float));
    return samples;
}

bool SileroVoice::AsrReady() {
    if (g_asr.load() == 1) return true;
    if (NowMs() - g_asrChecked.load() < 5000) return false; // спрашиваем сервер не чаще раза в 5 секунд
    g_asrChecked = NowMs();
    const bool ok = Http::Local(kPort, L"GET", L"/asr_health", "", 1000, [](const std::string&) { return true; }) == 200;
    if (ok) {
        g_asr = 1;
        Log::Write("распознавание речи: GigaAM-v3");
    }
    return ok;
}

std::optional<std::string> SileroVoice::Recognize(const std::vector<float>& pcm16k) {
    if (!AsrReady() || pcm16k.empty()) return std::nullopt;
    const std::string body(reinterpret_cast<const char*>(pcm16k.data()), pcm16k.size() * sizeof(float));
    std::string response;
    const int code = Http::Local(kPort, L"POST", L"/asr", body, 8000, [&](const std::string& chunk) { response += chunk; return true; });
    if (code != 200) {
        Log::Write("распознавание GigaAM: код " + std::to_string(code));
        if (code == 0) g_asr = 0; // сервер пропал — спросим снова позже
        return std::nullopt;
    }
    const auto text = Json::FindStrings(response, "text", 1);
    return text.empty() ? std::string() : Text::Trim(text[0]);
}
