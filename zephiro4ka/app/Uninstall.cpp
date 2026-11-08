#include "Uninstall.h"

#include "Autostart.h"
#include "Log.h"
#include "TextUtils.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
    std::wstring ExePath() {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return path;
    }

    std::wstring Quote(const std::wstring& path) { // строка для PowerShell в одинарных кавычках
        std::wstring out = L"'";
        for (wchar_t c : path) out += c == L'\'' ? std::wstring(L"''") : std::wstring(1, c);
        return out + L"'";
    }

    // Скрипт PowerShell (UTF-8 с BOM — русские пути не ломаются) работает уже после того, как Аня закрылась
    void RunHiddenScript(const std::wstring& body) {
        wchar_t temp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp);
        const std::wstring script = std::wstring(temp) + L"anya-cleanup.ps1";
        {
            std::ofstream out(std::filesystem::path(script), std::ios::binary);
            out << "\xEF\xBB\xBF" << Text::ToUtf8(body + L"\r\nRemove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\r\n");
        }
        std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\"";
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, nullptr, temp, &si, &pi) ||
            CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, temp, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
}

std::wstring Uninstall::Folder() { return Text::ExecutableDirectory(); }

bool Uninstall::SafeToDeleteFolder() {
    const std::filesystem::path dir(Folder());
    if (dir.empty() || dir == dir.root_path() || !dir.has_parent_path() || dir.parent_path() == dir.root_path().parent_path()) return false;
    std::wstring name = Text::LowerRu(dir.filename().wstring());
    static const wchar_t* kShared[] = { L"desktop", L"рабочий стол", L"downloads", L"загрузки", L"documents", L"документы", L"program files",
                                        L"program files (x86)", L"windows", L"users", L"пользователи", L"onedrive" };
    if (std::any_of(std::begin(kShared), std::end(kShared), [&](const wchar_t* s) { return name == s; })) return false;
    wchar_t profile[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) && std::filesystem::path(profile) == dir) return false;
    return true;
}

void Uninstall::ResetSettings() {
    const std::wstring ini = Folder() + L"\\anya.ini";
    for (const wchar_t* section : { L"anya", L"metrics", L"limits", L"layout", L"sounds", L"weather", L"ai", L"debug" })
        WritePrivateProfileStringW(section, nullptr, nullptr, ini.c_str()); // раздел целиком
    Log::Write("настройки сброшены по умолчанию — перезапуск");
    const std::wstring restart = std::wstring(L"Start-Sleep -Seconds 2") + L'\n' + L"Start-Process -FilePath " + Quote(ExePath());
    RunHiddenScript(restart);
    ExitProcess(0); // без сохранения — иначе настройки из памяти вернутся в файл
}

void Uninstall::RemoveEverything() {
    Autostart::Set(false);
    const std::wstring dir = Folder();
    std::wstring body = L"Start-Sleep -Seconds 3\r\n$dir = " + Quote(dir) + L"\r\n";
    if (SafeToDeleteFolder()) { // движки нейросети и голоса закрываются чуть позже Ани — пробуем несколько раз
        body += L"for ($i = 0; $i -lt 6 -and (Test-Path -LiteralPath $dir); $i++) {\r\n"
                L"  Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue\r\n  Start-Sleep -Seconds 2\r\n}\r\n";
    } else { // общая папка (например, «Загрузки») — только свои файлы
        for (const wchar_t* item : { L"ASTRIX.exe", L"Anya.exe", L"anya.ini", L"scenarios.json", L"anya.log", L"llama-server.log", L"devprobe.txt", L"models", L"third_party", L"llama.cpp" })
            body += L"Remove-Item -LiteralPath (Join-Path $dir " + Quote(item) + L") -Recurse -Force -ErrorAction SilentlyContinue\r\n";
    }
    Log::Write("полное удаление программы");
    RunHiddenScript(body);
    ExitProcess(0);
}
