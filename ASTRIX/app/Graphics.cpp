#include "Graphics.h"

#include "../ImGui/imgui.h"

#include <dcomp.h>
#include <dxgi1_3.h>

#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

// Окно прозрачное: кадр уходит в DirectComposition с альфа-каналом, а под ним Windows 11 рисует
// размытый фон (акрил или Mica, см. «Эффект окна» в настройках). Без DirectComposition — обычный swap chain.

namespace {
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    IDXGISwapChain1* g_swapChain = nullptr;
    ID3D11RenderTargetView* g_target = nullptr;
    ID3D11BlendState* g_additive = nullptr;
    IDCompositionDevice* g_dcomp = nullptr;
    IDCompositionTarget* g_dcompTarget = nullptr;
    IDCompositionVisual* g_visual = nullptr;
    bool g_transparent = false;
    UINT g_pendingWidth = 0, g_pendingHeight = 0;

    template <class T>
    void SafeRelease(T*& p) {
        if (p) { p->Release(); p = nullptr; }
    }

    void CreateTarget() {
        ID3D11Texture2D* back = nullptr;
        g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
        if (!back) return;
        g_device->CreateRenderTargetView(back, nullptr, &g_target);
        back->Release();
    }

    void SetAdditive(const ImDrawList*, const ImDrawCmd*) {
        const float factor[4] = {};
        g_context->OMSetBlendState(g_additive, factor, 0xffffffff);
    }

    bool CreateSwapChain(HWND window) {
        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory2* factory = nullptr;
        g_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        if (dxgiDevice) dxgiDevice->GetAdapter(&adapter);
        if (adapter) adapter->GetParent(IID_PPV_ARGS(&factory));

        RECT rc{};
        GetClientRect(window, &rc);
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
        sd.Height = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

        bool ok = false;
        if (factory && SUCCEEDED(factory->CreateSwapChainForComposition(g_device, &sd, nullptr, &g_swapChain)) &&
            SUCCEEDED(DCompositionCreateDevice(dxgiDevice, IID_PPV_ARGS(&g_dcomp))) &&
            SUCCEEDED(g_dcomp->CreateTargetForHwnd(window, TRUE, &g_dcompTarget)) && SUCCEEDED(g_dcomp->CreateVisual(&g_visual))) {
            g_visual->SetContent(g_swapChain);
            g_dcompTarget->SetRoot(g_visual);
            g_dcomp->Commit();
            g_transparent = ok = true;
        }
        if (!ok) { // старая Windows — непрозрачное окно
            SafeRelease(g_visual);
            SafeRelease(g_dcompTarget);
            SafeRelease(g_dcomp);
            SafeRelease(g_swapChain);
            sd.Scaling = DXGI_SCALING_NONE;
            sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            ok = factory && SUCCEEDED(factory->CreateSwapChainForHwnd(g_device, window, &sd, nullptr, nullptr, &g_swapChain));
        }
        SafeRelease(factory);
        SafeRelease(adapter);
        SafeRelease(dxgiDevice);
        return ok;
    }
}

bool Graphics::Create(HWND window) {
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
            &g_device, nullptr, &g_context)))
        return false;
    if (!CreateSwapChain(window)) return false;
    CreateTarget();

    D3D11_BLEND_DESC bd{};
    auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D11_BLEND_ONE;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_ONE;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_device->CreateBlendState(&bd, &g_additive);
    return true;
}

void Graphics::Destroy() {
    SafeRelease(g_target);
    SafeRelease(g_additive);
    SafeRelease(g_visual);
    SafeRelease(g_dcompTarget);
    SafeRelease(g_dcomp);
    SafeRelease(g_swapChain);
    SafeRelease(g_context);
    SafeRelease(g_device);
}

bool Graphics::Transparent() { return g_transparent; }

void Graphics::RequestResize(UINT width, UINT height) {
    g_pendingWidth = width;
    g_pendingHeight = height;
}

void Graphics::ApplyPendingResize() {
    if (!g_swapChain || g_pendingWidth == 0 || g_pendingHeight == 0) return;
    SafeRelease(g_target);
    g_swapChain->ResizeBuffers(0, g_pendingWidth, g_pendingHeight, DXGI_FORMAT_UNKNOWN, 0);
    g_pendingWidth = g_pendingHeight = 0;
    CreateTarget();
}

void Graphics::Clear(float r, float g, float b, float a) {
    const float color[4] = { r * a, g * a, b * a, a }; // премультиплицированный альфа-канал
    g_context->OMSetRenderTargets(1, &g_target, nullptr);
    g_context->ClearRenderTargetView(g_target, color);
}

void Graphics::Present() { g_swapChain->Present(1, 0); }

bool Graphics::SaveScreenshot(const std::wstring& path) {
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (!back) return false;
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    const bool created = SUCCEEDED(g_device->CreateTexture2D(&desc, nullptr, &staging));
    if (created) g_context->CopyResource(staging, back);
    back->Release();
    if (!created) return false;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    bool ok = false;
    if (SUCCEEDED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        const UINT w = desc.Width, h = desc.Height, row = w * 4;
        const bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM;
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(w);
        info.biHeight = -static_cast<LONG>(h); // сверху вниз
        info.biPlanes = 1;
        info.biBitCount = 32;
        file.bfType = 0x4D42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + row * h;
        HANDLE out = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(out, &file, sizeof(file), &written, nullptr);
            WriteFile(out, &info, sizeof(info), &written, nullptr);
            std::string line(row, '\0');
            for (UINT y = 0; y < h; ++y) {
                const auto* src = static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch;
                for (UINT x = 0; x < w; ++x) { // в BMP — BGR; прозрачное (там, где акрил) — на тёмно-сером
                    const unsigned a = src[x * 4 + 3], under = (255 - a) * 40 / 255;
                    line[x * 4 + 0] = static_cast<char>(std::min(255u, src[x * 4 + (rgba ? 2 : 0)] + under));
                    line[x * 4 + 1] = static_cast<char>(std::min(255u, src[x * 4 + 1] + under));
                    line[x * 4 + 2] = static_cast<char>(std::min(255u, src[x * 4 + (rgba ? 0 : 2)] + under));
                    line[x * 4 + 3] = static_cast<char>(255);
                }
                WriteFile(out, line.data(), row, &written, nullptr);
            }
            CloseHandle(out);
            ok = true;
        }
        g_context->Unmap(staging, 0);
    }
    staging->Release();
    return ok;
}

void* Graphics::CreateTexture(const unsigned char* bgra, int width, int height) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{ bgra, static_cast<UINT>(width * 4), 0 };
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, &data, &texture))) return nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    g_device->CreateShaderResourceView(texture, nullptr, &view);
    texture->Release();
    return view;
}

ID3D11Device* Graphics::Device() { return g_device; }
ID3D11DeviceContext* Graphics::Context() { return g_context; }

void Graphics::BeginAdditive(ImDrawList* drawList) { drawList->AddCallback(SetAdditive, nullptr); }
void Graphics::EndAdditive(ImDrawList* drawList) { drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr); }
