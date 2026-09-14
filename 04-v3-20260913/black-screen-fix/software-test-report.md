# 已编入软件的实板测试记录

测试对象：COM23 上的 ESP32-P4 v3.2，固件 SHA256 `61d7d1c8f03a1c01d50b1dfe8c9df9415933a01f7a8e6c752587d76b0659a6fe`。日期：2026-09-14。

本轮完成基础软件、文件系统与部分 C6 测试。测试在 Wi-Fi 扫描异常后中断，随后按用户反馈转入 WSL 原版联网配置对比；未执行的功能如下明确标注。本记录不表示所有软件通过验收。

后续更新：用户授权烧录独立的 `wifi-dhcp-fix` 固件后，已收到 StaConnected、DHCP ret=0，取得 192.168.0.105。新固件的烧录、显示驱动与网络验证见 `../wifi-dhcp-fix/README.md`。下表及异常记录保留原测试对象当时的结果。

| 项目 | 结果 | 范围与证据 |
|---|---|---|
| 桌面 / LVGL | 已验证渲染与 DMA 输出 | 此前帧缓冲读回正常、约 59.86 FPS，详见 followup-validation.md；未目视确认实体屏幕 |
| NSH | 基本命令通过，USB 控制台存在异常 | help、ps、free、目录查询正常；长命令及 Flash 写入问题见下文 |
| ESPClaw 0.1.0 | 离线自检连续 3 次 PASS | `software-local.log`；前后 cleanup pending no；无有效聊天配置，未测试网络模型 |
| SmartFS / tmpfs | 文件功能通过 | 小文件写入、复制、重命名、复位后持久化及 32 KiB 数据读回 cmp=0；`software-storage-finish.log` |
| C6 SDIO | 通过初始化 | 4-bit / 20 MHz、INIT 能力响应正常，初始化后 CMD52 ret=0；`software-c6.log` |
| C6 Wi-Fi | 部分通过，扫描详情未通过 | WifiInit / SetMode / WifiStart ret=0，MAC 读取成功，热点数量 13；ScanGetApRecords 后输出异常，35 秒内未返回；`software-wireless.log` |
| C6 DHCP | 用户反馈失败 | 用户执行 net 后 init ret=0，dhcp eth0 ret=-1；源码对比确认 EMAC/C6 eth0 重名问题，修复候选在 `../wifi-dhcp-fix/` |
| BLE | 未执行注册/扫描/连接 | builtin 有 c6ble；测试前没有 ttyHCI0，原定 BLE 用例在 Wi-Fi 超时后未执行 |
| Home Assistant | 帮助命令通过 | `ha_panel -h` 正常；无服务器 token，未连接 HA |
| QuickJS / QPK | 仅确认编入 | QuickJS 为嵌入库，无 qjs 命令；/data/qpk 无外部 QPK；未单独启动相机 QPK 验证 JS 执行 |
| NxCamera / 摄像头 | 未运行功能测试 | builtin 有 nxcamera，测试前未注册 /dev/video0 |
| 音频 | 仅确认输入设备注册 | 存在 /dev/audio/pcm_in0；无输出节点，未采集或播放 |
| SD 卡 / 触摸交互 | 未测 | 未发现 SD 块设备；input0 注册不等同于触摸交互通过 |

`help` 列出的 Builtin Apps：`c6ble c6probe desktop dumpstack espclaw ha_panel nsh nxcamera sh`。这不是桌面所有内嵌页面的独立可执行命令清单。

## 测试中发现的问题

1. 最初连续发送长命令，串口 echo 将 `count=64` 丢字为 `cont=64`；dd 未识别计数参数，测试文件实际写到 2,368,704 字节后通过受控 USB 复位停止。没有写满数据分区。这是串口输入丢字导致的测试事故，不能计作计划内大文件测试通过。
2. 改为每 8 字节分段发送后，短命令 `dd if=/dev/zero of=blocks bs=512 count=64` echo 完整，但控制台未返回提示。JTAG 显示 NSH 已等待 USB 串口 RX 信号量，`host_active=false`，桌面与 video_running 仍正常。受控复位后，文件恰好 32,768 字节，读回复制/校验成功。因此文件写入已完成，但 USB 控制台在 Flash 操作后失去响应的问题仍未修复。
3. Wi-Fi 初始化之前执行 `c6probe rpc` 返回 code=12289 / -5；执行 WifiInit 后相同 GetMacAddress 已成功，不能把前者单独判定为 SDIO/RPC 通路损坏。
4. Wi-Fi 扫描取得热点计数后，在取详情时输出异常并超时。随后 JTAG 工具未能打开设备；进一步读取现场的授权操作被用户中断。没有可靠现场证明是蓝屏、整机崩溃或单纯串口失联，根因未定。用户随后反馈自行连接未发生蓝屏。
5. 用户后续的 DHCP 失败与当前配置下有线 EMAC 和 C6 都注册为 eth0 的缺陷相符。`wifi-dhcp-fix` 是独立编译的候选修复，尚未完成上板验证，不能用本报告替代其验证。

## 清理与测试方法

已逐项删除测试文件并成功删除 `/data/.codex-sw-20260914-a3`。`software-c6.log` 中后续 `ls -l /data` 仅剩原有 qpk，`/tmp` 为空；`software-cleanup.log` 显示 SmartFS 4 MB、可用约 4073 KiB。

此版 NSH 不支持 `ls -la`，相关清理查询报 argument invalid，后续已改用 `ls -l` 验证。`rm blocks saved` 仅删除了首个测试文件，残留 saved 已单独删除。记录中 `completed=true` 仅表示返回提示符，不表示命令没有报错；本表依据原始输出和退出码人工判读。

测试驱动与用例保存在资料根目录 `diagnostics/software_probe.py` 和各 `software-*-suite.json`。日志与 JSON 保存在本目录 `evidence/`。本轮未重新烧录测试对象，没有调用外部聊天模型或提交 HA 操作。
