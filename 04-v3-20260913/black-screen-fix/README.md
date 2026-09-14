# ESP32-P4 v3.2 桌面黑屏修复（2026-09-14）

修复对象为 `04-v3-20260913/current-build-tree-source.tar.gz`。最终固件已烧录到 COM23 的 ESP32-P4 v3.2，桌面任务和显示驱动正常运行，并从当前扫描缓冲读出完整 VelaDesk 桌面图像。实体屏幕的目视结果尚待用户确认。

## 直接使用

- `nuttx.bin`：修复固件，3,960,944 字节，烧录地址 **0x2000**。
- `nuttx.elf`、`nuttx.map`：对应的调试符号和链接映射。
- `resolved.config`：实际构建配置，与原导出配置 SHA256 一致。
- `black-screen.patch`：相对原始源码包的完整补丁。
- `overlay/`：与源码目录层次一致的修改文件。
- `SHA256SUMS`、`build-metadata.json`：本次交付的校验与构建记录。
- `evidence/`：最终启动日志、内存/任务状态和桌面帧缓冲截图。
- `followup-validation.md`：后续约 60 帧/秒的显示输出、背光 GPIO、90 秒连续串口观察及 USB 复位原因记录。
- `software-test-report.md`：ESPClaw、存储、C6 的软件实测及未通过/未测试项目。

后续发现本包继承的配置同时启用有线 EMAC 与固定名为 eth0 的 C6 接口，导致 DHCP 选错接口。与 WSL 原版的完整对比及单独编译的配置修复见 `../wifi-dhcp-fix/README.md`；新包已烧录并取得 DHCP 地址 192.168.0.105。

BIN SHA256：`61d7d1c8f03a1c01d50b1dfe8c9df9415933a01f7a8e6c752587d76b0659a6fe`。

在本目录执行：

```powershell
python -m esptool --chip esp32p4 --port COM23 --baud 921600 --before usb-reset write-flash 0x2000 nuttx.bin
```

应用镜像结束于 `0x3c9070`；烧录擦除上界为 `0x3ca000`，低于 `/data` 起始地址 `0x400000`。本次未执行整片擦除。

原设备上的应用已完整备份到 `../../diagnostics/original-nuttx.bin`，其 SHA256 与历史构建记录一致：`77e36cc5e6d85216af739cbe0ae2feca06e9d937e4cbd5fe808cef1a18e3521f`。该文件可用同一地址回退；它不包含数据分区。

## 根因与修改

`hex_psram: vendor id : 0x0d (AP)` 是正常识别信息。完整原始日志显示 32 MB PSRAM 已以 200 MHz 初始化成功，随后才发生：

```text
gdma: do_allocate_gdma_channel(127): no mem for pair(0,0)
esp_timer: Not enough memory to create timer task
```

最新版本启用了独立内核堆。GDMA 使用 `heap_caps_calloc(..., MALLOC_CAP_INTERNAL)` 从 SRAM 内核堆分配，却在复用 group/pair、删除和错误清理时调用普通 `free()`，把内存交给 PSRAM 用户堆。第二次取得已有 group 时释放临时 group 即可触发堆破坏。

原机 SRAM 快照中，内核堆总量 `0x56ec0`、实际占用仅 `0x4de0`；大小 `0x52320` 的 SRAM 空闲块却链接到 `0x48000138/0x48000128` 的用户堆链表。这解释了有大量空闲内存仍报分配失败。

`gdma.c` 的八处释放统一改为 `heap_caps_free()`，利用已有的堆归属检查，覆盖 group/pair 复用、通道删除和失败清理。未调整 PSRAM 频率或扩大配置堆大小。

另有两个必要修复：

1. `platform/os.c` 删除重复的 `intr_adapter_to_nuttx` 定义，使用 `esp_irq.h` 的定义，解决源码导出后的重编译错误。
2. P4 eco7 ROM 会校验 RAM 段后的 SHA256，即使旧 Simple Boot 镜像声明没有摘要。新增 `simple_boot_digest.py` 从已有 padding 中取出 32 字节放置正确摘要，同时让 `esp_start.c` 跳过摘要。所有 Flash 映射段偏移和内容保持不变，避免移动 XIP 数据。`Config.mk` 仅对 P4 Simple Boot 调用处理脚本。它保留 RAM 校验和，并拒绝异常输入。

比较参考：`01-v3.2-pinned` 使用固定旧源码；`02-v3plus` 使用 PSRAM 80 MHz、revision 300–399；最新源码使用 PSRAM 200 MHz、revision 301–399。历史 `03-early-v3` 日志对应 v1.0 芯片，不能作为本次 v3.2 板的直接验证结果。修复保留最新桌面代码和配置。

## 构建复现

隔离构建目录：WSL Ubuntu 的 `/tmp/v3-desktop-black-screen-20260914`，内含 `nuttx/` 和 `apps/`。原始压缩包及历史元数据未修改。

构建依赖：Linux make、CMake、Python 3、kconfiglib、esptool 5.3.1，以及：

- 编译器：ESP GCC `esp-14.2.0_20251107`，安装于 `/home/streetartist/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/bin`。
- 运行库：xPack GCC 14.2.0 的 RV32IMAC/ILP32 `libgcc.a`，编译器路径 `/home/streetartist/toolchains/riscv-none-elf-gcc/bin/riscv-none-elf-gcc`。
- TLS：原 `espclaw-port-current.tar.gz/tls.lock.json` 锁定的 MbedTLS 4.0.0、TF-PSA-Crypto 和 framework。完整 commit、下载 URL、SHA256 见 `../../diagnostics/downloads/tls-manifest.json`，下载包已保留。

本机 ESP GCC 的软浮点 `__floatunsisf` 调用了 `frrm`，当前 `CONFIG_ARCH_FPU` 未启用，第一次重建触发非法指令。因此最终链接使用上述 ABI 兼容的 xPack 纯软浮点运行库。不要替换成会访问 FCSR 的 libgcc。原作者使用 GCC 15.2，本次不宣称二进制可与原构建逐字节复现。

以下命令在 WSL 中执行；`WORKSPACE` 指向本资料目录的 `/mnt/c/...` 路径：

```sh
# 仅首次解压执行；prepare_build.py 拒绝覆盖已有目标。
python3 "$WORKSPACE/diagnostics/prepare_build.py" \
  "$WORKSPACE/04-v3-20260913/current-build-tree-source.tar.gz" \
  /tmp/v3-desktop-black-screen-20260914
python3 "$WORKSPACE/diagnostics/build_linux.py"
```

`build_linux.py` 自动应用 overlay、校验并解压锁定 TLS 包、以 `GEN_FILES=ON` 生成上游 TLS 必要源文件、构建 TLS，再运行 `make -j8`。脚本中工具链和构建目录可按环境修改。源码包已包含打补丁的 LVGL/NimBLE/cJSON/QuickJS，脚本刷新这些目录的时间戳以避免 make 重新解压并重复打补丁。命令行 `EXTRA_LIBS` 会覆盖 Make 中的追加值，因此脚本显式包含 libgcc。

手工应用补丁时，在同时含 `nuttx/`、`apps/` 的解压根目录执行 `patch -p1 < black-screen.patch`，与 overlay 二选一。SHA256 打包脚本和启动解析器必须一起应用，不能单独对旧固件 BIN 添加摘要。

编译产物位于隔离目录的 `nuttx/nuttx.bin`、`nuttx/nuttx`。本目录交付的是已实板验证的那一版；重编译后请另存产物并重新计算校验值，不要沿用本次哈希。

## 验证记录与范围

- 完整 NuttX + 桌面 + 锁定 TLS 编译、链接通过。
- `gdma.o` 的八个释放调用均指向 `heap_caps_free`，无普通 `free` 调用。
- 四项镜像回归测试通过：实际旧镜像的 RAM 摘要和 Flash 段保持、损坏校验拒绝、padding 不足拒绝、当前镜像和分区边界检查。执行：`python diagnostics/test_simple_boot_digest.py`（在资料根目录）。
- esptool 报告 RAM checksum 和 validation hash 均有效，烧录后 Flash 内容校验通过。
- 最终固件连续两次 USB 复位均启动 NSH，未再出现 SHA256、GDMA OOM、timer OOM。
- JTAG 状态为 `OSINIT_IDLELOOP`，`g_lcd_ready=true`，显示为 1024×600 RGB565、三页 PSRAM 帧缓冲。
- `/dev/fb0`、`/dev/input0` 已注册；`desktop`、`esp_timer`、`nsh` 任务存活。
- 实测用户堆空闲 29,760,512 字节、内核堆空闲 317,496 字节；桌面使用约 9.7% CPU（一次采样）。
- 从显示驱动当前 `fb` 导出的 1,228,800 字节转换为 `evidence/desktop-framebuffer.png`，完整桌面渲染正常。这是帧缓冲读回图，不是实体屏幕照片。

USB 驱动原有的启动前丢弃日志策略仍会造成早期 NuttX 日志缺字；发送一次回车后 NSH 输出完整。未测试网络/TLS 会话、音频播放、摄像头、外部服务、触摸交互和长时间运行，也未穷举 GDMA 所有故障注入路径。
