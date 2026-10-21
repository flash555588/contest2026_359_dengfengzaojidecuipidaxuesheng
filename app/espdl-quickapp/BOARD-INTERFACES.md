# 官方开发板接口与固件协议

实板识别为 ESP32-P4 revision 3.2，使用 [ESP32-P4X-Function-EV-Board 官方说明书](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4x-function-ev-board/user_guide.html)。这是现有 openvela 固件的接口映射；说明书列有的物理接口不代表已经实现所有设备类别和应用协议。

`desktop interfaces`、快应用 `system.hardware.capabilities()`、Agent 的 `hardware_capabilities` 返回同一份 version 3 能力（21 项）。`boardInterfaces.interfaces` 中：`documented` 表示板上有该接口；`compiled` 来自最终配置；`available` 为节点／网卡注册或存储挂载检查，无检查项时为 null。存在硬件冲突时返回 `available:false` 和 `blockedBy`。节点存在不能证明摄像头连接、网络连通、触摸正常或外设传输成功，必须读取各服务的实际回执。

| 接口 | 当前协议／调用入口 | 验证边界 |
| --- | --- | --- |
| 显示与触摸 | MIPI DSI、GT911 I²C；LVGL、桌面服务 | `/dev/fb0`、`/dev/input0`，驱动独占引脚 |
| 默认摄像头 | SC2336、MIPI CSI；`assistant_camera`、`system.camera` | `/dev/video0`；CSI 拍照需要预览 |
| USB 摄像头 | USB HS Host、UVC、MJPEG；同一相机服务 | `/dev/uvcctl` 仅是控制节点；list 的 connected/supported/generation 才是设备状态 |
| USB HS Type-C Device | CDC ACM 数据口；`system.usb.mode({port:0,role:2})` | 与 Type-A Host 互斥；挂载／采集／传输占用时拒绝切换；CDC 不执行 NSH 命令 |
| USB FS Type-C | 独立 FS OTG、CDC 数据协议 | 出厂显示复位／背光连接 GPIO27/26，当前配置禁止开启并返回 EBUSY；FS Host 尚未实现 |
| USB U盘 | HS Host、MSC BOT、SCSI READ/WRITE10、FAT | `/dev/usbblkN`；默认只读挂载；显式 writable 后允许受限文件写入；不提供格式化／原始擦除 |
| USB HID | HS Host、Interrupt IN、SET_REPORT、Boot Protocol | 原始键鼠／HID 报告；最多512字节、8包队列；当前不自动映射为桌面触摸或键盘事件 |
| USB 音频 | HS Host、UAC1 Type I PCM16、同步／自适应播放和录音 | 单声道／双声道，8～48 kHz；最多16模式；不支持 UAC2 或异步扬声器反馈 |
| USB Serial/JTAG | 下载、调试、NSH 控制台 | 与摄像头口分离；不作为 Agent 任意串口 |
| 麦克风／扬声器 | ES8311 I²S、I²C；PCM16 WAV、`system.audio`、语音服务 | 编解码器共享占用；不承诺任意音频格式 |
| MicroSD | 4-bit SDMMC、FAT；存储卷服务 | 实际挂载、剩余空间与文件操作分别检查 |
| 板载 Flash | SPI NOR、SmartFS；内部存储卷 | 保留当前应用／数据分区；不暴露原始擦写工具 |
| Wi-Fi | ESP32-C6、SDIO、网络服务 | wlan0 注册与关联、DHCP、互联网可达性不同 |
| BLE | ESP32-C6、HCI；扫描、连接、GATT 服务／特征／描述符、读写、CCCD订阅、通知、配对、MTU | 每次连接需 generation；与 BLE HID 鼠标外设角色互斥；NoInputNoOutput 配对；没有 Classic A2DP |
| RJ45 | IP101GR、RMII、10/100 Ethernet、POSIX 网络；`system.net.status()` | 分别返回 eth0/wlan0 注册、up、link、IPv4／掩码／网关；不将 DHCP 地址等同互联网可达 |
| 扩展 GPIO | 3.3 V、`system.gpio`／`hardware_submit` | 仅开放 capabilities.gpioPins，当前为 GPIO2～6 |
| 扩展 I²C1 | 7 位地址、重复起始、10～400 kHz | SDA2、SCL3；实际设备地址／寄存器来自外设手册 |
| 扩展 SPI2 | Master、模式0～3、8-bit MSB、全双工 | CS6、SCK3、MOSI4、MISO5；应用定义命令帧 |
| 扩展 UART1 | 8N1、1200～921600 baud | TX4、RX5；读取可能是半帧，应用自行拆包 |
| LEDC PWM／舵机 | 硬件 PWM、占空比／脉宽 | 通道0/1/2/3 → GPIO2/3/4/6；不证明机械运动 |

USB 摄像头接开发板 **Type-A HS Host**；高速 Type-C 与它不能同时传输。Type-A 的板载限流为 500 mA，摄像头供电需求超过此值时需要合适的外部供电方案。USB Serial/JTAG 口继续用于烧录与控制台。

出厂 J1 的 GPIO0/1 因 32 kHz 晶振而不作为通用扩展使用；显示复位／背光 GPIO27/26、板载存储、音频、C6、USB 等引脚由对应服务保留。构建参数 `--factory-header` 将旧配置中的 I²C SDA1、SPI CS0、PWM0 pin0 改为上述引脚。运行时总线声明读取最终 CONFIG，拒绝占用保留引脚；旧配置中不安全的总线不会出现在 capabilities 数组内。

扩展引脚相互复用，资源占用在 C 层检查：I²C 与 SPI 共用3，SPI 与 UART 共用4/5，PWM 也与相应总线冲突。返回 EBUSY 时不能擅自关闭别人的设备。板载外设通过对应服务访问，不通过扩展总线重复驱动。

完整参数、错误和任务生命周期见 [HARDWARE_API.md](overlay/apps/system/espclaw/guide/HARDWARE_API.md)。摄像头／云端图片协议见 [MULTIMODAL-CAMERA.md](MULTIMODAL-CAMERA.md)。诊断命令 `desktop camera-agent '{"operation":"capture","source":"usb"}'` 会保留 CLI 所属线程直到 USB 采集结束，再输出保存回执；诊断不上传图片。

USB 类设备使用 `usb.list` 返回的 id 和 generation；拔插后旧 generation 失效。音频流属于创建它的会话，关闭会话自动停止；U盘有挂载引用时不会被复用。USB HS 角色为 0=关闭、1=Host、2=CDC Device，端口切换后需重新枚举。

`usb.list` 列举 MSC／HID／UAC1，USB 摄像头仍通过 `assistant_camera` 的 `list` 或 `desktop cameras` 查询。摄像头模式表和接收队列放在 PSRAM 用户堆；USB 控制、传输和 Device EP0 的 DMA 缓冲区使用内部 SRAM，保持64字节对齐。BLE 命令通过有界邮箱进入持有 HCI socket 的常驻任务组；通用 GATT 的宿主协议测试不等于某个实际 BLE 外设已经完成配对和读写。

现场诊断支持一组 operation/JSON 对共享一个会话，例如：

```sh
desktop hardware-call usb.list '{}' net.status '{}'
desktop hardware-call ble.scan '{}' wait 11000 ble.status '{}'
desktop hardware-call usb.mode '{"port":1,"role":2}'
```

最后一条在当前显示接线下应返回 EBUSY。`wait` 为本地诊断指令（最多15000ms），不是 Agent 工具。硬件实测记录放在 workspace 的 `output/interfaces-complete-20261020/`；协议替身测试通过不代表外部 U盘／键鼠／声卡已完成实物验收。
