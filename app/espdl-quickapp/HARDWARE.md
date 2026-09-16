# 录音机与快应用硬件 API

“全部应用”中新增内置 **录音机**，使用与第三方快应用相同的 `system.audio` 接口。
打开应用只读取本地列表，点击“开始录音”才采集麦克风；“停止并保存”提交新录音，
“放弃本次”取消写入。选择列表中的录音可播放或删除，删除需要再次确认。
返回桌面会取消未保存的录音并释放音频设备。保存的录音在重新打开应用或重启后仍可读取。

录音格式为 16 kHz、单声道 PCM16 WAV，每秒约 32 KB。输入音量柱与录音时间来自实际采集进度。
播放使用板载 ES8311 输出。文件保存在录音机自己的应用数据空间，不自动上传。
播放音量为 40%，快应用 API 可指定 0～100。

## AI 可直接使用的接口

[完整硬件 API 参考](overlay/apps/system/espclaw/guide/HARDWARE_API.md) 已拼入实际编译的 ESPClaw 快应用指导，
包含函数签名、参数、返回结果、异步轮询、停止与取消、错误处理和可编译示例。

| 接口组 | 能力 |
| --- | --- |
| `system.hardware` | 能力查询、任务状态、取消、正常提前结束 |
| `system.gpio` | 输入／输出／开漏、上下拉、读写、释放 |
| `system.i2c` | 打开、组合写读、关闭 |
| `system.spi` | 打开、全双工传输、关闭 |
| `system.uart` | 打开、读写、超时、关闭 |
| `system.pwm` / `system.servo` | PWM 频率／占空比、舵机脉宽、停止 |
| `system.audio` | 录音、播放、提示音、PCM 保存、文件信息、录音列表、删除、停止 |
| `system.ble` | 启动、扫描、状态、连接、取消、断开 |

硬件操作在工作线程执行，音频和其他硬件分别排队。应用退出时请求取消并释放资源，界面线程不等待硬件。
完成结果只消费一次；持续操作必须保留任务 ID 并轮询到 `done`。
`audio.stop(id)` 保存已采集数据，`hardware.cancel(id)` 丢弃这次录音，保留同名旧文件。
`audio.list()` 从真实 WAV 文件建立列表，不依赖额外的 JavaScript 索引；临时文件不出现在列表中。
`audio.info()` 校验文件头、格式和文件实际长度。

## 当前板卡映射

| 资源 | 映射 |
| --- | --- |
| 扩展 GPIO | 0～6 |
| I²C1 | SDA 1、SCL 2 |
| SPI2 | CS 0、SCK 3、MOSI 4、MISO 5 |
| UART1 | TX 4、RX 5，节点 `/dev/ttyS0` |
| PWM／舵机通道 0、1、2、3 | GPIO 0、3、4、6 |
| 音频 | ES8311／I²S，共用 `/dev/audio/pcm_in0` |

共享引脚实行互斥占用，冲突返回 `EBUSY`；显示、触摸、闪存和 C6 等系统引脚不开放重配。
GPIO／总线关闭后释放引脚。音乐播放器与录音机共用音频设备，占用冲突会显示忙碌错误。
BLE 当前提供连接管理，不包含 GATT、配对绑定或经典蓝牙 A2DP；ESP32-C6 不支持 A2DP。

## 实现与验证入口

- 录音机源码：[recorder/app.js](overlay/apps/system/desktop/recorder/app.js)。固件直接嵌入该文件，应用身份为 `com.openvela.recorder`。
- 音频驱动处理全双工录音的共享时钟、对齐的 RX 缓存同步、停止时的 DMA 中止和缓冲区回收。
- I²S 队列在中断、工作线程和停止操作之间同步；内存申请移出队列自旋锁，失败路径归还引用和内存。
- HAL 临界区同时保护本核中断和任务调度；LEDC 只在最后一个计时器关闭后释放共享硬件上下文。
- 主机测试：`diagnostics/test_qpk_hardware.py`、`test_qpk_native_audio.py`、`test_qpk_audio_store.py`、`test_qpk_authoring.py`，以及 `tests/test_recorder.js`。
- 实板诊断：`desktop hardware-test audio` 检查 WAV、录放音、提前停止、取消覆盖与退出释放；`desktop hardware-test` 还检查 GPIO、总线开关、引脚冲突、多路 PWM／舵机及 BLE 扫描取消。诊断仅清理 `hardware-diagnostic` 自己的数据。
- 界面诊断：`desktop ui recorder` 打开录音机；`desktop ui click:<按钮文字>` 在桌面线程触发可见按钮。

构建信息见 [firmware-validation.json](evidence/firmware-validation.json)。
固件更新使用 `diagnostics/flash_espdl.py --execute --firmware-only`，保留模型、已安装应用、配置、对话和已有录音。
外接 I²C／SPI／UART 设备、PWM 实际波形、舵机运动、指定 BLE 外设连接及声音效果需要对应实物验收。
