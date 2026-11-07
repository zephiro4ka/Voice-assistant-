#include "AudioOut.h"

#include "State.h"

#include "../miniaudio.h" // реализация miniaudio собрана в Ears.cpp

#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>

namespace {
    ma_device g_device;
    bool g_started = false;
    int g_deviceRate = 48000;
    std::mutex g_mutex;
    std::deque<float> g_buffer;

    void OnPlayback(ma_device*, void* output, const void*, ma_uint32 frames) {
        float* out = static_cast<float*>(output);
        double energy = 0.0;
        {
            std::lock_guard lock(g_mutex);
            const size_t available = std::min<size_t>(frames, g_buffer.size());
            for (size_t i = 0; i < available; ++i) {
                out[i] = g_buffer.front();
                g_buffer.pop_front();
                energy += static_cast<double>(out[i]) * out[i];
            }
            std::fill(out + available, out + frames, 0.0f);
        }
        // «рот» открывается по громкости речи, плавно закрывается в паузах
        const float level = std::min(1.0f, static_cast<float>(std::sqrt(energy / frames)) * 7.0f);
        const float mouth = g_app.mouth.load();
        g_app.mouth = level > mouth ? level : mouth * 0.82f;
    }
}

bool AudioOut::Start(int sampleRate) {
    if (g_started) return true;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = static_cast<ma_uint32>(sampleRate);
    g_deviceRate = sampleRate;
    config.dataCallback = OnPlayback;
    if (ma_device_init(nullptr, &config, &g_device) != MA_SUCCESS) return false;
    if (ma_device_start(&g_device) != MA_SUCCESS) {
        ma_device_uninit(&g_device);
        return false;
    }
    g_started = true;
    return true;
}

void AudioOut::Shutdown() {
    if (!g_started) return;
    ma_device_uninit(&g_device);
    g_started = false;
}

void AudioOut::Push(const std::vector<float>& samples, int sampleRate) {
    if (sampleRate == g_deviceRate || samples.empty()) {
        std::lock_guard lock(g_mutex);
        g_buffer.insert(g_buffer.end(), samples.begin(), samples.end());
        return;
    }
    // другая частота (Supertonic — 44,1 кГц, Silero — 48 кГц): линейная интерполяция
    const double step = static_cast<double>(sampleRate) / g_deviceRate;
    const size_t count = static_cast<size_t>(samples.size() / step);
    std::vector<float> converted(count);
    for (size_t i = 0; i < count; ++i) {
        const double pos = i * step;
        const size_t k = static_cast<size_t>(pos);
        const float t = static_cast<float>(pos - k);
        const float a = samples[std::min(k, samples.size() - 1)], b = samples[std::min(k + 1, samples.size() - 1)];
        converted[i] = a + (b - a) * t;
    }
    std::lock_guard lock(g_mutex);
    g_buffer.insert(g_buffer.end(), converted.begin(), converted.end());
}

void AudioOut::Clear() {
    std::lock_guard lock(g_mutex);
    g_buffer.clear();
    g_app.mouth = 0.0f;
}

bool AudioOut::IsPlaying() {
    std::lock_guard lock(g_mutex);
    return !g_buffer.empty();
}
