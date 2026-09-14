# 桌面蓝牙 BLE 修复版

已在当前 ESP32-P4 v3.2 + ESP32-C6 实板编译、烧录并验证。蓝牙页面可以进入“就绪”、扫描附近 BLE 设备，10 秒扫描结束后返回“就绪”；重复扫描和关闭页面取消扫描均通过。

固件 SHA-256：`314fc2168eb0873082018edc08a6e1f3fee5d58f18662f4458642273cccd14ee`。

## 修复内容

本轮共修改 9 个源文件，增量见 `bluetooth-startup.patch`：

- 初始化、释放 Bluetooth socket 连接锁，解决第一次发送 HCI 命令阻塞。
- 按蓝牙控制器编号寻找 HCI 设备，修正 `hci0` 被错误映射到 Wi-Fi `eth0` 的发送路径；统一接收回调和发送轮询的设备锁。
- 注册后启用实际的原始 HCI 网络接口 `bnep0`，让控制器回包进入 NimBLE。
- 修正 ESP-Hosted H4 封包长度及接收方向：主机发送时类型在 Hosted 头中，C6 回包的类型在 payload 首字节。
- 修正网络 procfs 路径字符串分配与释放的堆不匹配，避免执行 `ifconfig` 后破坏堆。
- NimBLE 定时器通过 POSIX `timer_gettime` 判断是否仍在计时，修复单次定时器到期后仍被视为活动、导致扫描永不结束的问题。

配置与已验证的 `ui-layout-fix` 逐字节一致，保留此前黑屏、应用存储、Wi-Fi DHCP、中文字体和桌面排版修复。本轮没有修改相机。

## 实板结果与范围

| 验证内容 | 结果与证据 |
| --- | --- |
| 蓝牙启动、扫描 | 原地点扫描到 12 个设备；`evidence/ble-final.json` |
| 自动结束、重复扫描、退出取消 | 配置扫描 10 秒，等待 11 秒后已回到“就绪”；`evidence/repeat-final.json` |
| 先蓝牙再 Wi-Fi | DHCP 成功，IPv4 `192.168.0.105`；`evidence/coexist-final.json` |
| 重启后先 Wi-Fi 再蓝牙 | DHCP、扫描和存储通过；`evidence/wifi-first-final.json` |
| 扫描期间应用存储 | 3 个命名空间的覆盖、读取、删除自测通过 |
| 深浅色蓝牙页面 | 实板帧缓冲截图及对象坐标审计通过，未发现非预期重叠和越界 |
| 定时器回归 | 旧代码复现“到期后仍活动”，修复代码通过到期、重复设定和停止测试；`evidence/callout-test.json` |

用户随后更换工作地点并确认原热点已经消失。因此后续两次关联/DHCP 失败记录按环境变化保留，不作为固件回归；新地点仍可扫描 BLE 和 Wi-Fi，结束时蓝牙页保持“就绪”。没有修改已保存的 Wi-Fi 凭据。

ESP32-C6 **只支持 BLE，不支持经典蓝牙耳机、音箱或 BR/EDR**。当前设备列表最多显示 12 条地址和 RSSI，数量随周围广播变化。尚未指定目标外设，未实测连接、配对、GATT 或具体设备协议，不能据此宣称这些功能已通过。

截图：[浅色蓝牙](evidence/review-light/light-bluetooth.png)、[深色蓝牙](evidence/review-dark-final/dark-bluetooth.png)。首次深色截图发生 USB/JTAG 通信中断和复位，失败记录保留在 `evidence/review-dark`，不计入通过项；降至 2000 kHz 后成功采集，最终串口检查确认系统继续运行。截图检查不等于物理触摸精度测试。

`evidence/evidence-index.json` 明确区分本固件通过记录、热点消失后的观察和早期调试；其他旧日志不得直接当作最终固件证据。旧 UI 包两份误归属的历史运行日志也已单独更正，见 `../ui-layout-fix/evidence/provenance-correction.json`。

## 固件与烧录

- `nuttx.bin`：3,930,376 字节，烧录偏移 `0x2000`，当前板已烧录此文件。
- `nuttx.elf`、`nuttx.map`：对应调试符号和链接结果。
- 擦除范围为 `[0x2000, 0x3c2000)`，`/data` 从 `0x400000` 开始，本轮没有擦除或格式化数据分区。
- `resolved.config` SHA-256：`8dd3f0c92913b1a123d7de7c6818a1f7d31260e0dc298b7454982470eefef03b`。

需要再次烧录时，在完整源码包根目录运行 `python diagnostics/flash_ble_fix.py --log flash-next.log`。脚本验证映像哈希、数据分区边界和 COM23 上的设备身份，日志文件须使用未占用的名称。烧录后复位设备即可启动。

桌面从控制面板打开“蓝牙”，点击“扫描”。NSH 诊断命令为 `desktop ui bluetooth`、`desktop ui ble-scan` 和 `desktop ui audit`。

## 源码与构建

`overlay/` 是累计源码覆盖层；`full-fix.patch` 对应本目录上级 `current-build-tree-source.tar.gz`；`bluetooth-startup.patch` 是本轮 9 个文件的增量。`build-metadata.json`、`SHA256SUMS` 和 `build-scripts/` 保存构建、来源和脚本。

本次 WSL 隔离构建目录为 `/tmp/v3-desktop-ble-startup-20260914`，原版 `/home/streetartist/nuttxspace` 保留。使用 Espressif GCC 14.2 和 xPack RV32IMAC/ILP32 软浮点 libgcc，不能随意换回会访问 FCSR 的运行库。

在完整工作区执行 `python diagnostics/prepare_ble_fix.py` 生成覆盖层，再从 WSL 执行 `python3 diagnostics/build_ble_fix.py`。此脚本依赖已准备的 `/tmp/v3-desktop-ui-layout-20260914`、工作区 `diagnostics/ble-baseline`、TLS 下载缓存及工具链；`build-scripts/` 是追溯副本，不是脱离完整工作区的独立构建入口。累计覆盖层和完整补丁用于向所附源码归并修复。
