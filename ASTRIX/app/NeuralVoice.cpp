#include "NeuralVoice.h"

#include "Platform.h" // первым: Windows без макросов min/max
#include "TextUtils.h"

// Внимание: helper.h объявляет глобальные Config/Style/TextToSpeech — поэтому настройки Ани (Settings.h) здесь не подключаем.
#include "../third_party/supertonic/helper.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace {
    // Шаги «проявления» голоса: больше — чище и естественнее, меньше — быстрее.
    // Замерено на i5-13450HX: 10 шагов всё ещё быстрее самой речи; короткие ответы делаем за 8, чтобы не ждать.
    constexpr int kQualitySteps = 10;
    constexpr int kQuickSteps = 8;
    constexpr size_t kShortPhrase = 60;      // байт UTF-8 ≈ 30 русских букв
    constexpr int kThreads = 4;              // больше потоков здесь только мешают друг другу
    constexpr const char* kLanguage = "ru";

    std::mutex g_mutex;
    std::unique_ptr<Ort::Env> g_env;
    OnnxModels g_models;
    std::unique_ptr<UnicodeProcessor> g_textProcessor;
    std::unique_ptr<TextToSpeech> g_tts;
    std::string g_styleDir;
    std::map<std::string, std::unique_ptr<Style>> g_styles;

    // Ищем models/supertonic-3 рядом с exe или выше по папкам
    std::wstring FindModelDir() {
        std::wstring dir = Text::ExecutableDirectory();
        for (int level = 0; level < 5; ++level) {
            for (const wchar_t* sub : { L"\\models\\supertonic-3", L"\\ASTRIX\\models\\supertonic-3" }) {
                const std::wstring candidate = dir + sub;
                if (GetFileAttributesW((candidate + L"\\onnx\\tts.json").c_str()) != INVALID_FILE_ATTRIBUTES) return candidate;
            }
            const size_t slash = dir.find_last_of(L"\\/");
            if (slash == std::wstring::npos) break;
            dir.resize(slash);
        }
        return {};
    }

    // Стиль голоса по имени. «F2+F4» — среднее двух стилей: высота одного и живость интонации другого.
    const Style* GetStyle(const std::string& name) {
        auto it = g_styles.find(name);
        if (it != g_styles.end()) return it->second.get();
        try {
            std::vector<Style> parts;
            for (size_t start = 0; start <= name.size();) {
                const size_t plus = std::min(name.find('+', start), name.size());
                parts.push_back(loadVoiceStyle({ g_styleDir + "\\" + name.substr(start, plus - start) + ".json" }));
                start = plus + 1;
            }
            std::vector<float> ttl = parts[0].getTtlData(), dp = parts[0].getDpData();
            for (size_t p = 1; p < parts.size(); ++p) {
                for (size_t i = 0; i < ttl.size(); ++i) ttl[i] += parts[p].getTtlData()[i];
                for (size_t i = 0; i < dp.size(); ++i) dp[i] += parts[p].getDpData()[i];
            }
            for (float& v : ttl) v /= static_cast<float>(parts.size());
            for (float& v : dp) v /= static_cast<float>(parts.size());
            auto style = std::make_unique<Style>(ttl, parts[0].getTtlShape(), dp, parts[0].getDpShape());
            return (g_styles[name] = std::move(style)).get();
        } catch (...) {
            return nullptr;
        }
    }

    // Поднять тон: «сжать» звук по времени в factor раз. Речь заранее синтезирована в factor раз медленнее,
    // поэтому темп остаётся прежним. Интерполяция Катмулла — Рома + лёгкий фильтр от «звона».
    std::vector<float> RaisePitch(const std::vector<float>& in, float factor) {
        if (in.empty() || (factor > 0.999f && factor < 1.001f)) return in;
        std::vector<float> smooth(in.size());
        float state = 0.0f;
        const float alpha = std::min(1.0f, 1.0f / factor); // чем сильнее сжимаем, тем мягче срезаем верха
        for (size_t i = 0; i < in.size(); ++i) smooth[i] = state += alpha * (in[i] - state);

        const size_t outSize = static_cast<size_t>(in.size() / factor);
        std::vector<float> out(outSize);
        const long long last = static_cast<long long>(smooth.size()) - 1;
        auto at = [&](long long i) { return smooth[static_cast<size_t>(std::clamp<long long>(i, 0, last))]; };
        for (size_t i = 0; i < outSize; ++i) {
            const double pos = i * static_cast<double>(factor);
            const long long k = static_cast<long long>(pos);
            const float t = static_cast<float>(pos - k);
            const float p0 = at(k - 1), p1 = at(k), p2 = at(k + 1), p3 = at(k + 2);
            out[i] = 0.5f * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
        }
        return out;
    }

    // Модель добавляет ~0.4 с тишины в начале и ~0.7 с в конце фразы — между предложениями это звучит как «лаги».
    // Оставляем короткие естественные паузы: 40 мс перед речью и 150 мс после.
    void TrimSilence(std::vector<float>& wav, int sampleRate) {
        const size_t window = static_cast<size_t>(sampleRate * 0.01); // 10 мс
        if (wav.size() < window * 4) return;
        auto loud = [&](size_t start) {
            double energy = 0.0;
            for (size_t i = start; i < start + window; ++i) energy += static_cast<double>(wav[i]) * wav[i];
            return std::sqrt(energy / window) > 0.004;
        };
        size_t first = 0, last = wav.size() - window;
        while (first + window < wav.size() && !loud(first)) first += window;
        while (last > first && !loud(last)) last -= window;
        if (first >= last) return;
        const size_t begin = first > sampleRate * 0.04 ? first - static_cast<size_t>(sampleRate * 0.04) : 0;
        const size_t end = std::min(wav.size(), last + window + static_cast<size_t>(sampleRate * 0.15));
        wav = std::vector<float>(wav.begin() + begin, wav.begin() + end);
    }

    // Ровная комфортная громкость и мягкие края — без щелчков и скачков громкости между фразами
    void Polish(std::vector<float>& wav, int sampleRate) {
        TrimSilence(wav, sampleRate);
        if (wav.empty()) return;
        double energy = 0.0;
        for (float v : wav) energy += static_cast<double>(v) * v;
        const float rms = static_cast<float>(std::sqrt(energy / wav.size()));
        const float gain = rms > 1e-4f ? std::min(0.08f / rms, 3.0f) : 1.0f; // цель около -22 dBFS
        const size_t fade = std::max<size_t>(1, std::min<size_t>(wav.size() / 2, static_cast<size_t>(sampleRate * 0.008)));
        for (size_t i = 0; i < wav.size(); ++i) {
            float v = wav[i] * gain;
            if (i < fade) v *= static_cast<float>(i) / fade;
            if (i >= wav.size() - fade) v *= static_cast<float>(wav.size() - 1 - i) / fade;
            wav[i] = std::clamp(v, -0.98f, 0.98f);
        }
    }
}

bool NeuralVoice::Load() {
    std::lock_guard lock(g_mutex);
    if (g_tts) return true;
    const std::wstring dir = FindModelDir();
    if (dir.empty()) return false;

    try {
        const std::string onnxDir = Text::ToUtf8(dir + L"\\onnx");
        g_styleDir = Text::ToUtf8(dir + L"\\voice_styles");

        g_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "anya-voice");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(std::min(kThreads, static_cast<int>(std::thread::hardware_concurrency())));
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        g_models.dp = loadOnnx(*g_env, onnxDir + "\\duration_predictor.onnx", options);
        g_models.text_enc = loadOnnx(*g_env, onnxDir + "\\text_encoder.onnx", options);
        g_models.vector_est = loadOnnx(*g_env, onnxDir + "\\vector_estimator.onnx", options);
        g_models.vocoder = loadOnnx(*g_env, onnxDir + "\\vocoder.onnx", options);
        g_textProcessor = loadTextProcessor(onnxDir);
        g_tts = std::make_unique<TextToSpeech>(loadCfgs(onnxDir), g_textProcessor.get(), g_models.dp.get(),
            g_models.text_enc.get(), g_models.vector_est.get(), g_models.vocoder.get());
        return true;
    } catch (...) {
        g_tts.reset();
        return false;
    }
}

bool NeuralVoice::IsLoaded() {
    std::lock_guard lock(g_mutex);
    return g_tts != nullptr;
}

int NeuralVoice::SampleRate() {
    std::lock_guard lock(g_mutex);
    return g_tts ? g_tts->getSampleRate() : 44100;
}

std::wstring NeuralVoice::VoicesFolder() {
    const std::wstring dir = FindModelDir();
    return dir.empty() ? std::wstring() : dir + L"\\voice_styles";
}

std::vector<std::string> NeuralVoice::Styles() {
    std::vector<std::string> list = { "F1", "F2", "F3", "F4", "F5", "F2+F4", "M1", "M2", "M3", "M4", "M5" };
    const std::wstring folder = VoicesFolder(); // свои файлы голосов из папки voice_styles
    WIN32_FIND_DATAW data;
    const HANDLE find = folder.empty() ? INVALID_HANDLE_VALUE : FindFirstFileW((folder + L"\\*.json").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = data.cFileName;
            name.resize(name.size() - 5);
            const std::string style = Text::ToUtf8(name);
            if (style.find('+') == std::string::npos && std::find(list.begin(), list.end(), style) == list.end()) list.push_back(style);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    return list;
}

std::vector<float> NeuralVoice::Synthesize(const std::string& text, const std::string& styleName, float speed, float pitchSemitones) {
    std::lock_guard lock(g_mutex);
    if (!g_tts) return {};
    const Style* style = GetStyle(styleName);
    if (!style) style = GetStyle("F1");
    if (!style) return {};
    try {
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const float pitch = std::pow(2.0f, std::clamp(pitchSemitones, -4.0f, 4.0f) / 12.0f);
        const int steps = text.size() < kShortPhrase ? kQuickSteps : kQualitySteps;
        auto result = g_tts->call(memory, text, kLanguage, *style, steps, speed / pitch, 0.15f);
        clearTensorBuffers();
        std::vector<float> wav = RaisePitch(result.wav, pitch);
        Polish(wav, g_tts->getSampleRate());
        return wav;
    } catch (...) {
        clearTensorBuffers();
        return {};
    }
}
