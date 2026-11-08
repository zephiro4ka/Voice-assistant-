#include "Logo.h"

#include "../Resource.h"
#include "Com.h"
#include "Graphics.h"
#include "Platform.h"

#include <shlwapi.h>
#include <wincodec.h>

#include <cstdint>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

namespace {
    void* LoadTexture() {
        const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(IDB_LOGO), RT_RCDATA);
        const HGLOBAL handle = resource ? LoadResource(nullptr, resource) : nullptr;
        if (!handle) return nullptr;
        ComScope com;
        Com<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return nullptr;
        Com<IStream> stream;
        stream.p = SHCreateMemStream(static_cast<const BYTE*>(LockResource(handle)), SizeofResource(nullptr, resource));
        Com<IWICBitmapDecoder> decoder;
        Com<IWICBitmapFrameDecode> frame;
        Com<IWICFormatConverter> converter;
        UINT width = 0, height = 0;
        if (!stream.p || FAILED(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) || FAILED(decoder->GetFrame(0, &frame)) ||
            FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(frame.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
            FAILED(converter->GetSize(&width, &height)))
            return nullptr;
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
        if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return nullptr;
        return Graphics::CreateTexture(pixels.data(), static_cast<int>(width), static_cast<int>(height));
    }
}

void Logo::Draw(ImDrawList* dl, ImVec2 min, float size) {
    static void* s_texture = LoadTexture();
    if (s_texture) dl->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(s_texture))), min, ImVec2(min.x + size, min.y + size));
}
