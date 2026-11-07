#include "Screen.h"

#include "Com.h"
#include "Platform.h"

#include <wincodec.h>

#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")

namespace {
    // Пиксели монитора (BGRA, сверху вниз)
    bool GrabMonitor(std::vector<uint8_t>& pixels, int& width, int& height) {
        HMONITOR monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{ sizeof(info) };
        if (!GetMonitorInfoW(monitor, &info)) return false;
        const RECT r = info.rcMonitor;
        width = r.right - r.left;
        height = r.bottom - r.top;

        HDC screen = GetDC(nullptr);
        HDC memory = CreateCompatibleDC(screen);
        BITMAPINFO bmi{};
        bmi.bmiHeader = { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB };
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        const bool ok = bitmap && bits;
        if (ok) {
            HGDIOBJ old = SelectObject(memory, bitmap);
            BitBlt(memory, 0, 0, width, height, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
            SelectObject(memory, old);
            pixels.assign(static_cast<uint8_t*>(bits), static_cast<uint8_t*>(bits) + static_cast<size_t>(width) * height * 4);
        }
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return ok;
    }
}

std::vector<uint8_t> Screen::CaptureJpeg(int maxWidth, int quality) {
    std::vector<uint8_t> pixels;
    int width = 0, height = 0;
    if (!GrabMonitor(pixels, width, height)) return {};

    ComScope com;
    Com<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return {};
    Com<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGR, width * 4, static_cast<UINT>(pixels.size()),
            pixels.data(), &bitmap)))
        return {};

    IWICBitmapSource* source = bitmap.p;
    Com<IWICBitmapScaler> scaler;
    UINT outWidth = width, outHeight = height;
    if (maxWidth > 0 && width > maxWidth && SUCCEEDED(factory->CreateBitmapScaler(&scaler))) {
        outWidth = maxWidth;
        outHeight = static_cast<UINT>(static_cast<double>(height) * maxWidth / width);
        if (SUCCEEDED(scaler->Initialize(bitmap.p, outWidth, outHeight, WICBitmapInterpolationModeFant))) source = scaler.p;
    }

    Com<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return {};
    Com<IWICBitmapEncoder> encoder;
    Com<IWICBitmapFrameEncode> frame;
    Com<IPropertyBag2> options;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) || FAILED(encoder->Initialize(stream.p, WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, &options)))
        return {};
    PROPBAG2 option{};
    option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_R4;
    value.fltVal = std::clamp(quality, 10, 100) / 100.0f;
    options->Write(1, &option, &value);
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->Initialize(options.p)) || FAILED(frame->SetSize(outWidth, outHeight)) || FAILED(frame->SetPixelFormat(&format)) ||
        FAILED(frame->WriteSource(source, nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
        return {};

    HGLOBAL memory = nullptr;
    if (FAILED(GetHGlobalFromStream(stream.p, &memory))) return {};
    STATSTG stat{};
    stream->Stat(&stat, STATFLAG_NONAME);
    const auto* data = static_cast<const uint8_t*>(GlobalLock(memory));
    std::vector<uint8_t> jpeg(data, data + stat.cbSize.QuadPart);
    GlobalUnlock(memory);
    return jpeg;
}

std::string Screen::Base64(const std::vector<uint8_t>& data) {
    static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        const uint32_t n = (data[i] << 16) | (i + 1 < data.size() ? data[i + 1] << 8 : 0) | (i + 2 < data.size() ? data[i + 2] : 0);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += i + 1 < data.size() ? kAlphabet[(n >> 6) & 63] : '=';
        out += i + 2 < data.size() ? kAlphabet[n & 63] : '=';
    }
    return out;
}
