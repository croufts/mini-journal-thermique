"""Generate the single short ESP32 SPP validation job."""
import struct
from pathlib import Path
from PIL import Image, ImageDraw
from mini_journal.layout import font

ROOT = Path(__file__).resolve().parents[1]


def generate():
    image = Image.new("1", (576, 160), 1)
    draw = ImageDraw.Draw(image)
    draw.text((20, 8), "ESP32 SPP / 576 points", font=font(27, True), fill=0)
    draw.text((20, 45), "Bonjour. Texte normal.", font=font(26), fill=0)
    draw.text((20, 80), "Texte gras : 0123456789", font=font(28, True), fill=0)
    draw.rectangle((20, 117, 554, 124), fill=0)
    draw.line((20, 147, 554, 147), fill=0, width=2)
    payload = bytes(value ^ 255 for value in image.tobytes())
    data = b"\x1b\x40\x1b\x61\x01"
    data += b"\x1d\x76\x30\x00" + struct.pack("<HH", 72, 160) + payload
    data += b"\x1b\x64\x02"
    lines = ["#pragma once", "#include <Arduino.h>",
             "// Generated with python -m scripts.generate_calibration.",
             "const uint8_t calibrationTicket[] PROGMEM = {"]
    lines += ["  " + ",".join(f"0x{x:02x}" for x in data[i:i+24]) + "," for i in range(0, len(data), 24)]
    lines.append("};")
    (ROOT / "firmware/include/calibration_ticket.h").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (ROOT / "out").mkdir(exist_ok=True)
    image.save(ROOT / "out/calibration.png")
    print(f"Calibration: {len(data)} bytes, 160 rows (~1.35 cm before feed)")


if __name__ == "__main__":
    generate()
