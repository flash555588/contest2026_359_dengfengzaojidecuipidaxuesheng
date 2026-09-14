# openvela 桌面生命周期候选 · 2026-09-13

后续已生成 [网络取消候选](../glass-network-cancel-20260913/README.md)；
本目录保留 QPK 回收与设置保存阶段产物。

本轮补齐 QPK 回调资源回收和后台设置保存，保留此前的文件服务、性能页、
CPU 采样配置及减少无效刷新的改动。v3 已完成离线编译、链接和最终核验，
尚未刷机。

## QPK 回收修复

旧代码在正常循环创建、删除按钮后耗尽事件槽；失败证据保存在
[修复前回归](tests/qpk-baseline-test.log)。现在按钮删除会释放所属的 JS
回调，LVGL 直接删除控件也会归还句柄。停止运行时先移除旧页面监听器，
旧页面的点击或延迟删除不会碰到新页面复用的槽。

回调执行期间保留独立 JS 引用。定时器自取消、创建替代定时器后再报错时，
只清理仍属于原 ID 的定时器；不重复释放，也不删除新定时器。定时器 ID
达到整数上限时明确失败，没有提高原有资源上限或 JS 栈预算。

真实 QuickJS 与 LVGL 的 7 项回归覆盖按钮反复删除、定时器、页面交接、
100 次启动/停止、100 次原生控件删除、自取消和替换后的异常处理。
完整运行时、QuickJS、LVGL 同时启用地址、泄漏和未定义行为检测后也全部通过。
硬件和 Home Assistant I/O 是测试桩，不能据此认定相机或网络已完成实板验证。

## 设置保存

外观设置不再在 UI 线程中写文件或执行 fsync。后台以 250 ms 窗口合并待保存
值，使用原有双记录、序号和校验格式；连续请求只保留最后一组四字节设置，
相同的已保存值不重复写盘。队列排空后工作线程退出，不常驻。

关闭设置页后保存继续，重新打开能显示“保存中”；只有写入、同步和读回
校验完成后才显示“已保存”。失败显示“保存失败”。fsync 失败可能留下
可读记录，因此后续重试会实际重新保存，不把读得到数据误判成已经同步成功。

回归覆盖受控突发请求合并、调用者缓冲区变化、保存中关闭/重开、失败提示、
相同值重试、代际恢复和 100 次工作线程生命周期；sanitizer 检查通过。
既有设置格式与截断、损坏回退测试继续通过。

## 验证与产物

[默认完整测试](tests/default-tests.log) 14 项全部通过；另行通过
[v3 Wi-Fi 界面](tests/wifi-v3-tests.log) 和
[v1 Wi-Fi 界面](tests/wifi-v1-tests.log) 回归。
构建脚本的 18 项测试通过，七个生产编译单元通过 RISC-V 编译，完整固件链接成功。
QPK 和设置服务的 sanitizer 结果分别在
[QPK](tests/qpk-asan-tests.log) 与 [设置保存](tests/preferences-asan-tests.log)。

[v3 候选固件](v3/nuttx.bin) 为 3,828,296 字节，SHA-256：
bf93032226aa2a819afeaf59f424624d8e857a1d53320a433f48b3e4474d9967。
按已有 0x2000 偏移布局，结束于 0x3a8a48，距离 0x400000 的 /data 分区
还有 357,816 字节。ELF、map、实际配置和构建日志位于 v3 目录。

[最终核验](VERIFICATION.json) 确认 19 个生产输入与当前源码、WSL 构建树及
[构建元数据](v3/BUILD-METADATA.json) 一致；QPK、设置、文件服务、性能页、
CPU/procfs、相机、C6 和 ESP-Claw 的关键入口都在最终 ELF 中。

## 复现与边界

主编辑源码仍在 camera-app/glass-desktop，WSL 构建树为
/home/flash/glass-desktop-resources-20260913。来源
/home/flash/glass-claw-v3-recovered-20260913 未改写。内核基线
dd92bcf425738734d1b8aed09c2bd4dbe3f2e438，apps 基线
dcc6a95c3b323e533c98fde8fb209f99e24f0fdd，保留既有移植补丁。
通过 build_joint.py 的 --resume --refresh-desktop --c6-desktop --cpu-load
在禁用网络的 namespace 内构建。

复现真实 QPK 宿主测试需为 tests/CMakeLists.txt 开启 GLASS_QPK_RUNTIME_TEST。
QuickJS 使用已有本地源码和原 Makefile 的 -fwrapv，不下载依赖；设备打开
被测试桩拒绝。主机测试仍保持生产的 16 KiB JS 栈预算。
verify_candidate.py 可重新检查本候选、源码和保存的测试证据。

没有构建新 v1 镜像，没有烧录或修改板级停止顺序。v3 仍沿用 301–399 修订范围
及原有 Simple Boot 无附加摘要格式。相机停流失败、线程退出等待、Home Assistant
网络线程停止、真实 CPU/堆恢复和 30 分钟长稳仍需进一步验证。宿主 100 次循环
不能替代整板验收；可用堆变化也要与壁纸缓存、驱动保留缓冲区和任务栈区分。
