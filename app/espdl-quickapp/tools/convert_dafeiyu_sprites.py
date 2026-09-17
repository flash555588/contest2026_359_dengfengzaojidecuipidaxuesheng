"""Convert upstream Da Fei Yu PNGs into one validated LVGL RGB565A8 bundle."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

from PIL import Image


MAGIC = b"DFYSPRT1"
VERSION = 1
HEIGHTS = (66, 88, 110)
VIEWS = (
    ("正面.png", False),
    ("背面.png", False),
    ("侧面.png", False),
    ("侧面.png", True),
)
HEADER_SIZE = 24
ENTRY_SIZE = 20


def rgb565a8(image: Image.Image) -> bytes:
    rgba = image.convert("RGBA")
    color = bytearray()
    alpha = bytearray()
    raw = rgba.tobytes()
    for index in range(0, len(raw), 4):
        red, green, blue, opacity = raw[index:index + 4]
        pixel = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        color += struct.pack("<H", pixel)
        alpha.append(opacity)
    return bytes(color + alpha)


def convert(sprites: Path) -> tuple[list[tuple[int, int, int, int, bytes]], str]:
    entries = []
    for view, (name, mirror) in enumerate(VIEWS):
        source_path = sprites / name
        if not source_path.is_file():
            raise FileNotFoundError(f"missing upstream sprite: {source_path}")
        source = Image.open(source_path).convert("RGBA")
        if mirror:
            source = source.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        for size_index, height in enumerate(HEIGHTS):
            width = max(1, round(source.width * height / source.height))
            scaled = source.resize((width, height), Image.Resampling.LANCZOS)
            entries.append((view, size_index, width, height, rgb565a8(scaled)))
    return entries, "RGB565 color plane followed by A8 alpha plane"


def make_bundle(sprites: Path) -> bytes:
    entries, _ = convert(sprites)
    table = bytearray()
    payload = bytearray()
    offset = HEADER_SIZE + ENTRY_SIZE * len(entries)
    for view, size_index, width, height, data in entries:
        table += struct.pack(
            "<BBHHHIII",
            view,
            size_index,
            0,
            width,
            height,
            offset,
            len(data),
            zlib.crc32(data),
        )
        payload += data
        offset += len(data)
    body = bytes(table + payload)
    header = struct.pack(
        "<8sIIII", MAGIC, VERSION, len(entries), HEADER_SIZE + len(body),
        zlib.crc32(body)
    )
    return header + body


def verify(bundle: bytes) -> None:
    if len(bundle) < HEADER_SIZE:
        raise ValueError("bundle is truncated")
    magic, version, count, total, checksum = struct.unpack_from("<8sIIII", bundle)
    if magic != MAGIC or version != VERSION or count != 12 or total != len(bundle):
        raise ValueError("bundle header mismatch")
    if zlib.crc32(bundle[HEADER_SIZE:]) != checksum:
        raise ValueError("bundle CRC mismatch")
    next_offset = HEADER_SIZE + count * ENTRY_SIZE
    for index in range(count):
        record = struct.unpack_from("<BBHHHIII", bundle, HEADER_SIZE + index * ENTRY_SIZE)
        view, size_index, flags, width, height, offset, length, crc = record
        if (view, size_index, flags) != (index // 3, index % 3, 0):
            raise ValueError(f"entry {index} identity mismatch")
        if offset != next_offset or length != width * height * 3:
            raise ValueError(f"entry {index} layout mismatch")
        data = bundle[offset:offset + length]
        if len(data) != length or zlib.crc32(data) != crc:
            raise ValueError(f"entry {index} CRC mismatch")
        next_offset += length
    if next_offset != len(bundle):
        raise ValueError("bundle has trailing data")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sprites", type=Path, required=True,
                        help="upstream dafeiyu-pet/sprites directory")
    parser.add_argument("--output", type=Path, required=True,
                        help="output dafeiyu.lvbin path")
    args = parser.parse_args()
    bundle = make_bundle(args.sprites)
    verify(bundle)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(bundle)
    print(f"wrote {args.output}: {len(bundle)} bytes, 12 RGB565A8 sprites")


if __name__ == "__main__":
    main()
