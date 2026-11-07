#include "BodySounds.h"

#include "Settings.h"
#include "TextUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
    constexpr int kFrame = 320;            // 20 мс при 16 кГц
    constexpr int kFrameMs = 20;

    // Пороги «по звуку». Настраиваются в anya.ini, раздел [sounds], — под свой микрофон
    struct Thresholds {
        int maxDurationMs;
        int maxAttackMs;
        float minLoudness;
        float minNoisiness;
    };

    Thresholds Load() {
        return {
            std::atoi(Config::Value("sounds", "cough_max_ms", "650").c_str()),
            std::atoi(Config::Value("sounds", "cough_attack_ms", "100").c_str()),
            static_cast<float>(std::atof(Config::Value("sounds", "cough_loudness", "15").c_str())),
            static_cast<float>(std::atof(Config::Value("sounds", "cough_noisiness", "0.15").c_str())),
        };
    }

    bool HasAny(const std::wstring& text, std::initializer_list<const wchar_t*> parts) {
        for (const wchar_t* p : parts)
            if (text.find(p) != std::wstring::npos) return true;
        return false;
    }
}

BodySounds::Burst BodySounds::Analyze(const std::vector<float>& pcm, float noiseLevel) {
    Burst burst;
    const float noise = std::max(noiseLevel, 1e-4f);
    std::vector<float> rms, zcr;
    for (size_t start = 0; start + kFrame <= pcm.size(); start += kFrame) {
        double energy = 0.0;
        int crossings = 0;
        for (int i = 0; i < kFrame; ++i) {
            const float v = pcm[start + i];
            energy += static_cast<double>(v) * v;
            if (i > 0 && ((v >= 0.0f) != (pcm[start + i - 1] >= 0.0f))) ++crossings;
        }
        rms.push_back(static_cast<float>(std::sqrt(energy / kFrame)));
        zcr.push_back(static_cast<float>(crossings) / kFrame);
    }
    if (rms.empty()) return burst;

    const size_t peak = static_cast<size_t>(std::max_element(rms.begin(), rms.end()) - rms.begin());
    const float loudLevel = noise * 4.0f;
    size_t first = rms.size(), last = 0;
    double noisy = 0.0;
    int loudFrames = 0;
    for (size_t i = 0; i < rms.size(); ++i) {
        if (rms[i] < loudLevel) continue;
        first = std::min(first, i);
        last = i;
        noisy += zcr[i];
        ++loudFrames;
    }
    if (loudFrames == 0) return burst;

    burst.durationMs = static_cast<int>(last - first + 1) * kFrameMs;
    burst.attackMs = static_cast<int>(peak >= first ? peak - first : 0) * kFrameMs;
    burst.loudness = rms[peak] / noise;
    burst.noisiness = static_cast<float>(noisy / loudFrames);
    return burst;
}

BodySounds::Kind BodySounds::Classify(const std::string& heard, const Burst& burst) {
    const std::wstring text = Text::LowerRu(Text::ToWide(heard));

    // по тексту: whisper иногда прямо пишет «Апчхи!» или «Кхм»
    if (HasAny(text, { L"апчх", L"пчх", L"чхи", L"апчи", L"чихн", L"чихает" })) return Kind::Sneeze;
    if (HasAny(text, { L"кхм", L"кха", L"кхе", L"кхх", L"кх-", L"кх ", L"кашел", L"кашля" }) || text.rfind(L"кх", 0) == 0) return Kind::Cough;

    // по звуку — только если это не похоже на нормальную фразу (одно слово или пусто, и не «Аня»)
    std::string command;
    if (Text::FindWakeWord(heard, command, Config::Get().assistant)) return Kind::None;
    const std::string trimmed = Text::Trim(heard);
    if (std::count(trimmed.begin(), trimmed.end(), ' ') >= 1) return Kind::None;

    const Thresholds t = Load();
    const bool burstLike = burst.durationMs > 0 && burst.durationMs <= t.maxDurationMs && burst.attackMs <= t.maxAttackMs &&
                           burst.loudness >= t.minLoudness && burst.noisiness >= t.minNoisiness;
    if (!burstLike) return Kind::None;
    // чих — «а-ПЧХИ»: в распознанном часто есть «ч» или «х»
    return HasAny(text, { L"ч", L"щ" }) ? Kind::Sneeze : Kind::Cough;
}

std::string BodySounds::Describe(const Burst& b) {
    char buf[160];
    sprintf_s(buf, "звук %d мс, пик за %d мс, громче фона в %.0f раз, шумность %.2f", b.durationMs, b.attackMs, b.loudness, b.noisiness);
    return buf;
}
