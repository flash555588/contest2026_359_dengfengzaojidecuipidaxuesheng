# ESP32-P4 公共板级支持

本目录包含公共 `include/`、`scripts/`、`src/` 以及 Make/CMake/Kconfig 接入文件。
返回[板级索引](../README.md)。

团队 manifest 将本目录映射到 `nuttx/boards/risc-v/esp32p4/common`。
具体板卡入口在 [esp32p4-function-ev-board](../esp32p4-function-ev-board/README.md)，
芯片架构与驱动实现位于 [chip/esp32p4](../../chip/esp32p4/README.md)。

链接脚本和公共初始化改动可能影响多套板级配置。修改时应同时检查 v1.x 与 v3.x 的
地址布局、PSRAM 和启动条件，不要依据一个 revision 的日志推断另一个 revision 正常。
本目录不是可以独立编译的完整 NuttX 工程。
