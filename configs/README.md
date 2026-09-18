# configs：桌面配置片段

本目录保存 [ESP32-P4 Function-EV-Board 桌面配置](esp32p4-function-ev-board/README.md)，
不是完整的板级配置集合，也不是会被 NuttX 自动发现的独立构建入口。
返回[仓库首页](../README.md)。

## 与其他配置的位置关系

`desktop-v1/defconfig` 与 `desktop-v3.2/defconfig` 位于本目录的板卡子目录。
NSH 等板级配置位于 [board/esp32p4-function-ev-board/configs](../board/esp32p4-function-ev-board/configs/)。
v3 应用快照的解析配置另见 [app/espdl-quickapp/resolved.config](../app/espdl-quickapp/resolved.config)。

配置片段、完整解析后的 `.config`、镜像所附的构建配置分别有不同用途。应用之前必须
确认它们来自同一基线，并通过对应构建流程生成依赖与上下文，不能简单拼接不同版本配置。

构建入口见 [tools](../tools/README.md)。不得从一个配置选项推断整套硬件功能已完成实测。
