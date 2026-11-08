#pragma once
// Голос Ани. Два движка:
//   • нейроголос Supertonic-3 — живой и человеческий (по умолчанию);
//   • голоса Windows (SAPI) — запасной вариант, если модели нет.
// Фразы идут через очередь: следующая готовится, пока звучит текущая.

#include <string>
#include <vector>

namespace Voice {
    struct Info {
        std::wstring name;
        std::wstring id;          // "neural:F1" для нейроголоса или id токена SAPI
        bool female = false;
        bool russian = false;
        bool neural = false;
    };

    void Start();
    void Shutdown();

    // Как ответить: 0 — обычно, 1 — тихо (человек говорит тихо), 2 — громко (человек далеко)
    void SetReplyLoudness(int mode);
    void Speak(const std::string& text);   // в очередь; не блокирует
    void Stop();                           // замолчать сразу и очистить очередь
    void ReloadVoice();                    // применить смену голоса в настройках

    std::vector<Info> List();
    // Голос по имени; если пусто — нейроголос, иначе самый подходящий женский русский. nullptr — голосов нет.
    const Info* Choose(const std::vector<Info>& voices, const std::wstring& wanted);
}
