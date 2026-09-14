# OpenVela ESP32-P4 port completion record

Date: 2026-08-28  
Workspace: `/home/flash/openvela` under WSL Ubuntu 22.04  
Connected hardware: ESP32-P4 revision v1.0 on COM7

## Outcome

The remaining OpenVela ESP32-P4 port work is complete for the available
hardware. Both the revision v1.0 and revision v3.2 configurations now build
from a clean object tree. The v1.0 image was flashed and verified on COM7.
The v3.2 image is build-verified only because no v3.2 board was attached.

## Implemented fixes

- Adapted the pinned Espressif HAL delay wrapper from unavailable
  `nxsched_usleep()` to OpenVela's `nxsig_usleep()` API.
- Made `patch_os_c.py` idempotent and invoked it from the ESP32-P4 `context`
  target. It supplies the OpenVela HAL compatibility changes for fcntl,
  interrupt adapter state, early critical sections, and `kthread_create()`.
- Enabled `CONFIG_MM_KERNEL_HEAP=y` in both v3.2 defconfigs.
- Restored the reference heap split for v3.2: internal SRAM is the kernel heap
  and mapped PSRAM is the user heap.
- Allocated and freed the v3.2 framebuffer through `memalign()`/`free()` so its
  1,228,800 bytes come from PSRAM instead of the internal kernel heap.
- Preserved the v1.0 fixed framebuffer mapping at `0x48000000`; early silicon
  does not expose its live scanout buffer to the general allocator.

## Clean build results

### Revision v1.0

The clean build selected `esp32p4_sections.ld`, 80 MHz PSRAM, no kernel heap,
and generated ELF, HEX, and a 16 MiB DIO/80 MHz RAM-only-header image.

Artifact: `esp32p4_v1_desktop/nuttx-v1-fixed-20260828.bin`  
Size: 854,176 bytes  
SHA-256: `D8A67AD816BC5EE8B6CF65A93D415F0F704E2E34040FD66AE5ACC1296376E72C`

Resolved config SHA-256:
`B32EB6895300DA2CF8BE2D11A2011C2E0782CEE218121362E8F2C655F9E7A58F`

### Revision v3.2

The clean build selected `esp32p4_sections.rev3.ld`, 200 MHz PSRAM, separate
kernel/user heaps, and generated ELF, HEX, and a 16 MiB DIO/80 MHz
RAM-only-header image. The log explicitly showed the automatic HAL patch hook.

Artifact: `esp32p4_v3_desktop/nuttx-v3-fixed-20260828.bin`  
Size: 849,132 bytes  
SHA-256: `9105D08CE56A16D13AD4C435C3655A0E71093D1718C5C7CE515271D7226ADEED`

Resolved config SHA-256:
`E1E08173BF6EB33C6FF283637915F80687931658176A0CF2ECF6F1270E3D42F2`

## v1.0 hardware verification

The final v1.0 image was written to COM7 at flash offset `0x2000`. Esptool
identified revision v1.0, wrote 854,176 bytes, verified the flash hash, and
reset the board successfully.

The captured boot log confirmed:

```text
PSRAM: mapped 0x48000000-0x4a000000 (33554432 bytes)
TOUCH: gt911 /dev/input0 -> 0
DISP: RGB565 framebuffer @0x48000000 size=1228800
DISP: fb bind -> 0
DISP: video start -> 0
DISP: /dev/fb0 register -> 0
DISP: display_init -> 0
DESKTOP: touch ready
DESKTOP: ui ready
NuttShell (NSH)
nsh>
```

Runtime checks showed `/dev/fb0`, `/dev/input0`, `/dev/console`, and
`/dev/ttyS0`. The `desktop` task was ready, `nsh_main` was running, and the
v1.0 user heap retained 67,168 free bytes because PSRAM scanout is deliberately
kept outside the general heap on early silicon.

## Reproduction command

```bash
export PATH=/home/flash/vela-p4/riscv32-esp-elf/bin:$PATH
export CROSSDEV=riscv32-esp-elf-
cd /home/flash/openvela/nuttx
make STORAGETMP=y \
  NXTMPDIR=/home/flash/openvela-contest359-release/nxtmpdir \
  USE_NXTMPDIR_ESP_REPO_DIRECTLY=y -j8
```

## Remaining external validation

A physical v3.2 board is still required to validate its 200 MHz PSRAM,
framebuffer allocation, display, and touch path. No commit or push was made.
