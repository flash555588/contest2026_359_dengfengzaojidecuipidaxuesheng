# Third-party provenance

This file records the third-party source material used by the ESP32-P4
overlay. It does not replace the license files shipped by the corresponding
upstream projects.

## JIZURA complete reference application

- Source: https://github.com/852wa/JIZURA
- Version: 0.10.1, commit `fc16bfe43ea4a6c25a21f1caf04d17326de14f00`.
- Copyright: hakoniwa; license: MIT.
- Imported unchanged into `app/jizura/upstream`; per-file SHA-256 values are in `app/jizura/upstream.lock.json`.
- Preserve its `LICENSE` and `THIRD_PARTY_NOTICES.md`, including the bundled mp4-muxer notice. Runtime fonts retain their own font licenses.
- Purpose: complete browser reference before ESP migration; this import does not indicate that its engine or editor runs on ESP hardware.

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

## Grok Ball replacement for the OuO entry

- Location: `ouo/`.
- Upstream: https://github.com/tycoding/grok-ball, commit
  `de368ce3acdc5a70e871ee46ebbe8f60b11d3d6a`.
- Original engine, 25 eye contours and 32 emotion definitions: MIT, copyright
  (c) 2026 tycoding. The unmodified engine and full license are retained under
  `ouo/vendor/`; source hashes are in `ouo/vendor/UPSTREAM.json`.
- The SVG scene adapter, LVGL contour renderer and native app integration
  are Apache-2.0, as declared by `ouo/LICENSE`. Generated app and embedded C
  resources include the upstream MIT notice and declare both licenses.

## Dafeiyu desktop pet

- Source: `https://github.com/1190fasheqi/dafeiyu-pet`.
- Upstream license: MIT; the original license text is retained at
  `app/espdl-quickapp/overlay/apps/system/desktop/dafeiyu/LICENSE.upstream`.
- Integration: the local dialogue and behavior are adapted for the native
  LVGL overlay and Quick App API; newly written device-side code declares
  Apache-2.0. The pet source has host tests but no new firmware or board
  validation yet.

## Release obligations

Before submission, run a license/SPDX scan over the materialized NuttX and apps
trees, retain all upstream license and NOTICE files, and update this ledger if
any source commit or register table changes. A repository-wide root LICENSE and
NOTICE remain release-level P0 deliverables and are intentionally tracked
separately from this P1 provenance record.

## Local and live media players

- OpenH264 2.6.0: Cisco BSD license, installed from a SHA-256 pinned archive.
  The installer preserves the upstream LICENSE and applies decoder-only RV32,
  bounded scratch storage, allocation guards and worker ownership patches.
- minimp4: CC0, revision and source hash in
  `app/bilibili_live/local_vendor/UPSTREAM.json`; upstream notice stays in header.
- Helix AAC via arduino-libhelix: original RealNetworks license retained as
  `app/bilibili_live/local_vendor/HELIX-UPSTREAM-LICENSE.txt`; revision and local
  adaptations are recorded in the same manifest. No Arduino wrapper is linked.
- TLS remains the existing desktop glass_https / mbedTLS implementation.

## Native music language assets

- Noto Sans SC/JP/KR and Noto Sans: SIL Open Font License 1.1; fixed Google Fonts
  commit and source/notice hashes in `app/espdl-quickapp/assets/music-fonts.lock.json`.
  `tools/prepare_global_font.py` instantiates regular weight, retains all source
  codepoints and emits the renamed Vela Global derivative plus four OFL notices
  for SD deployment. Font binaries are SD assets, not linked firmware arrays.
- OpenCC t2s dictionaries: Apache-2.0, commit
  `556ed22496d650bd0b13b6c163be9814637970ae` (`ver.1.1.9`). Original TSCharacters,
  TSPhrases, license and hashes remain in `app/espdl-quickapp/assets/opencc/`.
  The generated read-only table is used in the lyric worker; it is not the
  OpenCC runtime library.
- The private SD TinyTTF instance includes the existing pinned LVGL TinyTTF/STB
  sources with private entry-point names; their upstream notices stay in those
  dependencies. Global LVGL font configuration is unchanged.

## Album PNG decoder

- LodePNG: https://github.com/lvandeve/lodepng, commit
  `e584a8490db1b76b81bf298c11c09d3b55d9c270`, zlib license.
- Unmodified upstream source is under `app/bilibili_live/local_vendor/lodepng/`;
  `lodepng.cpp` is stored as `lodepng.c` and compiled as C. Original notices,
  license and per-file SHA-256 hashes are preserved beside the source.
- `album_png.c` enables decoding only, supplies bounded allocation and converts
  RGBA to RGB565 in the album worker. HEIC/AVIF recognition does not include
  those codecs or imply playback support.
