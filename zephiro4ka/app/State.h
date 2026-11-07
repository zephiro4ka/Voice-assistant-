#pragma once
// Общее состояние приложения, которое читают и пишут разные потоки.

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

using Clock = uint64_t;
Clock NowMs();

struct AppState {
    std::atomic<bool> running{ true };
    std::atomic<bool> micEnabled{ true };     // F8
    std::atomic<bool> audioReady{ false };    // микрофон и whisper готовы
    std::atomic<bool> capturing{ false };     // сейчас идёт фраза
    std::atomic<bool> transcribing{ false };  // whisper распознаёт
    std::atomic<bool> thinking{ false };      // ИИ сочиняет ответ
    std::atomic<bool> speaking{ false };      // в очереди голоса что-то есть
    std::atomic<bool> pttArmed{ false };      // нажали «говорить» — следующая фраза без «Аня»
    std::atomic<bool> aiReady{ false };
    std::atomic<bool> aiChecked{ false };
    std::atomic<bool> ttsReady{ false };

    std::atomic<Clock> pttArmedAt{ 0 };
    std::atomic<Clock> followUpUntil{ 0 };    // после ответа можно продолжать без «Аня»
    std::atomic<Clock> utteranceEndAt{ 0 };   // конец последней фразы — для замера скорости ответа
    std::atomic<int> lastLatencyMs{ -1 };

    std::atomic<float> micLevel{ 0.0f };      // 0..1, для анимации
    std::atomic<float> mouth{ 0.0f };         // 0..1, «открытость рта» по визимам голоса
    std::atomic<int> cancelGeneration{ 0 };   // ++ = бросить текущий ответ ИИ

    static constexpr int kLevelHistory = 64;
    std::array<std::atomic<float>, kLevelHistory> levelHistory{};
    std::atomic<int> levelIndex{ 0 };

    void PushLevel(float level) { levelHistory[levelIndex.fetch_add(1) & (kLevelHistory - 1)] = level; }
};

extern AppState g_app;

// Подпись под сферой
namespace Caption {
    void Set(const std::string& text, bool live = false);
    void Get(std::string& text, bool& live);
}

// История сообщений на экране
enum class Role { User, Anya, System };
struct ChatMessage {
    Role role;
    std::string text;
};

namespace Chat {
    size_t Add(Role role, const std::string& text);
    void Update(size_t index, const std::string& text);
    void Update(size_t index, Role role, const std::string& text);
    std::vector<ChatMessage> Snapshot();
    void Clear();
}

// Контекст диалога для ИИ
struct ConversationMessage {
    std::string role;
    std::string content;
};

namespace Conversation {
    void Push(const char* role, const std::string& content);
    std::vector<ConversationMessage> Recent();
    void Clear();
}
