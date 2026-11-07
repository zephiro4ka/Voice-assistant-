#pragma once
// Мелочи для COM (UI Automation, звук Windows): владеющий указатель и инициализация на время функции.

#include "Platform.h"

#include <objbase.h>

template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// CoInitialize на время жизни объекта (в каждом потоке, которому нужен COM)
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComScope() = default;
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
