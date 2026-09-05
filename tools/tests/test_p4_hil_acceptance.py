"""Host-side regression tests; these do not establish hardware acceptance."""

# SPDX-License-Identifier: Apache-2.0

import argparse
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import p4_hil_acceptance as hil


class EvidenceParserTests(unittest.TestCase):
    def test_ping_requires_positive_complete_summary(self):
        hil.check_ping(b"10 packets transmitted, 10 received, 0% packet loss")
        for output in (
            b"nsh> ",
            b"nsh: ping: command not found\nnsh> ",
            b"10 packets transmitted, 9 received, 10% packet loss",
            b"0 packets transmitted, 0 received, 0% packet loss",
        ):
            with self.subTest(output=output), self.assertRaises(hil.AcceptanceError):
                hil.check_ping(output)

    def test_throughput_requires_measurement(self):
        hil.check_throughput(b"0.00-60.00 sec 100000 Bytes 12.50 Mbits/sec")
        for output in (b"nsh> ", b"command not found", b"0.00 Mbits/sec"):
            with self.subTest(output=output), self.assertRaises(hil.AcceptanceError):
                hil.check_throughput(output)

    def test_command_failure_is_not_successful_prompt(self):
        for output in (b"nsh: open failed: 2", b"unknown command", b"PANIC"):
            with self.subTest(output=output), self.assertRaises(hil.AcceptanceError):
                hil.check_command(output, "test")

    def test_dsi_requires_valid_zero_error_snapshot(self):
        record = (
            b'{"device":"/dev/dsi-diag0","sampled_at_ticks":10,'
            b'"frames":20,"underruns":0,"worker_queue_errors":0}'
        )
        self.assertEqual(hil.parse_dsi(record)["frames"], 20)
        for output in (
            b"nsh> ",
            record.replace(b'"underruns":0', b'"underruns":1'),
            record.replace(b'"worker_queue_errors":0', b'"worker_queue_errors":1'),
        ):
            with self.subTest(output=output), self.assertRaises(hil.AcceptanceError):
                hil.parse_dsi(output)

    def test_empty_or_nonfinite_runs_are_rejected(self):
        self.assertEqual(hil.positive_int("100"), 100)
        self.assertEqual(hil.positive_float("0.5"), 0.5)
        for value in ("0", "-1"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                hil.positive_int(value)
        for value in ("0", "-1", "nan", "inf"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                hil.positive_float(value)


if __name__ == "__main__":
    unittest.main()
