"""Trigger the ESPHome page on the LVGL thread and collect bounded UART logs."""
import time
from pathlib import Path
import serial

with Path(__file__).with_suffix('.log').open('wb') as log:
    with serial.Serial('COM7', 115200, timeout=0.2) as uart:
        def receive(seconds):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                data = uart.read(4096)
                if data:
                    log.write(data)
                    log.flush()
                    print(data.decode('utf-8', errors='replace'), end='', flush=True)
        receive(5)
        uart.write(b'\ndesktop esphome\n')
        receive(20)
        uart.write(b'\nfree\n')
        receive(3)
