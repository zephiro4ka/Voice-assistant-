#include <vector>
#include <iterator>
#include "TextUtils.h"

#include "Platform.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <utility>
#include <cstdio>

std::wstring Text::ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    return wide;
}

std::string Text::ToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::wstring Text::LowerRu(std::wstring text) {
    if (text.empty()) return text;
    const int size = LCMapStringEx(L"ru-RU", LCMAP_LOWERCASE, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr, 0);
    if (size > 0) {
        std::wstring lowered(size, L'\0');
        if (LCMapStringEx(L"ru-RU", LCMAP_LOWERCASE, text.c_str(), static_cast<int>(text.size()), lowered.data(), size, nullptr, nullptr, 0) > 0)
            text.swap(lowered);
    }
    for (wchar_t& ch : text)
        if (ch == L'ё') ch = L'е';
    return text;
}

bool Text::Contains(const std::string& text, const char* phrase) {
    return LowerRu(ToWide(text)).find(LowerRu(ToWide(phrase))) != std::wstring::npos;
}

std::string Text::Trim(const std::string& text) {
    // Работаем с целыми символами: в UTF-8 «»» и «л» делят байт 0xBB, и побайтовая обрезка портила слова на «л»
    static const wchar_t* kLeading = L" \t\r\n,.-!?:;\"«»";
    static const wchar_t* kTrailing = L" \t\r\n,-:;\"«»";
    const std::wstring wide = ToWide(text);
    const size_t first = wide.find_first_not_of(kLeading);
    if (first == std::wstring::npos) return {};
    const size_t last = wide.find_last_not_of(kTrailing);
    return ToUtf8(wide.substr(first, last - first + 1));
}

std::string Text::Capitalize(const std::string& text) {
    std::wstring wide = ToWide(text);
    if (!wide.empty()) CharUpperBuffW(wide.data(), 1);
    return ToUtf8(wide);
}

std::string Text::NameForm(const std::string& name, int c) {
    std::wstring w = ToWide(Trim(name));
    if (w.size() < 2 || c <= 0 || c > 5) return ToUtf8(w);
    const std::wstring stem = w.substr(0, w.size() - 1);
    const wchar_t last = LowerRu(std::wstring(1, w.back()))[0], before = LowerRu(std::wstring(1, w[w.size() - 2]))[0];
    const bool hush = wcschr(L"гкхжшщч", before) != nullptr, hushIns = wcschr(L"жшщчц", before) != nullptr;
    const bool latin = last < 0x400;
    if (latin) return ToUtf8(w);                                     // «Jarvis» — не склоняем
    static const wchar_t* kYa[] = { L"", L"и", L"е", L"ю", L"ей", L"е" };   // Аня → Ани, Ане, Аню, Аней
    static const wchar_t* kA[] = { L"", L"ы", L"е", L"у", L"ой", L"е" };    // Лиза → Лизы, Лизе, Лизу, Лизой
    static const wchar_t* kJ[] = { L"", L"я", L"ю", L"я", L"ем", L"е" };    // Андрей → Андрея
    static const wchar_t* kC[] = { L"", L"а", L"у", L"а", L"ом", L"е" };    // Макс → Макса
    if (last == L'я') return ToUtf8(stem + kYa[c]);
    if (last == L'а') {
        std::wstring end = kA[c];
        if (c == 1 && hush) end = L"и";
        if (c == 4 && hushIns) end = L"ей";
        return ToUtf8(stem + end);
    }
    if (last == L'й') return ToUtf8(stem + kJ[c]);
    if (wcschr(L"бвгджзклмнпрстфхцчшщ", last)) return ToUtf8(w + kC[c]);
    return ToUtf8(w); // «Ассоль», «Нео» — как есть
}

std::string Text::Brand(const std::string& text, const std::string& name) {
    if (name.empty() || name == "Аня" || text.find("\xD0\x90\xD0\xBD") == std::string::npos && text.find("Ан") == std::string::npos) return text;
    static const char* kForms[] = { "Аня", "Ани", "Ане", "Аню", "Аней" };
    static const int kCases[] = { 0, 1, 2, 3, 4 };
    std::string out = text;
    for (int i = 0; i < 5; ++i) out = ReplaceWord(out, kForms[i], NameForm(name, kCases[i]));
    return out;
}

bool Text::FindWakeWord(const std::string& text, std::string& command, const std::string& name) {
    static const wchar_t* kAnya[] = { L"анечка", L"анюта", L"анька", L"аня", L"ань", L"аню", L"ане", L"anya", L"ania" };
    const bool anya = name.empty() || name == "Аня";
    std::vector<std::wstring> forms;
    if (anya) forms.assign(std::begin(kAnya), std::end(kAnya));
    else
        for (int c = 0; c <= 5; ++c) {
            const std::wstring form = LowerRu(ToWide(NameForm(name, c)));
            if (std::find(forms.begin(), forms.end(), form) == forms.end()) forms.push_back(form);
        }
    std::sort(forms.begin(), forms.end(), [](const std::wstring& a, const std::wstring& b) { return a.size() > b.size(); }); // длинные раньше
    const std::wstring wide = ToWide(text);
    const std::wstring lower = LowerRu(wide);

    for (const std::wstring& formText : forms) {
        const wchar_t* form = formText.c_str();
        const size_t length = formText.size();
        for (size_t pos = lower.find(form); pos != std::wstring::npos; pos = lower.find(form, pos + length)) {
            const bool leftOk = pos == 0 || !IsCharAlphaW(lower[pos - 1]);
            const bool rightOk = pos + length >= lower.size() || !IsCharAlphaW(lower[pos + length]);
            if (!leftOk || !rightOk) continue;

            const std::string after = Trim(ToUtf8(wide.substr(pos + length)));
            const std::string before = Trim(ToUtf8(wide.substr(0, pos)));
            command = !after.empty() ? after : before; // «привет, Аня» → «привет»
            return true;
        }
    }

    if (!anya) return false;
    // Whisper иногда слышит «Аня» как «Они», «Ани», «Анна» — но только в самом начале фразы это обращение
    static const wchar_t* kMisheard[] = { L"они", L"оня", L"ани", L"анна", L"аня", L"ая", L"оне" };
    size_t start = 0;
    while (start < lower.size() && !IsCharAlphaW(lower[start])) ++start;
    size_t end = start;
    while (end < lower.size() && IsCharAlphaW(lower[end])) ++end;
    const std::wstring first = lower.substr(start, end - start);
    for (const wchar_t* form : kMisheard) {
        if (first != form) continue;
        command = Trim(ToUtf8(wide.substr(end)));
        return true;
    }
    return false;
}

bool Text::IsWhisperGarbage(const std::string& text) {
    const std::string t = Trim(text);
    if (t.size() < 2) return true;

    static const char* kPhrases[] = {
        "субтитр", "продолжение следует", "спасибо за просмотр", "подписывайтесь", "редактор",
        "корректор", "dimatorzok", "amara.org", "музыкальная заставка", "ставьте лайк",
    };
    for (const char* phrase : kPhrases)
        if (Contains(t, phrase)) return true;

    const char a = t.front(), b = t.back();
    if ((a == '[' && b == ']') || (a == '(' && b == ')') || (a == '*' && b == '*')) return true;

    // Зацикливание: одна буква 6+ раз подряд («Ооооооо») или одно слово 5+ раз подряд («та-та-та-та-та»)
    {
        const std::wstring w = LowerRu(ToWide(t));
        int run = 1;
        for (size_t i = 1; i < w.size(); ++i) {
            run = (w[i] == w[i - 1] && IsCharAlphaW(w[i])) ? run + 1 : 1;
            if (run >= 6) return true;
        }
        std::wstring word, previous;
        int repeats = 1;
        for (wchar_t ch : w + L" ") {
            if (IsCharAlphaW(ch)) { word.push_back(ch); continue; }
            if (word.empty()) continue;
            repeats = (word == previous) ? repeats + 1 : 1;
            if (repeats >= 5) return true;
            previous = word;
            word.clear();
        }
    }

    // Кашель, вздохи и междометия: «Кхм.», «Кха-кха», «Апчхи!», «Э-э-э»
    static const wchar_t* kNoises[] = { L"кх", L"кхм", L"кха", L"кхе", L"кхэ", L"хм", L"хмм", L"м", L"мм", L"ммм", L"а", L"э", L"эм",
                                        L"ээ", L"эээ", L"ох", L"ах", L"ух", L"уф", L"апчхи", L"чхи", L"тьфу", L"кашель", L"кашляет",
                                        L"кашляю", L"смех", L"вздох", L"о", L"ой", L"ну" };
    const std::wstring lower = LowerRu(ToWide(t));
    std::wstring word;
    bool allNoise = true, anyVowel = false;
    auto checkWord = [&] {
        if (word.empty()) return;
        bool known = false;
        for (const wchar_t* noise : kNoises) known |= word == noise;
        allNoise &= known;
        word.clear();
    };
    for (wchar_t ch : lower) {
        if (IsCharAlphaW(ch)) {
            word.push_back(ch);
            if (wcschr(L"аеиоуыэюяaeiouy", ch)) anyVowel = true;
        } else {
            checkWord(); // пробел, дефис, знаки — разделители: «кха-кха» = «кха» + «кха»
        }
    }
    checkWord();
    const bool hasDigits = std::any_of(lower.begin(), lower.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
    return !hasDigits && (allNoise || !anyVowel); // «375×124» — пример, а не шум
}

std::string Text::CleanForSpeech(const std::string& text) {
    const std::wstring wide = ToWide(text);
    std::wstring out;
    out.reserve(wide.size());
    for (wchar_t ch : wide) {
        const bool emoji = (ch >= 0xD800 && ch <= 0xDFFF) || (ch >= 0x2600 && ch <= 0x27BF) || ch == 0xFE0F;
        const bool markdown = ch == L'*' || ch == L'#' || ch == L'`' || ch == L'_' || ch == L'~' || ch == L'|' || ch == L'>';
        if (!emoji && !markdown) out.push_back(ch);
    }
    return Trim(ToUtf8(out));
}

namespace {
    // 0…999 999 999 словами (мужской род; «одна тысяча», «две тысячи»)
    std::string SayNumber(unsigned long long n) {
        static const char* kOnes[] = { "", "один", "два", "три", "четыре", "пять", "шесть", "семь", "восемь", "девять",
                                       "десять", "одиннадцать", "двенадцать", "тринадцать", "четырнадцать", "пятнадцать",
                                       "шестнадцать", "семнадцать", "восемнадцать", "девятнадцать" };
        static const char* kTens[] = { "", "", "двадцать", "тридцать", "сорок", "пятьдесят", "шестьдесят", "семьдесят", "восемьдесят", "девяносто" };
        static const char* kHundreds[] = { "", "сто", "двести", "триста", "четыреста", "пятьсот", "шестьсот", "семьсот", "восемьсот", "девятьсот" };
        if (n == 0) return "ноль";

        auto triple = [&](unsigned n3, bool feminine) {
            std::string out;
            auto add = [&](const char* w) { if (*w) out += (out.empty() ? "" : " ") + std::string(w); };
            add(kHundreds[n3 / 100]);
            const unsigned rest = n3 % 100;
            if (rest < 20) {
                if (feminine && rest == 1) add("одна");
                else if (feminine && rest == 2) add("две");
                else add(kOnes[rest]);
            } else {
                add(kTens[rest / 10]);
                const unsigned one = rest % 10;
                add(feminine && one == 1 ? "одна" : feminine && one == 2 ? "две" : kOnes[one]);
            }
            return out;
        };
        auto plural = [](unsigned n3, const char* one, const char* few, const char* many) {
            const unsigned r100 = n3 % 100, r10 = n3 % 10;
            if (r100 >= 11 && r100 <= 14) return many;
            return r10 == 1 ? one : (r10 >= 2 && r10 <= 4) ? few : many;
        };

        std::string out;
        auto append = [&](const std::string& part) { if (!part.empty()) out += (out.empty() ? "" : " ") + part; };
        const unsigned millions = static_cast<unsigned>(n / 1000000 % 1000), thousands = static_cast<unsigned>(n / 1000 % 1000), units = static_cast<unsigned>(n % 1000);
        if (millions) append(triple(millions, false) + " " + plural(millions, "миллион", "миллиона", "миллионов"));
        if (thousands) append(triple(thousands, true) + " " + plural(thousands, "тысяча", "тысячи", "тысяч"));
        append(triple(units, false));
        return out;
    }
}

std::string Text::ReplaceWord(const std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return text;
    std::wstring wide = ToWide(text);
    const std::wstring needle = LowerRu(ToWide(from)), replacement = ToWide(to);
    for (size_t pos = LowerRu(wide).find(needle); pos != std::wstring::npos; pos = LowerRu(wide).find(needle, pos + replacement.size()))
        wide.replace(pos, needle.size(), replacement);
    return ToUtf8(wide);
}

std::string Text::NumbersToWords(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            out += text[i++];
            continue;
        }
        size_t j = i;
        while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) ++j;
        const bool insideWord = (i > 0 && std::isalpha(static_cast<unsigned char>(text[i - 1]))) ||
                                (j < text.size() && std::isalpha(static_cast<unsigned char>(text[j])));
        if (insideWord) { // «zephiro4ka», «mp3» — часть слова, а не число
            out += text.substr(i, j - i);
            i = j;
            continue;
        }
        const std::string digits = text.substr(i, j - i);
        if (digits.size() > 9) out += digits; // слишком длинное — пусть читает как есть
        else {
            // «05» в минутах: «ноль пять»
            if (digits.size() == 2 && digits[0] == '0') out += "ноль " + SayNumber(digits[1] - '0');
            else out += SayNumber(std::stoull(digits));
        }
        if (j < text.size() && text[j] == ':' && j + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[j + 1]))) {
            out += ' '; // 13:05 → «тринадцать ноль пять»
            ++j;
        }
        i = j;
    }
    return out;
}

std::string Text::LatinToCyrillic(const std::string& text) {
    // известные слова — как их произносят
    static const std::pair<const wchar_t*, const wchar_t*> kWords[] = {
        { L"youtube", L"ютуб" }, { L"google", L"гугл" }, { L"steam", L"стим" }, { L"discord", L"дискорд" }, { L"telegram", L"телеграм" },
        { L"chrome", L"хром" }, { L"edge", L"эдж" }, { L"windows", L"виндоус" }, { L"github", L"гитхаб" }, { L"ok", L"окей" },
        { L"chatgpt", L"чат джи пи ти" }, { L"gpt", L"джи пи ти" }, { L"ai", L"эй ай" }, { L"wi-fi", L"вай-фай" }, { L"wifi", L"вайфай" },
        { L"zenless", L"зенлес" }, { L"zone", L"зон" }, { L"zero", L"зиро" }, { L"lm", L"эл эм" }, { L"studio", L"студио" },
        { L"obs", L"о-бэ-эс" }, { L"cursor", L"курсор" }, { L"roblox", L"роблокс" }, { L"blender", L"блендер" }, { L"minecraft", L"майнкрафт" },
    };
    static const std::pair<const wchar_t*, const wchar_t*> kSounds[] = { // буквосочетания — до одиночных букв
        { L"sch", L"ш" }, { L"sh", L"ш" }, { L"ch", L"ч" }, { L"th", L"т" }, { L"ph", L"ф" }, { L"ck", L"к" }, { L"oo", L"у" },
        { L"ee", L"и" }, { L"ea", L"и" }, { L"ou", L"ау" }, { L"qu", L"кв" }, { L"ya", L"я" }, { L"yu", L"ю" }, { L"yo", L"ё" }, { L"zh", L"ж" },
    };
    static const wchar_t* kLetters[26] = { L"а", L"б", L"к", L"д", L"е", L"ф", L"г", L"х", L"и", L"дж", L"к", L"л", L"м",
                                           L"н", L"о", L"п", L"к", L"р", L"с", L"т", L"у", L"в", L"в", L"кс", L"й", L"з" };
    const std::wstring wide = ToWide(text);
    std::wstring out;
    for (size_t i = 0; i < wide.size();) {
        if (!(wide[i] < 128 && std::isalpha(static_cast<unsigned char>(wide[i])))) { out.push_back(wide[i++]); continue; }
        size_t end = i;
        while (end < wide.size() && wide[end] < 128 && (std::isalpha(static_cast<unsigned char>(wide[end])) || wide[end] == L'-')) ++end;
        std::wstring word = wide.substr(i, end - i);
        for (wchar_t& ch : word) ch = static_cast<wchar_t>(std::towlower(ch));
        i = end;

        bool known = false;
        for (const auto& [latin, russian] : kWords)
            if (word == latin) { out += russian; known = true; break; }
        if (known) continue;
        for (size_t k = 0; k < word.size();) {
            bool matched = false;
            for (const auto& [latin, russian] : kSounds) {
                const size_t n = wcslen(latin);
                if (word.compare(k, n, latin) == 0) { out += russian; k += n; matched = true; break; }
            }
            if (matched) continue;
            const wchar_t ch = word[k++];
            if (ch >= L'a' && ch <= L'z') out += kLetters[ch - L'a'];
            else out.push_back(ch);
        }
    }
    return ToUtf8(out);
}

std::string Text::PrettyMath(const std::string& input) {
    std::string s = input;
    auto replaceAll = [&](const std::string& from, const std::string& to) {
        for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) s.replace(pos, from.size(), to);
    };
    // \frac{A}{B} и \sqrt{X} — по одной паре скобок (вложенные раскрываются за несколько проходов)
    auto braced = [&](size_t open, size_t& close) {
        if (open >= s.size() || s[open] != '{') return false;
        int depth = 0;
        for (close = open; close < s.size(); ++close) {
            if (s[close] == '{') ++depth;
            if (s[close] == '}' && --depth == 0) return true;
        }
        return false;
    };
    for (int pass = 0; pass < 4; ++pass) {
        for (size_t pos; (pos = s.find("\\frac")) != std::string::npos;) {
            size_t a = 0, b = 0;
            if (!braced(pos + 5, a) || !braced(a + 1, b)) { s.erase(pos, 5); continue; }
            const std::string top = s.substr(pos + 6, a - pos - 6), bottom = s.substr(a + 2, b - a - 2);
            s.replace(pos, b - pos + 1, "(" + top + ")/(" + bottom + ")");
        }
        for (size_t pos; (pos = s.find("\\sqrt")) != std::string::npos;) {
            size_t a = 0;
            if (!braced(pos + 5, a)) { s.replace(pos, 5, "√"); continue; }
            s.replace(pos, a - pos + 1, "√(" + s.substr(pos + 6, a - pos - 6) + ")");
        }
        for (size_t pos; (pos = s.find("\\text{")) != std::string::npos;) {
            size_t a = 0;
            if (!braced(pos + 5, a)) break;
            s.replace(pos, a - pos + 1, s.substr(pos + 6, a - pos - 6));
        }
    }
    static const std::pair<const char*, const char*> kSymbols[] = {
        // порядок важен: длинные команды раньше коротких («\left» раньше «\le», «\rightarrow» раньше «\right»)
        { "\\Leftrightarrow", "⇔" }, { "\\Rightarrow", "⇒" }, { "\\rightarrow", "→" }, { "\\left", "" }, { "\\right", "" },
        { "\\cdot", "·" }, { "\\times", "×" }, { "\\div", "÷" }, { "\\pm", "±" }, { "\\mp", "∓" }, { "\\leq", "≤" }, { "\\geq", "≥" },
        { "\\le", "≤" }, { "\\ge", "≥" }, { "\\neq", "≠" }, { "\\ne", "≠" }, { "\\approx", "≈" }, { "\\infty", "∞" }, { "\\Rightarrow", "⇒" },
        { "\\rightarrow", "→" }, { "\\to", "→" }, { "\\Leftrightarrow", "⇔" }, { "\\in", "∈" }, { "\\mathbb{R}", "ℝ" }, { "\\alpha", "α" },
        { "\\beta", "β" }, { "\\pi", "π" }, { "\\Delta", "Δ" }, { "\\left", "" }, { "\\right", "" }, { "\\quad", " " }, { "\\,", " " },
        { "\\;", " " }, { "\\!", "" }, { "\\{", "{" }, { "\\}", "}" }, { "\\(", "" }, { "\\)", "" }, { "\\[", "" }, { "\\]", "" },
        { "$$", "" }, { "$", "" }, { "**", "" }, { "^{2}", "²" }, { "^{3}", "³" }, { "^2", "²" }, { "^3", "³" }, { "_1", "₁" }, { "_2", "₂" },
        { "\\boxed", "" }, { "### ", "" }, { "## ", "" },
    };
    for (const auto& [from, to] : kSymbols) replaceAll(from, to);
    replaceAll("{", "");
    replaceAll("}", "");
    return s;
}

std::string Text::UrlEncode(const std::string& text) {
    std::string out;
    for (unsigned char ch : text) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') out.push_back(static_cast<char>(ch));
        else if (ch == ' ') out.push_back('+');
        else {
            char hex[4];
            sprintf_s(hex, "%%%02X", ch);
            out += hex;
        }
    }
    return out;
}

std::wstring Text::ExecutableDirectory() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return L".";
    std::wstring dir(path, length);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash);
    return dir;
}
