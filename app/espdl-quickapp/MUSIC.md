# 声动音乐与中文输入

原生 LVGL 音乐应用，面向 1024×600 的 ESP32-P4 Function EV Board。布局采用侧边导航、歌曲列表、右侧唱片和底部播放控制，适配浅色与深色主题。

## 使用

- 点击桌面音乐卡片进入「声动」，点搜索栏输入歌名或歌手。
- 中文模式使用 Android 开源项目的 Google 拼音引擎（libgooglepinyin 0.1.2，Apache-2.0），附带约 6.5 万词、1,068,442 字节的离线词频词库。输入连续拼音即可选字、词或整句，支持分段选词；例如 `nihao` 首选「你好」，`woxihuanyinyue` 首选「我喜欢音乐」。左右箭头翻页，空格选当前页首项。
- 点击「中 / EN」切换中英文；`1#` 输入数字与符号。按键在抬手时生效，长按不连发。输入中的第一次确认负责选字，再次确认提交。
- 点歌曲开始播放；底部可暂停、恢复、调节音量和收藏。上一首、下一首在收藏列表中循环。退出应用后继续播放。
- 收藏最多 20 首，保存到 `/data/qpk/music`。保存歌曲信息与搜索词，每次播放重新获取地址，避免保存已过期的链接。

中文键盘也已接入 Wi-Fi 名称、文件新建/重命名及快应用输入框。密码框保留英文输入。

引擎用户词典位于 `/data/qpk/pinyin/user.dat`，关闭输入框时刷新缓存。系统词库直接从固件内存读取，不占用用户数据分区。长候选自动减少每页候选数量以获得更多显示空间。

## 接口与实现范围

参考 MicroReactor 的音乐接口协议，使用 `jkapi.com` 的 QQ 音乐检索。该接口每次返回一首匹配歌曲，曲库可用性依赖接口和资源提供方。当前支持 MP3 流播放，不提供歌词、完整专辑列表、离线下载或拖动定位。右侧为应用绘制的唱片图案；计时来自实际音频缓冲区完成数量。

网络使用验证证书的 TLS，256 KiB 环形缓冲与独立下载线程，minimp3 解码后下混为单声道，通过共享 ES8311 设备及 GPIO53 功放输出。搜索、播放和收藏落盘均在工作线程执行。停止和切歌通过代次编号取消旧请求。

私有 `music_credentials.h` 由 `diagnostics/prepare_music.py` 从参考工程中提取音乐接口密钥生成，已加入忽略规则；日志不记录密钥或带签名的播放地址。`music_vendor` 保存解码器、拼音引擎及许可证。Google 拼音源码来源、源码包/词库 SHA-256 与移植说明见 `music_vendor/googlepinyin/manifest.json`。移植固定了词库的 32 位整数格式、修复两处释放遗漏，并匹配 NuttX 的整数类型；候选搜索及词频算法沿用开源引擎。

## 构建与检查

沿用 README 的 WSL 构建环境。准备脚本为 `diagnostics/prepare_music.py`（Windows）、`diagnostics/prepare_google_pinyin.py`（WSL，使用已下载的 Debian 官方源码包）；已有依赖、生成词库和私有配置时不需要重新下载或生成。构建运行 `wsl -d Ubuntu --exec python3 diagnostics/build_espdl.py`，固件更新使用 `python diagnostics/flash_espdl.py --execute --firmware-only --log <新的日志名>`，保留已有模型和数据。

- `diagnostics/test_music.py`：真实 JSON 解析、UTF-8 URL 编码、畸形输入及实际 MP3 解码，使用 ASan/UBSan。
- `diagnostics/test_ime.py`：真实 LVGL 指针输入，覆盖点击、两秒长按、两次独立点击、词频排序、整句输入、分段选词、英文/数字切换和控件销毁，使用 ASan/UBSan。
- 实机状态命令：`desktop ui music-status`；搜索、播放结果、暂停切换、停止分别为 `desktop ui music-search:<歌名>`、`desktop ui music-play-result`、`desktop ui music-toggle`、`desktop ui music-stop`。

构建及烧录哈希见 `evidence/firmware-validation.json` 和对应 `flash-music-*.log`。实机验证记录与界面截图保存在 `evidence`。
