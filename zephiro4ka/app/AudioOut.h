#pragma once
// Вывод звука в колонки (miniaudio). Громкость того, что играет, двигает «рот» сферы.

#include <vector>

namespace AudioOut {
    bool Start(int sampleRate);              // открыть устройство воспроизведения (моно, float)
    void Shutdown();

    void Push(const std::vector<float>& samples, int sampleRate); // переведёт в частоту колонок, если другая
    void Clear();                            // замолчать сразу
    bool IsPlaying();                        // ещё есть что играть
}
