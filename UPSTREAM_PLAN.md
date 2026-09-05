# ESP32-P4 upstream split plan

Status as of 2026-09-04: preparation only. No upstream pull request was created
by this repair pass because publishing requires a reviewed commit and explicit
push/PR authorization. `UPSTREAM-PR=NONE` remains the truthful status.

## Dependency order

| Order | Scope | Target repository | Required boundary | Exit criteria |
|---:|---|---|---|---|
| 1 | ESP HAL compatibility | `espressif/esp-hal-3rdparty` | GCC 15/simple-boot and OS-port changes only | HAL CI passes at pinned base; patch is independently reviewable |
| 2 | ESP32-P4 SoC base | `open-vela/nuttx` | clock, revision, SMP, timer, DSI/CSI low-level support | NSH, ostest, SMP, timer, and style checks pass without board/app code |
| 3 | MIPI-DSI host and diagnostics | `open-vela/nuttx` | generic DSI host, buffer ownership, diagnostic ioctl | generic API docs, error unwind, stress and counter tests pass |
| 4 | Function EV Board | `open-vela/nuttx` or `vendor_espressif` | defconfig, GPIO, panel/touch binding only | Make/CMake parity and both supported board configs build |
| 5 | GT9xx lifecycle | `open-vela/nuttx` | cancellable polling and board reset/INT callbacks | missing-device, timeout, close, and 100-cycle tests pass |
| 6 | MIPI-CSI host | `open-vela/nuttx` | generic host with no SC2336 or fixed-mode dependency | no-sensor and alternate-mode builds pass; ISR path is bounded |
| 7 | SC2336 sensor | `open-vela/nuttx` | sensor registration and fixed mode descriptor | provenance retained; probe/stream/error recovery tests pass |
| 8 | V4L2 USERPTR/direct preview | `open-vela/nuttx` | DMA capability, cache and ownership contract | invalid-buffer tests and 100 open/close cycles pass |
| 9 | Shared app fixes | `open-vela/nuttx-apps` | LVGL/QuickJS fixes independent of contest UI | upstream app CI and style checks pass |
| 10 | VelaDesk/OuO demo | contest repository | product UI, QPK resources and acceptance harness | release SHA, video and HIL evidence are bound together |

## Existing parallel work

The contest audit identified openvela ESP32-P4 SoC PR #340 and Function EV
Board PR #347 as parallel work by other contributors. Before opening orders 2
through 4, compare APIs and file ownership against those changes and document
whether each contest change is a prerequisite, replacement, or incremental
fix. Do not claim those PRs as this project's upstream contribution.

## Submission rules

Each public PR must start from a public upstream commit, contain one logical
change, retain provenance, pass the repository's style/build checks, and link
its dependent PRs. Binary firmware, contest logs, the demo UI, and one-off WSL
diagnostics stay out of public driver PRs.

After a PR is created, replace `UPSTREAM-PR=NONE` with its URL, immutable head
SHA, checks, review state, and dependency order. A locally prepared patch is
not an upstream PR.
