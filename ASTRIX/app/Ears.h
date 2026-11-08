#pragma once

#include <string>
// Слух Ани: микрофон (miniaudio) → детектор речи → whisper.

namespace Ears {
    void ReloadModel();                                  // сменили модель распознавания
    bool ModelPresent(const std::string& fileName);
    float LastConfidence();              // уверенность в последней фразе, 0…1
    void Bench(const std::string& rawFile); // проверка моделей на записи
    void Start();
    void Shutdown();
}
