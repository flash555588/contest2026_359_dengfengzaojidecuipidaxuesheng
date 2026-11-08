# C6 shared BLE/Wi-Fi SDIO fix — 2026-11-08

Fix torn cumulative counter snapshots when separate CMD52 byte reads overlap
an RX-length or TX-credit carry. Retry bounded inconsistent snapshots without
publishing invented bytes/credits. Keep the BLE poller alive across transient
register/DMA preparation errors, publish actual link health to STA/AP users,
and latch ambiguous FIFO completion as `ENETRESET` without replay or resetting
the shared controller underneath active HCI/RPC owners.

The actual old read primitive reproduces RX `0xff -> 0x100` as `0x1ff` and TX
credit `255 -> 256` as `511` under controlled timing. The new regression covers
carry, wrap, unrelated register bits, bounded churn, errors and RX/TX deltas.
This establishes an implementation race; it does not uniquely identify the
cause of the user's original intermittent whole-C6 loss.

## Validation

- Full P4 build and image/partition/BSS/prepared-source/linked-symbol audits
  passed. All 10 offline regression groups passed (`BUILD-CHECKS.json`).
- Original board application matched the frozen 2026-11-05 input-preload
  baseline byte-for-byte. Only the 14 files in `PATCH-MANIFEST.json` changed.
  Their committed source text matches the compiled candidate after Git line
  ending normalization. Unrelated working-tree changes were excluded.
- P4 app-only flash at `0x2000` passed independent image readback. The data
  region hash stayed unchanged before first candidate boot. C6 firmware was
  not reflashed. Runtime XIP hashes and the new BLE identity matched.
- Initial 100 encrypted bidirectional rounds plus 287 Wi-Fi HTTP requests
  passed, including bounded RX queue rejection and recovery. Another 10 rounds
  and 39 HTTP requests passed after software restart with saved pairing.
- Wi-Fi PUT/GET each transferred 1,310,720 bytes with matching SHA256 while
  376 uncached BLE reads succeeded.
- The supplemental **2,087.053-second (34m47s) continuous duplex pressure**
  used independent PC and board producers on one encrypted BLE connection.
  PC -> board: **29,849 packets / 7,283,156 bytes**, checked in sequence by
  per-frame FNV32 from complete board SD receipts. Board -> PC:
  **65,736 packets / 1,314,720 bytes**, notifications checked byte-for-byte.
  **6,811 concurrent Wi-Fi requests** succeeded with zero failures.
- No unexpected BLE disconnect, C6 reset/fault marker, HCI send error or shared
  link error was observed. All 256 recorded transport-health samples passed.
  No automatic reconnect or board restart was used. All accepted RX packets
  were drained and verified; temporary SD files and PC radio state restored.

The 164 explicit ATT `0x11` queue-full rejections were retried only after a
definite rejection. They are application backpressure, not C6 disconnections.
The existing serial command limit constrained board -> PC frames to 20 bytes;
PC -> board frames were 244 bytes. This is sustained packet/concurrency
validation, not a maximum-throughput benchmark.

## Evidence and limits

`FULL-DUPLEX-EVIDENCE.zip` contains complete SD receipts, live summary,
independent receipt/digest audit, controller source at launch, and the
documented finish-only controller correction. That correction paused probes
after pressure ended and renewed the expired portal session before retrieving
large evidence through the single-worker HTTP server. During-pressure BLE
traffic, integrity checks and fault detection were unchanged. The archive has
no firmware image, full-flash backup, user data, pairing code or HTTP token.

The test peer was a Windows PC acting as a BLE central, with the board as a
peripheral. Board-central interaction with a real external peer remains
unaccepted: Windows test-peripheral advertising aborted. Cold power cycling,
physical screen acceptance and unbounded endurance were not performed.
The old firmware's short control did not reproduce the original intermittent
fault. This bounded run did not reproduce it either and cannot prove it
impossible. Persistent ambiguous FIFO failure still requires a board restart.

Verified candidate image SHA256:
`40d0d18f83c7e05950803ff6b5b49daf324ef017e8a329021739f029019c3a0d`.

Protocol reference: [Espressif SDIO slave protocol](https://espressif.github.io/idf-extra-components/latest/esp_serial_slave_link/sdio_slave_protocol.html).
The carry analysis is of this local host implementation, not a vendor
diagnosis of this board's incident.
