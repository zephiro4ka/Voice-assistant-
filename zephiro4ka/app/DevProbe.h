#pragma once
// Проверка интерфейса без рук: если в anya.ini [debug] probe=1, Аня выполняет команды из devprobe.txt рядом с exe
// и сохраняет снимки кадра. Нужна, чтобы проверять внешний вид и плавность после изменений.
//   wait 30            — подождать 30 кадров
//   settings 1         — открыть настройки на разделе 1;  settings close — закрыть
//   history on|off     — панель разговора
//   mouse 120 300      — навести мышь (координаты окна);  scroll 3 — прокрутить колесом вниз
//   down / up          — зажать / отпустить левую кнопку мыши (перетаскивание)
//   ini backdrop 2     — поменять настройку в [anya] и перечитать
//   shot name          — сохранить кадр в name.bmp
//   ask где погода     — сказать Ане фразу (как голосом)
//   perf               — записать в anya.log время кадров

namespace DevProbe {
    void Start();
    void BeforeFrame();   // до ImGui::NewFrame — мышь и клавиши
    void AfterRender();   // после отрисовки, до Present — снимок
    void FrameTime(double workMs, double intervalMs);
}
