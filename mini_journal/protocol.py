"""Independent implementation of the ESC/POS raster protocol documented by phomemo-tools.

626 pixels are padded to 632 (79 bytes). Padding bits are always white.
Historical jobs with an optional prefix remain readable.
"""
import struct

from PIL import Image

HEADER = b"\x1b\x40\x1b\x61\x01"
PREFIX = b"\x10\xff\xfe\x01"


def encode(image, feed_lines=2):
    if image.width != 626 or not 1 <= image.height <= 2362:
        raise ValueError("Format M02 Pro invalide")
    if not 0 <= feed_lines <= 10:
        raise ValueError("Avance papier invalide")
    mono = image.convert("1", dither=Image.Dither.NONE)
    # Pillow mode 1: white=1. Wire: black=1, most significant bit first.
    payload = bytearray(byte ^ 0xff for byte in mono.tobytes())
    stride = (image.width + 7) // 8
    for row in range(image.height):
        payload[(row + 1) * stride - 1] &= 0xc0
    stream = bytearray(HEADER)
    for row in range(0, image.height, 255):
        count = min(255, image.height - row)
        stream += b"\x1d\x76\x30\x00" + struct.pack("<HH", stride, count)
        stream += payload[row * stride:(row + count) * stride]
    stream += b"\x1b\x64" + bytes([feed_lines])
    return bytes(stream)


def decode(data):
    """Validate and reconstruct a job for tests and inspection (no printer needed)."""
    offset = len(PREFIX) if data.startswith(PREFIX) else 0
    if data[offset:offset + len(HEADER)] != HEADER:
        raise ValueError("En-tête invalide")
    offset += len(HEADER)
    raster, total, stride = bytearray(), 0, 79
    while data[offset:offset + 4] == b"\x1d\x76\x30\x00":
        if len(data) - offset < 8:
            raise ValueError("En-tête raster tronqué")
        width, count = struct.unpack("<HH", data[offset + 4:offset + 8])
        if width != stride or not 1 <= count <= 255:
            raise ValueError("Bloc raster invalide")
        offset += 8
        block = data[offset:offset + width * count]
        if len(block) != width * count or any(block[i] & 0x3f for i in range(78, len(block), 79)):
            raise ValueError("Bloc tronqué ou bourrage non blanc")
        raster.extend(b ^ 0xff for b in block)
        offset += len(block)
        total += count
    if (not 1 <= total <= 2362 or len(data) != offset + 3
            or data[offset:offset + 2] != b"\x1b\x64" or data[-1] > 10):
        raise ValueError("Fin de flux invalide")
    return Image.frombytes("1", (626, total), bytes(raster))
