# 当前本地与在线视频交付记录（2026-10-12）

系统基线为 `48da6d570c619900d3a1426e9efe04b2c29ef366`。本次提交包含桌面入口、本地 MP4 H.264/MJPEG＋AAC、直播三平台、单线程追帧、独立 AAC 播放、全屏/缩放和共享 PCM 设备集成。

## 最新固件与板上边界

- `out/local-video/v3-local-av/nuttx.bin` SHA-256：`684c6adae708ef09126d74c41e3a6b7f383b76a1079871e46895fa39fe6549ef`。
- 固件已烧录到身份匹配的 ESP32-P4，0x2000 写入、esptool 哈希校验通过；没有擦除模型、数据或 SD 卡。
- 按用户要求仅烧录，未自动操作 UI 或启动直播。最新修复版的 B站/虎牙音画效果尚待用户手动验收，不能声称已流畅或达到 24 fps。
- 上一版用户实测 B站 `frames=0`、视频长期跳帧，虎牙音频反复重开。当前修复保留正在完成的画面和下一依赖帧、不以墙钟落后拒绝所有 IDR、音频队列满只丢旧包不重开 PCM，并加入 16 KiB HTTP 读取缓存。
- 曾试验双线程、SD 环形缓冲及特殊视频容器。当前生产路径使用同步单线程 H.264；SD 环形缓冲代码未链接，特殊容器不再支持。所有音视频队列有界，退出/切房先取消并回收线程与 PCM 资源。

## 实际验证

- 本次重新完整编译宿主 OpenH264/Helix 并运行三种媒体回归：四种直播场景及本地 H.264/MJPEG＋AAC 的功能断言全部通过，未出现 AddressSanitizer 错误。
- **已知诊断，尚未修复：** 启用 UBSan 后，Helix 定点 AAC 运算在 `assembly.h`、`bitstream.c`、`huffman.c`、`dct4.c`、`pns.c`、`stproc.c` 和 `tns.c` 报告有符号移位溢出/负数左移。UBSan 默认恢复执行，因此进程退出 0 仅代表功能断言通过，不能称为 sanitizer 全通过。保留检测，不屏蔽告警；后续需单独评估并修复这些定点运算。

- 真实 800×450 H.264＋AAC FLV：正常显示 73 帧/142 个 AAC 帧；人为拖慢显示后仍输出画面。
- 人为将每包视频解码拖慢到 1.2 秒：2 帧输出、142 个 AAC 帧，避免队列作废导致画面饥饿。
- 突发收流、音频队列溢出：旧音频包被丢弃，设备只打开一次；四种场景取消后均完成线程回收和音频关闭。测试使用模拟 PCM 输出，不代表真实扬声器验收。
- HTTP 分片、chunked、长度、EOF、非法尺寸、截断、缓存小读取合并及缓存状态取消测试通过。
- 单线程 144 帧视频做 10 对重复解码、1152 帧连续视频做两次解码，PTS 与 YUV 哈希一致；该检查在最终队列/网络补丁前完成，补丁未改变解码器。
- 本地 H.264/MJPEG＋AAC、暂停/恢复、取消及页面生命周期的历史宿主验证见 LOCAL-VIDEO.md。最终固件编译通过。

原始构建/烧录证据留在本机 `out/local-video/evidence/live-starvation-*`；源文件哈希与测试摘要见 `live-starvation-fix-validation.json`。临时视频、签名 CDN 地址、凭据和固件构建产物不入 Git。

## 重跑宿主媒体回归

先用 `tools/install_bilibili_live.py` 安装同版本补丁 OpenH264，再在 Linux 上运行：

```sh
python3 tools/test_media_players_host.py --apps /path/to/apps --output /tmp/media-host \
  --live-flv /path/to/live-test.flv --h264-mp4 /path/to/local-av.mp4 \
  --mjpeg-mp4 /path/to/local-mjpeg.mp4
```

测试 FLV 为 3 秒、800×450、25 fps H.264、每秒 IDR，AAC-LC 48 kHz 单声道；本地 MP4 为 3 秒 320×180 测试片；H.264 配双声道 AAC-LC，MJPEG 配 48 kHz 单声道 AAC-LC。测试需要 GCC/G++，自动启用 ASan/UBSan；不触碰板卡。生成 MP4 MJPEG 的命令见 LOCAL-VIDEO.md。

---

以下为早期历史记录，固件与限制均不代表当前版本。

# 新版集成验证

基线：官方提交 `48da6d570c619900d3a1426e9efe04b2c29ef366`。以下区分宿主验证与实板记录。

## 历史视频模式固件（非当前）

- 文件：`out/bilibili/v3-switch/nuttx.bin`，7,794,896 字节。
- SHA256：`90157b975e5d9dbd3f7ce76644f991fd389ea5baf18326a790fb4fe448613c77`。
- COM23，USB 303A:1001，序列号 E8:F6:0A:E3:A9:5F，ESP32-P4 v3.2。
- 仅写入 0x2000 程序区，末尾 0x7710d0，未覆盖 0x800000 模型区及 0xc00000 数据区。
- esptool 写入校验通过；C6 Wi-Fi 自动连接，eth0 获得 192.168.0.103。
- 当前固件实板播放房间 6，解析为 7734200，解码 1280×720。
- 房间 6 的 30 帧样本：16.380 秒，约 1.83 fps；解码累计 13.330 秒，其中转色 1.480 秒；读流累计 3.040 秒。不同房间/时间样本不能用作公平性能对照。
- 较早性能固件播放 12005649：30 帧 / 18.250 秒，约 1.64 fps。播放占用约 31.47 MB 外部堆，剩余约 2.1 MB；停止后外部堆使用约 5.24 MB。

## 清晰度与切房

- 生产播放请求 qn=80。房间 6 当前匿名请求 qn=80、150、250 均返回 current_qn=250，即使 accept_qn 列表包含 150，也不代表当前请求取得低清流。
- 生产共享 TLS 宿主探测房间 6 与 12005649 均解码 3 帧 1280×720。实板房间 6 的源视频尺寸也是 1280×720。
- 640×360 是显示画布尺寸，先按源视频尺寸解码再缩放，不等于收到 360p 源视频。
- 修复播放中输入新房间继续旧会话的问题：取消并回收旧任务后启动最后一次请求；状态栏显示实际播放的输入房间号。
- 确定性 LVGL 宿主测试覆盖同页面切房、停止取消待切换、退出取消待切换；实板 UI 队列测试用于验证重开页面切房，两者是不同路径。

## 宿主验证

- `bili_http.c` 复用生产 `glass_https.c`，原有 TLS 实现未改动。
- ASan/UBSan HTTP 分片、chunked、长度、EOF、截断和非法尺寸测试通过。
- OpenH264 宿主对象复用同版本/同包装器的 sanitizer 构建。
- 实际 LVGL 1024×600 宿主渲染通过；I420 转色、黑边、取消、三缓冲所有权和 20 次启动/停止/删页测试通过。
- `out/bilibili/evidence/lvgl-idle-host.png` 是宿主模拟画面，不是板卡截图。

## 限制及下一步

当前仅视频，无音频/弹幕。单线程 OpenH264 720p 软件解码仍然卡顿，不能声称流畅播放。现有堆余量不足以直接增加解码线程；后续需验证可用低清源或评估解码及网络缓冲预算。固件启动时间校准依赖既有 NTP，本次串口测试先设置准确 UTC，以便证书校验。

证据目录：`out/bilibili/evidence/`，重点文件 `flash-switch.log`、`room6-switch-board.log`、`room6-quality.json`、`switch-ui-host.log`、`switch-build.log`。实板队列切换 12005649 → 6、退出再开 6 均出现新房间首帧；停止后外部堆使用 3,952,200 字节，空闲 29,602,232 字节。记录见 `room-switch-board-retry.log` 与 `switch-board-acceptance.json`。板卡最后留在房间 6。构建时源/配置/复用库哈希见 `out/bilibili/v3-switch/BUILD-METADATA.json`；构建完成后追加了测试和本文档，生产代码未再改动。

宿主测试片可用 FFmpeg 生成（以下输出名仅用于回归，不入 Git）：

```sh
ffmpeg -y -f lavfi -i testsrc2=size=800x450:rate=25 -f lavfi -i sine=frequency=440:sample_rate=48000 -t 3 -c:v libx264 -preset veryfast -threads 1 -g 25 -bf 2 -refs 4 -c:a aac -profile:a aac_low -b:a 64k -f flv live-test.flv
ffmpeg -y -f lavfi -i testsrc2=size=320x180:rate=24 -f lavfi -i sine=frequency=440:sample_rate=48000 -t 3 -c:v libx264 -preset veryfast -threads 1 -bf 2 -c:a aac -profile:a aac_low -ac 2 local-av.mp4
ffmpeg -y -f lavfi -i testsrc2=size=320x180:rate=24 -f lavfi -i sine=frequency=440:sample_rate=48000 -t 3 -c:v mjpeg -threads 1 -q:v 6 -pix_fmt yuvj420p -c:a aac -profile:a aac_low -ac 1 local-mjpeg.mp4
```
