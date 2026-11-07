#include "MediaNow.h"

#include "Platform.h"
#include "TextUtils.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <atomic>
#include <mutex>
#include <thread>

#pragma comment(lib, "windowsapp.lib")

namespace {
    using namespace winrt::Windows::Media::Control;

    std::mutex g_mutex;
    MediaNow::Track g_track;
    std::atomic<int> g_pending{ -1 }; // команда, которую надо выполнить в потоке опроса

    std::string AppName(const std::wstring& id) {
        // «Spotify.exe», «Yandex.Music_…!App», «MSEdge» → понятное имя
        std::wstring lower = Text::LowerRu(id);
        if (lower.find(L"yandex") != std::wstring::npos || lower.find(L"яндекс") != std::wstring::npos) return "Яндекс Музыка";
        if (lower.find(L"spotify") != std::wstring::npos) return "Spotify";
        if (lower.find(L"edge") != std::wstring::npos) return "Edge";
        if (lower.find(L"chrome") != std::wstring::npos) return "Chrome";
        if (lower.find(L"firefox") != std::wstring::npos) return "Firefox";
        if (lower.find(L"soundcloud") != std::wstring::npos) return "SoundCloud";
        const size_t cut = id.find_first_of(L"_!.");
        return Text::ToUtf8(id.substr(0, cut));
    }

    void Loop() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        GlobalSystemMediaTransportControlsSessionManager manager{ nullptr };
        for (;;) {
            try {
                if (!manager) manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                MediaNow::Track track;
                if (auto session = manager.GetCurrentSession()) {
                    if (const int action = g_pending.exchange(-1); action >= 0) {
                        if (action == 0) session.TryTogglePlayPauseAsync().get();
                        if (action == 1) session.TrySkipNextAsync().get();
                        if (action == 2) session.TrySkipPreviousAsync().get();
                    }
                    const auto props = session.TryGetMediaPropertiesAsync().get();
                    const auto playback = session.GetPlaybackInfo();
                    const auto timeline = session.GetTimelineProperties();
                    track.present = true;
                    track.playing = playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                    track.title = Text::ToUtf8(std::wstring(props.Title()));
                    track.artist = Text::ToUtf8(std::wstring(props.Artist()));
                    track.app = AppName(std::wstring(session.SourceAppUserModelId()));
                    using seconds = std::chrono::duration<double>;
                    track.duration = std::chrono::duration_cast<seconds>(timeline.EndTime() - timeline.StartTime()).count();
                    track.position = std::chrono::duration_cast<seconds>(timeline.Position()).count();
                    if (track.playing && track.duration > 0) { // позиция обновляется редко — досчитываем сами
                        const auto since = winrt::clock::now() - timeline.LastUpdatedTime();
                        track.position = std::min(track.duration, track.position + std::chrono::duration_cast<seconds>(since).count());
                    }
                } else {
                    g_pending = -1;
                }
                std::lock_guard lock(g_mutex);
                g_track = track;
            } catch (...) {
                manager = nullptr; // служба перезапустилась — подключимся заново
            }
            for (int i = 0; i < 10 && g_pending.load() < 0; ++i) Sleep(100); // кнопка — реагируем сразу
        }
    }
}

void MediaNow::Start() { std::thread(Loop).detach(); }

MediaNow::Track MediaNow::Snapshot() {
    std::lock_guard lock(g_mutex);
    return g_track;
}

void MediaNow::Control(Action action) { g_pending = static_cast<int>(action); }
