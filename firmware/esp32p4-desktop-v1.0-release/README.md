# ESP32-P4 v1.0 OpenVela 桌面发行包

这是 ESP32-P4 Function EV Board、芯片 revision v1.0 的专用 OpenVela/NuttX 桌面固件。2026-08-27 已在本机 COM7 实板完成烧录、写后校验和两次冷复位启动验证。

启动后 LCD 运行 `desktop_main`，UART0 以 115200 8N1 提供后台 NSH。已验证 `/dev/fb0`、GT911 `/dev/input0` 和 32 MiB PSRAM 用户堆可用。

不要把本包烧录到 v3.x 芯片；v3.2 使用独立配置和固件包。

## 烧录

在 Windows PowerShell 中执行：

```powershell
.\flash.ps1 -Port COM7
```

等价命令：

```powershell
py -m esptool --chip esp32p4 --port COM7 --baud 921600 `
  --before default-reset --after hard-reset write-flash `
  --flash-size 4MB --flash-mode dio --flash-freq 80m `
  0x2000 .\nuttx.bin
```

## 固件校验

```text
nuttx.bin  2947548 bytes  FDD73E4CEB43BFC7F3CBA030A3A0CA3A0A99784B78A6BDD1121BA59A9D75F62B
```

ELF 和 HEX 由构建脚本在本地输出，不纳入 Git 固件包；提交到仓库的是可直接烧录的 BIN、配置、元数据和校验文件。

## 可复现来源

```text
NuttX          cd61ccdd11498e22c058c3b2540828f88e23172e
apps           93fb5ac72249ae766cbeea9f0e3d484bdd6807f7
ESP HAL        8d0a898910084206721a0892ab093021bca1496a
LVGL           59a6b61c9580b65089010c5273f2fcdd6c4d2aae (v9.2.1)
toolchain       riscv32-esp-elf GCC 15.2.0_20251204
configuration   esp32p4-function-ev-board:desktop-v1
```

上述两个成员仓提交已经推送，并由大赛仓 `openvela.xml` 固定引用。下一道发布门槛是从空目录按该 manifest 再构建一次，确认无需本机残留文件。
