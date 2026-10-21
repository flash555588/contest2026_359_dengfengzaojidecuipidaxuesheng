# 默认 CSI / USB 多模态摄像头

目标为现有 ESP32-P4 Function-EV-Board/openvela，保留默认 SC2336 CSI；USB 接开发板 Type-A HS Host 口（OTG HS/UTMI）和已有 CherryUSB DWC2 UVC MJPEG 驱动。高速 Type-C 与 Type-A 共用控制器，不能同时传输；USB Serial/JTAG 控制台是另一个端口。接口总表见 [BOARD-INTERFACES.md](BOARD-INTERFACES.md)。只接入摄像头功能；U 盘、键鼠和 USB 音频不在本次范围。

## 选择与拍照

`assistant_camera({operation:"list"})` 返回真实设备清单、USB 的 `device/generation` 与默认标记。默认规则为存在 `/dev/video0` 时选择 CSI，否则只有一台已连接且受支持 USB 时选择它；多个 USB 返回歧义，要求用户选具体设备。`source` 与 `device/generation` 不能同时传。

```javascript
assistant_camera({operation:"list"})
assistant_camera({operation:"capture",source:"default"})
// 仅有一台受支持 USB 摄像头时可以明确选择 USB。
assistant_camera({operation:"capture",source:"usb"})
// 或用刚刚清单中的真实编号与代次。
assistant_camera({operation:"capture",device:0,generation:123})
// 使用上一步实际返回的 request_id，不能照抄此示例编号。
assistant_camera({operation:"photo_status",request_id:7})
```

CSI 仍需要先在原生相机应用启动预览；通过 board 的应用发现与实际控件操作启动，不能猜按钮标签。USB snapshot 可直接采一帧，不依赖分类模型，也不占用 LVGL 线程。确认 done、ok、path 且没有 photo_save_error 后才算已保存。USB 拔插后必须重新 list，旧 generation 不复用；其他页面替换请求时拒绝旧回执。

原生相机已有 CSI/USB 切换、设备分页、实时预览、倍率／色彩、照片／相册和录像。USB 支持范围仍由实际 MJPEG 格式、分辨率、端点和控制器能力决定；不能据工具 schema 声称支持所有 UVC 格式或所有摄像头。设备完整列表和状态以实际驱动为准。

## 本地识别与云端看图

Bot 顶部区分两个通道，默认都关闭：“端侧 · 人脸跟随”仅用 ESP-DL 本机推理；“AI · 采集并上传”由用户点击授权本次采集和上传。进入聊天或助手模式后，AI入口先等待本页端侧任务关闭，独占当前唯一 USB 摄像头，采集约5秒，保留约0、2、4秒的3张原始JPEG及实际时间戳。多个可用USB相机时拒绝歧义；CSI不作为该入口的隐式回退。默认选择不超过640×480的最大可用MJPEG模式，协商帧缓冲不超过1MiB，保留帧每张不超过128KiB。没有本地解码、LCD直接预览、临时视频文件或Flash写入；3张帧只在本轮内存中持有，相机关闭后一次请求已有视觉服务，随后释放图片并保存结果文字。

这是短时画面采样，无音频，不上传AVI／MP4，也不是完整视频理解。时间戳与稀疏采样限制写入视觉请求，模型不应补造帧间动作。取消、拔出、超时或失败清理本次资源，不自动重传；普通聊天重试会拒绝camera_samples来源，必须由用户再次点击采集。上传已经到达服务的内容无法由取消撤回。结果显示在Bot右侧与同模式ESPClaw历史，端侧识别不冒充云端看图。端侧入口在AI工作期间不可开启；语音开始时关闭本页端侧任务，并在实际释放后才发送。

`desktop camera-sample` 只验证同一采集路径与时间戳，输出帧数量／JPEG字节数并释放，不调用AI、不保存图片。宿主回归为 `tools/test_camera_clip.py --output <目录>`、`tools/test_vision.py`、`tools/test_agent.py`。功能实现／构建通过和实体屏幕稳定性、实际服务支持多图仍须分别验收。

原有工具相机流程仍在请求前后保存内部Flash聊天历史。798445cb候选虽通过采集，用户真实Bot请求后仍闪屏且AI响应失败，因此未通过整条链验收。新的Bot相机及语音轮次在成功、失败、取消时都不保存Flash历史；成功观察文字独立保存到`/sdcard/espclaw-observations/camera-<id>-<序号>.json`，含实际时间戳、问题和回答，不保存图像。旧Flash历史保留原位，新轮次在本次会话仍可追问；后续普通历史保存过滤这些新轮次。SD不可用／写入失败时只保留会话并明确提示，不回退Flash，重启后从SD文件查看新观察。打开ESPClaw保留当前模式和进行中的相机任务，显示完整结果。此措施针对已发现的写入干扰，不是底层显示／Flash问题根治证明。

当前设备模型`mimo-v2.6-flash`默认思考；有界视觉观察单独使用`max_completion_tokens:1024`与`thinking:{type:"disabled"}`，Anthropic兼容路径保留`max_tokens`字段。主对话设置不改。`[vision]`仅报告HTTP码、返回长度、结束原因类别、答案／推理字节数；输出额度耗尽单独映射错误，不接受截断答案，也不自动重传。协议依据[MiMo官方接口](https://mimo.mi.com/docs/zh-CN/api/chat/openai-api)。保存回归为`tools/test_camera_observation.py --apps <准备树apps> --output <目录>`。

`recognize` 使用 USB 单帧本地 ESP-DL 分类，需要已部署模型；轮询 `recognition_status`。低分只能描述为可能，不是通用 OCR 或人脸身份识别。

通用看图／文字读取使用独立 `assistant_vision`。用户明确同意把本次图片发给已配置模型后，再调用：

```javascript
assistant_vision({request_id:7,question:"画面中是什么？读取能看清的文字。",upload:true})
```

只接受本次相机封装持有的最新已保存照片，不能传路径、URL、相册文件或 Base64。回执从拍摄起 2 分钟有效；同一回执仅一次分析尝试，成功、超时或失败都不自动重传。需要再次分析时，应由用户明确发起新的拍摄。取消停止当前请求；已经到达模型服务的图片不能通过取消撤回。

沿用设备 AI 设置的 `backend/base_url/model/api_key` 和现有 CA 校验，支持 `openai_compatible` 的 Chat Completions 与 `anthropic_compatible` 的 Messages 图片请求。模型必须实际支持图片；400、401/403、其他 HTTP 错误、输出截断与超时分别返回失败，不伪造观察。URL 与文本模型一样，在 base_url 后追加 chat/completions 或 messages；不另存一份密钥。

JPEG 上限 512 KiB；设备 RGB565／24-bit BMP 检查尺寸、掩码、行布局与文件长度后，按比例缩小到不超过 320×240 的 PNG。BMP 源尺寸最多 1920×1080，文件最多 8 MiB；PNG 使用无压缩 DEFLATE，控制固件依赖与 CPU 占用。小字可能无法辨认，应靠近并重新拍摄。响应最多 16 KiB，观察文字最多 4096 UTF-8 字节，单次 HTTPS 截止 45 秒。不发送历史图片或连续视频，不自动重试上传，拒绝重定向。

请求格式参考 [OpenAI Images and vision](https://developers.openai.com/api/docs/guides/images-vision?api-mode=chat) 和 [Claude Vision](https://platform.claude.com/docs/en/build-with-claude/vision)。图片内的文字／二维码只当作数据，视觉子请求不提供任何工具。

## 固件与部署

使用匹配 v3 硬件的已准备基线，在独立目录构建；传入原工程所需的语音资源参数以保留现有功能。

```bash
python3 tools/build_bilibili_v3.py \
  --baseline /path/to/prepared-v3 \
  --workspace /path/to/isolated-vision-build \
  --toolchain /path/to/gcc14/bin \
  --archive /path/to/openh264-2.6.0.tar.gz \
  --output /path/to/output --usb-camera --factory-header --jobs 6
```

`--usb-camera` 启用 `CONFIG_ESP32P4_CAMERA_USB=y`，验证最终配置、camera_usb_initialize 与 glass_vision_analyze 的链接；默认 CSI 配置从基线继承。CherryUSB 的主机／hub CPU 描述符使用按需分配的用户堆，DMA 缓冲继续保留既有容量、64 字节对齐和缓存处理。主机池保留到主机生命周期结束，插拔复用，不能在 IRQ 使用时释放。界面诊断报告改为请求时分配，输出后释放，以满足启动 BSS 边界。

构建指纹包含所有 overlay 头文件，头文件改变后强制清理重编译，避免 DWC2 ISR 沿用旧结构布局。`--factory-header` 为出厂晶振保留 GPIO0/1，扩展引脚从最终配置生成。

部署前核对实际芯片 revision、16 MiB Flash、当前分区、Simple Boot 及输出 BUILD-METADATA.json。本次候选沿用 0x2000 应用入口、0xc00000 起的 4 MiB 数据区；仅在实板与该布局匹配时使用，不能照搬到其他板。不要用固定 0x400000 数据边界的旧 NSH 烧录脚本烧这个应用镜像。

SD 卡若有旧 `/sdcard/espclaw/agents/<mode>/agent.md`，更新对应提示词或移走覆写以使用固件默认。共享 Harness 无论 SD profile 是否存在均强制附加。本地分类需要 manifest 对应的三个模型与许可证；`tools/deploy_vision_models.py --source app/espdl-quickapp/models --board <设备IPv4> --port COM4 --output <日志目录>` 验证编译表／源文件摘要，通过仅面向该开发板的局域网临时服务写入 SD 暂存目录，再发布到原本缺失的 `/sdcard/espdl`。已有同名目录会保留，禁止隐式覆盖。板端加载时再次校验 SHA-256，并执行模型夹具自检。NAME_MAX 与 FAT_MAXFNAME 必须至少支持模型文件名；构建固定为 64，原 32 字符配置无法安装最长的分类模型名。云端图片分析不依赖分类模型，但需要网络、有效时间与视觉模型配置。

验收按真实结果逐项记录：烧录摘要／冷启动；CSI 预览和拍照；USB 插入、拔出、再次插入及设备代次；支持／不支持的摄像头反馈；USB 预览、拍照、模型分类；明确授权的云端看图；断网／取消／重复请求；原有显示、触摸、语音和音乐恢复。构建和宿主回归通过不能替代这些实板项目。

CSI USERPTR 页在 DQBUF 后仍需进行 P4 HAL 的 M2C 缓存同步，再由 CPU 缩放并绘制控件；此配置未启用 `CONFIG_ARCH_DCACHE`，不能依赖通用 `up_invalidate_dcache()`。同步按16 KiB分段，要求整页缓存行对齐；显示前继续执行有界 C2M 写回。`FBIOPAN_DISPLAY` 的 upper half 可能忽略底层错误，因此 CSI 和 UVC 共用 `qpk_camera_present()`，查询真实 x/y offset，确认交换完成才允许回收旧页。查询／交换失败立即停止 CSI 流，不能将疑似正在显示的页重新交给 DMA。回归命令为 `python3 tools/test_camera_sync.py --output /tmp/camera-sync`，覆盖64／128字节缓存行、完整帧同步、中途错误和翻页未生效；宿主通过后仍需观察实体屏幕，单张帧缓冲截图不能证明没有瞬时闪屏。

CSI、UVC 原始 JPEG 和助手快照统一通过 `qpk_photo_storage.h` 选择存储：已挂载的 SD 卡优先写 `/sdcard/photos`；没有 SD 卡时仅在内部 SmartFS 空间足够时写 `/data/photos`。按预计文件长度预留64 KiB余量，空间不足在写图前返回 ENOSPC。SD 满盘或 I/O 故障不能静默回退到 Flash；目录和挂载点不能是符号链接。原有照片保持不动，相机相册入口优先打开 SD 照片目录，内部照片仍可从本机入口访问。CSI 诊断输出保存目录和耗时，排查内部 Flash 回收与显示并发时要分别测试纯预览、保存、回桌面；照片保存成功不能替代实体屏幕稳定性验收。回归命令为 `python3 tools/test_photo_storage.py --output /tmp/photo-storage`，助手拍照适配另运行 `tools/test_assistant_platform.py`。

照片编码与文件操作由 `qpk_photo_store.c` 的常驻保存线程执行。PREVIEW 与 ASSISTANT 各保留一个槽，每槽最多2 MiB输入副本；同一所有者尚未完成时返回 EBUSY，不积累无界队列。提交返回前完成复制，之后帧缓冲／JPEG 输入可立即复用；只有副本分配与复制发生在采集侧。槽内保留最终结果直到该所有者提交新任务，使用独立 ticket 拒绝旧状态查询。取消在写入、fsync 后和发布完成前核对，错误／取消清理本次不完整文件。`done` 只有关闭和清理完成后才为 true。助手提交后缓存必要推理元数据，不要求全局推理结果一直保留。桌面启动时初始化 worker，保持 NuttX 任务组内的文件描述符生命周期。

`[photo-io]` 输出复制和写盘微秒耗时；`[display-time]` 输出实际 DSI 帧完成中断最大间隔、翻页最大等待与错误；`[flash-time]` 输出 Flash API 读／写／擦除次数、累计／最大耗时及当前调用数。Flash 总计针对直接 MTD/HAL 调用，不代表全部缓存取指或总线占用。大于20 ms的单次操作至多每秒打印一次；IRQ 中禁止打印。`desktop cameras` 可读取当前统计，CSI 每60帧也输出一次。构建通过 `tools/instrument_p4_timing.py` 对已准备好的 DSI／Flash 源码做窄范围、可重复插桩，遇到未知源码结构立即失败，摘要记入 BUILD-METADATA.json。回归为 `tools/test_photo_store.py`、`tools/test_camera_timing.py`；长时闪屏仍需实体观察。

Bot 调用摄像头还会在请求前后保存聊天历史，路径仍为内部 Flash，因此照片写 SD 不等于该流程没有 Flash 操作。DSI 每帧需要 DW-GDMA 完成中断重新启动。缓存关闭期间，原来的非 IRAM 显示 IRQ 会延后；实测 Bot 期间曾记录约63 ms的帧完成中断间隔，这是一条已发现的干扰路径，不能据此断言实体蓝屏持续时间的全部原因。

此前尝试过 IRAM IRQ 保护原型，叶子函数宿主测试通过，但实板在通用 IRQ 返回路径发生取指异常并持续黑屏，候选已拒绝并回退。`tools/protect_p4_display_flash.py` 保留原型供复现分析；正常构建只调用其 `restore()`，撤销复用工作区中的残留补丁，并拒绝链接原型保护符号。`flash_guard_frames` 在正常构建应为0；不能把原型测试或内部地址当作整条中断链安全证明。

Flash 自动挂起候选也已拒绝：实读 JEDEC 为 GigaDevice `C84018`、GD 能力、挂起初始化、最终链接和运行时配置均通过，但实板在 Bot 回复／历史写入附近仍发生 PSRAM store bus fault，已回退。能力标志和成功初始化不能证明当前 SDK／Flash 组合可靠。`ESP32P4_FLASH_AUTO_SUSPEND` 强制关闭，builder 明确拒绝旧 `--flash-auto-suspend-jedec` 参数；准备脚本及宿主测试保留为实验复现材料。正常构建继续采用原缓存保护与显示 IRQ，不迁移或清除聊天、照片、配置。P4 的异步 bus fault 可能在无关 MEPC 处报告，首故障额外保存 MEXSTATUS 与 STPC／LDPC 队列供定位。

相机启动从 LVGL 回调排队，整次 handler 返回后，由桌面轮询执行一次 GUI 刷新再启动硬件。停止采集并 join 后，先将最后稳定画面停靠到第三个显示页，查询确认实际 scanout，再发布 GUI 重绘；查询、刷新或翻页失败则保留相机所有权并重试清理。GUI 只绘制前两页，避免退出时改写正在显示的相机页。`tools/test_camera_boundary.py` 与 `tools/test_camera_sync.py` 覆盖同次 handler 内延后启动、取消、异步失败、重绘门禁和三个停止页的交接；录像仍要求真实活跃采集状态，不能用待重绘状态判断。

USB 预览每约5秒输出 `[usb-preview]`：实际显示 fps、累计显示／接收／丢弃帧与传输错误，以及等待、读帧、JPEG 解码与缩放、绘制、缓存刷新、翻页的平均微秒耗时。fps 是实际已翻页的图像帧率；界面模式中的 fps 是摄像头协商值，不能混用。MJPEG 解码器复用自身的 RGB565 暂存和列映射，在停止／销毁时释放；缩放保留原最近邻像素与黑边，只清理黑边、复制重复源行，避免逐像素除法和每帧分配。共享 JPEG 回归：`tools/test_weather_decode.py --jpeg <320x240标准JPEG> --output <目录>`，覆盖多种目标宽高、逐像素参考、控件区边界、重复解码、缺 DHT 和截断输入。USB 优化后仍要单独验证实际显示流畅度与实体屏幕无闪烁。

USB 的1倍、原色、拍照模式在无需提交照片源帧时，使用 `qpk_mjpeg_decode_view()` 直接输出最终取景框，合并原有两次最近邻采样的整数坐标映射，省去大画布和随后的搬运。关闭预览分辨率限制时，最终像素与原两阶段路径相同。默认按“流畅优先”设置 `qpk_mjpeg_preview_limits(decoder,320,240)`：仅直接预览降低 JPEG 输出分辨率，跳过不显示像素的 RGB 转换，使用最近邻采样；原始 MJPEG 传输分辨率和 JPEG 照片不变。`qpk_mjpeg_decode_region/still` 不受这个限制影响。缩放、色彩、录像以及照片源帧仍经过原有完整画布路径。快门若在直接预览期间到达，下一帧提交；副本已提交后恢复直接预览，不等待后台写盘。`tools/test_mjpeg_preview.py --jpeg <夹具列表> --output <目录>` 验证降采样结果、MCU 裁剪、照片隔离和截断输入；共享解码器回归继续覆盖关闭限制时的精确坐标映射与矩形边界。

Bot、全局唤醒和语音面板发起的新对话使用相同会话策略：请求前、失败、取消及完成均不写Flash历史，成功文字独立保存SD；相机观察为camera-<id>-<序号>.json，语音对话为dialog-<id>-<序号>.json。长答案分页文件为sd-reply-<id>-<序号>.txt，只从实际挂载SD读取，不回退Flash。旧历史不迁移，文字工作台保留原有持久化策略。此措施仍须真实AI全流程和实体屏幕验收。

## 2026-10-20 Bot 闪屏对照验收

本轮使用独立源码快照和相同构建配置，保留旧数据。它不是恢复得到的视频接入前固件，不能据此指定某次历史提交为根因。

| 固件 | 唯一实验变化 | 结果 |
| --- | --- | --- |
| A | 跳过 CSI 和 USB 主机初始化，保留 Bot Flash 历史 | 仍闪屏；普通 AI 返回成功，显示 IRQ 最大间隔 66.107 ms，产生 155 次写入、15 次擦除 |
| A1 | 相对 A，仅两个 Bot 入口改用会话保存策略 | 5 轮普通对话成功并保存 SD，1 次取消；用户确认无问题，Flash 写/擦为 0，IRQ 最大间隔 16.736 ms |
| B1 | 相对 A1，仅恢复 CSI 和 USB 初始化 | 用户确认普通对话和相机多模态全程无闪屏；Flash 写/擦为 0，补充采集后的 IRQ 最大间隔 16.692 ms |

B1 已烧录并校验，SHA256 为 `8d3413ebe4b31ab2a27679999afc369353cac110ddd97761084fb8dff9748792`。实际相机请求采集 3 帧耗时 5.3 秒，关闭相机后上传，约 14.55 秒返回 HTTP 200 和可解析答案；SD 回执 `camera-24-0.json` 已核对存在。随后独立采集测试以约 5.02 秒取得 3 帧、正常关闭，没有再次上传。实际发送内容是带时间偏移的 JPEG 多帧样本，尚非视频容器。

此对照支持“Bot 自动 Flash 历史保存触发当前显示链路停顿”，并验证了该应用路径的修正。尚未证明任意 Flash 工作负载均无闪屏，也未确定最早暴露问题的历史视频改动。本轮做了应用烧录后的重启，未做断电冷启动或重新验收所有预览/Flash 压力组合。原有缓存保护保留，三个已拒绝的底层试验仍关闭。完整日志与实验边界见工作区 `output/camera-ab-20261020/VERIFICATION.md`。
