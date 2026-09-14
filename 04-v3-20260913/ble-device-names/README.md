# 蓝牙设备名称显示版

已编译、烧录到当前 ESP32-P4 v3.2 + ESP32-C6 实板。真实广播中的 **EDIFIER BLE** 已在最终界面显示；最后一次扫描发现 16 个设备。

## 本次完成

- 主动扫描，合并广播和扫描响应中的完整／缩略设备名，完整名称优先；不带名称的后续数据不会覆盖已有名称。
- 名称和地址／RSSI 分成两行，中文使用桌面中文字体，长名称自动省略；未提供名称时显示“未命名设备”。
- 扫描结束后有名称的设备优先显示，同组内按信号强度排序。扫描期间更新原有行，保留滚动位置。
- 列表容量从 12 增至 32；每个名称最多保存 63 字节，截断保留完整 UTF-8 字符，处理畸形和控制字符。
- 保留此前蓝牙启动、10 秒扫描结束、取消扫描、桌面排版、存储与启动修复；配置未变，相机未修改。

## 验证

实际名称接收、深浅色界面、重复扫描、退出取消和存储自测通过。解析器使用 ASan／UBSan 验证完整名称优先、扫描响应合并、中文、截断以及 25,600 组异常数据，无错误。

截图：[浅色](evidence/review-light-final/light-bluetooth.png)、[深色](evidence/review-dark-final/dark-bluetooth.png)。可见控件坐标审计未发现非预期重叠与越界。物理触摸精度、真实外设连接和配对未实测。

用户已更换工作地点，原 Wi-Fi 热点消失，本版未再尝试 DHCP。最终串口检查确认蓝牙就绪、存储挂载、系统继续运行。`evidence/evidence-index.json` 区分最终固件与初次未排序版本的记录。

## 耳机／音箱放音尚未完成

板载 C6 只有 BLE，不支持经典蓝牙 BR/EDR，不能直接实现普通耳机／音箱的 A2DP 放音。显示出耳机的 BLE 名称并不代表能够传输音频。

需要外接支持 **A2DP Source** 的音频发射模块，或原版 ESP32 作为音频协处理器。配对、绑定、重连和播放器音频路由需根据模块型号、协议与接线实现；目前尚未收到这些硬件信息。[具体接入要求](AUDIO-INTEGRATION.md)。

## 固件与源码

- `nuttx.bin` SHA-256：`0ac1e9ff601f28db20a6878baf77cef7b48662c4837c427d76b7f34766dc6b09`，大小 3,931,440 字节，偏移 `0x2000`。
- 擦除范围止于 `0x3c2000`，未触及从 `0x400000` 开始的 `/data`，测试临时目录已清理。
- `nuttx.elf`、`nuttx.map`、`resolved.config`、`SHA256SUMS` 保存对应调试结果、配置和校验。
- `overlay/` 为累计覆盖层；`bluetooth-names.patch` 为相对上一 BLE 修复版的 5 个文件增量；`full-fix.patch` 相对上级 `current-build-tree-source.tar.gz`。

在完整源码工作区运行 `python diagnostics/prepare_ble_names.py`，然后从 WSL 运行 `python3 diagnostics/build_ble_names.py`。构建依赖已验证的 `/tmp/v3-desktop-ble-startup-20260914`，在 `/tmp/v3-desktop-ble-names-20260914` 隔离目录进行；使用 Espressif GCC 14.2、xPack 软浮点 libgcc 和原 TLS 缓存。`build-scripts/` 是追溯副本，需放回完整工作区的诊断脚本位置执行。

如需重烧，在源码根目录运行 `python diagnostics/flash_ble_fix.py --delivery 04-v3-20260913/ble-device-names --log flash-next.log`，日志须采用新文件名；完成后复位启动。
