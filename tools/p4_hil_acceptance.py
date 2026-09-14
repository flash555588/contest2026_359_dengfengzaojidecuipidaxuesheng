#!/usr/bin/env python3
"""Run repeatable ESP32-P4 serial HIL acceptance scenarios."""

# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import json
import math
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path
from typing import Pattern

import serial


PROMPT = re.compile(rb"nsh>\s*")
FIRST_FRAME = re.compile(rb"camera first frame displayed")
FATAL = re.compile(
    rb"PANIC|ASSERT(?:ION)? FAILED|Guru Meditation|watchdog|"
    rb"dqerr=[1-9][0-9]*|qbuferr=[1-9][0-9]*|panerr=[1-9][0-9]*",
    re.IGNORECASE,
)
COMMAND_ERROR = re.compile(
    rb"command not found|nsh:.*(?:failed|not found|invalid)|"
    rb"unrecognized option|unknown command",
    re.IGNORECASE,
)
DSI_JSON = re.compile(
    r'\{"device":"[^"]+","sampled_at_ticks":(?P<ticks>\d+),'
    r'"frames":(?P<frames>\d+),"underruns":(?P<underruns>\d+),'
    r'"worker_queue_errors":(?P<worker>\d+)\}',
)


class AcceptanceError(RuntimeError):
    """Raised when a HIL acceptance condition is not met."""


class NshSerial:
    """Small prompt-oriented NSH serial client with a raw evidence log."""

    def __init__(self, port: str, baud: int, log_path: Path) -> None:
        self._serial = serial.Serial(port=None, baudrate=baud, timeout=0.1)
        self._serial.dtr = False
        self._serial.rts = False
        self._serial.port = port
        log_path.parent.mkdir(parents=True, exist_ok=True)
        self._log = log_path.open("w", encoding="utf-8", newline="\n")
        self.transcript = bytearray()

    def __enter__(self) -> "NshSerial":
        self._serial.open()
        return self

    def __exit__(self, *_exc: object) -> None:
        self._serial.close()
        self._log.close()

    def _record(self, data: bytes) -> None:
        text = data.decode("utf-8", errors="backslashreplace")
        self.transcript.extend(data)
        self._log.write(text)
        self._log.flush()
        sys.stdout.write(text)
        sys.stdout.flush()

    def note(self, message: str) -> None:
        line = f"\n[host {time.time():.3f}] {message}\n"
        self._log.write(line)
        self._log.flush()
        print(line, end="")

    def _read_until(self, pattern: Pattern[bytes], timeout: float) -> bytes:
        start = len(self.transcript)
        deadline = time.monotonic() + timeout
        window = bytearray()

        while time.monotonic() < deadline:
            data = self._serial.read(self._serial.in_waiting or 1)
            if not data:
                continue

            self._record(data)
            window.extend(data)
            if len(window) > 65536:
                del window[:-65536]
            if pattern.search(window):
                return bytes(self.transcript[start:])

        raise AcceptanceError(
            f"timed out after {timeout:.1f}s waiting for {pattern.pattern!r}"
        )

    def drain(self, seconds: float) -> bytes:
        start = len(self.transcript)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            data = self._serial.read(self._serial.in_waiting or 1)
            if data:
                self._record(data)
        return bytes(self.transcript[start:])

    def reset(self, timeout: float = 45.0) -> bytes:
        self.note("reset board and wait for NSH")
        self._serial.reset_input_buffer()
        self._serial.dtr = False
        self._serial.rts = True
        time.sleep(0.1)
        self._serial.rts = False
        return self._read_until(PROMPT, timeout)

    def attach(self, timeout: float = 10.0) -> bytes:
        self.note("attach to running NSH")
        self._serial.write(b"\r\n")
        self._serial.flush()
        return self._read_until(PROMPT, timeout)

    def command(self, command: str, timeout: float = 30.0) -> bytes:
        self.note(f"command: {command}")
        self._serial.write(command.encode("ascii") + b"\r\n")
        self._serial.flush()
        output = self._read_until(PROMPT, timeout)
        check_command(output, command)
        return output

    def wait_for(self, pattern: Pattern[bytes], timeout: float) -> bytes:
        return self._read_until(pattern, timeout)


def check_fatal(data: bytes, context: str) -> None:
    match = FATAL.search(data)
    if match:
        raise AcceptanceError(
            f"{context}: fatal/error marker found: {match.group().decode(errors='replace')}"
        )


def check_command(data: bytes, context: str) -> None:
    """Reject shell failures even when NSH returns its usual prompt."""
    check_fatal(data, context)
    if COMMAND_ERROR.search(data):
        raise AcceptanceError(f"{context}: NSH rejected or failed the command")


def check_ping(data: bytes) -> None:
    """Require an explicit complete, loss-free NuttX ping summary."""
    check_command(data, "ping")
    match = re.search(
        rb"(\d+) packets transmitted, (\d+) (?:packets )?received, "
        rb"(\d+)% packet loss", data
    )
    if not match:
        raise AcceptanceError("ping returned no recognized completion summary")
    sent, received, loss = map(int, match.groups())
    if sent <= 0 or received != sent or loss != 0:
        raise AcceptanceError("ping did not receive every transmitted packet")


def check_throughput(data: bytes) -> None:
    """Require a positive measured rate, not just absence of an error."""
    check_command(data, "iperf")
    rates = re.findall(rb"(\d+(?:\.\d+)?)\s+[KMG]?bits/sec", data)
    if not rates or not any(float(rate) > 0 for rate in rates):
        raise AcceptanceError("iperf returned no positive throughput measurement")
    if re.search(rb"error|failed", data, re.IGNORECASE):
        raise AcceptanceError("iperf command reported an error")


def parse_dsi(data: bytes) -> dict[str, int]:
    matches = list(DSI_JSON.finditer(data.decode("utf-8", errors="replace")))
    if not matches:
        raise AcceptanceError("dsi_diag did not return a JSON statistics object")

    match = matches[-1]
    stats = {name: int(match.group(name)) for name in match.groupdict()}
    if stats["underruns"] != 0 or stats["worker"] != 0:
        raise AcceptanceError(f"DSI error counters are non-zero: {stats}")
    return stats


def start_camera(client: NshSerial, timeout: float) -> bytes:
    output = client.command("desktop camera", timeout=timeout)
    if not FIRST_FRAME.search(output):
        output += client.wait_for(FIRST_FRAME, timeout)
    check_fatal(output, "camera start")
    return output


def camera_cycles(client: NshSerial, cycles: int, dwell: float) -> None:
    baseline = client.command("free")
    client.command("dsi_diag --reset --json")
    start = len(client.transcript)

    for cycle in range(1, cycles + 1):
        client.note(f"camera cycle {cycle}/{cycles}")
        start_camera(client, timeout=20.0)
        check_fatal(client.drain(dwell), f"camera cycle {cycle}")
        check_fatal(
            client.command("desktop camera-stop", timeout=20.0),
            f"camera stop cycle {cycle}",
        )
        if cycle % 10 == 0:
            client.command("free")

    final_free = client.command("free")
    dsi = parse_dsi(client.command("dsi_diag --json"))
    if dsi["frames"] == 0:
        raise AcceptanceError("camera cycles produced no DSI frames")
    check_fatal(bytes(client.transcript[start:]), "camera cycle run")
    client.note(
        "camera cycle checks PASS (heap trend review still required); "
        f"frames={dsi['frames']} underruns={dsi['underruns']} "
        f"worker_queue_errors={dsi['worker']}"
    )
    client.note(f"baseline free output bytes={len(baseline)} final={len(final_free)}")


def soak(
    client: NshSerial,
    minutes: float,
    sample_seconds: float,
    ping_host: str | None,
    iperf_command: str | None,
) -> None:
    client.command("dsi_diag --reset --json")
    start_camera(client, timeout=20.0)
    start = len(client.transcript)
    deadline = time.monotonic() + minutes * 60.0
    sample = 0
    previous_frames = 0

    if iperf_command:
        client.command(f"{iperf_command} &", timeout=10.0)

    while time.monotonic() < deadline:
        client.drain(min(sample_seconds, max(0.0, deadline - time.monotonic())))
        sample += 1
        client.note(f"soak sample {sample}")
        client.command("free")
        client.command("ifconfig")
        if ping_host:
            check_ping(client.command(f"ping -c 5 {ping_host}", timeout=30.0))
        stats = parse_dsi(client.command("dsi_diag --json"))
        if stats["frames"] <= previous_frames:
            raise AcceptanceError("DSI frame count did not advance during soak")
        previous_frames = stats["frames"]
        check_fatal(bytes(client.transcript[start:]), "concurrent soak")

    client.command("desktop camera-stop", timeout=20.0)
    dsi = parse_dsi(client.command("dsi_diag --json"))
    if iperf_command:
        check_throughput(bytes(client.transcript[start:]))
    client.note(
        "camera/DSI soak checks PASS (touch/load/heap review still required); "
        f"minutes={minutes:g} frames={dsi['frames']} underruns=0"
    )


def rtos(client: NshSerial) -> None:
    start = len(client.transcript)
    ostest = client.command("ostest", timeout=900.0)
    if b"ostest_main: Exiting with status 0" not in ostest:
        raise AcceptanceError("ostest did not exit with status 0")

    smp = client.command("smp", timeout=180.0)
    if b"CPU0" not in smp or b"CPU1" not in smp or b"Error" in smp:
        raise AcceptanceError("SMP test did not complete cleanly on CPU0 and CPU1")

    timer = client.command("timerjitter -m 1000 10000", timeout=60.0)
    if b"timer jitter in 10000 run" not in timer or b"failed" in timer:
        raise AcceptanceError("timerjitter did not return a valid result")

    check_fatal(bytes(client.transcript[start:]), "RTOS test suite")
    client.note("ostest, SMP and 10,000-sample timerjitter PASS")


def ethernet(
    client: NshSerial, ping_host: str, iperf_command: str | None
) -> None:
    start = len(client.transcript)
    ifconfig = client.command("ifconfig")
    if b"eth0" not in ifconfig or b"UP" not in ifconfig:
        raise AcceptanceError("eth0 is absent or not UP")

    ping = client.command(f"ping -c 10 {ping_host}", timeout=60.0)
    check_ping(ping)

    if iperf_command:
        iperf = client.command(iperf_command, timeout=180.0)
        check_throughput(iperf)

    check_fatal(bytes(client.transcript[start:]), "Ethernet test")
    client.note("Ethernet link, ping and requested throughput test PASS")


def boot_cycles(
    client: NshSerial,
    cycles: int,
    power_cycle_command: str | None,
    forbid: list[str],
) -> None:
    patterns = [re.compile(item.encode("utf-8"), re.IGNORECASE) for item in forbid]

    for cycle in range(1, cycles + 1):
        client.note(f"boot cycle {cycle}/{cycles}")
        if power_cycle_command:
            subprocess.run(shlex.split(power_cycle_command), check=True, timeout=60)
            boot = client._read_until(PROMPT, 60.0)
        else:
            boot = client.reset(timeout=60.0)
        boot += client.drain(2.0)
        check_fatal(boot, f"boot cycle {cycle}")
        for pattern in patterns:
            if pattern.search(boot):
                raise AcceptanceError(
                    f"boot cycle {cycle}: forbidden marker {pattern.pattern!r}"
                )

    client.note(f"boot cycles PASS; cycles={cycles}")


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def positive_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--no-reset", action="store_true")
    subparsers = parser.add_subparsers(dest="scenario", required=True)

    cycles = subparsers.add_parser("camera-cycles")
    cycles.add_argument("--cycles", type=positive_int, default=100)
    cycles.add_argument("--dwell", type=positive_float, default=1.0)

    soak_parser = subparsers.add_parser("soak")
    soak_parser.add_argument("--minutes", type=positive_float, default=30.0)
    soak_parser.add_argument("--sample-seconds", type=positive_float, default=60.0)
    soak_parser.add_argument("--ping-host")
    soak_parser.add_argument("--iperf-command")

    subparsers.add_parser("rtos")

    net = subparsers.add_parser("ethernet")
    net.add_argument("--ping-host", required=True)
    net.add_argument("--iperf-command")

    boots = subparsers.add_parser("boot-cycles")
    boots.add_argument("--cycles", type=positive_int, default=100)
    boots.add_argument("--power-cycle-command")
    boots.add_argument(
        "--forbid",
        action="append",
        default=["camera first frame displayed", "2048.*start"],
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        with NshSerial(args.port, args.baud, args.log) as client:
            if args.no_reset:
                client.attach()
            elif args.scenario != "boot-cycles":
                client.reset()

            metadata = {
                "scenario": args.scenario,
                "port": args.port,
                "baud": args.baud,
                "started_at": time.time(),
            }
            client.note(f"metadata: {json.dumps(metadata, sort_keys=True)}")

            if args.scenario == "camera-cycles":
                camera_cycles(client, args.cycles, args.dwell)
            elif args.scenario == "soak":
                soak(
                    client,
                    args.minutes,
                    args.sample_seconds,
                    args.ping_host,
                    args.iperf_command,
                )
            elif args.scenario == "rtos":
                rtos(client)
            elif args.scenario == "ethernet":
                ethernet(client, args.ping_host, args.iperf_command)
            else:
                boot_cycles(
                    client,
                    args.cycles,
                    args.power_cycle_command,
                    args.forbid,
                )
    except (AcceptanceError, OSError, serial.SerialException,
            subprocess.SubprocessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
