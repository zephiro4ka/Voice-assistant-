#pragma once
// Снимок экрана: монитор, на котором сейчас активное окно (там же, где задача), в JPEG.

#include <cstdint>
#include <string>
#include <vector>

namespace Screen {
    // maxWidth — уменьшить, если шире (для нейросети хватает 1280: быстрее и не хуже читается)
    std::vector<uint8_t> CaptureJpeg(int maxWidth = 0, int quality = 85);
    struct MonitorArea { int left = 0, top = 0, width = 0, height = 0; };
    int MonitorCount();                          // мониторы слева направо
    bool Monitor(int index, MonitorArea& area);
    // Конкретный монитор, вместе с курсором — для удалённого рабочего стола
    std::vector<uint8_t> CaptureMonitorJpeg(int index, int maxWidth, int quality);
    std::string Base64(const std::vector<uint8_t>& data);
}
