#pragma once
// DirectX 11: устройство, swap chain и аддитивное смешивание для свечения.

#include "Platform.h"

#include <d3d11.h>

#include <string>

struct ImDrawList;

namespace Graphics {
    bool Create(HWND window);
    void Destroy();

    void RequestResize(UINT width, UINT height); // из WM_SIZE
    void ApplyPendingResize();                    // перед кадром

    void Clear(float r, float g, float b, float a = 1.0f); // a < 1 — сквозь окно виден акрил Windows
    bool Transparent(); // окно умеет быть прозрачным (DirectComposition)
    void Present();
    bool SaveScreenshot(const std::wstring& path); // текущий кадр (до Present) → BMP
    void* CreateTexture(const unsigned char* bgra, int width, int height); // картинка для ImGui (ID3D11ShaderResourceView*)

    ID3D11Device* Device();
    ID3D11DeviceContext* Context();

    // Всё, что нарисовано между этими вызовами, светится (цвета складываются)
    void BeginAdditive(ImDrawList* drawList);
    void EndAdditive(ImDrawList* drawList);
}
