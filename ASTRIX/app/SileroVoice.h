#pragma once
// Голос Silero (v5_6_ru): самый живой из локальных русских голосов и очень быстрый — ~0,3 с на фразу.
// Работает через маленький сервер на Python (third_party/silero), который Аня запускает скрыто и закрывает вместе с собой.
// Тот же сервер распознаёт речь моделью GigaAM-v3 (если установлен пакет onnx-asr) — точнее и быстрее whisper.

#include <optional>
#include <string>
#include <vector>

namespace SileroVoice {
    constexpr int kSampleRate = 48000;

    void Start();       // в фоне: запустить сервер и загрузить модель (~10 с)
    bool IsReady();

    // Женские голоса: xenia, kseniya, baya
    std::vector<std::string> Speakers();
    std::vector<float> Synthesize(const std::string& text, const std::string& speaker);

    // Распознавание речи (звук 16 кГц, моно). Пусто — сервер не может (тогда распознаёт whisper).
    bool AsrReady();
    std::optional<std::string> Recognize(const std::vector<float>& pcm16k);
}
