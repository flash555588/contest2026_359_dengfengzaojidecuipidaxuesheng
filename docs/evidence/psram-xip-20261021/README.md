# PSRAM XIP 底层试验验收

2026-10-21，ESP32-P4 Function-EV-Board rev3.2，32MiB PSRAM，16MiB Flash，EK79007 DSI，USB摄像头0bda:5844。

**本轮有限试验通过。设备继续运行 XIP 试验固件，已停止相机并返回桌面。**

固件SHA256：`ddf284dfa3cfe7410e2cc50591d25a40ee711f5edf8afc6088474bd73942ce25`。回退镜像：`rollback/nuttx.bin`，SHA256：`28f43ea68b036e38a84b6182ec03b7d189974c04d0d947ad43a590aa2a63e8c7`。只烧录应用区0x2000，未重分区、未擦除用户数据；原第6版“恐龙跑酷”草稿保留。回退脚本 `rollback.py` 也只写应用区。

## 底层实现与启动证据

复用工程所带 Espressif P4 CPU-copy XIP 实现；Simple Boot 供给实际镜像物理偏移，链接指令与常量到PSRAM窗口，外部BSS/堆避开复制页。复制前依赖和全部校准回调由最终 ELF 核对；SPI1使用SDK原有mutex/cache-on路径，保留内部栈dispatcher及已拒绝实验的禁用。

启动日志直接证明：text `48000000..48370480`，hash `a444e362`；rodata `48380020..489ebec4`，hash `3e251a3d`；heap `489f0000..4a000000`。两段经每页MMU及独立Flash别名全段哈希校验。PMP在复制后锁定，CPU1发布ready前配置并回读保护；控制台多次报告 `ready=1 protected_cpus=3`。SPI1实际回调计数从4174增长到27253。

## 实测

| 场景 | 轮次 | 内部Flash写入/擦除（该轮增量） | 显示IRQ最大间隔 | 实体屏幕 |
| --- | ---: | ---: | ---: | --- |
| 原小游戏运行 | 8 | 2775 / 285 | 16.593 ms | 用户确认全程无蓝屏、黑屏或闪烁 |
| USB摄像头预览 | 4 | 1087 / 102 | 16.612 ms | 用户确认全程无蓝屏、黑屏或闪烁 |

累计3862次写入、387次擦除，最长Flash擦除55.413ms，最长写入32.267ms；显示间隔没有跟随擦除扩展到此前约62ms。两轮storage selftest均逐次通过覆盖、回读及删除，均使用内部 `/data/qpk` 下独占新建测试目录；测试文件由既有自检清理，未触碰用户配置/应用数据。显示欠载与翻页错误均为0。累计计数包含该时间窗内正常应用活动，不能把每一次底层操作都等同于一笔用户保存。

USB实际预览帧持续增长，收包错误为0；本轮软件预览约9.7～14.3fps，驱动丢帧为处理速度低于接收速度的统计，未把它表述为零丢帧。试验中还记录到一次SD拍照保存成功。结束时相机busy=0/alive=0，原草稿revision=6/preview_active=0。用户堆剩余约13.69MB，内核堆剩余18416字节。

## 验证范围

宿主反例测试、完整固件构建、最终镜像/启动闭包审计、烧录哈希、实板启动与两核PMP回读、两轮物理观察分别有证据。源码与确切固件一致；`source/`保存实际移植后的底层文件。通用fatal/assert诊断路径未纳入正常启动闭包证明，不能用这些测试推断任意故障可继续运行。

本次未做长期老化、断电冷启动、所有AI工具组合或任意动态Flash mmap并发验证。PSRAM代码/常量占用了约10MiB原可用内存，需继续按真实负载保留容量余量。正常构建默认不启用 `--psram-xip`；本次不把试验结果扩成所有ESP系列和所有Flash映射场景的保证。

日志：`host-result.log`、`build.log`、`ELF-AUDIT.json`、`REVIEW.md`、`flash.log`、`boot.log`、`boot-state.log`、`flash-stress.log`、`usb-flash-stress.log`、`final-state.log`；汇总为 `RESULTS.json`、用户观察为 `PHYSICAL.json`。


## 仓库证据与本地回退材料

本目录提交精简验收记录：RESULTS.json、PHYSICAL.json、ELF-AUDIT.json、host-result.log、boot.log。
上述完整构建、串口、烧录及源码快照保留在测试工作区 output/psram-xip-20261021，未随本次源码提交上传。
固件与回退镜像同样保留本地，摘要见本报告；仓库不包含可直接烧录的二进制。
报告记录的是提交前已构建并实测的工作区候选；构建时基线为 f4b486f5663008f2e5ca04d758668ea4cb01153a，包含此次一并提交的未提交源码。
复现实验须使用 tools/build_bilibili_v3.py --psram-xip 和已准备的兼容基线，具体依赖与边界见 app/espdl-quickapp/DISPLAY-FLASH.md。
