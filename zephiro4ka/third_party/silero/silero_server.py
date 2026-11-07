"""Голос Silero для Ани: маленький локальный сервер озвучки.

Аня запускает его скрыто и закрывает вместе с собой. Модель грузится один раз,
дальше каждая фраза озвучивается за доли секунды.

  GET  /health                              -> 200 "ok", когда модель загружена
  POST /tts  {"text", "speaker", "rate"}    -> звук: float32, моно, little-endian; частота — в заголовке X-Sample-Rate
  GET  /asr_health                          -> 200, когда распознавание речи загружено (иначе 503)
  POST /asr  float32 моно 16 кГц            -> {"text": "…"} — распознанная фраза с пунктуацией

Распознавание: GigaAM-v3 e2e CTC (Сбер, MIT) через onnx-asr — лучший открытый русский распознаватель,
работает на процессоре за доли секунды. Если пакет onnx-asr не установлен, /asr отвечает 503 и Аня
распознаёт речь сама (whisper).

Модель: Silero TTS v5_6_ru (https://github.com/snakers4/silero-models), лицензия CC BY-NC-SA 4.0.
"""
import argparse
import json
import os
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

import torch

SPEAKERS = {"baya", "kseniya", "xenia", "aidar", "eugene"}


def load_model(path: str):
    # Файл открываем сами: внутренний загрузчик PyTorch не понимает пути с кириллицей («C:\Херня\…»)
    with open(path, "rb") as file:
        data = file.read()
    import io
    model = torch.package.PackageImporter(io.BytesIO(data)).load_pickle("tts_models", "model")
    model.to(torch.device("cpu"))
    return model


def synthesize(model, text: str, speaker: str, rate: int) -> torch.Tensor:
    # ударения и «ё» модель ставит сама; старые версии не знают этих параметров — пробуем без них
    try:
        return model.apply_tts(text=text, speaker=speaker, sample_rate=rate, put_accent=True, put_yo=True)
    except TypeError:
        return model.apply_tts(text=text, speaker=speaker, sample_rate=rate)


ASR = {"model": None}


def load_asr(cache_dir: str) -> None:
    """GigaAM-v3 в фоне: голос уже работает, пока грузится распознавание."""
    try:
        os.environ.setdefault("HF_HUB_CACHE", cache_dir)  # модель — в папке models проекта, а не в профиле
        import numpy as np
        import onnx_asr
        try:
            model = onnx_asr.load_model("gigaam-v3-e2e-ctc", quantization="int8")  # быстрее и в 4 раза меньше памяти
        except Exception:
            model = onnx_asr.load_model("gigaam-v3-e2e-ctc")
        model.recognize(np.zeros(16000, dtype=np.float32), sample_rate=16000)  # прогрев
        ASR["model"] = model
        print("asr ready", flush=True)
    except Exception as error:
        print(f"asr unavailable: {error}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True)
    parser.add_argument("--port", type=int, default=18081)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--asr-cache", default="")
    args = parser.parse_args()
    if args.asr_cache:
        threading.Thread(target=load_asr, args=(args.asr_cache,), daemon=True).start()

    torch.set_num_threads(args.threads)
    model = load_model(args.model)
    synthesize(model, "Привет.", "xenia", 48000)  # прогрев: первая настоящая фраза не будет тормозить

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):  # без шума в консоли
            pass

        def do_GET(self):
            if self.path == "/health":
                self._reply(200, b"ok", "text/plain")
            elif self.path == "/asr_health":
                self._reply(200 if ASR["model"] else 503, b"ok" if ASR["model"] else b"loading", "text/plain")
            else:
                self._reply(404, b"not found", "text/plain")

        def do_POST(self):
            if self.path == "/asr":
                return self._asr()
            if self.path != "/tts":
                return self._reply(404, b"not found", "text/plain")
            try:
                request = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))).decode("utf-8"))
                text = str(request.get("text", "")).strip()[:900]
                speaker = request.get("speaker", "xenia")
                rate = int(request.get("rate", 48000))
                if speaker not in SPEAKERS:
                    speaker = "xenia"
                if rate not in (8000, 24000, 48000):
                    rate = 48000
                if not text:
                    return self._reply(400, b"empty text", "text/plain")
                audio = synthesize(model, text, speaker, rate)
                body = audio.to(torch.float32).contiguous().numpy().astype("<f4").tobytes()
                self._reply(200, body, "application/octet-stream", {"X-Sample-Rate": str(rate)})
            except Exception as error:  # отдаём текст ошибки — Аня запишет его в журнал
                self._reply(500, str(error).encode("utf-8"), "text/plain")

        def _asr(self):
            if not ASR["model"]:
                return self._reply(503, b"asr not loaded", "text/plain")
            try:
                import numpy as np
                raw = self.rfile.read(int(self.headers.get("Content-Length", 0)))
                wave = np.frombuffer(raw, dtype="<f4").astype(np.float32)
                text = ASR["model"].recognize(wave, sample_rate=16000) if wave.size else ""
                self._reply(200, json.dumps({"text": text}, ensure_ascii=False).encode("utf-8"), "application/json")
            except Exception as error:
                self._reply(500, str(error).encode("utf-8"), "text/plain")

        def _reply(self, code, body, content_type, extra=None):
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            for key, value in (extra or {}).items():
                self.send_header(key, value)
            self.end_headers()
            self.wfile.write(body)

    server = HTTPServer(("127.0.0.1", args.port), Handler)
    print("silero ready", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    sys.exit(main())
