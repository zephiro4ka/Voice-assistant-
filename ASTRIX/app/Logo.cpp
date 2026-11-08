#include "Logo.h"

#include "../Resource.h"
#include "Com.h"
#include "Graphics.h"
#include "Platform.h"
#include "TextUtils.h"

#include <shlwapi.h>
#include <wincodec.h>

#include <cstdint>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

namespace {
    UINT g_customW = 0, g_customH = 0;
    void* g_custom = nullptr;

    void* Decode(IWICImagingFactory* factory, IWICBitmapDecoder* decoder, UINT* outW = nullptr, UINT* outH = nullptr) {
        Com<IWICBitmapFrameDecode> frame;
        Com<IWICFormatConverter> converter;
        UINT width = 0, height = 0;
        if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(frame.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
            FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 || width > 8192 || height > 8192)
            return nullptr;
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
        if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return nullptr;
        if (outW) *outW = width;
        if (outH) *outH = height;
        return Graphics::CreateTexture(pixels.data(), static_cast<int>(width), static_cast<int>(height));
    }

    void* LoadTexture(int id) {
        const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
        const HGLOBAL handle = resource ? LoadResource(nullptr, resource) : nullptr;
        if (!handle) return nullptr;
        ComScope com;
        Com<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return nullptr;
        Com<IStream> stream;
        stream.p = SHCreateMemStream(static_cast<const BYTE*>(LockResource(handle)), SizeofResource(nullptr, resource));
        Com<IWICBitmapDecoder> decoder;
        if (!stream.p || FAILED(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) return nullptr;
        return Decode(factory.p, decoder.p);
    }
}

void Logo::Draw(ImDrawList* dl, ImVec2 min, float size) {
    static void* s_texture = LoadTexture(IDB_LOGO);
    if (s_texture) dl->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(s_texture))), min, ImVec2(min.x + size, min.y + size));
}

void Logo::DrawTinted(ImDrawList* dl, ImVec2 c, float size, ImU32 color) {
    static void* s_mask = LoadTexture(IDB_LOGO_MASK);
    const float half = size * 0.5f;
    if (s_mask) dl->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(s_mask))), ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), ImVec2(0, 0), ImVec2(1, 1), color);
}

bool Logo::LoadCustom(const std::string& path) {
    ComScope com;
    Com<IWICImagingFactory> factory;
    Com<IWICBitmapDecoder> decoder;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(Text::ToWide(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
        return false;
    UINT w = 0, h = 0;
    void* texture = Decode(factory.p, decoder.p, &w, &h);
    if (!texture) return false;
    if (g_custom) static_cast<IUnknown*>(g_custom)->Release();
    g_custom = texture;
    g_customW = w;
    g_customH = h;
    return true;
}

bool Logo::HasCustom() { return g_custom != nullptr; }

void Logo::DrawCustom(ImDrawList* dl, ImVec2 c, float size) {
    if (!g_custom) return;
    const float half = size * 0.5f, aspect = static_cast<float>(g_customW) / g_customH;
    const ImVec2 uv0(aspect > 1 ? 0.5f - 0.5f / aspect : 0, aspect < 1 ? 0.5f - 0.5f * aspect : 0), uv1(1 - uv0.x, 1 - uv0.y); // по центру, без искажений
    dl->AddImageRounded(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_custom))), ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), uv0, uv1, IM_COL32_WHITE, half);
}
