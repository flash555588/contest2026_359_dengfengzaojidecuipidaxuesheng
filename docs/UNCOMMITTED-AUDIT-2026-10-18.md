# 未提交改动验证清单（2026-10-18）

范围：仅 `project` Git 仓库尚未提交的修改和新增文件，不包含父工作区中的工具、输出和其他仓库。审计基线为 `6732275`。整理前共有 **224 个文件：114 个已修改、110 个未跟踪；没有已暂存文件**。本报告是审计新增文件，不计入这 224 个文件。

本次只整理和检查，没有提交、推送、烧录、移动或删除原有文件。除明确列出的重新执行检查外，其他结果来自本地已有证据，不表示本次重新运行了全部测试。

完整逐文件清单：[files.csv](../../output/uncommitted-audit-20261017/files.csv)；当前文件 SHA-256 与历史证据逐项对照：[inventory.json](../../output/uncommitted-audit-20261017/inventory.json)。两者位于本地工作区，不随本仓库自动提交。

## 状态口径

- **本次通过**：针对当前工作区重新执行的检查通过，仍以检查实际覆盖范围为限。
- **已有验证**：有主机或实板记录，但不能自动覆盖之后修改的代码。
- **部分验证**：仅覆盖界面、模拟传输、离线音频、单板诊断等部分环节。
- **未验证／未通过**：没有找到对应验收，或现有记录明确失败；不能算作通过。

文件哈希匹配只证明内容与某个证据快照相同，不证明该文件所有分支都被测试。本次在所扫描的顶层及 `firmware/verification.json` 证据中，81 个未提交文件能匹配至少一个历史哈希；其余文件包含缺少哈希记录、后续修改、工具和文档等情况，不能直接称为“143 个文件完全没测试”。

## 按功能整理

| 未提交功能 | 已有或本次验证 | 尚未验证／限制 | 判断 |
| --- | --- | --- | --- |
| 家居 MCP：`home_*`、共享 HA 服务、对话设备卡片 | 本次 `test_agent.py` 通过；`BOARD_HOME_OK` 覆盖状态缓存、确认／未确认控制、参数与白名单拒绝、场景卡片，工具路径不打开页面；已有 46 张聊天界面截图 | 当前新增家居工具未完成整机链接、刷机及真实 HA 设备动作验收；主机传输为夹具 | 主机已验证，实板待验 |
| 音乐 MCP：搜索、播放、暂停、恢复、停止、音量、切歌、状态 | 本次 `test_music_background.py` 通过；使用真实 MP3 解码器和工作线程，HTTP／PCM 为离线夹具；覆盖幂等暂停恢复、取消、脱敏、释放，未链接 UI；已有 50 张聊天截图 | 当前 MCP 版本未完成整机链接与实板验收；真实音乐接口、扬声器、AI 连续调用尚未验证 | 主机已验证，实板待验 |
| ESPClaw MCP 框架／模式／流式聊天／卡片历史 | 本次 Agent、MCP HTTP、桥接、模式隔离、卡片持久化及 Markdown 测试通过（ASan/UBSan）；聊天紧凑顶部有原生渲染记录 | 当前组合的真实云端 LLM→MCP→设备完整会话未验收；旧板载 MCP 尝试启动验证失败 | 部分验证 |
| 首页、合并状态岛、字体、顶栏对齐、按下边框和进入设置 | 当前版本此前的真实 LVGL 渲染通过，32 张明暗主题截图，覆盖点击设置、锁屏、后台音乐、40 次复开；RSSI 辅助函数有真实 protobuf + ASan/UBSan 主机记录 | 新状态岛未链接或烧录；实时 RSSI、真实蓝牙连接与显示联动待实板核验；现有蓝牙 HID 只支持 0／1 个连接 | 界面已验证，硬件联动待验 |
| 电量和充电显示 | 未知电量、百分比、电池与独立闪电外观有夹具截图 | overlay 中 `CONFIG_BATTERY_GAUGE` 未启用；没有真实电量计／充电状态验收，无数据时为 `--%` | 仅显示层验证 |
| 米家瀑布流、拖动、侧栏、详情、紧凑顶部、资源与授权绑定 | 本次 HA 前端测试通过；已有 512 设备、固定对象池、原生滚动与滑条冲突等记录；旧板上有侧栏／页面性能记录；本次确认两份 JS 相同、三个 C 资源副本相同、JS 为 LF 且 SHA-256 与授权头一致 | 当前最新外壳／顶栏组合未做实板回归；本地性能验收记录真实设备控制为 0，不能算动作联动验收；身份检查静态测试仍失败 | 部分验证，存在测试失败 |
| 相机预览、控制布局、缩放、重开与带音轨 AVI 录像 | 本次相机前端检查通过；已有宿主写入器／工作线程、取消授权／缺 SD／麦克风失败及完整解码记录；实板两段约 6 秒、320×180／10fps、16kHz PCM 录像及重开记录 | 当前共享运行时又有后续修改，仍需最终组合回归；不能由短录制推导长时稳定、极限存储容量或所有播放器兼容性 | 有主机及历史实板验证 |
| 自定义唤醒、模型、流式特征与取消交接 | 既有 ASan/UBSan、连续负样本、分块一致性、实板离线正负夹具与麦克风重开记录；`glass_custom_wake.c` 与 v3 证据哈希一致 | 主要是合成训练／评测；真人唤醒率、真实环境每小时误唤醒尚未验证；语音公共代码后续已变动 | 部分验证 |
| 连续语音、本地命令、设备绑定、停止与资源释放 | 既有主机状态机／停止竞态检查、实板采集启停／选择器／相机重开与对话状态记录 | 真人唤醒→云 ASR→LLM→播报全链路未验收；本地家居命令未进行真实设备动作与真人识别准确率验收 | 部分验证 |
| 中文 TTS 计算取消、缓存、分块与双核 | 历史主机 560 组卷积逐位对照、PCM 一致性和取消检查；历史板上冷暖启动、双核、缓存释放和麦克风／相机重开记录 | 不能把旧 FP32 后端的延迟结果套用到当前 ESP-TTS；共享语音代码已变化，当前组合仍需回归；人工听感不由数值一致性代替 | 有历史验证，需区分后端 |
| 乐鑫 ESP-TTS、语速 0–5、576 帧缓冲、codec 错误回调 | 仓内已有 2026-10-17 实板播报、语速 2／3／4 对比、PCM 导出哈希与恢复聆听记录；已有 90 组 PCM framing、codec 回调及适配器主机记录 | `human_continuity_passed` 为 null，部署的 `quality_passed` 为 false；不能称字间衔接、自然度已验收；当前组合没有重做完整内存压力及云端会话测试 | 播放路径部分验证，音质未通过验收 |
| Nano 官方英语 INT8／PIE 实验 | 有锁定官方模型、真实板载 PIE 用例、标量／PIE PCM 相同与计时记录；官方英语证据记录约 3.03 倍推理加速 | 属于特定英语诊断模型，不代表中文能力；当前源码与部署关系仍需最终构建快照；人工听音未验收 | 特定模型诊断已验证 |
| 中文蒸馏／量化／PIE 实验与训练工具 | 有训练、C／Python 数值对照、诊断候选构建和分句试听记录 | 文档明确仍有发音／电音问题；最新冻结声码器／均衡声母候选未通过人工音质和完整量化验收；不能替换正式中文入口 | 实验阶段，未达交付验收 |
| 动效、分页、文件、设置、共享运行时与构建打包工具 | 有对应的原生宿主／旧构建记录；部分后端与历史实板记录相同 | 多项改动与同一 `desktop_main.c`、Makefile、构建／打包脚本耦合；当前完整未提交集合没有统一构建及实板验收；仓库完整性检查存在下述失败 | 集成待验 |

## 本次重新执行的检查

| 检查 | 结果 | 记录 |
| --- | --- | --- |
| `tools/test_agent.py --apps <prepared apps> --output <audit>/agent`（WSL） | 通过，包含 Agent、MCP HTTP、桥接、模式、家居 provider 和聊天格式用例 | [agent-current.log](../../output/uncommitted-audit-20261017/agent-current.log) |
| `tools/test_music_background.py --apps <prepared apps>`（WSL，未使用 live 选项） | 通过，真实解码／工作线程，离线服务与音频夹具 | [music-current.log](../../output/uncommitted-audit-20261017/music-current.log) |
| Node：`homeassistant_frontend.test.cjs`、`portal_frontend.test.cjs`、`camera_upgrade.test.cjs`、`package.test.cjs` | 169 项，167 通过、2 失败、0 跳过；两项失败均在 `package.test.cjs` | [node-current.log](../../output/uncommitted-audit-20261017/node-current.log) |
| `python tools/check_package.py` | 未通过：15 个固件文本文件 SHA-256 不符；诊断为 Windows CRLF，详见下节 | [package-current.log](../../output/uncommitted-audit-20261017/package-current.log) |
| HA JS／生成资源副本／授权摘要比对 | 通过：JS 副本及 C 资源副本相同；JS 无 CRLF；SHA-256 为 `e571fb13353a4aa87cb5980b76d1042d5d4b0b5c68aa0faa35935aa766328450`，与授权头一致 | 本报告及当前源码 |

没有运行完整固件编译、刷机、串口控制、真人语音、真实家居控制、实时音乐接口或长时压力测试。

## 当前失败与证据缺口

1. **桌宠生成脚本字节不一致**：`package.test.cjs:12` 比较内置脚本与 engine + LF + app 拼接，实际差一个 `CR`。这组文件没有出现在未提交改动清单中；HEAD 原始 blob 的拼接检查通过，说明属于当前 Windows 检出字节问题，不是本次桌宠功能修改。未修改文件或删除断言。
2. **身份检查静态断言失败**：`package.test.cjs:31` 禁止整个 `qpk_runtime.c` 出现特定 HA 包名比较；当前 `qpk_runtime_ha_status()` 为只读状态查询增加了该比较。这个失败需要核对测试范围和授权行为后处理，不能仅凭正则命中认定存在权限漏洞，也不能忽略它就宣称安全测试全绿。
3. **15 项固件文本校验失败**：所有失败项去除 CRLF 后均与所在 `SHA256SUMS` 一致；对应 HEAD blob 也均匹配。它们不属于这次未提交代码功能，但当前工作区的打包检查确实未通过。不能通过更新校验值将错误检出字节包装为有效发布物。
4. **旧板载 MCP 启动未确认**：[deployment.json](../../output/board-mcp-20261017/deployment.json) 记录 `board_written=true`、`boot_verified=false`、`AssertionError()`。不能将写入成功算作运行成功，更不能用于证明后续新增的家居／音乐 MCP 已上板可用。
5. **旧实板证据不能直接覆盖新代码**：家居记录中 12 个已改文件哈希相同、4 个不同；音乐记录为 10 个相同、1 个不同；米家瀑布流记录为 22 个相同、16 个不同；语音双核记录为 15 个相同、18 个不同。这里只统计记录中能映射到当前未提交文件的去重路径。已有后续主机测试的模块按新记录判断；没有当前实板证据的仍保留为待验。

## 建议补验及提交顺序

1. 先处理两项 Node 打包／身份检查失败和换行导致的仓库完整性失败，保留原断言意图及资源字节一致性。
2. 按依赖拆分：相机与运行时；唤醒／语音基础；ESP-TTS 与音频回调；MCP 框架、家居与音乐 provider；状态岛／紧凑页面／字体；训练实验与锁定清单。共用的 Makefile、`desktop_main.c`、资源和生成文件需要随对应源码一起检查，不能简单按文件名独立提交。
3. 对当前组合做一次完整固件链接、分区和内存边界检查，再在能正常挂载 `/data` 的固件上回归首页、聊天、米家、相机、音频和 MCP。
4. 单列人工验收：真人唤醒／误唤醒、连续云端对话、真实 HA 设备控制、在线音乐与扬声器、TTS 发音及衔接、长时录像／压力。
5. 电量和充电必须先确认实际硬件与驱动接入。中文蒸馏候选继续作为实验，不与“可发布功能”合并验收。

## 文件分组数量

以下为逐文件导航分组，不是可以直接执行的独立提交边界。共享代码可能同时服务多个功能。

| 分组 | 文件数 |
| --- | ---: |
| 共享运行时构建打包 | 27 |
| ESPClaw MCP 框架与聊天 | 32 |
| 米家快应用与 HA 服务 | 14 |
| 音乐 MCP 与播放 | 10 |
| 相机预览拍照录像 | 20 |
| 状态岛首页导航与动效 | 20 |
| 语音计算会话与本地控制 | 36 |
| ESP-TTS 与音频回调 | 12 |
| 证据与文档 | 5 |
| 自定义唤醒与唤醒交接 | 17 |
| 家居 MCP 与对话卡片 | 4 |
| Nano 英文推理实验 | 6 |
| 中文蒸馏与量化实验 | 21 |
| 合计（不含本报告） | 224 |

主要证据索引：

- [家居 MCP](../../output/home-mcp-review/verification.json)、[音乐 MCP](../../output/music-mcp-review/verification.json)、[状态岛](../../output/status-island-review/verification.json)。
- [米家／相机](../../output/mijia-waterfall/validation.json)、[米家紧凑标题截图目录清单](../../output/mijia-compact-header/screens.json)。
- [自定义唤醒](../../output/speech-wake-v3-20261016/verification.json)、[语音会话](../../output/voice-wake-chat-v2-20261016/verification.json)、[本地语音停止](../../output/local-voice-stop-20261016/verification.json)。
- [历史 TTS 双核](../../output/speech-tts-dual-20261017/verification.json)、[ESP-TTS 板上播报](../app/espdl-quickapp/evidence/2026-10-17-esp-tts-playback/verification.json)、[官方英语 PIE](evidence/official-nano-pie-20261017.json)。
- [中文蒸馏的当前限制](../app/espdl-quickapp/speech/zh-distill/README.md)。

## 提交结果（2026-10-18 追加）

本报告记录的 224 个文件中，180 个已提交为 `031253f`（分支 `codex/integration-20261018`，已推送至 `flash`），本报告本身也随该提交进入仓库。以下条目按“排除语音合成”的要求保留为未提交：ESP-TTS 与音频回调整组（含 `es8311.c`、`tools/test_es8311_return.py`、`tools/tests/test_music_pcm_framing.py`）、中文蒸馏与量化实验整组、Nano 英文推理实验整组、三份 ESP-TTS 证据与官方 Nano PIE 证据，以及 `tools/train_zh_front_frozen_decoder.py`。

语音会话共享代码（`glass_speech.c`、`glass_speech_tts.c/h`、`glass_speech_compute.*`、`glass_speech_conv.h` 以及唤醒与本地语音相关文件）随该提交进入：`glass_speech.c` 在默认构建下无条件调用 `glass_sano_cache_clear()`，单独拆出既有中文 TTS 引擎会使该提交无法链接。ESP-TTS、Q8 蒸馏与 Nano 的调用点都包在 `#ifdef GLASS_ESP_TTS`、`#ifdef GLASS_*_PROBE` 中，这些宏只由 `tools/build_speech_runtime.py` 在对应预备目录存在时定义，因此缺少上述文件时默认固件与语音运行时仍可构建，相关诊断入口返回 `-ENOSYS`。
