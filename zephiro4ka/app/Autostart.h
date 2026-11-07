#pragma once
// Запуск Ани вместе с Windows (раздел «Run» реестра текущего пользователя, без прав администратора).
// При автозапуске Аня стартует тихо — в трее, окно не открывается.

namespace Autostart {
    bool Enabled();
    void Set(bool enabled);
    constexpr const wchar_t* kFlag = L"--autostart";
}
