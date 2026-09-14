# CSI / USB MJPEG 相机测试版

**当前为第 11 版，USB 预览、JPEG 拍照保存和关闭后重开已实板通过。** 已定位视频下方约 1/5 黑线闪烁的缓存问题：本配置未启用 `CONFIG_ARCH_DCACHE`，通用 `up_clean_dcache()` 是空宏，最后写入的像素仍在 256KB 缓存中，DMA 可能读到旧像素。本版改用显示驱动的 `esp_mipi_dsi_flush_framebuffer()`，每次实际写回 16KB，失败时不提交画面；已核对二进制中存在真实调用。现场动态显示效果待用户确认。

第 10 版保留的改进：视频只占上方 1024×500，每个缓冲页的底部按钮只在状态改变时重画，确认翻页成功后才复用。用户已确认按钮区改善。正常使用不需要每次插拔摄像头；此前重插建议只是故障对照测试。

## 已实现

- 列表显示实际检测数量、设备名称和 CSI / USB 类型。CSI 仅在启动时探测成功、注册 `/dev/video0` 后出现；CSI 排线应在断电状态连接，连接后重启检测。USB 按连接状态枚举，常驻 `/dev/uvcN` 节点不算作已连接设备。
- 最多列出 1 个 CSI 和 4 个 USB 视频设备，选择后启动一个预览；切换前等待上一会话停止。USB 重连使用新的 generation，避免误用旧设备句柄。
- UVC 描述符提供 MJPEG 分辨率、离散帧间隔或连续区间；Probe / Commit 协商实际参数。UVC 1.0 使用 26 字节 Probe，兼容不支持或错误实现 `GET_LEN` 的设备。
- 独立 USB HS Host、UVC 收流、MJPEG 组帧、软件解码和等比例预览；可处理摄像头省略标准 JPEG Huffman 表的情况。
- USB 拍照保存当前协商分辨率的 JPEG 到 `/data/photos/camera-XXXXXXXX.jpg`；独占创建，取消或写入失败时删除本次不完整文件。CSI 保留原 BMP 保存路径。640×480 JPEG 已实板保存并读回，用独立解码器完整解码通过。
- 预览底部保留拍照、返回选择和关闭控制；设备页的真实截图已检查，没有非预期控件重叠。

## 已取得的硬件证据

当前这只摄像头为 `HD Web Camera`，VID:PID `05a3:9331`。检测结果为 **CSI 0，USB 1**。摄像头通过协议报告以下 MJPEG 模式，均为约 30 fps（333333 × 100 ns 帧间隔）：

| 分辨率 |
| --- |
| 1920 × 1080 |
| 1280 × 960 |
| 1280 × 720 |
| 800 × 600 |
| 640 × 480 |
| 640 × 360 |

这里的帧率是设备能力／协商帧率，**不代表软件解码后的实际显示帧率**。1080p 的取景和拍照尚未实测通过。

当前第 11 版 SHA-256 为 `ee8465189b45167e567c9cd606106d9673e3cc9fe75c25226d444e1729e1f2d0`，3,965,660 字节，已增量编译并烧录校验，擦除范围止于 `0x3cb000`，未改动 `/data` 分区。88 个覆盖文件与实际构建树一致。第 11 版预览／拍照／重开过程中屏幕欠载和翻页错误均为 0，保存了 18,220 字节的新 JPEG。见 `toolbar-camera-11.json`、`cache-writeback-validation.json`。没有重新验证热点联网、蓝牙音频或真实 BLE 外设连接。

[第 10 版预览截图](evidence/ui-toolbar-10/dark-camera-preview.png)显示取景与独立操作栏；它读取 CPU 可见内容，不能证明 DMA 当时已读到全部新像素，也不能用于证明动态闪烁消失。第 10 版照片独立解码见 `toolbar-image-validation.json`。第 9 版首次启动仍超时，随后设备 generation 由 1 变为 2，三轮实际开停通过；重连触发原因未记录，不能归因于单一 PHY 修改。历史失败证据保留，不混作当前版本结果。

## 当前兼容范围与待验证项

- 重点支持 USB 2.0 **高速** UVC MJPEG：高速等时 IN，`bInterval=1`，每微帧 1～3 个事务；bulk 路径要求协商 payload 为端点最大包长的整数倍。
- 暂未支持全速等时、其他等时间隔、YUYV / H.264、摄像头麦克风及多路同时预览。不能据此宣称兼容全部 USB 2.0 摄像头。
- 一层外部 Hub、最多 4 个端口；Hub、多 USB 摄像头同时连接及 CSI 接入没有实际硬件验收结果。
- 单个压缩帧上限 4 MB，最多 32 个模式、每模式 16 个离散帧间隔；更大的设备能力表会截断。
- 仍需验证：现场动态闪烁、拍照中退出、真实热拔插、高分辨率及多设备切换、长时间运行。

## 构建与源码

`overlay/` 是在 `ble-device-names` 上追加相机支持的累计源码覆盖层。原来的 Wi-Fi、存储、桌面排版和 BLE 修复保留。原 `/home/streetartist/nuttxspace` 未修改。

当前构建使用 WSL Ubuntu 的 `/tmp/v3-desktop-camera-usb-20260914` 隔离树、Espressif GCC 14.2、xPack RV32IMAC/ILP32 软浮点 libgcc 和既有 TLS 缓存。完整工作区中运行：

```text
wsl -d Ubuntu --exec python3 /mnt/c/.../diagnostics/build_camera_usb.py clean
wsl -d Ubuntu --exec python3 /mnt/c/.../diagnostics/build_camera_usb.py
python diagnostics/validate_camera_usb.py
```

`...` 需替换为真实工作区路径。构建依赖既有隔离树，并非无需依赖的一键全新环境构建。不要重复运行初始移植脚本覆盖后续手工修复。

`nuttx.bin` 烧录偏移 `0x2000`，`/data` 从 `0x400000` 开始。验证脚本检查镜像摘要及擦除边界，烧录脚本只写固件区。对应哈希、编译状态和各阶段证据归属见 `build-metadata.json`、`SHA256SUMS` 与 `evidence/evidence-index.json`。

诊断命令：

```text
desktop cameras
desktop camera
desktop ui camera-start
desktop ui camera-mode
desktop ui camera-photo
desktop camera-stop
```

`desktop cameras` 的 `camera app` 行报告应用是否忙、显示帧数、错误及拍照状态；`photo=2` 才表示文件保存完成。`ready` 只说明界面诊断命令被处理。

USB Host core / Hub / DWC2 来自 CherryUSB，遵循 Apache-2.0，保留 `LICENSE` 和 `UPSTREAM.json`；未采用其视频类实现。UVC 驱动为本轮独立实现，私有 TJpgDec 副本保留 ChaN 许可证。
