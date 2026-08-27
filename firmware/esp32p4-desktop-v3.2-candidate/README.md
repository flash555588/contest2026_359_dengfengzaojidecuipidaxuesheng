# ESP32-P4 v3.2 OpenVela 桌面构建候选

本目录是 `esp32p4-function-ev-board:desktop` 的 v3.2 编译候选，配置固定 `CONFIG_ESP32P4_REV_MIN_301=y`，GCC 15.2.0 构建成功且 LVGL 版本告警为零。

当前本机只有 revision v1.0 板卡，因此本目录没有 v3.2 实机启动证据，严禁标记为正式发布。取得真正 v3.2 板卡后，必须完成烧录校验、5 次冷启动、LCD/GT911/PSRAM/桌面/控制台验收，并从最新 manifest 固定提交重新构建后替换本目录产物。

```text
nuttx.bin  2954852 bytes  715B258DDF56604A9FA0AC6318C53A52BDF2959E3DFD25F7C572C8A4E80BB2E7
```

ELF 和 HEX 由构建脚本在本地输出，不纳入 Git 候选包。

烧录偏移为 `0x2000`，但只能在确认芯片 revision v3.x 后使用。
