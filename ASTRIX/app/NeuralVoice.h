#pragma once
// Нейросетевой голос Supertonic-3 (Supertone, открытая модель): живой, человеческий, работает без интернета.
// Модель лежит в папке models/supertonic-3 рядом с проектом.

#include <string>
#include <vector>

namespace NeuralVoice {
    bool Load();                                   // загрузить модель (~1–2 с). false — модели нет
    bool IsLoaded();
    int SampleRate();

    // Доступные голоса: F1…F5 и «милый» (смесь F2 и F4). Смесь задаётся через «+»: "F2+F4".
    std::vector<std::string> Styles();
    // Синтез одной фразы. speed: 1.0 — обычно, больше — быстрее.
    // pitchSemitones: на сколько полутонов поднять голос (темп при этом не меняется).
    std::vector<float> Synthesize(const std::string& text, const std::string& style, float speed, float pitchSemitones = 0.0f);
}
