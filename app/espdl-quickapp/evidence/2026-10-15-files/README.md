# File manager and album validation — 2026-10-15

The final local source tree was built for the ESP32-P4 v3 configuration. The firmware is 8,707,188 bytes, SHA-256 `7b32fe51a14dd21433739bbb6c6d6ccd194dc62360d3f58cb8011fb973ba06c4`. This update was not flashed or tested on hardware.

- Native LVGL file-manager and album runs each cover 26 light/dark screenshots, navigation and cleanup. Filesystem operations and media I/O in these UI runs use offline fixtures; PNG/JPEG/BMP and MOV/MP4 decoding have separate real backend tests.
- The file manager reuses up to 32 tiles and five pager positions. Tests verify 200 warm refreshes without tile recreation, release on viewer entry, queued-open cancellation, busy retry, pending-close cancellation, error recovery and a 204-byte direct media filename without a directory scan.
- All 26 file-manager content areas match the previous design pixel-for-pixel; the top 32 clock/status pixels are excluded from that comparison.
- The classifier covers 92 extensions with at most seven comparisons. Host assertions cover mixed case, directory dots, unknown names and 20,000 generated names. Styling does not add decoders: HEIC and other unsupported formats display conversion guidance only after opening.
- Real H.264/AAC MOV and MP4 host playback assertions pass. Existing Helix signed-shift UBSan diagnostics remain in the logs; these runs are not a clean UBSan bill of health.

`BUILD-METADATA.json` records the complete tested working-tree source hashes, prepared dependencies and build inputs. It identifies the historical checkout revision; the source-hash list is the exact build provenance for the subsequent commits. Source files use Git line-ending normalization when committed. The prepared toolchain, external speech runtime libraries and SD models are not bundled here. Firmware binaries remain local build outputs.

[Open the native screenshot preview](preview/preview.html). Preview images use synthetic files and images and do not execute device commands.
