# ESP32-P4 桌面配置索引

本目录保存 [desktop-v1/defconfig](desktop-v1/defconfig) 和
[desktop-v3.2/defconfig](desktop-v3.2/defconfig)。返回[配置总览](../README.md)。

这些文件是桌面配置材料，不代表同名目录已经安装到外部 NuttX 板级配置路径。
历史构建脚本使用的目标名分别是 `desktop-v1` 与 `desktop`，不能直接根据本目录名
拼出一个 `desktop-v3.2` 的 configure 命令。

NSH 配置在 [board/esp32p4-function-ev-board/configs](../../board/esp32p4-function-ev-board/configs/)。
应用前先确认外部基线、板卡 revision 和集成流程；变更后重新解析配置与生成上下文，
不要复用另一套配置编译出来的对象文件。具体构建步骤见[根 README](../../README.md#构建)。
