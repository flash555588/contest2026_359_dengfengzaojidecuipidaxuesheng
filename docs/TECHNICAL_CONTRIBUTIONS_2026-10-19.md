# ESP32-P4 项目技术贡献与验证证据

本项目可确认的技术贡献集中在 ESP32-P4 上的 NuttX 执行上下文适配、Flash 缓存故障修复、PPA 图像转换、媒体调度优化，以及 QuickJS 与原生服务的资源和生命周期管理。现有源码、实板记录和对照数据支持将技术难度模拟评分从 **25/30 调整为 28/30**。

评估日期为 2026-10-19，源码基线为 `79783795d4d3f576deec75b642e09a649f2a6d13`，另包含索引中明确标记的工作区文件。首轮整理核对既有记录；随后补证新增了冻结源码构建、上游差异重放和宿主回归，详见下节。新镜像尚未烧录，历史实板结果仍归属各自镜像。该评分不追溯认定初赛截止版本，也不替代组委会评分。

逐项来源、文件 SHA-256、HEAD 关系和证据匹配结果见[机器可读索引](evidence/technical-contributions-20261019.json)，[贡献清单 CSV](evidence/technical-contributions-20261019.csv)可直接用于筛选。原有索引覆盖 10 项贡献、32 个源码文件、34 份证据文件。适合公开评审的指标、逐行摘录、上游对照和新构建收据已归档到[本轮证据包](evidence/technical-20261019/manifest.json)。`../../output/` 仍为本机完整记录入口，不随参赛仓自动提交。

## 本轮补证结果

当前工作区已冻结为 2647 个文件的源码快照，归档 SHA-256 为 `df7283ebd4d975a42d10a710b653c7bfde00a0617dd234cf727881b0f32c45b7`，逐文件清单摘要为 `2e6216f402b19a16c66fc4667f795dcf7d8db3f720b63082ee43a6f7a1541a10`。归档包含未提交代码，不把它等同于 HEAD 的 Git 归档；本机文件位于 `output/technical-completion-20261019/snapshot-final/`。[公开源码清单](evidence/technical-20261019/source-manifest.json)保留逐文件摘要。

该快照沿用 ESP32-P4 v3、ILP32F、程序写入上限 `0xc00000` 和 4MiB 数据区配置，在独立构建目录重新编译链接。最终固件为 **10,296,996 字节**，SHA-256 为 `ae92d3c2115d5b6bc0078f2489e3357b5b8dc056505ea7379076307e76a29b6e`；`_ebss=0x4ffae92c`，低于既有边界 `0x4ffaefc0`。构建复用固定摘要的 ESP-DL、TLS 等静态库，属于准备树上的快照构建，不等同于空机全依赖源码构建。[构建收据](evidence/technical-20261019/current-build.json)绑定最终 BIN、ELF、配置、库和源码快照；镜像没有烧录。

| 新增验证 | 结果与边界 |
| --- | --- |
| JavaScript 六组回归 | 179 项通过，0 失败、0 跳过 |
| 工具 unittest 和导入时运行的 C harness | 26 项 unittest 通过；网络生命周期、SDIO、PCM 等 C 用例另按输出记录，不混入 unittest 数量 |
| Agent 与 MCP | 实际服务代码通过模拟 HTTP、权限拒绝、已确认/未确认控制和取消检查；不操作真实家居 |
| 长流与请求生命周期 | SSE、超限正文、100 次异常流、100 次不结束请求、100 次超大错误响应及恢复通过 |
| 真实 QuickJS 安全回归 | 身份伪造拒绝、异常对象钩子、参数转换中删除/重用对象等原有用例通过；LVGL 显示与动效为显式桩 |
| 编译缓存 | 当前源码通过真实 QuickJS 与 ASan/UBSan；本次宿主时间不替代原实板启动数据 |
| 相机与语音资源交接 | 三次录制/重开、音视频完整解码、授权撤销、麦克风失败、缺卡及取消竞态宿主检查通过；不代表新镜像实板验收 |

测试收据见[当前宿主验证](evidence/technical-20261019/current-host-tests.json)。本轮修复了临时测试树缺少相对头文件、CRLF 检出导致的摘要不一致、权限静态断言范围过宽，以及过时的安全测试显示桩。没有放宽生产授权；安全测试首次两次未编译通过的记录保留摘要于[失败尝试索引](evidence/technical-20261019/retained-attempts.json)。最终快照包含这些夹具修复。

声明的 NuttX `2f1387d…` 和 apps `88827af…` 已通过匿名 GitHub API 验证可访问。上下文、启动、显示和 PPA 的 8 个关键文件已生成分项差异，在临时树重放后全部与快照对应文件按 LF 比较一致，见[来源对照](evidence/technical-20261019/upstream/upstream-comparison.json)和[重放结果](evidence/technical-20261019/upstream/patch-replay.json)。这些声明基线本身均为无父提交的导入快照；另一个官方公共参考 `dd92bcf…` 已用公开 tree 摘要核对，但它不是已确认的导入祖先。早期 `cd61ccdd…` 的匿名访问仍返回 404，不能据此判断它不存在或属于私有仓。

克隆仓库后可离线检查证据包字节完整性：

```bash
python3 tools/package_technical_evidence.py --output docs/evidence/technical-20261019 --verify
```

SHA-256 证明字节一致性，不单独证明测量真实性或独立创作。公开包只包含选定指标、日志行、上游差异和构建/测试摘要，未归档原始会话、用户配置、全盘 Flash 备份或音视频内容。

## 贡献归属口径

本报告中的“团队技术增量”指项目新增的代码、适配机制和经过记录的工程解决方案，不等同于全球首创、专利新颖性或独立著作权鉴定。Git 提交、SPDX 声明和测试通过各自不能单独证明独立创作。

Apache NuttX、Espressif HAL、LVGL、QuickJS、ESP-DL 及其模型、OpenH264、Helix、Home Assistant、sanoTTS 和桌宠上游的能力保留原归属。团队应主张自己实现或适配的连接层、运行时、驱动接口、调度、内存和故障处理增量。具体来源见 [THIRD_PARTY.md](../THIRD_PARTY.md)。

`48da6d5` 可用于比较参赛仓内后来增加的文件，不能作为未修改的公共上游原创性基线。该提交已包含大量导入材料。关键路径现已补齐与声明基线的差异；导入快照对应的原生公共上游祖先仍需进一步确认。他人的 SoC PR #340、板级 PR #347 不属于本团队贡献，见 [UPSTREAM_PLAN.md](../UPSTREAM_PLAN.md)。

仓内新增记录可以进一步确认增量的交付位置：PPA 驱动和 `bili_ppa.c` 首次加入当前路径的提交为 `f351b0c`；编译缓存和原生视口为 `6732275`；家居 MCP provider、自定义唤醒和 AVI 写入器为 `031253f`。这些提交证明增量进入本仓的记录，不单独证明其首次创作日期或不存在其他来源。与此不同，Nano 点积汇编和诊断仍是未提交文件。

## 技术贡献总表

| 编号 | 可主张的技术增量 | 已有验证 | 当前证据边界 |
| --- | --- | --- | --- |
| T01 | 针对具体芯片 revision 的 Simple Boot、PSRAM、DSI 和板级显示适配 | 历史固定输入构建、v1 启动及 v3 应用运行记录 | 不能把整个 ESP32-P4 芯片目录或厂商寄存器表声明为独立原创；不同 revision 的证据分别有效 |
| T02 | PIE、FPU、硬件循环的 NuttX 抢占上下文和 ESP-DL 运行适配 | 同核上下文自检、固定图片重复推理、模型摘要和选定预处理数值对照 | ESP-DL 算子及模型属于上游；没有证明任意任务迁移、任意中断组合和真实人脸准确率 |
| T03 | PSRAM 栈调用 Flash 时的缓存恢复故障定位、IRAM 链接和内部栈路径 | 12 轮存储自检、16 轮聊天并发存储记录；操作计数达到 27,102 | 证明已捕获故障路径得到修复，不代表穷尽所有异常或长期负载 |
| T04 | PPA DMA2D 的 NuttX ioctl、同步完成、超时复位、转换与软件回退 | 同固件四轮软件/PPA交替测试，渲染阶段平均耗时减少 23.68% | 阶段耗时降低不等于显示帧率提高；当时为 v3.x flat、独占 DMA2D 原型 |
| T05 | OpenH264 移植中的热点布局、媒体线程亲和性和所有权恢复 | 480p 两轮对照中解码阶段平均耗时减少 8.83%；解码哈希一致 | 使用上游编解码算法；小样本、限定码流，完整 480p 稳定 10fps 未获证明 |
| T06 | 来源绑定的进程内 QuickJS 编译缓存、原生视口及有界卡片复用 | 512 设备宿主夹具、12 卡片槽、325 控件；实板冷编译 890ms，四次热编译均 30ms | 512 设备是宿主夹具；编译计时不是总启动或触摸到显示延迟 |
| T07 | 共享异步 HA 服务、权限与类型约束、设备回读、MCP 家居和音乐接入 | 宿主工具测试；实板读取 200 实体、4 设备分组；真实 AI 查询两个只读状态工具 | 没有真实家居动作和扬声器播放的完整验收，不能将只读调用扩大为控制闭环 |
| T08 | 官方英语 Nano 模型的精确 INT8 PIE 点积和板端运行诊断 | 7 轮板载记录，标量/PIE中位数 2.18s/0.72s，约 3.03 倍，PCM 哈希一致 | 属于本地诊断，含未提交源码；不是原创模型、中文模型或已完成听感验收的产品 |
| T09 | 指定唤醒词的流式 MFCC/DTW、端点和资源交接实现 | 合成场景、分块一致性、离线正负样本及麦克风重开 | 未完成真人唤醒率、每小时误唤醒和唤醒到云端连续对话验收 |
| T10 | 相机预览和带 PCM 音轨 AVI 的异步录制、退出保存及重开管理 | 实板两段约 6 秒录制，62/61 帧，320×180、10fps、16kHz 单声道；有文件头记录 | 短片不能证明长时同步、掉电恢复、所有播放器兼容性或存储上限 |

## 可直接用于技术答辩的证据

### 抢占式神经网络运行适配

[`riscv_extctx.S`](../app/espdl-quickapp/overlay/nuttx/arch/risc-v/src/esp32p4/riscv_extctx.S)保存和恢复 Q0–Q7、QACC、UA、XACC、SAR 等 PIE 状态及硬件循环状态；[`riscv_exception_common.S`](../app/espdl-quickapp/overlay/nuttx/arch/risc-v/src/common/riscv_exception_common.S)接入异常路径。[`context_verify.S`](../app/espdl-quickapp/overlay/apps/system/desktop/espdl_port/context_verify.S)使寄存器保持活跃，覆盖厂商叶函数的不同栈对齐位置，再通过调度让出和回读验证上下文。

[`face-preprocess-10.json`](../app/espdl-quickapp/evidence/face-preprocess-10.json)记录 `P4 SIMD + same-CPU context test result=0`，两次固定 320×240 人脸推理结果稳定，所选 C 预处理参考检查通过。此前 [`face-simd-9.json`](../app/espdl-quickapp/evidence/face-simd-9.json)明确失败，不能算作通过：SDK 图像 SIMD helper 仍有 34,033/57,600 项差异。因此实际贡献是保留加速算子，选择经验证的 C 图像预处理并修复运行环境。

上述上下文实现、测试汇编和推理 backend 在当前源码与 PPA 构建元数据中可匹配。匹配证明文件内容关联，不能单独把早期测试升级为当前整机验收。这里适合主张“实现并验证 NuttX 上的抢占式 ESP-DL 运行适配”，不应主张原创人脸识别模型或所有 SIMD 图像处理均正确。

### Flash 缓存恢复故障修复

[`CHAT-STABILITY.md`](../app/espdl-quickapp/CHAT-STABILITY.md)记录了两个核心互相等待及外部缓存未恢复时取指的故障链。具体增量是把 `esp_cache_utils` 的代码与常量加入[内部 RAM 链接段](../app/espdl-quickapp/overlay/nuttx/boards/risc-v/esp32p4/common/scripts/esp32p4_sections.rev3.ld)，并由 [`esp_spiflash.c`](../app/espdl-quickapp/overlay/nuttx/arch/risc-v/src/common/espressif/esp_spiflash.c)提供内部栈工作路径。

对应修复镜像为 `4b90e80765c630db939d9d80031fa2a083f67d773851838f8718d299fbe21de4`。[12 轮存储记录](../app/espdl-quickapp/evidence/chat-cache-iram-stress-1.json)与[16 轮聊天并发存储记录](../app/espdl-quickapp/evidence/chat-cache-iram-live-stress-1.json)的逐轮存储状态均通过；[后续诊断](../app/espdl-quickapp/evidence/chat-cache-iram-scroll-1.json)的 `flash-stack ops=27102` 是累计操作计数，不能改称 27,102 次冷启动或独立完整压力测试。

可使用的表述为：“针对 PSRAM 栈下 Flash 操作引发的失效取指，定位缓存恢复函数的链接位置问题，加入 IRAM 与内部栈保护，并保留对应回归记录。”

### PPA 图像转换的同固件对照

新增实现位于 [`esp32p4_ppa.c`](../app/espdl-quickapp/overlay/nuttx/arch/risc-v/src/esp32p4/esp32p4_ppa.c)、[`p4_ppa.h`](../app/espdl-quickapp/overlay/nuttx/include/nuttx/video/p4_ppa.h) 和 [`bili_ppa.c`](../app/bilibili_live/bili_ppa.c)。包含 128 字节对齐、cache 维护、IRQ/信号量同步、250ms 超时后复位，以及缓冲区释放前终止 DMA 的处理。当前这三个文件均能与当时构建元数据匹配。

镜像 `8dfe65ddef454c28383edcee33090570e1c5494628592efa962a7206fdea84eb` 上，848×480、8 秒、10fps 的 Baseline H.264/AAC 素材按软件/PPA/PPA/软件顺序播放。[原始串口记录](../../output/media-v3-ppa-irq/paired848.log)显示前 60 个样本的渲染耗时分别为 **1860、1320、1420、1730ms**。软件平均 1795ms，PPA 平均 1370ms，减少 **23.68%**；读取、解码、渲染、等待的计时项合计减少 **5.02%**，不是完整短片墙钟耗时减少 5.02%。

两种模式平均均提交 31.5 帧，因而没有证明显示 FPS 提升。PPA 插值与原软件最近邻的边界像素也不应声称逐位相等。[汇总及限制](../../output/media-v3-ppa-irq/RESULTS.txt)保留了这些条件。

### 媒体调度和解码验证

[`bili_cpu.c`](../app/bilibili_live/bili_cpu.c)管理播放期间的视频 CPU1、音频和 UI CPU0 亲和性，退出恢复 UI 双核范围；[`install_bilibili_live.py`](../tools/install_bilibili_live.py)保留上游版本与本地热点布局补丁。[CPU 状态记录](../../output/media-v3-decode-affinity-scoped/cpu-status.log)验证 UI 掩码 `3 → 1 → 3`。

限定 848×480 素材的前 60 样本，基线两轮解码为 5840/5940ms，优化后为 5350/5390ms，平均 **5890 → 5370ms，减少 8.83%**。独立 80 帧纯解码滚动 I420 哈希保持 `a5eefb09`。[对照数据](../../output/media-v3-decode-affinity-scoped/COMPARISON.json)与[原始播放记录](../../output/media-v3-decode-affinity-scoped/play848.log)保留计数。

不能把解码阶段速率写成完整播放器 FPS。该历史记录中的 `bili_cpu.c` 与当前文件未建立相同摘要，指标继续归属原镜像 `629cc8dc7430f25e220f63d824e9ac11dbc1e8639762b5ced6f35a2071522223`。PPA 和亲和性百分比也不能相加为总收益。

### QuickJS 与有界界面运行时

[`qpk_compile_cache.h`](../app/espdl-quickapp/overlay/apps/system/desktop/qpk_compile_cache.h)仅缓存同引擎、来源和摘要受限的内置脚本字节码；每次启动创建新 context，不保留旧授权和业务状态，容量上限为 512KiB。当前缓存实现与已有验证记录的摘要匹配。

[脚本优化后的实板日志](../../output/mijia-waterfall/board-verification/script-optimized.log)及[汇总](../../output/mijia-waterfall/validation.json)记录一次冷编译 **890ms**，四次热编译均 **30ms**；相应执行阶段为 120ms 和 110ms。可以主张降低了特定内置脚本的重复编译耗时，不能把约 29.7 倍的编译阶段比值写成整机启动提速。

[`qpk_viewport.inc`](../app/espdl-quickapp/overlay/apps/system/desktop/qpk_viewport.inc)及 HA 页面以视口重绑卡片，配合世代检查、删除回调取消和原生滚动。宿主 512 设备夹具使用 12 个卡片槽、325 个控件，覆盖滑条与纵向滚动冲突和返回位置恢复。其原生渲染记录与实板导航记录分别证明界面逻辑和具体板上行为，不能相互替代。

### 官方模型上的 PIE 内核诊断

[`glass_nano_pie.S`](../app/espdl-quickapp/overlay/apps/system/desktop/glass_nano_pie.S)提供对齐 INT8 点积，使用 `esp.vmulas.s8.xacc`；[`glass_nano_probe.c`](../app/espdl-quickapp/overlay/apps/system/desktop/glass_nano_probe.c)检查调用长度、对齐、标量回退、内核对照和 PCM 摘要。这两个文件与诊断准备记录的摘要匹配，但尚未进入当前 HEAD。

[官方英语 Nano 诊断](evidence/official-nano-pie-20261017.json)及[7 轮记录](../../output/official-tts-q8-20261017/verification.json)记录标量中位数 **2.18s**、PIE 中位数 **0.72s**，约 **3.03 倍**；每轮内核检查 288 个用例，输出 PCM 哈希一致。设备计时粒度为 10ms，推理计时不含分配、内核自检和播放。权重来自固定的 sanoTTS 官方模型，并未修改。

这可证明特定官方英语模型上存在经过数值对照的底层加速增量。不能证明中文模型、真人听感、通用语音请求的相同提速或当前发行镜像默认启用了该路径。

## 服务和交互贡献的验证边界

家居贡献位于 [`glass_board_home.c`](../app/espdl-quickapp/overlay/apps/system/desktop/glass_board_home.c) 与[共享 HA 服务](../app/espdl-quickapp/overlay/apps/system/hass/hass_service.c)：类型约束、AI 控制开关、实体白名单和回读复用原生服务，工具结果不依赖跳转页面。宿主记录包含确认、未确认和拒绝路径；[10 月 18 日实板记录](../../output/flash-20261018/deployment.json)确认真实 HA 读取和 AI 只读工具调用。`home_status_ok`、`music_status_ok` 证明两个只读查询成功，不证明真实开灯、调光或扬声器播放。

唤醒贡献位于 [`glass_custom_wake.c`](../app/espdl-quickapp/overlay/apps/system/desktop/glass_custom_wake.c) 与 [`glass_wake_features.c`](../app/espdl-quickapp/overlay/apps/system/desktop/glass_wake_features.c)，采用 MFCC/DTW 模板，并非重新训练了 WakeNet。模型与运行时文件可与[v3 唤醒验证](../../output/speech-wake-v3-20261016/verification.json)匹配。该记录明确 `synthetic_only=true`、`human_wake_tested=false`；[连续对话记录](../../output/voice-wake-chat-v2-20261016/verification.json)明确完整唤醒到云端闭环未验证。

相机贡献位于 [`qpk_camera_video.inc`](../app/espdl-quickapp/overlay/apps/system/desktop/qpk_camera_video.inc) 和 [`qpk_camera_avi.h`](../app/espdl-quickapp/overlay/apps/system/desktop/qpk_camera_avi.h)。[历史记录](../../output/mijia-waterfall/validation.json)包含两段 6229/6105ms、62/61 帧的带音轨 AVI 及退出保存和重开；当前录像与共享运行时代码已与该证据存在摘要差异，原验证继续归属当时快照。

## 尚缺的证据与补证方式

| 优先级 | 缺口 | 需要补出的具体材料 | 对判断的影响 |
| --- | --- | --- | --- |
| P0 部分完成 | 导入底层代码的增量归属 | 已完成匿名访问验证、8 个关键文件分项 diff 和补丁重放；仍需导入快照对应的 Apache/openvela 祖先提交或可核查导入清单 | 能复核相对声明基线的增量；无父历史的快照不能证明其上游祖先关系 |
| P0 构建完成 | 最新整机版本与历史测试未完全绑定 | 已有不可变源码归档、配置/库/BIN/ELF 摘要和宿主测试；仍需最终镜像的真实板启动与功能验收 | 构建和宿主验证已统一，旧实板性能不能自动归属新镜像 |
| P1 | 自定义执行与硬件边界保证覆盖不足 | PIE/FPU/循环状态在不同栈对齐、异常和调度条件下的检查；DMA 超时与错误注入后所有权、缓存及重开检查；逐项记录配置和结果 | 决定是否能把成功路径实验扩大为完整底层能力保证 |
| P1 | 性能对照的代表性 | 同固件固定素材 A/B、增加重复次数，保留均值/中位数/波动、CPU/堆/阶段计时和像素结果；明确样本帧率与显示提交数 | 现有小样本收益可引用，不支持所有码流、分辨率和负载的普遍收益 |
| P1 | 真人语音与真实设备控制 | 指定距离和噪声下真人唤醒率、每小时误唤醒；真实 LLM→工具→设备动作→状态读回记录；连续语音取消及重开 | 当前只能主张合成/离线唤醒与只读服务闭环 |
| P1 | 最终语音产品路径 | 说明 Nano 诊断是否纳入发行配置；中文模型、真人听音、资源争用和取消的独立结果 | 英语 PIE 诊断加速不能直接作为中文 TTS 产品效果 |
| P1 | 持续媒体和录制能力 | 真实 SD 长录、音画漂移、空间不足和掉电恢复；固定直播检查段记录音频丢弃和停止回收 | 两段短录与短直播不能证明长时稳定或持续低延迟 |
| P2 已完成归档 | 公开评审可用证据 | 指标、逐行摘录、上游 diff、当前构建/测试摘要与离线校验工具已进入仓库；完整私有原始记录仍留本机 | 评委可检查归档内容和摘要；不把摘录等同于全部原始记录 |

可先完成已有固定素材和来源对照，不必为技术评分继续增加功能。新验证应新增记录，不覆盖原失败记录，也不把旧摘要改成当前源码的摘要。

## 技术难度复评分

[官方评分规则](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md#评分规则)仅给技术难度 30 分及其范围，没有公布以下子项权重。下表是本次模拟评审的解释口径，不是官方评分细则。

| 评审子项 | 暂评 | 依据 |
| --- | --- | --- |
| 芯片板级和执行上下文适配 | 11/12 | 有实际板级启动、上下文保存与固定推理验证；部分导入底层增量尚缺公共上游对照 |
| 系统架构与资源生命周期机制 | 9/10 | 有 IRAM/内部栈修复、异步共享服务、来源绑定缓存与有界视口；跨配置和异常并发保证尚未完整验证 |
| 硬件性能优化与数值验证 | 8/8 | PPA 同固件 A/B、媒体调度对照、解码哈希及官方模型 PIE 诊断已构成多组可核对证据 |
| 合计 | **28/30** | 相较前次 25 分增加 3 分 |

上调源于重新核实了之前低估的量化和底层验证证据；没有把新写的文档当成新硬件成果。剩余 2 分保留在贡献归属和自定义系统机制能力范围的确认上。未提交状态、日志 schema、Skill、演示视频、许可证整理和商业材料不在本次技术项重复扣分。

其他项目沿用此前暂评分时，已评分合计由 59/90 变为 **62/90**，展示效果 10 分仍待评。此处只是技术项调整后的算术结果，不代表重新审计并确认了其他项目或正式参赛资格。
