# Desktop BLE Candidate

Target: ESP32-P4 v3 with C6 Hosted. Not flashed or board-tested.

The Bluetooth desktop page now calls the native NimBLE service. It starts HCI
and the host on background threads, scans for ten seconds, lists up to twelve
devices by address and RSSI, connects to a selected device with a ten-second
timeout, and disconnects an established connection. Selection is enabled after
scanning stops. Page deletion requests scan cancellation and deletes its timer.
The stack remains resident across page changes; closing a page preserves an
established connection. Initialization after partial failure does not blindly
restart the stack; a reboot may be required.

GAP commands run on the NimBLE event queue. Workers never touch LVGL objects.
The official example entry point is replaced, avoiding two host instances.
The service uses explicit 32 KiB host/HCI pthread stacks; the upstream NPL task
helper ignores its stack-size argument in this pinned version.

Verification: actual LVGL desktop interaction regression passed with a service
fixture, covering startup gating, scan/stop, selection, connection, disconnection
and page deletion. Full RISC-V firmware compilation and link succeeded. ELF
contains glass_ble_start, glass_ble_scan, glass_ble_disconnect, ble_gap_disc,
and ble_gap_connect. The UI fixture does not validate native GAP concurrency
or real radio behavior.

Files: v3/nuttx.bin, v3/nuttx.elf, v3/resolved.config and build.log.
Binary size: 3910336 bytes. Flash offset 0x2000 leaves 275776 bytes before /data.
SHA-256: c348c31b1f6c55921f3ff2818dd6f8357aa9596905d317cbcf1614bd47883c14.
Build tree: /home/flash/glass-ble-v3-20260913.
Integration helper: camera-app/glass-desktop/tools/integrate_ble.py.

Remaining: hardware HCI startup, radio scanning, connection and Wi-Fi coexistence
acceptance; native service fault-injection tests; device-name parsing; pairing
UI and general GATT discovery/read/write/notifications. This candidate does not
claim these unfinished features. Do not flash to v1. No Git commit/push made.
