#pragma once
// Работа с текстом: UTF-8 ⇄ UTF-16, русский нижний регистр, поиск обращения «Аня».

#include <string>

namespace Text {
    std::wstring ToWide(const std::string& utf8);
    std::string ToUtf8(const std::wstring& wide);

    std::wstring LowerRu(std::wstring text);                     // нижний регистр, «ё» → «е»
    bool Contains(const std::string& text, const char* phrase);  // без учёта регистра
    std::string Trim(const std::string& text);                   // убирает пробелы и знаки по краям
    std::string Capitalize(const std::string& text);             // первая буква — заглавная

    // Ищет имя помощницы (и его формы) отдельным словом. command — то, что сказано после (или до) имени.
    bool FindWakeWord(const std::string& text, std::string& command, const std::string& name = "Аня");

    // Имя в падеже: 0 — кто (Аня), 1 — кого нет (Ани), 2 — кому (Ане), 3 — кого вижу (Аню), 4 — с кем (Аней), 5 — о ком (Ане)
    std::string NameForm(const std::string& name, int grammaticalCase);
    // Подставить своё имя вместо «Аня» во всех падежах: «Спроси Аню» → «Спроси Лизу». name == «Аня» — текст как есть
    std::string Brand(const std::string& text, const std::string& name);

    // Типичные «галлюцинации» whisper на тишине: «Продолжение следует…», «Субтитры…» и т.п.
    bool IsWhisperGarbage(const std::string& text);

    // Убирает то, что не стоит произносить вслух: эмодзи, markdown
    std::string CleanForSpeech(const std::string& text);

    // Заменить слово без учёта регистра: «zephiro4ka» → «Зефирочка»
    std::string ReplaceWord(const std::string& text, const std::string& from, const std::string& to);

    // Числа словами для голоса: «Сейчас 13:05» → «Сейчас тринадцать ноль пять»
    std::string NumbersToWords(const std::string& text);

    // Латиница по-русски — для голоса, который не читает английские буквы: «Steam» → «стим», «YouTube» → «ютуб»
    std::string LatinToCyrillic(const std::string& text);

    // Формулы из LaTeX/Markdown — в читаемый текст: «$x^2 \\cdot 4$» → «x² · 4», «\\frac{a}{b}» → «(a)/(b)», «**жирный**» → «жирный»
    std::string PrettyMath(const std::string& text);

    std::string UrlEncode(const std::string& text);
    std::wstring ExecutableDirectory();
}
