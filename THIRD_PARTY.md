# Third-party provenance

This file records the third-party source material used by the ESP32-P4
overlay. It does not replace the license files shipped by the corresponding
upstream projects.

## Apache NuttX and openvela apps

- Components: NuttX kernel, drivers, board framework, and NuttX apps.
- Release bases: NuttX `2f1387d56eb04ad2599baca58a3fa2380cdaaedb` and
  apps `88827afd368d4bbb4802b96ed44d9582f85b2f92`.
- License: Apache License 2.0; retain upstream copyright, NOTICE, and SPDX
  declarations.
- Use in this repository: `tools/patches/0001` through `0015` are applied to
  those bases by `tools/apply_final_overlays.sh`.
- Limitation: these release bases are imported snapshots. Their exact public
  Apache NuttX/openvela ancestry still requires an upstream commit mapping;
  see `UPSTREAM_PLAN.md`.

## Espressif ESP HAL

- Source: `https://github.com/espressif/esp-hal-3rdparty`.
- Commit: `78c092909fca38d1e2ccf767b5eff66bddc5c789`.
- License: Apache License 2.0.
- Local changes: the two reviewable patches installed by
  `0013-esp32p4-hal-reproducibility.patch`; no Python in-place rewrite is part
  of the release procedure.

## Espressif SC2336 register sequence

- Source repository: `https://github.com/espressif/esp-video-components`.
- Source commit: `3620887638419f7afbe4aa3a909422c640b14061`.
- Source path:
  `esp_cam_sensor/sensors/sc2336/private_include/sc2336_mipi_2lane_24Minput_1024x600_raw8_30fps.h`.
- Upstream copyright: Espressif Systems (Shanghai) CO LTD.
- License: Apache License 2.0.
- Transformation: the NuttX port keeps the fixed 1024x600, RAW8, two-lane,
  30 fps initialization sequence and wraps it in the NuttX image-sensor API.
  The source commit and path are also recorded beside the generated table by
  patch `0015-third-party-provenance-comments.patch`.
- Hardware note: Espressif documents that sensor initialization tables may
  originate from the sensor vendor/FAE. Redistribution remains under the
  copyright and license declarations carried by Espressif's source.

## Espressif EK79007 panel sequence

- Source repository: `https://github.com/espressif/esp-iot-solution`.
- Source commit: `c8c4726501e13849b785f2cb9ae55ec1266b44dc`.
- Source path:
  `components/display/lcd/esp_lcd_ek79007/esp_lcd_ek79007.c`.
- Upstream copyright: Espressif Systems (Shanghai) CO LTD.
- License: Apache License 2.0.
- Transformation: the board driver keeps the two-lane vendor command sequence
  and implements reset, sleep-out, display-on, and framebuffer binding through
  NuttX APIs. Patch `0015` records the immutable source beside the table.
- Hardware limitation: the physical panel-controller marking must still be
  confirmed for every supported board batch.

## LVGL

- Source: `https://github.com/lvgl/lvgl`.
- Commit: `59a6b61c9580b65089010c5273f2fcdd6c4d2aae` (v9.2.1 source).
- License: MIT.
- Integration: NuttX apps LVGL port plus local framebuffer and allocation
  fixes. The Kconfig version was corrected to 9.2.1 by patch `0012`.

## QuickJS

- Source: `https://github.com/bellard/quickjs`.
- Commit: `6e2e68fd0896957f92eb6c242a2e048c1ef3cae0`.
- Archive SHA-256:
  `d4542882686eefa8c0c80493dfb30d858588c0553ffe17a058189132607ff24e`.
- License: MIT.
- Integration: NuttX compatibility patches and the contest QPK runtime. Patch
  `0012` fixes the archive name and verifies the archive before extraction.

## Contest-owned OuO application

- Location: `ouo/`.
- License: Apache License 2.0, as declared by `ouo/LICENSE`.
- Copyright: Vela Mood Console contributors.

## Release obligations

Before submission, run a license/SPDX scan over the materialized NuttX and apps
trees, retain all upstream license and NOTICE files, and update this ledger if
any source commit or register table changes. A repository-wide root LICENSE and
NOTICE remain release-level P0 deliverables and are intentionally tracked
separately from this P1 provenance record.
