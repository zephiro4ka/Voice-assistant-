#include "State.h"

#include "Platform.h"

#include <mutex>

AppState g_app;

Clock NowMs() { return GetTickCount64(); }

// ---------------------------------------------------------------------
namespace {
    std::mutex g_captionMutex;
    std::string g_caption;
    bool g_captionLive = false;

    std::mutex g_chatMutex;
    std::vector<ChatMessage> g_chat;
    constexpr size_t kChatLimit = 200;

    std::mutex g_conversationMutex;
    std::vector<ConversationMessage> g_conversation;
    constexpr size_t kConversationLimit = 16;
}

// ---------------------------------------------------------------------
void Caption::Set(const std::string& text, bool live) {
    std::lock_guard lock(g_captionMutex);
    g_caption = text;
    g_captionLive = live;
}

void Caption::Get(std::string& text, bool& live) {
    std::lock_guard lock(g_captionMutex);
    text = g_caption;
    live = g_captionLive;
}

// ---------------------------------------------------------------------
size_t Chat::Add(Role role, const std::string& text) {
    std::lock_guard lock(g_chatMutex);
    if (g_chat.size() >= kChatLimit) g_chat.erase(g_chat.begin(), g_chat.begin() + kChatLimit / 4);
    g_chat.push_back({ role, text });
    return g_chat.size() - 1;
}

void Chat::Update(size_t index, const std::string& text) {
    std::lock_guard lock(g_chatMutex);
    if (index < g_chat.size()) g_chat[index].text = text;
}

void Chat::Update(size_t index, Role role, const std::string& text) {
    std::lock_guard lock(g_chatMutex);
    if (index < g_chat.size()) g_chat[index] = { role, text };
}

std::vector<ChatMessage> Chat::Snapshot() {
    std::lock_guard lock(g_chatMutex);
    return g_chat;
}

void Chat::Clear() {
    std::lock_guard lock(g_chatMutex);
    g_chat.clear();
}

// ---------------------------------------------------------------------
void Conversation::Push(const char* role, const std::string& content) {
    std::lock_guard lock(g_conversationMutex);
    g_conversation.push_back({ role, content });
    if (g_conversation.size() > kConversationLimit)
        g_conversation.erase(g_conversation.begin(), g_conversation.end() - kConversationLimit);
}

std::vector<ConversationMessage> Conversation::Recent() {
    std::lock_guard lock(g_conversationMutex);
    return g_conversation;
}

void Conversation::Clear() {
    std::lock_guard lock(g_conversationMutex);
    g_conversation.clear();
}
