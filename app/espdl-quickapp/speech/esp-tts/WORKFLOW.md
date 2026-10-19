# 官方 ESP-TTS 完整流程

目标为 ESP32-P4 Function-EV-Board v3.2、openvela/NuttX、GCC 14.2 ILP32F。
官方输入固定为 ESP-SR v2.4.6，原始库、头文件、音色与 LICENSE 均以仓内
`upstream.lock.json` 的长度、SHA-256 和 Git blob 校验，不信任外部目录自带的新清单。

## 准备与检查

从仓库根目录，在 Linux/WSL 中执行；需要 Python 3、GCC 与 ASan/UBSan。

```sh
python3 tools/prepare_esp_tts.py --output /path/to/esp-tts
python3 tools/prepare_esp_tts.py --output /path/to/esp-tts --verify-only
python3 tools/verify_esp_tts.py --prepared /path/to/esp-tts --output /path/to/checks
```

已缓存输入可用 `--offline`；校验通过的缓存不会重新下载或重写。
检查包括篡改清单、损坏下载恢复、适配器生命周期、语速、官方 SD 音色完整性、
合成取消、预缓冲、麦克风交接、实际 576 帧 PCM 缓冲和 codec 错误传播。
主机适配器测试使用模拟合成引擎；不把它当作官方 P4 库执行或试听通过。

## 构建

加载准备树的环境，确保 `kconfig-conf`、交叉工具链及其动态库可用。
`build_bilibili_v3.py` 保留显式基线和独立候选目录，语音启用后默认选择 ESP-TTS；
只有显式 `--tts-backend sanotts` 才选择旧后端。

```sh
python3 tools/build_bilibili_v3.py \
  --baseline /path/to/prepared-v3 --workspace /path/to/isolated-candidate \
  --toolchain /path/to/gcc14/bin --archive /path/to/openh264-2.6.0.tar.gz \
  --speech-runtime /path/to/prepared-speech --esp-tts-prepared /path/to/esp-tts \
  --output /path/to/firmware --storage-layout preserve --jobs 6
python3 tools/verify_esp_tts.py --prepared /path/to/esp-tts \
  --firmware /path/to/firmware --toolchain /path/to/gcc14/bin --output /path/to/checks
```

默认 `--esp-tts-storage sd` 保留 12 MB 固件区与 4 MB 数据区：完整小新音色 3,821,311 字节
位于 `/sdcard/speech/esp-tts/voice.dat`；小乐音色 2,938,039 字节位于 `voice-xiaole.dat`，
小新精简音色 2,913,777 字节位于 `voice-small.dat`。启动后默认选择完整小新，
在后台工作线程中分块读取并完整校验，
随后播报复用这份不可变缓存。句柄、解析状态和 PCM 仍在结束或取消时释放；音色缓存
只保留所选音色的约 2.9–3.8 MB 用户堆，可用 `desktop speech cache-clear` 显式释放，借用中的缓存延迟
释放。缺失、截断或哈希不符会报错，不回退到旧模型。构建目录也输出 SD 文件和许可证。

`--esp-tts-storage flash` 保留嵌入固件的方式，必须满足镜像大小检查；当前完整资源组合
无法装入 12 MB 固件区，不能直接烧录。独立 `build_speech_runtime.py` 为兼容历史调用仍
默认 flash，需显式传入 `--esp-tts-storage sd`。

构建在最终 `olddefconfig`、清理和 `context` 之后编译语音；检查实际 ESP-TTS 链接符号、
旧后端未链接、启动 BSS 边界、数据分区边界、最终配置及镜像摘要。

## SD 部署与实板

Windows 主机安装 pyserial 后，使用真实局域网地址和目标板串口：

```powershell
python tools/deploy_speech_models.py --esp-tts-prepared C:/path/to/esp-tts `
  --host 192.168.x.x --board-address 192.168.x.y --port COM4 `
  --output C:/path/to/voice-deploy
```

只向指定板提供已校验音色和 LICENSE，先在 SD 临时目录下载，再重命名发布；
已有目录默认保留。明确替换时使用 `--replace-existing`，旧目录会作为回滚保留。

烧录前保存完整应用区备份，**不包含数据区**。对于经核对的数据起点 `0xc00000`，
从 `0x2000` 读取 `0xbfe000` 字节；其他布局必须重新核对。将备份摘要记录为同目录
`rollback.json` 的 `sha256` 字段，然后使用校验过的镜像：

```powershell
python tools/deploy_esp_tts_device.py --firmware C:/path/to/firmware `
  --verification C:/path/to/checks/verification.json --backup C:/path/to/rollback-app.bin `
  --expected-serial <目标板USB序列号> --port COM4 --output C:/path/to/device --check-listen
```

该工具核对板身份、当前数据分区、镜像与备份摘要，执行烧录写入校验、三句实际播放、
播放时长对照、取消及可选的 ASR 开麦交接。失败自动恢复原应用区；通过后保留新固件。
不会写入 Home Assistant、AI 或 ASR 密钥。结果不代表音色自然度已经人工验收。
设备冷启动后必须等待 NTP 校时再使用 TLS/ASR；需要在部署测试中由主机提供 UTC 时，
显式加 `--sync-clock`，报告会记录时钟来源。这不代表自动 NTP 已验收。
开麦检查要求监听状态持续至少 2 秒，并排除 DMA 分配错误，避免仅以刚进入监听的一瞬间
判定成功。云端收音与离线收音使用同样的 1,984 字节缓冲，降低 128 KiB 内核堆对连续
DMA 空间的要求；需要在 AI 查询后继续收音复测。以运行时 `free` 的实际内核堆为准，
不能把配置中的 128 KiB 当作可用 DMA 空间。

运行时可用 `desktop speech status` 确认 `backend=esp-tts`；
`desktop speech voice [xiaoxin|xiaole|small]` 查询或切换音色，合成或加载期间拒绝切换。
`desktop speech speed 0..5` 调整下一句语速，默认 3，重启恢复。
`desktop speech volume 0..100` 调整下一句音量，默认 75，重启恢复。日常 ESP-TTS 播放在
整句预合成后，以重叠的 32 ms RMS 窗口测量局部响度，每 4 ms 生成一个增益控制点。
先前后平滑，再逐样本插值，两遍分别最高放大 12 倍和 1.5 倍，合计最高 18 倍；
每遍 PCM 峰值限制为 30,000。低幅度窗口保持相邻增益，不把弱词尾门限截断。
增益不在合成或 DMA 分块边界重置；采样数、语速和停顿不变。导出的官方原始 PCM
保持不变。三句同一有效窗口的响度 P90/P10 差从 8.5–13.6 dB 降至 1.5–2.5 dB，
这只是客观幅度指标，不能代替听感验收。

## 离线神经 TTS 对照

显式使用 `--tts-backend sanotts`，并省略 `--esp-tts-prepared`，可构建已锁定的
SanoTTS 中文小雅 F32 对照版。模型权重约 6.2 MB；中文声码器以 64 帧中心块和
前后各 16 帧上下文解码，工作区最高 8,036,352 字节，保持完整解码的输出 PCM。
固定模型结构之外拒绝套用该上下文长度。`test_speech_host.py` 使用真实中文模型
比较完整解码，覆盖 1/97/256 帧、取消、缓存释放和中文前端。

神经版也先预合成整句再播放，避免合成不足实时造成 DMA 等待和断字；它增加的
开口等待必须单独测量。上游非中文 MCU 模型的实时指标不能当作此中文模型的 P4
性能。音质、时延、内存三者均通过后，才适合作为日常默认后端。

## AI 操控修复

助手模式直接提供 `home_*` 与 `music_*` 工具，减少本机发现工具的模型往返；
`home_list({query:"床头灯"})` 在服务端筛选设备后分页，避免遍历完整设备列表。
权限与共享服务校验继续生效，旧 MCP 调用入口仍兼容。

灯光优先 `home_light({query:"床头灯"})`，或合并 `on`、`brightness` 和 `rgb_color`。
唯一灯匹配和实时查询/控制回读在一次工具调用中完成；重名、连接配置变化、权限关闭
均拒绝写入。已验证的控制无需追加状态查询，不把缓存状态当作新的控制确认。

保留模型原有思考参数；每次设备查询或控制必须取得本轮真实工具回执，历史只用于
确认设备身份，不能证明当前状态。测速也必须检查实际工具调用及结果卡片，不能只
检查模型回答是否完成。HTTP 每轮记录首响应字节时间、总耗时、
请求/响应字节数，家居工具记录实际执行时间；日志不记录密钥或完整正文。

任务栏显示当前最新请求；颜色控制回读比较 RGB 数组并返回实际颜色属性，状态未一致时
只重读，不重复发送控制。回读间隔为 150/300/600 ms，最长仍受截止时间约束。
说明文字优先一条简短结果，避免重复卡片和内部字段名。

```sh
python3 tools/test_agent.py --apps /path/to/apps --output /path/to/agent-checks
python3 tools/render_all_ui.py --apps /path/to/apps --cache /path/to/native-lvgl-cache \
  --output /path/to/workspace-checks \
  --harness-source app/espdl-quickapp/tests/render_espclaw_workspace.c
```

浏览器移植实验已移出当前工程源码；本轮不集成浏览器。
