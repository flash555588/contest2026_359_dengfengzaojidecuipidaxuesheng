# V3 source collection

## 当前入口（2026-09-15）

本仓库以本地工作区为准同步。最新任务状态见 [CURRENT-TASKS.md](CURRENT-TASKS.md)，
相机第 11 版源码和实测范围见 [camera-usb-fix](04-v3-20260913/camera-usb-fix/README.md)，
ESP-Claw 语音输入的开发进度见 [espclaw-voice](04-v3-20260913/espclaw-voice/README.md)。
语音版尚未烧录，当前镜像超出程序分区，不能当作可烧录发布版。

大源码归档以分卷保存；克隆后执行 `python diagnostics/restore_source_archives.py` 还原。
同步范围和校验说明见 [GITHUB-SYNC.md](GITHUB-SYNC.md)。
以下是原始源码集合导出时的说明，仅描述 2026-09-14 的归档过程。

Export date: 2026-09-14. Yesterday means 2026-09-13.
This is a local source collection, not a newly built or hardware-qualified release.
Original repositories were not modified. No network access or firmware flashing.

## 01-v3.2-pinned

NuttX and Apps archives are exact Git exports of the commits recorded in the
candidate BUILD-METADATA.txt. Project overlay is a CURRENT local reference,
not a proven historical snapshot. External HAL/LVGL/QuickJS dependencies are
identified by the original metadata and are not all bundled at those exact pins.

## 02-v3plus

Full available desktop source tree from the isolated 2026-09-06 v3.0+ build,
including local dependency storage. Separate original v3.1+ and v3.0+ build
evidence is included. application-snapshot belongs to the separate current-v3plus
application variant; do not silently overlay it onto the isolated build.

## 03-early-v3

Recovered 2026-08-30 desktop/runtime/board source files, original resolved config
and porting notes. The reference repository archive is its CURRENT working tree.
An exact source-to-binary match for the old v3 firmware has NOT been established.
These files must not be presented as a reproducible historical release.

## 04-v3-20260913

Current source tree of /home/flash/glass-ble-v3-20260913, including NuttX, Apps,
TLS source and joint-input metadata. This tree also participated in 2026-09-14
builds. It is NOT an immutable snapshot of yesterday's final firmware.
Yesterday's microphone/chat/BLE/recovered candidate evidence is kept separately.
The 2026-09-14 metadata is explicitly dated. Staged changes and ESP-Claw local
sources are included separately; their presence does not establish firmware use.

## Reading and verification

Open recovered-source, application-snapshot and desktop for direct source browsing.
Extract each .tar.gz into a separate directory to inspect the larger source trees.
Archives preserve Linux symbolic links; extract under Linux/WSL for best results.
Absolute links may still reference the original build environment. Build scripts
may need path adjustments. No clean build or dependency-completeness test was run.
Git metadata, common build outputs, conversation logs and credential-style files
are excluded from working-tree archives. Git-pinned archives retain tracked files.
SHA256SUMS covers every delivered file except itself. Archive readability and
SHA-256 checks were performed after export; this is not a compile test.
