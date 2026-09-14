"""Capture serial output, optionally restarting an ESP USB Serial/JTAG device."""

import argparse
import pathlib
import time

import serial


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("port")
parser.add_argument("--seconds", type=float, default=15)
parser.add_argument("--output", type=pathlib.Path, required=True)
parser.add_argument("--reset-usb", action="store_true")
parser.add_argument("--activate-console", action="store_true",
                    help="Send one CR after startup so the USB console enables buffered TX")
args = parser.parse_args()

port = serial.Serial(port=None, baudrate=115200, timeout=0.25)
port.dtr = False
port.rts = False
port.port = args.port
args.output.parent.mkdir(parents=True, exist_ok=True)
with port, args.output.open("xb") as output:
    if args.reset_usb:
        from esptool.reset import HardReset

        HardReset(port, uses_usb=True)()
    deadline = time.monotonic() + args.seconds
    activate_at = time.monotonic() + 1.0
    activated = not args.activate_console
    total = 0
    while time.monotonic() < deadline:
        if not activated and time.monotonic() >= activate_at:
            port.write(b'\r')
            activated = True
        data = port.read(max(1, port.in_waiting))
        if data:
            output.write(data)
            output.flush()
            total += len(data)
            print(data.decode("utf-8", errors="replace"), end="", flush=True)
    print(f"\nCaptured {total} bytes to {args.output}")
