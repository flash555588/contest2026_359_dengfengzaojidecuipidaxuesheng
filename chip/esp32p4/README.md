# ESP32-P4 芯片层 overlay

openvela `dev-ai-contest-2026` 的 `nuttx` 目前没有 `arch/risc-v/src/esp32p4`。
本目录从 Apache NuttX master 抽出芯片 glue，经 manifest `<linkfile>` / `tools/apply_esp32p4_overlay.py` 挂进编译树：

- `src/` → `nuttx/arch/risc-v/src/esp32p4`
- `include/` → `nuttx/arch/risc-v/include/esp32p4`
- `common-espressif/` → `nuttx/arch/risc-v/src/common/espressif`（仅当目标不存在时）
- `tools-espressif/` → `nuttx/tools/espressif`（仅当目标不存在时）

`arch/risc-v/Kconfig` 必须补上 `ARCH_CHIP_ESP32P4`，这是对 nuttx 仓的本地补丁，不提交到选手仓。获奖后应 PR 到 `open-vela/nuttx` 的 `dev-ai-contest-2026`。

编译时会再 clone `espressif/esp-hal-3rdparty`（NuttX Espressif HAL）。
