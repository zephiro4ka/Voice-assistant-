#pragma once
// Скачивание нейросетей прямо из программы: файлы проверяются по контрольной сумме.

#include <cstdint>
#include <string>

namespace ModelStore {
    struct File { const char* url; const char* name; const char* sha256; uint64_t size; };
    struct Model { const char* id; const char* name; const char* tech; const char* about; File files[2]; };

    int Count();          // нейросети
    int SpeechFirst();    // с этого номера — модели распознавания речи
    int SpeechCount();
    const Model& Get(int index);
    uint64_t SizeOf(int index);
    bool Installed(int index);
    std::string Path(int index);   // файл самой модели

    struct Progress { bool running = false; int index = -1; uint64_t done = 0, total = 0; std::string status; };
    Progress Now();
    void Download(int index, bool activate = true); // в фоне; после загрузки сразу включает модель
    void Cancel();
}
