#pragma once
// Компьютер: точная громкость, батарея, загрузка процессора и памяти, буфер обмена.

#include <optional>
#include <string>

namespace Pc {
    std::optional<int> Volume();          // громкость 0…100
    bool SetVolume(int percent);
    bool IsMuted();
    bool SetMute(bool mute);

    // Громкость одного приложения в микшере: «яндекс музыка», «дискорд», «браузер».
    // delta — на сколько процентов изменить; set >= 0 — поставить ровно; mute: -1 не трогать, 0 включить звук, 1 выключить
    struct AppVolume {
        bool found = false;
        std::string name;   // как приложение называет себя («Яндекс Музыка»)
        int percent = 0;    // громкость после изменения
        bool muted = false;
    };
    AppVolume ChangeAppVolume(const std::string& spokenApp, int delta, int set = -1, int mute = -1);

    std::string BatteryReport();          // «Заряд 76%, заряжается…»
    std::string LoadReport();             // «Процессор загружен на 12%, памяти занято 9 из 16 гигабайт»

    std::string Clipboard();              // текст из буфера обмена
    std::string CopySelection();          // скопировать выделенное в активном окне и вернуть текст
}
