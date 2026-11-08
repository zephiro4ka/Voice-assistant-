#include "Ears.h"

#include "Platform.h"
#include "BodySounds.h"
#include "Brain.h"
#include "LocalAi.h"
#include "Log.h"
#include "Settings.h"
#include "SileroVoice.h"
#include "State.h"
#include "TextUtils.h"
#include "Voice.h"

#define MINIAUDIO_IMPLEMENTATION
#include "../miniaudio.h"
#include "../whisper.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace {
    // «Стоп», «хватит», «замолчи», «Аня, стоп» — но не её собственные слова из колонок (их видно в подписи)
    bool IsStopWhileSpeaking(const std::string& heard) {
        const std::wstring text = Text::LowerRu(Text::ToWide(heard));
        std::string caption;
        bool live = false;
        Caption::Get(caption, live);
        const std::wstring saying = Text::LowerRu(Text::ToWide(caption));
        for (const wchar_t* word : { L"стоп", L"хватит", L"замолчи", L"помолчи", L"перестань", L"тихо", L"заткнись", L"остановись" })
            if (text.find(word) != std::wstring::npos && saying.find(word) == std::wstring::npos) return true;
        return false;
    }

    constexpr int kSampleRate = 16000;
    constexpr int kFrame = 320;                         // 20 мс
    constexpr size_t kPreRoll = kSampleRate * 3 / 10;   // 300 мс звука до начала речи
    constexpr size_t kMaxUtterance = kSampleRate * 20;  // фраза не длиннее 20 секунд
    constexpr Clock kMinSpeechMs = 350;                 // короче — не речь (но может быть кашлем или чихом)
    constexpr Clock kMinBurstMs = 150;                  // ещё короче — щелчок, даже не смотрим
    constexpr Clock kArmTimeoutMs = 8000;               // «говори» без речи — сдаёмся

    std::thread g_thread;
    std::atomic<float> g_confidence{ 1.0f }; // насколько модель уверена в последней фразе
    std::mutex g_inputMutex;
    std::vector<float> g_input; // сэмплы из колбэка микрофона

    void OnMicData(ma_device*, void*, const void* input, ma_uint32 frames) {
        if (!input || frames == 0) return;
        const float* samples = static_cast<const float*>(input);
        std::lock_guard lock(g_inputMutex);
        if (g_input.size() < kSampleRate * 5) g_input.insert(g_input.end(), samples, samples + frames);
    }

    std::wstring FindModelFile(const wchar_t* only = nullptr) {
        static const wchar_t* kAll[] = { L"ggml-small.bin", L"ggml-small-q5_1.bin", L"ggml-base.bin", L"ggml-tiny.bin", L"ggml-medium-q5_0.bin", L"ggml-medium.bin", L"ggml-large-v3-turbo-q5_0.bin" }; // сначала быстрые
        const std::wstring chosen = Text::ToWide(Config::Value("ears", "model", "ggml-small.bin"));
        std::vector<const wchar_t*> kNames;
        if (only) kNames.push_back(only);
        else { kNames.push_back(chosen.c_str()); kNames.insert(kNames.end(), std::begin(kAll), std::end(kAll)); }
        static const wchar_t* kSubdirs[] = { L"", L"\\models", L"\\models\\speech", L"\\ASTRIX\\models" };
        std::wstring dir = Text::ExecutableDirectory();
        for (int level = 0; level < 4; ++level) {
            for (const wchar_t* sub : kSubdirs) {
                for (const wchar_t* name : kNames) {
                    const std::wstring path = dir + sub + L"\\" + name;
                    const DWORD attributes = GetFileAttributesW(path.c_str());
                    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return path;
                }
            }
            const size_t slash = dir.find_last_of(L"\\/");
            if (slash == std::wstring::npos) break;
            dir.resize(slash);
        }
        return {};
    }

    // Текст на лету: отдельный поток распознаёт ещё не законченную фразу
    std::mutex g_liveMutex;
    std::vector<float> g_liveAudio;
    std::string g_draftText;             // готовый черновик: если он покрывает всю фразу, ответ начинается сразу
    size_t g_draftSamples = 0;
    std::atomic<float> g_draftConfidence{ 1.0f };
    std::atomic<bool> g_liveWanted{ false }, g_liveBusy{ false }, g_reload{ false };

    std::string Transcribe(whisper_context* ctx, std::vector<float>& pcm, whisper_state* state = nullptr) {
        // тихий микрофон → нормальная громкость
        float peak = 0.0f;
        double energy = 0.0;
        for (float v : pcm) { peak = std::max(peak, std::fabs(v)); energy += static_cast<double>(v) * v; }
        if (peak > 1e-5f && !pcm.empty()) { // по средней громкости, а не по одному щелчку: тихая речь становится обычной
            const float rms = static_cast<float>(std::sqrt(energy / pcm.size()));
            const float gain = std::min({ 0.1f / std::max(rms, 1e-6f), 0.95f / peak, 200.0f });
            for (float& v : pcm) v *= gain;
        }
        if (pcm.size() < kSampleRate) pcm.resize(kSampleRate, 0.0f); // whisper не любит совсем короткие куски

        // whisper на тишине в конце иногда зацикливается: «…это сколько? Аня, …это сколько? …» — оставляем каждое предложение один раз
        auto DropRepeats = [](const std::string& text) {
            std::string out, sentence;
            std::vector<std::wstring> seen;
            auto flush = [&] {
                const std::string trimmed = Text::Trim(sentence);
                sentence.clear();
                if (trimmed.empty()) return;
                const std::wstring key = Text::LowerRu(Text::ToWide(trimmed));
                if (std::find(seen.begin(), seen.end(), key) != seen.end()) return;
                seen.push_back(key);
                out += (out.empty() ? "" : " ") + trimmed;
            };
            for (char ch : text) {
                sentence.push_back(ch);
                if (ch == '.' || ch == '?' || ch == '!') flush();
            }
            flush();
            return out;
        };
        whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY); // на проверке точнее перебора и в полтора раза быстрее
        if (state) p.abort_callback = [](void*) { return !g_liveWanted.load(); }; // фраза кончилась — бросаем черновик сразу
        p.language = "ru";
        p.n_threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) / 2, 2, 8);
        p.print_progress = p.print_special = p.print_realtime = p.print_timestamps = false;
        p.no_timestamps = true;
        p.single_segment = true;
        p.no_context = true;
        p.suppress_blank = true;
        p.suppress_nst = true;
        p.temperature_inc = state ? 0.0f : 0.2f; // итоговый текст: если вышло неуверенно — пробует ещё раз
        // Подсказка словарю whisper: как пишется имя, типичные команды и имена друзей из [contacts] —
        // меньше «Они откроетим» и «прыща вы» вместо «прыщавый»
        std::string prompt = "Аня, открой Steam. Аня, какая сейчас погода? Аня, почисти память. Аня, очисти оперативку. Аня, что произошло в этом городе в 1941 году? Разговорная русская речь: вопросы об истории, географии, науке, играх и программах.";
        std::string names;
        for (const auto& [spoken, handle] : Config::Contacts()) names += (names.empty() ? "" : ", ") + Text::Capitalize(spoken);
        if (!names.empty()) prompt += " Аня, зайди в чат с " + names + ".";
        prompt = Text::Brand(prompt, Config::Get().assistant); // своё имя — whisper лучше его расслышит
        p.initial_prompt = prompt.c_str();
        // Главный ускоритель: энкодер считает только реальную длину звука, а не все 30 секунд
        const float seconds = static_cast<float>(pcm.size()) / kSampleRate;
        p.audio_ctx = std::clamp(static_cast<int>(seconds * 50.0f) + 64, 256, 1500);

        if (state) { // черновик на лету: быстро и только текст
            if (whisper_full_with_state(ctx, state, p, pcm.data(), static_cast<int>(pcm.size())) != 0) return {};
            std::string draft;
            double sure = 0.0;
            int count = 0;
            for (int i = 0; i < whisper_full_n_segments_from_state(state); ++i) {
                draft += whisper_full_get_segment_text_from_state(state, i);
                for (int t = 0; t < whisper_full_n_tokens_from_state(state, i); ++t) {
                    const auto data = whisper_full_get_token_data_from_state(state, i, t);
                    if (data.id < whisper_token_eot(ctx)) { sure += data.p; ++count; }
                }
            }
            g_draftConfidence = count ? static_cast<float>(sure / count) : 0.0f;
            return DropRepeats(Text::Trim(draft));
        }
        if (whisper_full(ctx, p, pcm.data(), static_cast<int>(pcm.size())) != 0) return {};
        std::string text;
        double sureSum = 0.0;
        int sureCount = 0;
        for (int i = 0; i < whisper_full_n_segments(ctx); ++i) {
            text += whisper_full_get_segment_text(ctx, i);
            for (int t = 0; t < whisper_full_n_tokens(ctx, i); ++t)
                if (whisper_full_get_token_id(ctx, i, t) < whisper_token_eot(ctx)) { sureSum += whisper_full_get_token_p(ctx, i, t); ++sureCount; }
        }
        g_confidence = sureCount ? static_cast<float>(sureSum / sureCount) : 0.0f;
        return DropRepeats(Text::Trim(text));
    }

    // Детектор речи с адаптивным уровнем шума
    class VoiceDetector {
    public:
        enum class Event { None, Started, Finished };

        Event Feed(const float* frame, float rms, int silenceMs) {
            if (!inSpeech_) {
                // шум отслеживаем быстро вниз и медленно вверх
                noise_ = rms < noise_ ? noise_ * 0.9f + rms * 0.1f : noise_ * 0.997f + rms * 0.003f;
                preroll_.insert(preroll_.end(), frame, frame + kFrame);
                if (preroll_.size() > kPreRoll) preroll_.erase(preroll_.begin(), preroll_.end() - kPreRoll);

                loudFrames_ = rms > std::max(noise_ * 2.2f, 0.00035f) ? loudFrames_ + 1 : 0; // ловит и тихую речь
                if (loudFrames_ < 3) return Event::None;
                inSpeech_ = true;
                segment_ = std::move(preroll_);
                preroll_.clear();
                lastVoice_ = start_ = NowMs();
                return Event::Started;
            }

            segment_.insert(segment_.end(), frame, frame + kFrame);
            if (rms > std::max(noise_ * 1.5f, 0.00025f)) lastVoice_ = NowMs();
            if (NowMs() - lastVoice_ <= static_cast<Clock>(silenceMs) && segment_.size() < kMaxUtterance) return Event::None;

            inSpeech_ = false;
            loudFrames_ = 0;
            // хвост тишины почти целиком отрезаем — меньше работы для whisper
            const size_t silentSamples = std::min<size_t>((NowMs() - lastVoice_) * kSampleRate / 1000, segment_.size());
            segment_.resize(segment_.size() - silentSamples + std::min<size_t>(silentSamples, kSampleRate / 5));
            return Event::Finished;
        }

        void Reset() {
            inSpeech_ = false;
            loudFrames_ = 0;
            preroll_.clear();
            segment_.clear();
        }

        bool InSpeech() const { return inSpeech_; }
        Clock VoicedMs() const { return lastVoice_ - start_; }
        float Noise() const { return noise_; }
        std::vector<float>& Segment() { return segment_; }

    private:
        float noise_ = 0.002f;
        int loudFrames_ = 0;
        bool inSpeech_ = false;
        Clock lastVoice_ = 0, start_ = 0;
        std::vector<float> preroll_, segment_;
    };

    // Распознать фразу: GigaAM-v3 через сервер голоса (точнее), иначе — whisper (грузится только если нужен)
    std::string Recognize(whisper_context*& ctx, const std::wstring& modelPath, std::vector<float>& pcm) {
        if (auto text = SileroVoice::Recognize(pcm)) return *text;
        if (!ctx) {
            whisper_context_params params = whisper_context_default_params();
            params.use_gpu = false;
            ctx = whisper_init_from_file_with_params(Text::ToUtf8(modelPath).c_str(), params);
            if (!ctx) return {};
        }
        return Transcribe(ctx, pcm);
    }

    void LiveLoop(whisper_context** ctx, whisper_state** state) {
        Clock last = 0;
        while (g_app.running) {
            Sleep(40);
            if (!g_liveWanted.load() || NowMs() - last < 500) continue;
            g_liveBusy = true;
            if (g_liveWanted.load() && *ctx && *state) {
                std::vector<float> audio;
                { std::lock_guard lock(g_liveMutex); audio = g_liveAudio; }
                if (audio.size() > kSampleRate / 2) {
                    last = NowMs();
                    const size_t covered = audio.size();
                    const std::string draft = Transcribe(*ctx, audio, *state);
                    if (g_liveWanted.load()) {
                        std::lock_guard lock(g_liveMutex);
                        g_draftText = draft;
                        g_draftSamples = covered;
                    }
                    if (g_liveWanted.load() && !draft.empty() && !Text::IsWhisperGarbage(draft)) {
                        std::string command;
                        const bool mine = g_app.pttArmed.load() || NowMs() < g_app.followUpUntil.load() || Text::FindWakeWord(draft, command, Config::Get().assistant);
                        if (mine) { Caption::Set(draft, true); Ai::Prewarm(); } // чужой разговор на экран не выводим; нейросеть просыпается заранее
                    }
                }
            }
            g_liveBusy = false;
        }
    }

    void StopLive() {
        g_liveWanted = false;
        while (g_liveBusy.load()) Sleep(2);
    }

    void Run() {
        std::wstring modelPath = FindModelFile();
        if (modelPath.empty()) {
            Chat::Add(Role::System, "Не найдена модель whisper (ggml-small.bin). Положи её в папку models рядом с проектом.");
            Caption::Set("нет модели распознавания — можно писать текстом");
            return;
        }

        Caption::Set("просыпаюсь…");
        whisper_context_params params = whisper_context_default_params();
        params.use_gpu = false;
        whisper_context* ctx = whisper_init_from_file_with_params(Text::ToUtf8(modelPath).c_str(), params);
        if (!ctx) {
            Chat::Add(Role::System, "Не удалось загрузить модель whisper.");
            return;
        }
        { // прогрев: первая фраза распознаётся так же быстро, как остальные
            std::vector<float> silence(kSampleRate, 0.0f);
            Transcribe(ctx, silence);
        }

        whisper_state* liveState = whisper_init_state(ctx);
        std::thread live(LiveLoop, &ctx, &liveState);

        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.capture.format = ma_format_f32;
        config.capture.channels = 1;
        config.sampleRate = kSampleRate;
        config.periodSizeInFrames = kFrame;
        config.dataCallback = OnMicData;
        ma_device device;
        if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS || ma_device_start(&device) != MA_SUCCESS) {
            Chat::Add(Role::System, "Не удалось открыть микрофон. Проверь разрешения Windows и устройство ввода.");
            Caption::Set("микрофон недоступен — можно писать текстом");
            StopLive();
            g_app.running ? live.detach() : live.join();
            return;
        }

        g_app.audioReady = true;
        Caption::Set(Config::Get().wakeWord ? "скажи «" + Config::Get().assistant + "» и вопрос — или нажми на сферу" : std::string("нажми на сферу или пробел — и говори"));

        VoiceDetector vad;
        std::vector<float> pending;
        Clock liveShared = 0;

        while (g_app.running) {
            Sleep(15);
            {
                std::lock_guard lock(g_inputMutex);
                pending.insert(pending.end(), g_input.begin(), g_input.end());
                g_input.clear();
            }

            if (ctx && SileroVoice::AsrReady()) { // распознаёт GigaAM — whisper больше не нужен, освобождаем ~500 МБ
                StopLive();
                g_liveBusy = true;
                if (liveState) { whisper_free_state(liveState); liveState = nullptr; }
                whisper_free(ctx);
                ctx = nullptr;
                g_liveBusy = false;
                Log::Write("распознавание: whisper выгружен, слушает GigaAM-v3");
            }
            if (g_reload.exchange(false)) { // выбрали другую модель распознавания
                StopLive();
                vad.Reset();
                g_liveBusy = true;
                const std::wstring next = FindModelFile();
                whisper_context_params reloadParams = whisper_context_default_params();
                reloadParams.use_gpu = false;
                if (whisper_context* fresh = next.empty() ? nullptr : whisper_init_from_file_with_params(Text::ToUtf8(next).c_str(), reloadParams)) {
                    if (liveState) whisper_free_state(liveState);
                    if (ctx) whisper_free(ctx);
                    ctx = fresh;
                    liveState = whisper_init_state(ctx);
                    modelPath = next;
                    Log::Write("распознавание: новая модель");
                }
                g_liveBusy = false;
            }
            const Settings cfg = Config::Get();
            // пока Аня говорит — слушаем только «стоп» (свою речь из колонок она отбрасывает); пока думает — не слушаем вовсе
            const bool bargeIn = g_app.micEnabled && g_app.speaking && !g_app.thinking;
            const bool deaf = g_app.setupOpen || !g_app.micEnabled || g_app.thinking || (g_app.speaking && !bargeIn) || (!bargeIn && !cfg.wakeWord && !g_app.pttArmed);
            if (g_app.pttArmed && !vad.InSpeech() && NowMs() - g_app.pttArmedAt > kArmTimeoutMs) {
                g_app.pttArmed = false;
                Caption::Set("ничего не услышала… нажми ещё раз");
            }

            size_t offset = 0;
            for (; offset + kFrame <= pending.size(); offset += kFrame) {
                const float* frame = pending.data() + offset;
                double sum = 0.0;
                for (int i = 0; i < kFrame; ++i) sum += static_cast<double>(frame[i]) * frame[i];
                const float rms = static_cast<float>(std::sqrt(sum / kFrame));

                if (deaf) {
                    g_app.micLevel = g_app.micLevel * 0.85f;
                    g_app.PushLevel(0.0f);
                    if (vad.InSpeech()) { g_app.capturing = false; g_liveWanted = false; }
                    vad.Reset();
                    continue;
                }

                // громкость относительно шума, в логарифме — для анимации
                const float level = std::clamp(std::log10(std::max(rms, 1e-6f) / std::max(vad.Noise(), 1e-5f)) / 1.4f, 0.0f, 1.0f);
                g_app.micLevel = g_app.micLevel * 0.6f + level * 0.4f;
                g_app.PushLevel(level);

                const auto event = vad.Feed(frame, rms, bargeIn ? 450 : cfg.silenceMs);
                if (bargeIn) { // во время её речи: короткая фраза со стоп-словом — замолчать
                    if (event == VoiceDetector::Event::Finished) {
                        if (vad.VoicedMs() >= kMinBurstMs && vad.VoicedMs() <= 3000 && IsStopWhileSpeaking(Recognize(ctx, modelPath, vad.Segment()))) {
                            Log::Write("услышала «стоп», пока говорила — замолкаю");
                            Brain::Interrupt();
                        }
                        vad.Reset();
                        offset = pending.size();
                        break;
                    }
                    continue;
                }
                if (vad.InSpeech() && !bargeIn && ctx && NowMs() - liveShared > 250) { // свежий звук — черновику
                    liveShared = NowMs();
                    std::lock_guard lock(g_liveMutex);
                    g_liveAudio = vad.Segment();
                    g_liveWanted = true;
                }
                if (event == VoiceDetector::Event::Started) { std::lock_guard lock(g_liveMutex); g_draftText.clear(); g_draftSamples = 0; }
                if (event == VoiceDetector::Event::Started) {
                    g_app.capturing = true;
                    if (g_app.pttArmed) Caption::Set("слушаю…", true);
                } else if (event == VoiceDetector::Event::Finished) {
                    StopLive(); // черновик больше не нужен, ядра отдаём итоговому распознаванию
                    g_app.capturing = false;
                    if (vad.VoicedMs() >= kMinBurstMs) {
                        // параметры звука считаем до распознавания: Transcribe меняет громкость сигнала
                        const BodySounds::Burst burst = BodySounds::Analyze(vad.Segment(), vad.Noise());
                        const bool shortSound = vad.VoicedMs() < kMinSpeechMs;
                        if (!shortSound) { // насколько громко человек говорит по сравнению с обычным для него уровнем
                            static float s_usual = 0.0f;
                            double energy = 0.0;
                            int crossings = 0;
                            const auto& sound = vad.Segment();
                            for (size_t s = 0; s < sound.size(); ++s) {
                                energy += static_cast<double>(sound[s]) * sound[s];
                                if (s && (sound[s] > 0) != (sound[s - 1] > 0)) ++crossings;
                            }
                            const float level = static_cast<float>(std::sqrt(energy / std::max<size_t>(sound.size(), 1)));
                            const float zcr = static_cast<float>(crossings) / std::max<size_t>(sound.size(), 1);
                            if (s_usual <= 0.0f) s_usual = level;
                            const float ratio = level / std::max(s_usual, 1e-6f);
                            // издалека голос очень слабый, но звонкий; шёпот — шипящий и не такой слабый
                            const int mode = ratio < 0.14f && zcr < 0.2f ? 2 : ratio < 0.55f ? 1 : 0;
                            Voice::SetReplyLoudness(mode);
                            if (ratio > 0.4f) s_usual = s_usual * 0.85f + level * 0.15f;
                            Log::Write("громкость голоса: " + std::to_string(ratio) + ", режим ответа " + std::to_string(mode));
                        }

                        g_app.utteranceEndAt = NowMs();
                        g_app.transcribing = true;
                        std::string text;
                        {
                            std::lock_guard lock(g_liveMutex);
                            if (!g_draftText.empty() && g_draftSamples + kSampleRate * 3 / 10 >= vad.Segment().size()) { // черновик уже знает всю фразу
                                text = g_draftText;
                                g_confidence = g_draftConfidence.load();
                            }
                        }
                        if (text.empty()) text = Recognize(ctx, modelPath, vad.Segment());
                        g_app.transcribing = false;

                        const BodySounds::Kind body = BodySounds::Classify(text, burst);
                        const bool garbage = text.empty() || Text::IsWhisperGarbage(text);
                        if (shortSound || garbage || body != BodySounds::Kind::None)
                            Log::Write("услышала: «" + text + "» — " + BodySounds::Describe(burst) +
                                       (body == BodySounds::Kind::Cough ? "  → кашель" : body == BodySounds::Kind::Sneeze ? "  → чих" : "  → шум, пропускаю"));
                        else
                            Log::Write("услышала: «" + text + "»");

                        if (body != BodySounds::Kind::None) Brain::OnBodySound(body);
                        else if (!garbage) Brain::OnHeard(text); // короткое «Аня» — тоже речь
                        else if (g_app.pttArmed && !shortSound) Caption::Set("не разобрала… попробуй ещё раз", true);

                        std::lock_guard lock(g_inputMutex);
                        g_input.clear(); // всё, что пришло во время распознавания, — устарело
                    }
                    vad.Reset();
                    offset = pending.size();
                    break;
                }
            }
            pending.erase(pending.begin(), pending.begin() + std::min(offset, pending.size()));
        }

        g_app.audioReady = false;
        ma_device_uninit(&device);
        StopLive();
        live.join();
        if (liveState) whisper_free_state(liveState);
        if (ctx) whisper_free(ctx);
    }
}

void Ears::Start() { g_thread = std::thread(Run); }

void Ears::ReloadModel() { g_reload = true; }
bool Ears::ModelPresent(const std::string& fileName) { return !FindModelFile(Text::ToWide(fileName).c_str()).empty(); }

float Ears::LastConfidence() { return g_confidence.load(); }

void Ears::Bench(const std::string& rawFile) {
    std::thread([rawFile] {
        std::ifstream in(rawFile, std::ios::binary | std::ios::ate);
        std::vector<float> sound(static_cast<size_t>(in.tellg()) / sizeof(float));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(sound.data()), sound.size() * sizeof(float));
        for (const wchar_t* name : { L"ggml-small.bin", L"ggml-medium-q5_0.bin", L"ggml-large-v3-turbo-q5_0.bin" }) {
            const std::wstring path = FindModelFile(name);
            if (path.empty()) continue;
            whisper_context_params params = whisper_context_default_params();
            params.use_gpu = false;
            whisper_context* ctx = whisper_init_from_file_with_params(Text::ToUtf8(path).c_str(), params);
            if (!ctx) continue;
            std::vector<float> pcm = sound;
            const Clock start = NowMs();
            const std::string text = Transcribe(ctx, pcm);
            if (whisper_state* state = whisper_init_state(ctx)) { // черновик на лету
                g_liveWanted = true;
                std::vector<float> draftAudio = sound;
                const Clock draftStart = NowMs();
                const std::string draft = Transcribe(ctx, draftAudio, state);
                g_liveWanted = false;
                Log::Write("бенч черновик: " + std::to_string(NowMs() - draftStart) + " мс «" + draft + "»");
                whisper_free_state(state);
            }
            Log::Write("бенч " + Text::ToUtf8(name) + ": " + std::to_string(NowMs() - start) + " мс, уверенность " + std::to_string(g_confidence.load()) + " «" + text + "»");
            whisper_free(ctx);
        }
        Log::Write("бенч: готово");
    }).detach();
}

void Ears::Shutdown() {
    if (g_thread.joinable()) g_thread.join();
}
