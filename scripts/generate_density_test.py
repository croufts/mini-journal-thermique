"""Three independent, short raster jobs for the optional PC BLE diagnostic."""
import json
import struct
from pathlib import Path

from PIL import Image, ImageDraw

from mini_journal.layout import font


def generate():
    output = Path(__file__).resolve().parents[1] / "out" / "density"
    output.mkdir(parents=True, exist_ok=True)
    jobs = []
    for label, density in (("M", 4), ("N", 8), ("O", 12)):
        image = Image.new("1", (576, 140), 1)
        draw = ImageDraw.Draw(image)
        draw.text((20, 8), f"{label} : chauffe {density} / 576 points", fill=0, font=font(27, True))
        draw.text((20, 45), "Bonjour Mathias. Texte normal.", fill=0, font=font(26))
        draw.text((20, 80), "Texte gras : 0123456789", fill=0, font=font(28, True))
        draw.rectangle((20, 117, 554, 124), fill=0)
        payload = bytes(value ^ 255 for value in image.tobytes())
        assert b"\x0a" not in payload
        data = b"\x1b\x40\x1b\x61\x00\x1f\x11\x02" + bytes([density])
        data += b"\x1d\x76\x30\x00" + struct.pack("<HH", 72, 140) + payload
        data += b"\x1b\x64\x02\x1b\x64\x02\x1f\x11\x08\x1f\x11\x0e\x1f\x11\x07\x1f\x11\x09"
        name = f"{label}.bin"
        (output / name).write_bytes(data)
        image.save(output / f"{label}.png")
        jobs.append({"label": label, "file": name})
    (output / "series.json").write_text(json.dumps(jobs, indent=2), encoding="utf-8")
    print("3 jobs, 420 rows (~3.6 cm before feeds), density overrides only in diagnostic")


if __name__ == "__main__":
    generate()
