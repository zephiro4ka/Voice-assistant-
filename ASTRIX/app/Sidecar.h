#pragma once
// Фоновые помощники Ани (движок нейросети, голос Silero): запуск без окна, журнал рядом с Аней,
// и главное — Windows сама закрывает их вместе с Аней, даже если она упадёт (Job Object).

#include "Platform.h"

#include <initializer_list>
#include <string>

namespace Sidecar {
    // Запустить программу скрыто; вывод — в logName рядом с exe Ани. nullptr — не запустилась.
    HANDLE Launch(const std::wstring& exe, const std::wstring& arguments, const std::wstring& logName);
    void Stop(HANDLE& process);
    bool HasExited(HANDLE process);

    // Ищет файл рядом с exe Ани или выше по папкам, в любом из вариантов подпапок
    std::wstring FindUpwards(std::initializer_list<const wchar_t*> relativePaths);
}
