#include "Fuzzy.h"

#include "TextUtils.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

namespace {
    std::string Transliterate(const std::wstring& lower) {
        static const char* kTable[] = { "a", "b", "v", "g", "d", "e", "zh", "z", "i", "i", "k", "l", "m", "n", "o", "p",
                                        "r", "s", "t", "u", "f", "h", "ts", "ch", "sh", "sh", "", "i", "", "e", "yu", "ya" };
        std::string out;
        for (wchar_t ch : lower) {
            if (ch >= L'а' && ch <= L'я') out += kTable[ch - L'а'];
            else if (ch == L'ё') out += "e";
            else if (ch < 128 && std::isalnum(static_cast<unsigned char>(ch))) out += static_cast<char>(ch);
            else out += ' ';
        }
        return out;
    }

    void ReplaceAll(std::string& s, const char* from, const char* to) {
        const size_t n = strlen(from);
        for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + strlen(to))) s.replace(pos, n, to);
    }

    // Русское и английское написание — к общему «как слышится»: steam ≈ стим ≈ stim
    std::string PhoneticWord(std::string w) {
        if (w.size() > 3 && w.back() == 'e') w.pop_back(); // немая e: code → cod, chrome → chrom
        for (auto [from, to] : { std::pair{ "ph", "f" }, { "th", "t" }, { "ck", "k" }, { "qu", "kv" }, { "ea", "i" }, { "ee", "i" },
                                 { "oo", "u" }, { "ai", "i" }, { "ay", "i" }, { "ey", "i" }, { "kh", "h" }, { "dzh", "j" } })
            ReplaceAll(w, from, to);
        std::string out;
        for (char c : w) {
            switch (c) {
            case 'x': out += "ks"; break;
            case 'w': out += 'v'; break;
            case 'c': case 'q': out += 'k'; break;
            case 'y': out += 'i'; break;
            default: out += c;
            }
        }
        out.erase(std::unique(out.begin(), out.end()), out.end()); // двойные буквы
        return out;
    }

    template <class Str>
    float Levenshtein(const Str& a, const Str& b) {
        if (a.empty() || b.empty()) return 0.0f;
        std::vector<size_t> row(b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
        for (size_t i = 1; i <= a.size(); ++i) {
            size_t diagonal = row[0];
            row[0] = i;
            for (size_t j = 1; j <= b.size(); ++j) {
                const size_t up = row[j];
                row[j] = std::min({ row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1) });
                diagonal = up;
            }
        }
        return 1.0f - static_cast<float>(row[b.size()]) / static_cast<float>(std::max(a.size(), b.size()));
    }
}

std::vector<std::string> Fuzzy::PhoneticWords(const std::string& utf8) {
    const std::string latin = Transliterate(Text::LowerRu(Text::ToWide(utf8)));
    std::vector<std::string> words;
    std::string word;
    for (char c : latin + " ") {
        if (c == ' ') {
            if (!word.empty()) words.push_back(PhoneticWord(word));
            word.clear();
        } else {
            word += c;
        }
    }
    return words;
}

std::string Fuzzy::Join(const std::vector<std::string>& words) {
    std::string s;
    for (const auto& w : words) s += w;
    return s;
}

float Fuzzy::Similarity(const std::string& a, const std::string& b) { return Levenshtein(a, b); }
float Fuzzy::Similarity(const std::wstring& a, const std::wstring& b) { return Levenshtein(a, b); }

float Fuzzy::TitleMatch(const std::string& spoken, const std::string& title) {
    const auto said = PhoneticWords(spoken);
    const auto words = PhoneticWords(title);
    if (said.empty() || words.empty()) return 0.0f;

    // каждое сказанное слово ищем в названии: точно, началом слова или просто похожее
    float found = 0.0f, total = 0.0f;
    for (const auto& s : said) {
        const float weight = s.size() <= 2 ? 0.3f : 1.0f; // «и», «в», «на» почти ничего не значат
        float best = 0.0f;
        for (const auto& w : words) {
            if (w == s) { best = 1.0f; break; }
            if (s.size() >= 3 && w.rfind(s, 0) == 0) best = std::max(best, 0.9f);
            best = std::max(best, Similarity(s, w) >= 0.7f ? Similarity(s, w) : 0.0f);
        }
        found += best * weight;
        total += weight;
    }
    const float coverage = found / total;
    // длинные названия, в которых сказанное — лишь малая часть, чуть штрафуем
    const float extra = static_cast<float>(words.size()) / static_cast<float>(said.size() + 4);
    return coverage - std::max(0.0f, extra - 1.0f) * 0.03f;
}
