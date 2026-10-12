# 网络直播（新版 v3 系统）

原生 LVGL 应用，直接请求 B站、虎牙、网易CC 房间 API、取得 H.264/FLV CDN 地址并由 OpenH264 2.6.0 软件解码。
入口为“全部应用”第一页的“网络直播”，用下拉菜单选择平台，输入数字房间号后播放。网络沿用新版 C6 Wi-Fi、DHCP 和时间同步；没有另建网络服务。
HTTPS 直接调用桌面的 `glass_https_tls_ops()`，使用原有 mbedTLS、证书校验、TLS 1.3 ticket
处理、取消和空闲超时机制。持续收到数据会续期，停止播放会取消请求。

直播使用单线程 H.264 解码，AAC-LC 音频复用本地播放器的 Helix 和板载 PCM 输出；暂不支持弹幕。
视频解码、AAC 播放与 FLV 取流独立运行。压缩视频最多排队 6 帧 / 2 MiB，音频最多 64 个包；
过时非参考帧可直接跳过，视频队列积压则保留下一个依赖帧以释放解码器延迟输出，再等待新的 IDR，携带最新 SPS/PPS 恢复。
音频独立播放并提供显示时钟；停止或切换会先取消、唤醒并加入音视频线程，再释放资源。

虎牙根据接口公布的兼容 AVC 档位选择最低正码率，沿用 Pure Live 网页 AntiCode 模板算法，
MD5 和 Base64 直接复用现有 TLS/PSA 库。保留服务端到期时间，尚未移植 WUP 长会话续期。
网易CC 根据接口的最低可用 Ali CDN 档位选择对应 streamname/token，以 `/live/<streamname>.flv`
直接取流；房间重定向入口会忽略档位并返回原画，不能用于低清播放。
本次实测虎牙 660000、11342412 的 500 kbps 及 CC 163163、5200 的 600 kbps 均为 800×450。
码率档位不是分辨率保证，界面以实际解码尺寸为准；超过上限会停止，不盲目分配大解码缓冲。
画面显示为 640×360 RGB565，码流限制为 1280×720、
渐进 8 bit I420 和有界参考帧/FLV 缓冲。请求 qn=80，服务端实际清晰度和正在播放的房间号在界面显示。
播放中输入新房间并点击“播放”会取消旧任务、回收后启动新房间；停止或退出取消待切换请求。
房间 6 当前匿名请求仍返回 qn=250、1280×720，不能凭请求参数强制降为低清。
支持最多三条 CDN 线路和三次重定向，拒绝 HTTPS 降级。退出、返回或切换应用取消旧会话。
可选登录 Cookie 从 `/data/bili_cookie.txt` 读取，不打印 Cookie 或签名播放地址。

## 安装和构建

需要准备好的新版 v3 NuttX/Apps、GCC 14.2 ILP32F 工具链和已匹配的 TLS/ESP-DL 静态库。
本仓库是 overlay；本次复用本机已准备的 `/tmp/v3-desktop-espdl-20260915`，不宣称全新机器
可直接构建。`tools/build_bilibili_v3.py` 将其复制到独立构建树，应用当前 overlay、
清理旧系统对象并重新编译，复用库的哈希写入 BUILD-METADATA.json。

```sh
python3 tools/build_bilibili_v3.py \
  --baseline /tmp/v3-desktop-espdl-20260915 \
  --workspace /home/streetartist/p4-bilibili-v3-current \
  --toolchain /home/streetartist/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/bin \
  --archive /path/to/openh264-2.6.0.tar.gz \
  --output out/bilibili/v3 --jobs 4
```

OpenH264 tar SHA256 为 `558544ad358283a7ab2930d69a9ceddf913f4a51ee9bf1bfb9e377322af81a69`。
安装器不下载另一份 TLS。系统 `/data` 位于 `0xc00000`，模型区从 `0x800000` 开始，
固件从 `0x2000` 写入并检查不得覆盖模型区；不要擦除或格式化现有数据。

## 实板诊断

NSH 使用新版既有的 UI 请求队列，由桌面线程操作 LVGL：

```text
desktop ui bilibili
desktop ui bilibili-play:12005649
desktop ui huya-play:660000
desktop ui cc-play:163163
desktop ui current
desktop ui bilibili-stop
free
```

`desktop ui bilibili-stop` 返回桌面并释放会话。也可用触摸“停止”留在当前页。
实际验证范围见 [VALIDATION.md](VALIDATION.md) 和 [MULTI_PLATFORM.md](MULTI_PLATFORM.md)。

直播修复：排队丢帧不再作废正在完成的画面；不因墙钟落后而拒绝所有关键帧。
音频队列溢出只丢弃旧压缩包，保留 AAC 状态和 PCM 设备；网络读取采用 16 KiB 有界缓存，避免 FLV/HTTP 小头部反复跨 C6 传输。
