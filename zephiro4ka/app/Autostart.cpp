#include "Autostart.h"

#include "Platform.h"

#include <string>

namespace {
    constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr const wchar_t* kValue = L"Anya";

    std::wstring Command() {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return L"\"" + std::wstring(path) + L"\" " + Autostart::kFlag;
    }
}

bool Autostart::Enabled() {
    wchar_t value[1024] = {};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValue, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return false;
    return Command() == value; // запись есть и ведёт именно на эту Аню
}

void Autostart::Set(bool enabled) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (enabled) {
        const std::wstring command = Command();
        RegSetValueExW(key, kValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kValue);
    }
    RegCloseKey(key);
}
