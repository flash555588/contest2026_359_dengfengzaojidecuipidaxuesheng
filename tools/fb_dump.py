#!/usr/bin/env python3
"""Dump the ESP32-P4 /dev/fb0 framebuffer over the NSH serial console.

Usage:
  python tools\\fb_dump.py --port COM7 --out fb.raw

The firmware must have CONFIG_NSH_DD enabled.  The script resets the board,
waits for the NSH prompt, runs `dd if=/dev/fb0 of=/dev/console bs=2048
count=600`, captures the raw UART stream, extracts the 1228800-byte RGB565
payload and writes it to --out.
"""

import argparse
import time
from pathlib import Path

import serial

FB_BYTES = 1024 * 600 * 2
CMD = b"dd if=/dev/fb0 of=/dev/console bs=2048 count=600\r\n"
CMD_TAIL = b"count=600"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--out", default="fb.raw")
    parser.add_argument(
        "--capture",
        help="optional full UART capture path (default: <out>.capture)",
    )
    parser.add_argument("--seconds", type=float, default=150.0)
    parser.add_argument("--no-reset", action="store_true")
    parser.add_argument(
        "--desktop-pid",
        type=int,
        help="stop this verified desktop PID before capture",
    )
    args = parser.parse_args()

    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    if args.desktop_pid is not None and args.desktop_pid <= 0:
        parser.error("--desktop-pid must be positive")

    output_path = Path(args.out)
    capture_path = Path(args.capture or f"{args.out}.capture")

    ser = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    ser.dtr = False
    ser.rts = False
    ser.port = args.port
    ser.open()
    try:
        if not args.no_reset:
            ser.dtr = False
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False
            time.sleep(0.1)

        ser.reset_input_buffer()
        if args.no_reset:
            ser.write(b"\r\n")
            ser.flush()

        # Wait for a prompt, then issue the dump command.
        buf = bytearray()
        deadline = time.monotonic() + 30.0
        while b"nsh>" not in buf and time.monotonic() < deadline:
            data = ser.read(ser.in_waiting or 1)
            if data:
                buf.extend(data)
                if len(buf) > 8192:
                    del buf[:-8192]

        if b"nsh>" not in buf:
            print("ERROR: no NSH prompt", flush=True)
            return 1

        if args.desktop_pid is not None:
            ser.write(f"kill {args.desktop_pid}\r\n".encode("ascii"))
            ser.flush()
            time.sleep(0.5)
            # Drain anything the kill printed (usually nothing) and wait
            # for the prompt to come back.
            while True:
                data = ser.read(ser.in_waiting or 1)
                if not data:
                    break
                if len(data) > 4096:
                    break
            time.sleep(0.3)
            ser.reset_input_buffer()

        ser.write(CMD)
        ser.flush()

        raw = bytearray()
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            data = ser.read(ser.in_waiting or 1)
            if not data:
                continue
            raw.extend(data)
            if len(raw) > FB_BYTES + 512:
                break

        with capture_path.open("wb") as fh:
            fh.write(bytes(raw))

        # The echoed command appears before the payload.  Take the first
        # occurrence of the tail that still leaves FB_BYTES after it.
        start = None
        idx = raw.find(CMD_TAIL)
        while idx != -1:
            if len(raw) - idx - len(CMD_TAIL) >= FB_BYTES + 8:
                start = idx + len(CMD_TAIL)
                break
            idx = raw.find(CMD_TAIL, idx + 1)

        if start is None:
            print(f"ERROR: payload start not found (captured {len(raw)} bytes)",
                  flush=True)
            return 1

        # Skip only the command echo's line ending.  Do not use lstrip():
        # 0x0a and 0x0d are valid bytes in the first RGB565 pixel.

        for ending in (b"\r\r\n", b"\r\n", b"\n"):
            if raw[start:start + len(ending)] == ending:
                start += len(ending)
                break

        payload = bytes(raw[start:])
        payload = payload[:FB_BYTES]
        if len(payload) != FB_BYTES:
            print(f"ERROR: short payload {len(payload)}", flush=True)
            return 1

        with output_path.open("wb") as fh:
            fh.write(payload)
        print(
            f"OK: wrote {len(payload)} bytes to {output_path}; "
            f"capture={capture_path}",
            flush=True,
        )
    finally:
        ser.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
