# Camera Capture Candidate

Date: 2026-09-13. Target: ESP32-P4 v3. Offline candidate, not flashed.

## Implementation

The existing three-page RGB565 USERPTR preview and asynchronous cleanup remain.
The capture worker composes a bottom toolbar into a dequeued page before pan.
The center shutter is white when idle, yellow while saving, green after success,
and red after failure. The right-hand cross stops preview asynchronously.
Pointer input is polled on the UI thread without running LVGL rendering.
Entry requires a release; dragging outside the pressed target cancels the tap.

`system.camera.capture()` queues one request and returns a boolean.
`system.camera.photoStatus()` returns `{status, path}`; status is 0 idle,
1 pending/saving, 2 saved, or a negative errno. `active()` reports display ownership,
including asynchronous cleanup. These APIs are wired into the camera Quick App.

The next dequeued frame is downsampled to a 512x300 RGB565 BITFIELDS BMP,
307266 bytes, before toolbar composition. Files use exclusive creation at
`/data/photos/camera-NNNNNNNN.bmp`; existing photos are not overwritten.
Writing, flushing, and syncing run on the capture worker, not the UI thread.
Cancellation is checked between scanlines; failed partial files are unlinked.
The capture worker stack is explicitly 16 KiB rather than the 2 KiB default.

## Verification

Normal QuickJS/LVGL runtime and camera suite: 28/28 passed.
Address/UndefinedBehavior Sanitizer suite: 28/28 passed.
Evidence: `tests/normal.log` and `tests/sanitizer.log`.
New tests inspect the BMP header and every pixel, exercise pointer-driver
shutter input, drag cancellation and touch stop. Existing lifecycle and
100-cycle capture/stop/reopen tests remain enabled.
Generated camera resource matches the JS source; runtime source matches WSL.
RISC-V compilation and full firmware link completed successfully.

An initial all-target host build stopped on existing Home Assistant
`-Werror=maybe-uninitialized` diagnostics. That unrelated module was not edited;
the results above cover the dedicated QPK/camera targets, not all desktop tests.

## Firmware

- `v3/nuttx.bin`: 3831664 bytes.
- SHA-256: `577148a366e0d1b88541b673f1f4f15920040928db516306ea75529dd515165e`.
- `v3/nuttx.elf` and `v3/resolved.config` are included.
- Config SHA-256: `2056361b9a5e45c95b126a256d13b06e03ac68b1f7bb2e4c6021c4532a20d172`.
- Flash offset remains 0x2000; 354448 bytes remain before /data at 0x400000.
- Build tree: `/home/flash/glass-desktop-resources-20260913`.

## Remaining Acceptance

This is a functional implementation candidate, not hardware acceptance.
Verify actual pointer coordinates, cache/DMA coherence, capture stack margin,
preview frame rate, storage mount/capacity, saved image colors and long runs.
Saving can pause preview and slow storage can prolong background cleanup.
No JPEG encoder, photo gallery, storage quota, or automatic photo deletion is
implemented. Full-disk and power-loss behavior still need fault-injection and
board testing; a power loss may leave a partial file. Existing uncertain-device
cleanup continues to retain display ownership rather than resume unsafe drawing.
No v1 image was built. No flashing, Git commit or push was performed.
