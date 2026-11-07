#pragma once
// Нечёткое сравнение того, что услышал whisper, с настоящими названиями:
// «стим» ≈ Steam, «прощал» ≈ «прыщавый», «всё проще чем кажется» ≈ «Всё проще, чем кажется.»

#include <string>
#include <vector>

namespace Fuzzy {
    // Слова «как слышится», латиницей: «Стим» → {"stim"}, «Steam» → {"stim"}
    std::vector<std::string> PhoneticWords(const std::string& utf8);
    std::string Join(const std::vector<std::string>& words);

    // Похожесть 0…1 по расстоянию Левенштейна
    float Similarity(const std::string& a, const std::string& b);
    float Similarity(const std::wstring& a, const std::wstring& b);

    // Насколько название (заголовок видео, ссылки) похоже на сказанное: доля сказанных слов,
    // нашедшихся в названии (с поправкой на ошибки распознавания). 0…1.
    float TitleMatch(const std::string& spoken, const std::string& title);
}
