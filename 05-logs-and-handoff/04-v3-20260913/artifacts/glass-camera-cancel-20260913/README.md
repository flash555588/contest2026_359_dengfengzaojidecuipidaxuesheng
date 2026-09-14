# Glass 相机异步退出交接 · 2026-09-13

相机退出、启动失败和采集异常的硬件收尾已移到后台；v3 离线候选已构建并核验，尚未刷机。
本交接接续上一份网络取消候选，后续仍由当前任务单独推进，不启用子代理。

## 已完成的改动

`qpk_runtime.c` 将相机硬件会话与会被清零的 QuickJS 页面状态分开。
UI 线程删除画布、定时器和两块空白预览缓冲区，然后提交清理任务。
后台依次停流、等待采集线程退出、关闭视频设备和 framebuffer；工作线程不访问 JS 或 LVGL 对象。
只有硬件清理成功才释放绘制暂停，并通知主循环重绘。

`desktop_main.c` 在暂停 LVGL 定时器时仍轮询相机状态，处理采集线程异常。
NSH 的 `desktop camera` 与 `desktop camera-stop` 通过互斥锁交给主循环；旧会话未清理完成时重开排队，
显式停止取消排队。内置相机的点击启动延迟到当前 LVGL 处理轮次结束。
`qpk_runtime.h` 新增忙状态和轮询接口，供主循环协调退出与重开。

保留原有 USERPTR 三页零拷贝预览。启动时检查 framebuffer 几何、页数、容量和 REQBUFS 返回数量，
采集时检查返回索引与 USERPTR，防止使用显示页范围之外的地址。相机缓冲区索引的符号比较警告已修正。

## 已验证的结果

完整宿主回归 33 项通过，包括 v1 Wi-Fi 桌面、文件、设置、性能采样、网络、QPK 与相机；
另行关闭 v1 分支运行 v3 Wi-Fi 桌面交互测试，1 项通过。
真实 QuickJS/LVGL 配合模拟 V4L2/framebuffer 的相机测试共 18 项，覆盖慢停流、等待采集线程、
视频和 framebuffer 关闭、快速重开、启动失败、异常退出、限次重试及不确定清理结果。
其中含 100 次采集/退出/重开和 100 次空白画布循环，检查句柄、工作线程、画布内存与定时器回收。

AddressSanitizer、LeakSanitizer、UndefinedBehaviorSanitizer 下的 25 项 QPK/相机测试通过。
相同受控停流阻塞下，旧版退出耗时 1,500,926 微秒并触发失败；新版 sanitizer 测试的
四种阻塞场景最大退出耗时分别为 720、619、636、770 微秒。这些是宿主观测值，不是实板时延保证。
原生 RISC-V 编译通过八个生产单元；既有桌面符号比较与上游 QuickJS 函数指针转换警告仍存在。

[核验记录](VERIFICATION.json) 检查了 21 个生产输入与本地源码、WSL 构建树一致；
相对上一份网络候选，仅 `desktop_main.c`、`qpk_runtime.c`、`qpk_runtime.h` 改变，解析配置一致。
最终 ELF 包含后台清理和相机轮询入口，停止函数反汇编没有直接调用 ioctl、close、pthread_join、usleep 或 munmap。
[测试日志与输入哈希](tests/INPUTS.json) 位于 tests 目录，详细 sanitizer 日志为 tests/asan-detail.log。

## 固件与构建位置

[v3 固件](v3/nuttx.bin) 为 3,830,076 字节，比上一候选增加 1,124 字节。
SHA-256：`cc592c0e47fdc2828226ce1faaef42090fd982ade083eaaaa34bd9de7a7e238c`。
现有布局偏移 0x2000，镜像结束于 0x3a913c，距 0x400000 的 /data 分区还有 356,036 字节。
[构建元数据](v3/BUILD-METADATA.json)、ELF、map、实际配置及完整日志都在 v3 目录。
这是 v3 候选，未生成新的 v1 固件。

Windows 工程：`C:/Users/flash/Desktop/新建文件夹 (2)/vlae`。
WSL 发行版：Ubuntu-22.04；构建树：`/home/flash/glass-desktop-resources-20260913`。
基线树：`/home/flash/glass-claw-v3-recovered-20260913`。
工具链：`/home/flash/vela-p4/riscv32-esp-elf/bin`。
openvela 内核基线 `dd92bcf425738734d1b8aed09c2bd4dbe3f2e438`，
apps 基线 `dcc6a95c3b323e533c98fde8fb209f99e24f0fdd`，保留已有移植补丁。
本轮未修改底层相机驱动，未提交或推送 Git。

在工程对应的 WSL 目录运行以下命令可重新核对当前源码、构建树、产物和保存的测试证据：

```bash
python3 -B artifacts/glass-camera-cancel-20260913/verify_candidate.py
```

测试构建目录分别为 `.codex-work/desktop-resume-20260913-050604/host`（完整 v1 配置）、
`.codex-work/desktop-resources-20260913/ui`（v3 Wi-Fi）、
`.codex-work/desktop-lifecycle-20260913/qpk-asan`（sanitizer）。
旧版源码快照与回归对照保存在 `.codex-work/desktop-camera-20260913/before` 和 `baseline`。
本轮其余原始日志位于 `.codex-work/desktop-camera-20260913`。

## 剩余边界与下一步

STREAMOFF 返回不能单独证明 DMA 已停止：当前驱动还可能通过 LPWORK 延迟收尾，视频 close 会等待它。
清理线程创建或停流失败最多尝试三次，重试间隔 250 ms；join、munmap、close 失败不盲目重试。
不确定资源是否安全时保持忙状态和绘制暂停，可能需要重启恢复。
如果底层系统调用一直不返回，清理线程也会一直保留；本改动没有强制取消该线程。
启动的 open/ioctl 仍是同步路径，本轮解决退出和异常收尾等待，不承诺启动无阻塞。

下一步在确认板型后进行 v3 实板冷启动、相机启停/快速重开、采集异常和长期 CPU/堆恢复验证，
重点核对 CSI 的延迟停流、显示页交还顺序和后台 8192 字节栈的实际余量。
宿主测试使用设备替身，不能证明真实 DMA、射频、DHCP、帧率或 30 分钟稳定性。
随后继续独立 v1 固件构建与对应板级验收；不要把 v3 镜像用于 v1。
