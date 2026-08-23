#!/usr/bin/env python3
"""Reset an ESP32-P4 board and capture a short OpenVela boot smoke test."""

import argparse
import sys
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--seconds", type=float, default=20.0)
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="attach to the running firmware without toggling EN",
    )
    parser.add_argument(
        "--command",
        action="append",
        default=[],
        help="NSH command to run after the first prompt (repeatable)",
    )
    args = parser.parse_args()

    commands = args.command or ["ls /dev", "free"]

    # Configure modem-control lines before opening the CP210x port.  Opening
    # Serial(port=...) first applies pyserial's default asserted DTR/RTS and
    # can reset the board even in --no-reset mode.
    ser = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    ser.dtr = False
    ser.rts = False
    ser.port = args.port
    ser.open()
    try:
        if not args.no_reset:
            # CP210x auto-reset wiring: hold EN low, then release it while IO0
            # remains high so the ROM performs a normal flash boot.
            ser.dtr = False
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False
            time.sleep(0.1)
        ser.reset_input_buffer()

        if args.no_reset and args.command:
            # A running NSH is normally sitting at a prompt that was printed
            # before we attached.  Request a fresh prompt so command mode
            # works without resetting the target.
            ser.write(b"\r\n")
            ser.flush()

        deadline = time.monotonic() + args.seconds
        prompt_window = bytearray()
        commands_sent = False

        while time.monotonic() < deadline:
            data = ser.read(ser.in_waiting or 1)
            if not data:
                continue

            sys.stdout.write(data.decode("utf-8", errors="backslashreplace"))
            sys.stdout.flush()
            prompt_window.extend(data)
            if len(prompt_window) > 256:
                del prompt_window[:-256]

            if not commands_sent and b"nsh>" in prompt_window:
                for command in commands:
                    ser.write(command.encode("ascii") + b"\r\n")
                    ser.flush()
                    time.sleep(0.25)
                commands_sent = True
    finally:
        ser.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
