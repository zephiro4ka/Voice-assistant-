#include "Log.h"

#include "Platform.h"
#include "TextUtils.h"

#include <cstdio>
#include <mutex>

namespace {
    std::mutex g_mutex;
    constexpr LONGLONG kMaxSize = 1024 * 1024; // больше мегабайта — начинаем заново
}

void Log::Write(const std::string& line) {
    std::lock_guard lock(g_mutex);
    const std::wstring path = Text::ExecutableDirectory() + L"\\anya.log";

    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        const LONGLONG size = (static_cast<LONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        if (size > kMaxSize) DeleteFileW(path.c_str());
    }

    const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t{};
    GetLocalTime(&t);
    char stamp[32];
    sprintf_s(stamp, "[%02u.%02u %02u:%02u:%02u] ", t.wDay, t.wMonth, t.wHour, t.wMinute, t.wSecond);
    const std::string text = stamp + line + "\r\n";
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}
