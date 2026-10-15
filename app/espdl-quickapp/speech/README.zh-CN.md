# ESP32-P4 v3 本地中文语音

本适配将 SanoTTS 中文 Xiaoya 文字转语音，以及 ESP-SR WakeNet9 / MultiNet7 中文固定命令接入 openvela 桌面。仅在构建时显式传入 `--speech-runtime` 时启用；启动不会自动打开麦克风。

## 使用

AI 聊天页右上角进入「本地语音」。可以手动开始离线聆听、测试中文播报和停止，关闭页面也会请求停止。唤醒词为「嗨，乐鑫」，唤醒后有 6 秒指令窗口。默认支持打开/关闭灯光、窗帘、风扇，以及返回桌面、停止聆听；当前只显示识别事件，尚未绑定真实设备操作。其中「停止聆听」结束本次监听。

语音任务独占音频会话，其他音频会话开始时请求取消本地语音。模型按需从 SD 加载，停止后释放；合成在后台线程执行。取消在模型调用、音频帧和输出分块之间检查，单次推理调用内不能立即中断。

CLI（供手动测试）：

```text
desktop speech status
desktop speech listen
desktop speech say "你好，欢迎使用智能家居。"
desktop speech stop
```

## 构建和 SD 资源

在项目根目录运行：

```sh
python3 tools/prepare_speech_runtime.py --cache /path/to/speech-upstream --output /path/to/speech-prepared
```

准备脚本按 `upstream.lock.json` 的提交、大小和 SHA-256 验证依赖，生成 C 运行时、紧凑中文字典及 SD 资源。使用现有 ESP32-P4 v3 完整构建命令，额外加入：

```text
--speech-runtime /path/to/speech-prepared
```

将生成的 `sdcard/speech/` 放到 SD 根目录；实际路径须为 `/sdcard/speech/`。加载器只接受编译时固定的资源及哈希。`front_f16.bin` / `dec_f16.bin` 按官方参考模型精确扩展为 float32，并再次校验上游 SHA-256。

当前所有构建均只从 `/sdcard/espdl/` 加载视觉模型，不再读 Flash 模型区。16 MiB Flash 的前 14 MiB（`0x000000–0xDFFFFF`）用于启动及固件，固件仍从 `0x2000` 开始；最后 2 MiB（`0xE00000–0xFFFFFF`）为 `/data` 内置存储。部署包包含分类和人脸检测所需的三份视觉模型文件，以及 `/sdcard/speech/` 下的语音资源。

快应用安装支持选择内置 `/data/qpk` 或 SD `/sdcard/qpk`。桌面合并扫描两个位置并显示来源；应用键值和录音数据使用所在卷的 `qpk/.data`，卸载只清理对应卷。AI 生成应用在预览后保存时弹出位置选择，手机网页安装页也提供选择。没有 SD 卡时不会自动改装到内置存储；系统设置和草稿仍保存在内置存储。

首次升级此布局前必须备份旧 `/data`。旧版数据起点为 `0xC00000`，不能将旧 SmartFS 原始镜像直接写入新的 2 MiB 分区。使用旧固件的配对网页导出文件，或运行 `tools/backup_storage_export.py --url http://设备IP:8080 --output 本机备份.zip`（交互输入屏幕配对码），再用 `--verify 本机备份.zip` 校验。工具只读取旧固件数据，不烧录、不格式化。升级后首次挂载失败时按板级提示手动初始化新分区，再按可用空间恢复设置和应用；容量较大的应用重新安装到 SD。备份包含用户配置，勿放入公开部署包。当前未操作实机或迁移真实数据。


## 已验证范围

- 使用实际 ESP32-P4 v3 / RV32 单精度浮点编译参数，完成源码编译和整机链接。
- 主机 ASan / UBSan 下运行真实 SanoTTS 中文模型，验证 PCM 输出、取消和参数检查。
- 114 组文字的紧凑字典 + QuickJS 结果，与锁定上游原版 JS 前端的音素 ID 完全一致。
- 识别模型注册、音频分帧、唤醒窗口、拼音注册、置信度、未知命令拒绝、超时和释放：使用模拟推理接口验证。
- 私有互斥量竞争、内存对齐、数学兼容函数，以及损坏/缺失资源拒绝。
- 实际 LVGL 页面在明暗主题下渲染，验证手动按钮和退出清理。

尚未烧录或验证实机识别率、合成速度、麦克风增益、长期运行和音频交接。当前从已有 16 kHz 录音路径下混为单声道直接送入 WakeNet / MultiNet，尚未接入 ESP-SR AFE 降噪和回声消除。应先在安静环境近距离测试，再根据录音和性能数据决定 AFE 或内核优化。

## 上游与许可

- SanoTTS：Ampixa/sanoTTS，提交 `f73b8382bc98a1c8de3488ba30d727ab82587828`。保留上游 `LICENSE`、`LICENSE.MIT` 和语音 `meta.json`；不能将整套组件一概视为 MIT。中文 G2P 沿用上游 GPL-3.0-or-later 来源，修改仅替换字典存储接口，保留文本规范化、分词及变调逻辑。
- ESP-SR：espressif/esp-sr，提交 `cfdbb52172d4f1b3e5f7469d6c3c6e5dd0738143`；保留其限用于乐鑫产品的上游许可。中文命令注册使用不带声调的拼音。
- FFT：espressif/esp-dl，提交 `17e9abb1c59cd66b77b4bdb95587abcfafbb124a`；采用可移植 C 路径，保留 Apache-2.0 许可。

完整依赖文件清单、校验值和来源均记录在锁文件；准备脚本可恢复用到的对应上游源码。未修改既有常规构建的默认行为。
