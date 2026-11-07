#include "Voice.h"

#include "Platform.h"
#include "AudioOut.h"
#include "NeuralVoice.h"
#include "SileroVoice.h"
#include "Settings.h"
#include "State.h"
#include "TextUtils.h"

#include <sapi.h>
#include <sphelper.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#pragma comment(lib, "ole32.lib")

namespace {
    std::thread g_thread;
    std::mutex g_mutex;
    std::condition_variable g_cv;
    std::deque<std::string> g_queue;
    std::atomic<bool> g_stop{ false };
    std::atomic<bool> g_voiceDirty{ true };
    std::atomic<bool> g_failed{ false };

    std::mutex g_voicesMutex;
    std::vector<Voice::Info> g_voices;

    template <class T>
    struct ComPtr {
        T* p = nullptr;
        ~ComPtr() { if (p) p->Release(); }
        T** operator&() { return &p; }
        T* operator->() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    bool QueueEmpty() {
        std::lock_guard lock(g_mutex);
        return g_queue.empty();
    }

    // Засечь время от конца фразы пользователя до первого звука ответа
    void MarkFirstSound() {
        if (const Clock endedAt = g_app.utteranceEndAt.exchange(0))
            g_app.lastLatencyMs = static_cast<int>(NowMs() - endedAt);
    }

    // ================================================================ нейроголос
    void SpeakNeural(const std::string& text, const std::string& style) {
        const Settings cfg = Config::Get();
        const float speed = 1.0f + cfg.rate * 0.06f;
        const float semitones = cfg.neuralPitch / 3.0f; // 0 — естественный голос; каждые 3 деления — полутон
        std::vector<float> wav = NeuralVoice::Synthesize(Text::NumbersToWords(text), style, speed, semitones);
        if (wav.empty() || g_stop) return;
        MarkFirstSound();
        AudioOut::Push(wav, NeuralVoice::SampleRate()); // играет в фоне — тем временем готовим следующую фразу
    }

    // ================================================================ Silero
    // false — Silero ещё не готов или не ответил (тогда скажем другим голосом)
    bool SpeakSilero(const std::string& text, const std::string& speaker) {
        if (!SileroVoice::IsReady()) return false;
        // Silero не читает цифры и латиницу: «в 15:10 открою Steam» → «в пятнадцать десять открою стим»
        std::vector<float> wav = SileroVoice::Synthesize(Text::LatinToCyrillic(Text::NumbersToWords(text)), speaker);
        if (wav.empty()) return false;
        if (g_stop) return true;
        MarkFirstSound();
        AudioOut::Push(wav, SileroVoice::kSampleRate);
        return true;
    }

    // ================================================================ голоса Windows (SAPI)
    constexpr float kVisemeOpenness[22] = { 0.0f, 0.85f, 1.0f, 0.9f, 0.7f, 0.6f, 0.5f, 0.45f, 0.8f, 0.95f, 0.8f,
                                            0.9f, 0.35f, 0.4f, 0.3f, 0.3f, 0.3f, 0.35f, 0.2f, 0.4f, 0.3f, 0.1f };

    std::wstring XmlEscape(const std::wstring& s) {
        std::wstring out;
        for (wchar_t c : s) {
            if (c == L'&') out += L"&amp;";
            else if (c == L'<') out += L"&lt;";
            else if (c == L'>') out += L"&gt;";
            else out.push_back(c);
        }
        return out;
    }

    std::wstring ReadAttribute(ISpObjectToken* token, const wchar_t* name) {
        ComPtr<ISpDataKey> attributes;
        if (FAILED(token->OpenKey(L"Attributes", &attributes)) || !attributes) return {};
        WCHAR* value = nullptr;
        if (FAILED(attributes->GetStringValue(name, &value)) || !value) return {};
        std::wstring result = value;
        CoTaskMemFree(value);
        return result;
    }

    void EnumerateSapi(const wchar_t* category, std::vector<Voice::Info>& out) {
        ComPtr<IEnumSpObjectTokens> tokens;
        if (FAILED(SpEnumTokens(category, nullptr, nullptr, &tokens)) || !tokens) return;
        ULONG count = 0;
        tokens->GetCount(&count);
        for (ULONG i = 0; i < count; ++i) {
            ComPtr<ISpObjectToken> token;
            if (FAILED(tokens->Item(i, &token)) || !token) continue;
            Voice::Info info;
            if (WCHAR* text = nullptr; SUCCEEDED(SpGetDescription(token.p, &text)) && text) { info.name = text; CoTaskMemFree(text); }
            if (WCHAR* id = nullptr; SUCCEEDED(token->GetId(&id)) && id) { info.id = id; CoTaskMemFree(id); }
            info.female = _wcsicmp(ReadAttribute(token.p, L"Gender").c_str(), L"Female") == 0;
            info.russian = ReadAttribute(token.p, L"Language").find(L"419") != std::wstring::npos;
            const std::wstring lower = Text::LowerRu(info.name);
            if (lower.find(L"russian") != std::wstring::npos) info.russian = true;
            for (const wchar_t* girl : { L"irina", L"svetlana", L"dariya", L"ирина" })
                if (lower.find(girl) != std::wstring::npos) info.female = true;
            const bool duplicate = std::any_of(out.begin(), out.end(), [&](const Voice::Info& v) { return v.name == info.name; });
            if (!info.name.empty() && !duplicate) out.push_back(info);
        }
    }

    void SpeakSapi(ISpVoice* voice, const std::string& text, const Voice::Info* chosen) {
        if (!voice) return;
        if (g_voiceDirty.exchange(false) && chosen && !chosen->neural) {
            ComPtr<ISpObjectToken> token;
            if (SUCCEEDED(SpGetTokenFromId(chosen->id.c_str(), &token)) && token) voice->SetVoice(token.p);
        }
        const Settings cfg = Config::Get();
        voice->SetRate(cfg.rate);
        MarkFirstSound();

        const std::wstring xml = L"<pitch absmiddle=\"" + std::to_wstring(cfg.pitch) + L"\">" + XmlEscape(Text::ToWide(text)) + L"</pitch>";
        voice->Speak(xml.c_str(), SPF_ASYNC | SPF_IS_XML, nullptr);
        const HANDLE done = voice->SpeakCompleteEvent();
        for (;;) {
            const DWORD wait = WaitForSingleObject(done, 10);
            CSpEvent ev;
            while (ev.GetFrom(voice) == S_OK) {
                if (ev.eEventId == SPEI_VISEME) {
                    const int viseme = LOWORD(ev.lParam);
                    g_app.mouth = (viseme >= 0 && viseme < 22) ? kVisemeOpenness[viseme] : 0.5f;
                } else if (ev.eEventId == SPEI_WORD_BOUNDARY) {
                    g_app.mouth = std::max(g_app.mouth.load(), 0.7f);
                }
            }
            if (g_stop || !g_app.running) {
                voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
                break;
            }
            if (wait == WAIT_OBJECT_0) break;
        }
        g_app.mouth = 0.0f;
    }

    // ================================================================ поток голоса
    // Дождаться, пока доиграет нейроголос (или пока не появится новая фраза / не попросят замолчать)
    void WaitPlaybackOrNext() {
        while (AudioOut::IsPlaying() && !g_stop && g_app.running && QueueEmpty()) Sleep(15);
        if (g_stop) AudioOut::Clear();
    }

    void RunLoop() {
        ComPtr<ISpVoice> sapi;
        CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, reinterpret_cast<void**>(&sapi));
        if (sapi) {
            const ULONGLONG interest = SPFEI(SPEI_VISEME) | SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_END_INPUT_STREAM);
            sapi->SetInterest(interest, interest);
        }
        SileroVoice::Start(); // грузится в фоне ~10 с; пока не готов — говорим голосом Windows
        const bool speakers = AudioOut::Start(SileroVoice::kSampleRate);
        // Supertonic (~400 МБ памяти) грузим, только если его выбрали в настройках
        bool neuralLoaded = false;
        auto neuralReady = [&] {
            if (!neuralLoaded && speakers) neuralLoaded = NeuralVoice::Load();
            return neuralLoaded;
        };
        if (Config::Get().voice.rfind(L"neural:", 0) == 0 && neuralReady()) NeuralVoice::Synthesize("Привет", "F1", 1.0f, 0.0f); // прогрев

        {
            std::vector<Voice::Info> voices;
            if (speakers) { // Silero — самые живые голоса, поэтому первыми (и по умолчанию)
                static const std::pair<const char*, const wchar_t*> kSilero[] = {
                    { "xenia", L"Silero — Ксения (живой)" }, { "kseniya", L"Silero — Ксения 2 (живой)" }, { "baya", L"Silero — Байя (живой)" } };
                for (const auto& [speaker, name] : kSilero) voices.push_back({ name, L"silero:" + Text::ToWide(speaker), true, true, true });
            }
            if (speakers) { // в списке — всегда; модель загрузится, когда голос выберут
                for (const auto& style : NeuralVoice::Styles()) {
                    const bool blend = style.find('+') != std::string::npos;
                    const std::wstring name = blend ? L"Нейроголос — милый" : L"Нейроголос " + Text::ToWide(style.substr(1)) + L" (живой)";
                    voices.push_back({ name, L"neural:" + Text::ToWide(style), true, true, true });
                }
            }
            EnumerateSapi(SPCAT_VOICES, voices); // классические голоса Windows
            EnumerateSapi(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech_OneCore\\Voices", voices);
            std::lock_guard lock(g_voicesMutex);
            g_voices = std::move(voices);
        }
        if (!speakers && !sapi) {
            Chat::Add(Role::System, "Не удалось запустить ни один голос.");
            g_failed = true;
            g_app.speaking = false;
            return;
        }
        g_app.ttsReady = true;

        while (g_app.running) {
            std::string text;
            {
                std::unique_lock lock(g_mutex);
                g_cv.wait(lock, [] { return !g_queue.empty() || g_stop || !g_app.running; });
                if (!g_app.running) break;
                if (g_stop) {
                    g_stop = false;
                    if (g_queue.empty()) {
                        g_app.speaking = false;
                        continue;
                    }
                }
                text = std::move(g_queue.front());
                g_queue.pop_front();
            }

            Voice::Info chosen;
            {
                std::lock_guard lock(g_voicesMutex);
                if (const Voice::Info* v = Voice::Choose(g_voices, Config::Get().voice)) chosen = *v;
            }
            const bool silero = chosen.id.rfind(L"silero:", 0) == 0;
            if (silero && SpeakSilero(text, Text::ToUtf8(chosen.id.substr(7)))) {
                // сказали голосом Silero
            } else if (chosen.id.rfind(L"neural:", 0) == 0 && neuralReady()) {
                SpeakNeural(text, Text::ToUtf8(chosen.id.substr(7)));
            } else {
                SpeakSapi(sapi.p, text, &chosen);
            }

            if (QueueEmpty()) WaitPlaybackOrNext();
            std::lock_guard lock(g_mutex);
            if (g_queue.empty() && !AudioOut::IsPlaying()) {
                g_app.speaking = false;
                g_app.followUpUntil = NowMs() + 8000; // ответ закончен — можно продолжить без «Аня»
            }
        }
        AudioOut::Shutdown();
    }

    void Run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        RunLoop(); // все COM-объекты освобождаются внутри, до CoUninitialize
        CoUninitialize();
    }
}

void Voice::Start() { g_thread = std::thread(Run); }

void Voice::Shutdown() {
    g_cv.notify_all();
    if (g_thread.joinable()) g_thread.join();
}

void Voice::Speak(const std::string& text) {
    const Settings cfg = Config::Get();
    const std::string spoken = cfg.spokenName.empty() ? cfg.name : cfg.spokenName;
    const std::string clean = Text::CleanForSpeech(Text::ReplaceWord(text, cfg.name, spoken));
    if (clean.empty() || g_failed) return;
    {
        std::lock_guard lock(g_mutex);
        g_queue.push_back(clean);
    }
    g_app.speaking = true; // сразу, чтобы микрофон не услышал саму Аню
    g_cv.notify_one();
}

void Voice::Stop() {
    {
        std::lock_guard lock(g_mutex);
        g_queue.clear();
    }
    AudioOut::Clear();
    g_stop = true;
    g_cv.notify_one();
}

void Voice::ReloadVoice() { g_voiceDirty = true; }

std::vector<Voice::Info> Voice::List() {
    std::lock_guard lock(g_voicesMutex);
    return g_voices;
}

const Voice::Info* Voice::Choose(const std::vector<Info>& voices, const std::wstring& wanted) {
    if (!wanted.empty())
        for (const auto& v : voices) if (v.name == wanted) return &v;
    for (const auto& v : voices) if (v.neural) return &v;
    for (const auto& v : voices) if (v.female && v.russian) return &v;
    for (const auto& v : voices) if (v.russian) return &v;
    return voices.empty() ? nullptr : &voices.front();
}
