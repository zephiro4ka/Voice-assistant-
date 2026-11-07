#pragma once
// Снимок экрана: монитор, на котором сейчас активное окно (там же, где задача), в JPEG.

#include <cstdint>
#include <string>
#include <vector>

namespace Screen {
    // maxWidth — уменьшить, если шире (для нейросети хватает 1280: быстрее и не хуже читается)
    std::vector<uint8_t> CaptureJpeg(int maxWidth = 0, int quality = 85);
    std::string Base64(const std::vector<uint8_t>& data);
}
