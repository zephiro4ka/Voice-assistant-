#include "Wallpaper.h"

#include "Com.h"
#include "Graphics.h"
#include "Log.h"
#include "Platform.h"
#include "Settings.h"
#include "TextUtils.h"

#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace {
    ID3D11ShaderResourceView* g_texture = nullptr;
    int g_width = 0, g_height = 0;
    Wallpaper::Colors g_colors{ { 180, 160, 250 }, { 28, 16, 48 } };

    std::mutex g_pickMutex;
    std::string g_picked;
    bool g_hasPick = false, g_picking = false;

    // Картинка → пиксели BGRA (уменьшенные до 2560 по ширине — больше экрану не нужно)
    bool Decode(const std::wstring& path, std::vector<uint8_t>& pixels, int& w, int& h) {
        ComScope com;
        Com<IWICImagingFactory> factory;
        Com<IWICBitmapDecoder> decoder;
        Com<IWICBitmapFrameDecode> frame;
        Com<IWICFormatConverter> converter;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
            FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
            FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(frame.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
            return false;
        UINT width = 0, height = 0;
        converter->GetSize(&width, &height);
        IWICBitmapSource* source = converter.p;
        Com<IWICBitmapScaler> scaler;
        if (width > 2560 && SUCCEEDED(factory->CreateBitmapScaler(&scaler))) {
            const UINT nw = 2560, nh = static_cast<UINT>(static_cast<double>(height) * nw / width);
            if (SUCCEEDED(scaler->Initialize(converter.p, nw, nh, WICBitmapInterpolationModeFant))) { source = scaler.p; width = nw; height = nh; }
        }
        pixels.resize(static_cast<size_t>(width) * height * 4);
        if (FAILED(source->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return false;
        w = static_cast<int>(width);
        h = static_cast<int>(height);
        return true;
    }

    // Цвета по картинке: самый «живой» оттенок (насыщенный и яркий, часто встречается) и тёмный средний — для фона
    Wallpaper::Colors Analyze(const std::vector<uint8_t>& px, int w, int h) {
        constexpr int kBins = 24;
        double weight[kBins] = {}, sum[kBins][3] = {};
        double avg[3] = {};
        int samples = 0;
        const int step = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(w) * h / 40000.0)));
        for (int y = 0; y < h; y += step)
            for (int x = 0; x < w; x += step) {
                const size_t at = (static_cast<size_t>(y) * w + x) * 4;
                if (at + 3 >= px.size()) continue;
                const uint8_t* p = &px[at];
                if (p[3] < 16) continue; // прозрачные места картинки не считаем
                const float b = p[0] / 255.0f, g = p[1] / 255.0f, r = p[2] / 255.0f;
                avg[0] += r, avg[1] += g, avg[2] += b;
                ++samples;
                float hue = 0, sat = 0, val = 0;
                ImGui::ColorConvertRGBtoHSV(r, g, b, hue, sat, val);
                if (!(hue >= 0.0f && hue <= 1.0f)) continue; // защита от «не числа»
                const double score = sat * sat * val; // серое и тёмное почти не считается
                const int bin = std::clamp(static_cast<int>(hue * kBins), 0, kBins - 1);
                weight[bin] += score;
                sum[bin][0] += r * score, sum[bin][1] += g * score, sum[bin][2] += b * score;
            }
        const int best = static_cast<int>(std::max_element(weight, weight + kBins) - weight);
        Wallpaper::Colors c;
        if (weight[best] > 0.001) {
            float r = static_cast<float>(sum[best][0] / weight[best]), g = static_cast<float>(sum[best][1] / weight[best]), b = static_cast<float>(sum[best][2] / weight[best]);
            float hue, sat, val;
            ImGui::ColorConvertRGBtoHSV(r, g, b, hue, sat, val);
            ImGui::ColorConvertHSVtoRGB(hue, std::clamp(sat, 0.45f, 0.75f), std::max(val, 0.85f), r, g, b); // любимый цвет — светлый и сочный
            c.accent = { r * 255, g * 255, b * 255 };
        } else {
            c.accent = { 190, 196, 206 }; // чёрно-белая картинка — серебристый
        }
        samples = std::max(1, samples);
        const Theme::Color mean{ static_cast<float>(avg[0] / samples * 255), static_cast<float>(avg[1] / samples * 255), static_cast<float>(avg[2] / samples * 255) };
        c.base = mean.Mix(c.accent, 0.25f).Mix({ 0, 0, 0 }, 0.78f); // тёмный, но с оттенком картинки
        return c;
    }
}

bool Wallpaper::Load(const std::string& path) {
    Unload();
    if (Text::Trim(path).empty()) return false;
    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    if (!Decode(Text::ToWide(path), pixels, w, h)) {
        Log::Write("обои: не открылась картинка «" + path + "»");
        return false;
    }
    g_texture = static_cast<ID3D11ShaderResourceView*>(Graphics::CreateTexture(pixels.data(), w, h));
    g_width = w, g_height = h;
    g_colors = Analyze(pixels, w, h);
    return g_texture != nullptr;
}

void Wallpaper::Unload() {
    if (g_texture) g_texture->Release();
    g_texture = nullptr;
    g_width = g_height = 0;
}

bool Wallpaper::Loaded() { return g_texture != nullptr; }

void Wallpaper::Draw(ImDrawList* dl, ImVec2 size, float alpha) {
    if (!g_texture || g_width <= 0 || g_height <= 0) return;
    // «заполнить с обрезкой»: картинка закрывает всё окно без полос, лишнее — по краям
    const float screen = size.x / std::max(1.0f, size.y), image = static_cast<float>(g_width) / g_height;
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (image > screen) { const float cut = (1.0f - screen / image) * 0.5f; uv0.x = cut; uv1.x = 1.0f - cut; }
    else { const float cut = (1.0f - image / screen) * 0.5f; uv0.y = cut; uv1.y = 1.0f - cut; }
    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_texture))), ImVec2(0, 0), size, uv0, uv1,
        IM_COL32(255, 255, 255, static_cast<int>(255 * std::clamp(alpha, 0.0f, 1.0f))));
}

Wallpaper::Colors Wallpaper::Suggested() { return g_colors; }

void Wallpaper::ApplyAutoColors(bool enable) {
    const Colors c = g_colors;
    Config::Update([&](Settings& s) {
        if (enable) {
            if (!s.autoColors) { // запомнить, что было, — чтобы вернуть при выключении
                s.savedLook = static_cast<int>(s.look);
                s.savedLookColor = s.lookColor;
                s.savedAccent = s.accent;
                s.savedAccentColor = s.accentColor;
                s.savedAnimationColor = s.animationColor;
            }
            s.autoColors = true;
            s.look = Look::Custom;
            s.lookColor = Theme::ToRgb(c.base);
            s.accent = Theme::kAccentPresets; // «свой цвет»
            s.accentColor = Theme::ToRgb(c.accent);
            s.animationColor = Theme::ToRgb(c.accent);
        } else if (s.autoColors) {
            s.autoColors = false;
            s.look = static_cast<Look>(std::clamp(s.savedLook, 0, 4));
            s.lookColor = s.savedLookColor;
            s.accent = s.savedAccent;
            s.accentColor = s.savedAccentColor;
            s.animationColor = s.savedAnimationColor;
        }
    });
}

void Wallpaper::PickAsync() {
    {
        std::lock_guard lock(g_pickMutex);
        if (g_picking) return;
        g_picking = true;
    }
    std::thread([] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); // окно выбора файла хочет однопоточный COM
        std::string chosen;
        { // COM-объекты должны освободиться до CoUninitialize
        Com<IFileOpenDialog> dialog;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            const COMDLG_FILTERSPEC filter[] = { { L"Картинки", L"*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.gif" } };
            dialog->SetFileTypes(1, filter);
            dialog->SetTitle(L"Обои для Ани");
            Com<IShellItem> item;
            PWSTR path = nullptr;
            if (SUCCEEDED(dialog->Show(FindWindowW(L"AnyaWindow", nullptr))) && SUCCEEDED(dialog->GetResult(&item)) &&
                SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                chosen = Text::ToUtf8(path);
                CoTaskMemFree(path);
            }
        }
        }
        CoUninitialize();
        std::lock_guard lock(g_pickMutex);
        g_picking = false;
        if (!chosen.empty()) { g_picked = chosen; g_hasPick = true; }
    }).detach();
}

bool Wallpaper::Picked(std::string& path) {
    std::lock_guard lock(g_pickMutex);
    if (!g_hasPick) return false;
    g_hasPick = false;
    path = g_picked;
    return true;
}
