# 本地视频播放器

桌面打开“本地视频”，插入 FAT32 SD 卡后点击刷新，选择 MP4 文件。可浏览子目录、暂停/恢复、停止和调节音量。没有卡时显示提示，不会格式化卡。

当前支持普通 MP4 中的 H.264（8-bit、逐行、最高 1280×720）和 AAC-LC（单/双声道，8–48 kHz），也支持无音轨视频。画面按比例放入 640×360 显示区域。HEVC、AV1、AC3、HE-AAC、MKV、DRM 和分片 MP4 不属于当前支持范围。推荐先用 360p/480p 文件；720p 支持不代表实时播放帧率保证。

视频复用现有 OpenH264；音频使用 Helix 固定点 AAC 解码，并复用已验证的 glass_music_pcm/ES8311 输出及设备互斥。MP4 使用 minimp4，第三方来源与许可证见 local_vendor。播放时先停止音乐，录音占用音频设备时给出提示。AAC 播放采样时钟驱动画面同步；MP4 edit list/编码器 priming 尚未补偿，部分文件可能有初始同步偏移。

资源限制：MP4 索引总预算 4 MiB，单包 1 MiB，最多 8 音视频轨道；目录每次最多显示 64 项。长视频可分段或放入子目录。不要在播放中拔卡；读取失败会停止并提示。

主机验证已覆盖 H.264 B 帧 + AAC-LC 3 秒测试视频、重复播放清理、暂停恢复、取消退出和 LVGL 页面生命周期。主机音频结果是解码 PCM，不等于实机扬声器验收。烧录/启动记录见 out/local-video/evidence，插卡播放和实际声音需实机补验。

## MJPEG + AAC 播放

支持标准 MP4 中的 MJPEG（FFmpeg `mp4v` sample entry，object type 0x6c）+ AAC-LC。MP4 复用 minimp4 索引解析、Helix AAC 音频和现有 PCM 输出；JPEG 复用桌面 qpk_mjpeg/qpk_tjpgd。视频直接解码到 RGB565 后台缓冲，按原尺寸居中显示，板上不缩放。JPEG 仍需解码，换成 MP4 容器本身不会提升帧率。音频采样时钟驱动画面；过期 JPEG 在解码前跳过，避免积累延迟。

将文件复制到 FAT32 SD 卡，打开本地视频，点击刷新选择文件。提供的 areyouok-mjpeg-640x360.mp4 约 44 MB，areyouok-mjpeg-320x180.mp4 约 19 MB；均为 24 fps，AAC-LC 48 kHz 单声道 64 kbit/s。320×180 显示为原尺寸，不放大。

电脑转换命令（需要 FFmpeg）：

```powershell
ffmpeg -n -i input.mp4 -map 0:v:0 -map 0:a:0? -vf "fps=24,scale=640:360:force_original_aspect_ratio=decrease,pad=640:360:(ow-iw)/2:(oh-ih)/2:black,setsar=1" -c:v mjpeg -q:v 6 -pix_fmt yuvj420p -threads 1 -c:a aac -profile:a aac_low -ar 48000 -ac 1 -b:a 64k -movflags +faststart output-mjpeg.mp4
```

缩放和保持比例的黑边填充在电脑完成。更小版本将命令中的 640/360 改为 320/180。JPEG quality 数字越大压缩越多。

视频尺寸限制 2..640 × 2..360，baseline JPEG 单帧不超过 512 KiB。MP4 索引预算 4 MiB，保留原 H.264 MP4 支持；分片 MP4 不在支持范围。AAC-LC 支持单/双声道 8–48 kHz。MP4 edit list/编码器 priming 尚未补偿，可能有初始同步偏移。

主机 ASan 验证覆盖 MP4 MJPEG/AAC 实际解码、完整重复播放、暂停恢复和取消清理，原 H.264/AAC  回归通过。LVGL 测试验证 320×180 图像描述符、步长、居中、原尺寸发布和页面清理。主机解码不等于实机音画/24 fps 验收；实机需复制文件到 SD 卡后测量。
