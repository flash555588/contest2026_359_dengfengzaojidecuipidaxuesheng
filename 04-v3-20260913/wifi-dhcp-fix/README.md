# Wi-Fi DHCP 配置修复（2026-09-14）

已对比 WSL `/home/streetartist/nuttxspace` 原版、当前导出源码和此前黑屏修复构建，确认当前包存在有线网卡与 C6 Wi-Fi 重名为 `eth0` 的配置缺陷。此包按原版关闭有线 EMAC 和 NSH 自动联网，保留黑屏修复及当前桌面代码。

**状态：已烧录 COM23，Flash 内容校验、NSH 启动、显示驱动和 DHCP 实测通过。** 本包读到 C6 的连接成功事件，取得 IPv4 `192.168.0.105`。之前的桌面帧缓冲截图对应 `black-screen-fix` 中的旧固件；本包的显示验证来自匹配 ELF 的 JTAG 状态读取，未拍摄实体屏幕。

## 烧录后实测

2026-09-14 将本目录镜像写入 ESP32-P4 v3.2 的 `0x2000`，esptool 返回 `Hash of data verified`。复位后 PSRAM 32 MB / 200 MHz 初始化成功，NSH 与 desktop 正常启动。数据分区未擦除，`/data/qpk` 仍存在。

执行 `c6probe`、`c6probe net`，使用 C6 已保存的 Wi-Fi 配置；收到 `rpc: EVENT StaConnected` 后执行 `c6probe dhcp eth0`，结果为 `DHCP ret=0`。没有在脚本或日志中重新保存 Wi-Fi 密码。

| 项目 | 实测结果 |
|---|---|
| 网络接口 | 仅一个 eth0，MAC 10:bd:a3:89:58:74 |
| IPv4 | 192.168.0.105 |
| 网关 | 192.168.0.1 |
| 子网掩码 | 255.255.255.0 |
| DHCP 提供的 DNS | 183.221.253.100 |
| 显示驱动 | initialized / dma_enabled / video_running 均为 true，1024×600 RGB565 |
| 收尾 | JTAG 退出后 NSH 再次响应，IP 保留，desktop / c6net 存活，串口已释放 |

证据：`evidence/flash.log`、`boot1.log`、`postflash.json` / `.log`、`running-state.txt`、`final-console.json` / `.log`，汇总为 `hardware-validation.json`。`validation.json` 是此前烧录前的静态检查记录，其 pending 字段不代表最新实板状态。

本轮已验证无线关联和局域网 DHCP，没有测试外网 HTTP/TLS、长时间续租或实体屏幕目视效果。

## 对比发现

| 配置或行为 | WSL 原版 nuttxspace | 当前黑屏修复包 | 本修复包 |
|---|---|---|---|
| `CONFIG_ESPRESSIF_EMAC` | 关闭 | 开启 | 关闭 |
| `CONFIG_NSH_NETINIT` | 关闭 | 开启 | 关闭 |
| C6 注册接口名 | 固定 `eth0` | 固定 `eth0` | 固定 `eth0` |
| 启动后抢先注册的有线接口 | 无 | `eth0` | 无 |

原版 NuttX Git HEAD 为 `88f2644ee73207961fad3a9de14dda9c654abe13`，apps HEAD 为 `23c90a2372409e04767a090c9753fe44c4b145bd`。原版与当前 `c6probe_main.c` 的 SHA256 完全相同，联网命令并未改名。

证据链：

1. 当前原始导出配置和此前构建配置均启用 EMAC；实板 `ifconfig` 在 C6 尚未初始化时已有 MAC 为 `e8:f6:0a:e3:a9:5f` 的 `eth0`，同时有 `emac_rx` 任务。
2. `apps/system/c6probe/c6net.c` 第 254–257 行把 C6 接口名固定写为 `eth0`。
3. `netdev_register()` 用该字符串作为格式，未自动变为 `eth1`，也未拒绝重名；将新接口追加到链表末尾。
4. `netdev_findbyname()` 返回第一个名称匹配的接口。因此对 `eth0` 的 DHCP 操作会选中先注册的有线接口。

这解释了 `c6probe net` 返回 `network init ret=0` 后，`c6probe dhcp eth0` 仍失败的一个确定软件原因。`network init ret=0` 表示初始化/连接请求被接受，不代表收到 `StaConnected` 或已经取得 IP。本包烧录后已实际收到关联成功事件并取得 DHCP 地址，详见上方实测结果。

该配置缺陷已存在于 `current-build-tree-source.tar.gz`；此前黑屏修复保留了该配置，遗漏了与原版 Wi-Fi 配置的对比。此前建议仅执行 `c6probe connect` 也不完整：该命令仅执行控制请求并轮询 10 秒，`c6probe net` 才会建立 NuttX 网络数据通路。

本次没有把旧版网络协议实现整体覆盖回来。当前还包含 RPC 序号/互斥保护、扫描结果校验与桌面后台服务，DHCP 库版本也不同；这些差异不能仅凭返回 `-1` 判定有错。

## 固件与使用

- `nuttx.bin`：3,925,060 字节，烧录偏移 **0x2000**。
- SHA256：`559e8f13dc4f1627048c6a5250383762d96262f3b892018382a1a68c60bca953`。
- 应用结束地址 `0x3c0444`；扇区擦除结束地址 `0x3c1000`，低于 `/data` 的 `0x400000`。
- `nuttx.elf`、`nuttx.map`：此包对应的调试符号与链接映射。
- `resolved.config`：实际编译配置。
- `network-config.patch`：相对此前黑屏修复包的联网配置差异。
- `full-fix.patch`、`overlay/`：相对原始导出源码的完整修复，包含之前的 GDMA、Simple Boot 摘要等修改。
- `evidence/validation.json`：检查结果；`evidence/comparison.json` 和 `original-to-current.diff`：原版对比证据。

在本目录手动烧录：

```powershell
python -m esptool --chip esp32p4 --port COM23 --baud 921600 --before usb-reset write-flash 0x2000 nuttx.bin
```

新固件启动后，尚未执行 `c6probe net` 时没有 `eth0` 属于预期行为。联网：

```sh
c6probe net "TP-LINK_E641" "你的WiFi密码"
```

等待 `rpc: EVENT StaConnected` 后申请地址：

```sh
c6probe dhcp eth0
ifconfig
```

预期只有一个 `eth0`，其 MAC 应为 C6 的 MAC（本板之前读到 `10:bd:a3:89:58:74`），`DHCP ret=0` 且 IP 非 `0.0.0.0`。若仍失败，保存 `c6probe net` 后的事件、`ifconfig`、`ps` 和 `c6probe stat` 输出以区分无线关联与 DHCP 收发问题。

本配置与原版一致，适用于 C6 Wi-Fi 联网；需要同时启用有线网口时，应先让两种接口使用唯一名称，并同步修改桌面/DHCP 的接口选择。

## 构建与验证

隔离目录为 `/tmp/v3-desktop-wifi-dhcp-20260914`。原版 `nuttxspace`、前一版黑屏修复目录及其 BIN 均保留。

在资料根目录执行：

```powershell
wsl -d Ubuntu --exec python3 diagnostics/build_wifi_fix.py
python diagnostics/validate_wifi_fix.py
python diagnostics/package_wifi_fix.py
```

构建沿用 ESP GCC 14.2 与 xPack RV32IMAC/ILP32 软浮点运行库、已锁定 TLS 依赖。复制的是先前的隔离构建环境；原包复现依赖说明见 `../black-screen-fix/README.md`。脚本先完整清理对象文件，再重编译、链接。一般的编译告警保存在 `evidence/build.log`，不宣称无告警。

Kconfig 的参考解析结果会重置原包手动启用的隐藏选项 `ESPRESSIF_HR_TIMER`。构建脚本仅合入联网相关选项及 EMAC/PHY 依赖变化，保留此前实际验证的其余配置。`evidence/kconfig-reference.config` 仅作解析参考，**不是实际编译配置**；应使用 `resolved.config`。

检查已确认：

- 实际配置差异仅限 EMAC、其 PHY 依赖和 NSH 自动联网。
- ELF 中不存在 EMAC 驱动符号，C6 收发线程和 DHCP 客户端保留。
- NSH 与桌面正常启动路径不再调用自动联网；HA 在 NSH 创建失败时的可选 fallback 仍在链接中。
- 关闭有线初始化后，不再引用由 `esp32p4_ethernet.o` 启动的 `esp_hr_timer_init` / `esp_timer` 工作线程。SYSTIMER 早期初始化仍保留；不应把旧固件中 `esp_timer` 任务存在作为本包的验收条件。
- GDMA 等黑屏修复 overlay 内容一致；新编译 gdma.o 的 8 处内存释放均为 heap_caps_free，无普通 free 调用。ROM RAM 摘要和校验和有效，Flash 映射段可解析，镜像未越过数据分区。
- 完整补丁已在临时提取的原始源码上实际应用，6 个修改/新增文件均与 overlay 一致；原版 nuttxspace 的 13 个对比文件哈希保持一致。

静态检查和本轮实测的范围应分别理解：当前已完成启动、显示驱动状态及路由器 DHCP 验证，长期运行与外网通信尚未验证。
