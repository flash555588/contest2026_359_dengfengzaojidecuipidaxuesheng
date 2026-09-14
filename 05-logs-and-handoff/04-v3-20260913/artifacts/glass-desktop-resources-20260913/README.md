# Glass 桌面资源管理候选 · 2026-09-13

后续已生成包含性能页与过期提示优化的
[新候选](../glass-system-monitor-20260913/README.md)。本目录保留文件服务阶段产物。

本轮完成后台文件服务与真实 LVGL 界面的接入，并在 openvela v3 恢复基线上
完成离线编译和链接。这是未经过实板验收的候选，不是稳定发布包。

## 产物

[v3 固件](v3/nuttx.bin) 为 3,824,292 字节，SHA-256：
65a2aec4f98c6c5e4972c089b81f8faadaa7e739a68e2c8ecbea36079cc78533。

[构建元数据](v3/BUILD-METADATA.json) 保存全部 13 个桌面输入的哈希、
实际配置和固件哈希；[最终核验](VERIFICATION.json) 确认源码与构建树一致、
关键符号已链接、内核/apps 的 openvela 基线正确。ELF、map、实际配置和
完整构建日志都在 v3 目录。镜像按已有 0x2000 偏移布局，结束于 0x3a7aa4，
距离 0x400000 的 /data 分区还有 361,820 字节。本轮没有执行烧录。

此镜像沿用原 v3 Simple Boot 格式及 301–399 修订范围；构建输出仍说明
没有附加 SHA-256 摘要。启动兼容性与原先的板级问题需要实板证据，
不能因为桌面编译通过就认定冷启动问题解决。

## 完成内容与验证

目录读取、容量查询、预览、复制/fsync、移动及目录/名称/删除操作均移入
后台。请求保存独立字符串，工作线程不持有 LVGL 对象。关闭窗口只取消并
释放界面引用，旧任务在后台收尾；重新打开等待旧任务完成后继续。
列表原始名称和显示文字分开保存，长名称选择、分页、返回均已回归。

[默认配置](tests/host-tests.log)、[v3 Wi-Fi](tests/wifi-v3-tests.log) 和
[v1 Wi-Fi](tests/wifi-v1-tests.log) 各通过 3 项测试。实际 LVGL 测试让
lstat/fsync 停住，确认其他定时器、关闭与重新打开仍能完成。
100 次浏览/预览/退出后，定时器 4 → 4、文件句柄 7 → 7；具体输出见
[v3 详细日志](tests/wifi-v3-detail.log)。前序相同内容刷新优化仍为 0 次刷新。

[文件服务 sanitizer 日志](tests/file-service-asan.log) 覆盖短写、EINTR、
同步/关闭/清理失败、取消和反复释放；其中两条 cleanup 消息来自刻意
注入的错误。AddressSanitizer、LeakSanitizer、UndefinedBehaviorSanitizer
均通过。[RISC-V 单元编译](tests/native-check.log) 与随后完整固件链接通过。
这轮没有构建新的 v1 固件，也没有修改 QPK 运行时。

## CPU 与下一步实板验收

新配置选择 SCHED_CPULOAD_SYSCLK，100 Hz 系统时钟采样，时间常数 2 秒；
/proc/cpuload 与进程 procfs 已启用。最终 ELF 已核验 clock_cpuload、
g_cpuload_operations，以及文件服务、C6 桌面、相机、ESP-Claw 入口。
它是采样估算，双核总负载使用系统提供的汇总，不能只取一个 idle PID。

下一步先确认目标 v3 板冷启动和重复复位，然后记录空闲桌面、文件复制、
取消后恢复以及相机/应用启停时的 CPU 和内存。至少保留固件哈希、ps、
free 和 /proc/cpuload 输出，完成 100 次应用启停和 30 分钟长稳。
这里的 100 次宿主文件窗口测试不能替代这些实板项目。

## 复现与交接

完整构建树为 /home/flash/glass-desktop-resources-20260913，来源为
/home/flash/glass-claw-v3-recovered-20260913，原恢复树没有改写。
内核基线 dd92bcf425738734d1b8aed09c2bd4dbe3f2e438，apps 基线
dcc6a95c3b323e533c98fde8fb209f99e24f0fdd，均保留已有移植补丁。

使用 espclaw-port/tools/build_joint.py 的 v3、--c6-desktop、--cpu-load
参数构建，--jobs 为 8。构建进程运行在禁用网络的独立 namespace 中；
没有下载依赖、修改上游提交或向远端提交代码。完整命令及中间日志位于
../../.codex-work/desktop-resume-20260913-050604/integration-build.log。

在项目根目录通过 WSL 执行本目录的 verify_candidate.py 可重新核验最终
产物、当前源码与保存的测试证据。后续修改源码后，该核验会如实提示版本不符。
