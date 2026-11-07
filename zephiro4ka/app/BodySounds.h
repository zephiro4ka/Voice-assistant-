#pragma once
// Кашель и чихание: Аня отвечает «Всё в порядке? Не заболел?» и «Будь здоров!».
// Узнаём двумя способами: по тексту whisper («Кхм», «Апчхи») и по самому звуку —
// кашель короткий, резкий, намного громче фона и «шумный» (не похож на гласные речи).

#include <string>
#include <vector>

namespace BodySounds {
    enum class Kind { None, Cough, Sneeze };

    // Параметры короткого звука
    struct Burst {
        int durationMs = 0;      // сколько длился громкий звук
        int attackMs = 0;        // за сколько дошёл до пика (кашель — почти мгновенно)
        float loudness = 0.0f;   // во сколько раз пик громче фонового шума
        float noisiness = 0.0f;  // доля смены знака сигнала: гласные ~0.05, шипение и кашель — заметно больше
    };

    Burst Analyze(const std::vector<float>& pcm, float noiseLevel);
    Kind Classify(const std::string& heard, const Burst& burst);
    std::string Describe(const Burst& burst);   // для журнала: подбирать пороги под свой микрофон
}
