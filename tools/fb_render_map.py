#!/usr/bin/env python3
"""Render an RGB565 framebuffer dump as a coarse ASCII color map."""

import argparse
import struct
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("raw")
    parser.add_argument("--w", type=int, default=1024)
    parser.add_argument("--h", type=int, default=600)
    parser.add_argument("--step", type=int, default=16)
    args = parser.parse_args()

    if args.w <= 0 or args.h <= 0 or args.step <= 0:
        parser.error("--w, --h and --step must be positive")

    with Path(args.raw).open("rb") as fh:
        data = fh.read()

    if len(data) < args.w * args.h * 2:
        print(f"short file: {len(data)}", file=sys.stderr)
        return 1

    def pixel(x: int, y: int):
        off = (y * args.w + x) * 2
        v = struct.unpack_from("<H", data, off)[0]
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        return r, g, b

    palette = [
        ("#", (0x10, 0x20, 0x30)),   # dark text
        ("W", (0xFF, 0xFF, 0xFF)),   # white
        (".", (0xDD, 0xE8, 0xF0)),   # light bg
        ("T", (0x1E, 0x88, 0xE5)),   # title blue
        ("t", (0x21, 0x96, 0xF3)),   # lvgl default button blue
        ("g", (0x45, 0x5A, 0x64)),   # gray
        ("o", (0xFB, 0x8C, 0x00)),   # orange
        ("R", (0xFF, 0x00, 0x00)),
        ("G", (0x00, 0xFF, 0x00)),
        ("B", (0x00, 0x00, 0xFF)),
        ("Y", (0xFF, 0xFF, 0x00)),
        ("C", (0x00, 0xFF, 0xFF)),
        ("M", (0xFF, 0x00, 0xFF)),
        ("k", (0x00, 0x00, 0x00)),   # black
    ]

    def classify(x: int, y: int) -> str:
        r, g, b = pixel(x, y)
        r = r * 255 // 31
        g = g * 255 // 63
        b = b * 255 // 31
        best = "?"
        best_d = 1 << 30
        for ch, (pr, pg, pb) in palette:
            d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
            if d < best_d:
                best_d = d
                best = ch
        return best

    cols = args.w // args.step
    rows = args.h // args.step
    for ry in range(rows):
        line = []
        for rx in range(cols):
            # Use the center pixel of each cell.
            line.append(classify(rx * args.step + args.step // 2,
                                 ry * args.step + args.step // 2))
        print("".join(line))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
