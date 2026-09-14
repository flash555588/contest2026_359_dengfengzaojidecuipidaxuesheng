# ESP32-P4 摄像头与内存/频率优化交接日志

日期：2026-08-31  
状态：180 MHz 版已编译、烧录并通过摄像头实机测试；360 MHz 和 PPA 仍为禁用实验项。

## 1. 交付结论

当前可交付方案是：SC2336 保持 1024×600 RAW8 传感器输入，由 ESP32-P4 ISP 直接输出 RGB565，应用层按需分配 2 个采集缓冲和 2 个预览缓冲，关闭相机时立即释放。CPU 从原有实际 90 MHz 提升到实测稳定的 180 MHz。

实机结果：首帧正常，ISP RGB565 帧长 1,228,800 字节；CPU 缩放耗时由 90 MHz 的约 50.9–54.6 ms 降到 180 MHz 的约 27.6–29.8 ms；预览由约 4.0–4.2 FPS 提升到 7.0–7.8 FPS；采集阶段约 11.4–15.7 FPS。空白画布基准约 13.9 FPS，因此目前页面刷新上限也在这一量级。

内存不需要加周期性强制回收。相机启动时 Umem 增量约 3.177 MB，与四个动态图像缓冲 3.072 MB 及约 105 KB 运行时开销相符；停止后核心缓冲全部释放。强制 GC 无法回收 C/V4L2/LVGL 原生缓冲，反而会引入卡顿。

## 2. 开发环境

| 项目 | 实际环境 |
| --- | --- |
| 主机 | Windows 11，NT 10.0.26200.0 |
| Shell | PowerShell；构建通过 WSL2 |
| WSL | Ubuntu-22.04 |
| Python | 3.13.14 |
| esptool | 5.3.1 |
| GNU Make | 4.3 |
| RISC-V 工具链 | `riscv32-esp-elf-gcc 15.2.0` (`esp-15.2.0_20251204`) |
| NuttX 构建树 | `/home/flash/openvela-contest359-release/nuttx` |
| apps 构建树 | `/home/flash/openvela-contest359-release/apps` |
| NuttX commit | `2f1387d56eb04ad2599baca58a3fa2380cdaaedb` |
| apps commit | `88827afd368d4bbb4802b96ed44d9582f85b2f92` |
| 目标板 | ESP32-P4 revision v1.0 / ROM ECO2，32 MB PSRAM |
| 串口 | COM7，115200 baud；烧录为 921600 baud |

该芯片低于 NuttX 当前正式支持的 rev3，启动时会输出 `THIS MAY NOT WORK! DON'T USE THIS CHIP IN PRODUCTION!`。当前结果仅代表这块 rev1 样板上的大赛/原型验证，不等于量产承诺。

## 3. 可重现构建与烧录

构建：

```powershell
wsl.exe -d Ubuntu-22.04 -- env PATH=/home/flash/.local/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin make -C /home/flash/openvela-contest359-release/nuttx -j8 CROSSDEV=/home/flash/vela-p4/riscv32-esp-elf/bin/riscv32-esp-elf-
```

烧录 180 MHz 验证固件：

```powershell
py -3.13 -m esptool --chip esp32p4 --port COM7 --baud 921600 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x2000 "C:\Users\flash\Desktop\新建文件夹 (2)\vlae\camera-app\nuttx-camera-isp-rgb565-180mhz.bin"
```

启动相机诊断：

```text
desktop camera
```

停止并触发原生资源清理：

```text
desktop camera-stop
```

查看堆内存：

```text
free
```

自动抓取启动日志：

```powershell
py -3.13 camera-port/capture_boot.py COM7 45 "desktop camera"
```

`make olddefconfig` 可能先输出 `riscv64-unknown-elf-gcc: command not found`，这是环境默认 CROSSDEV 探测噪声。正式构建必须使用上述显式 `CROSSDEV` 路径。

## 4. 固件产物

| 产物 | 大小 | SHA-256 | 状态 |
| --- | ---: | --- | --- |
| `camera-app/nuttx-camera-isp-rgb565-180mhz.bin` | 3,150,160 B | `17FC20BF2EEEFCC3E2B8187BB82F15AE815B1E2DB45D0B7A068FFCDD9D23C09A` | 已烧录验证，当前推荐基线 |
| `camera-app/nuttx-camera-isp-rgb565-360mhz.bin` | 3,150,160 B | `B50266503EE512EBB8A51A0FB745C3290F780FBD47CC514696E48D8032AC8F8A` | 失败实验，禁止交付/烧录 |
| `camera-app/nuttx-camera-isp-rgb565.bin` | 2,998,228 B | `26CE86F8E73B8FC1F7130DF5376191E0ED9A83012D3D80386584A3C48074B425` | 旧 90 MHz 已知稳定回退固件 |
| `camera-app/nuttx-camera-ppa-rgb565.bin` | 3,015,956 B | `2F0295212084577ED75AA87CE53C4246C4AFD6D51359D02D64ACF519E76C3E03` | PPA 首次事务锁死，禁止烧录 |

Simple Boot 使用 RAM-only image header，ROM 输出 `SHA-256 comparison failed` 是当前镜像形式的已知提示，不是本次固件损坏。可交付性应以本节文件 SHA-256 为准。

## 5. 关键代码节点

### 5.1 CSI + ISP 硬件色彩转换

文件：`camera-port/esp_mipi_csi.c`

- `esp_mipi_csi_configure_isp()`：将 ISP 输入设为 RAW8，输出设为 RGB565，使用 BT.601/full range，帧长切换为 1,228,800 字节。
- `esp_mipi_csi_set_buf()`：仅在格式变化时配置 ISP；V4L2 轮转缓冲时不重配 ISP，避免每帧关闭活动管线。
- `esp_mipi_csi_validate()` / `esp_mipi_csi_start()`：同时验证 RAW8 和 RGB565，并按格式设定 DMA 帧长。

关键约束：不要把 ISP 重配置放回每次 QBUF/set_buf 路径，否则会重现“一张一张/低帧率”。

### 5.2 SC2336 对上层声明 RGB565

文件：`camera-port/sc2336.c`

- `sc2336_validate()` 接受 `IMGSENSOR_PIX_FMT_RGB565`。
- `g_sc2336_formats` 向 V4L2 公布 `V4L2_PIX_FMT_RGB565`，描述为 `RGB565 via ESP32-P4 ISP`。
- 传感器本身仍输出 RAW8，RGB565 是 ISP 输出协议，不要误改 SC2336 寄存器为“传感器直出 RGB565”。

### 5.3 动态内存和 CPU 缩放

文件：`camera-app/qpk_runtime.c`

- `qpk_camera_start()`：按需分配 2 × 1024×600×2 采集缓冲和 2 × 512×300×2 预览缓冲，申请 V4L2 ring mode 双缓冲。
- `qpk_camera_thread()`：DQBUF 后做 32-bit 打包的 2:1 RGB565 抽样，再 QBUF；阶段日志分别记录 DQ、cache invalidate、scale 和 QBUF。
- `qpk_camera_stop()`：删除 LVGL timer，请求线程停止，cancel DQBUF/join，STREAMOFF，关闭 fd，删除 canvas，释放 raw/RGB565 缓冲和 mutex。

`camera-port/qpk_runtime.c` 是早期摄像头移植镜像，与当前实际构建的 `camera-app/qpk_runtime.c` 不同：后者还包含 Home Assistant 和 UI 变更。后续合并时必须以 `camera-app/qpk_runtime.c` 为应用基线，不要反向整文件覆盖。

### 5.4 按需打开/关闭相机

文件：`camera-app/desktop_main.c`

- `desktop camera`：通过 `g_camera_launch_requested` 把打开请求交给 LVGL 主循环。
- `desktop camera-stop`：通过 `g_camera_stop_requested` 清理 panel，由 QPK 销毁回调进入 `qpk_camera_stop()`。
- 默认 desktop 不再常驻打开相机。

### 5.5 rev1 两阶段 CPU 时钟

文件：`esp32p4_nuttx/arch/risc-v/src/common/espressif/esp_start.c`

rev1 Simple Boot 路径不能直接跑完整 rev3 `esp_clk_init()`。当前代码先用 90 MHz 初始化 PSRAM，成功后再切到 `CONFIG_ESPRESSIF_CPU_FREQ_MHZ`：

```c
if (!rtc_clk_cpu_freq_mhz_to_config(CONFIG_ESPRESSIF_CPU_FREQ_MHZ,
                                    &cpu_config))
  {
    PANIC();
  }

rtc_clk_cpu_freq_set_config(&cpu_config);
```

相关配置位于 `esp32p4_nuttx/arch/risc-v/src/common/espressif/Kconfig`、`camera-port/defconfig` 和 `camera-port/resolved.config`。主镜像已补齐 HAL 实际支持但原 Kconfig 未暴露的 90/180 MHz 档位，当前 defconfig 选择 180 MHz。

## 6. 内存测量与设计选择

180 MHz 版的同一次冷启动三点采样：

| 阶段 | Umem used | 相对冷启动 |
| --- | ---: | ---: |
| 冷启动/desktop 空闲 | 2,558,736 B | 0 |
| 相机运行 | 5,735,736 B | +3,177,000 B |
| `camera-stop` 后短时采样 | 2,578,808 B | +20,072 B |

缓冲精确计算：

```text
采集：2 × 1024 × 600 × 2 = 2,457,600 B
预览：2 ×  512 × 300 × 2 =   614,400 B
合计：                            3,072,000 B
实测其他运行时开销：              约 105,000 B
```

保留两个 V4L2 采集缓冲是必要的。NuttX ring mode 只有一个容器时 `vbuf_top == vbuf_next`，DQBUF 无法得到有效帧；改 FIFO 单缓冲虽可省 1,228,800 B，但每帧都会停采、QBUF 后重启 CSI/传感器，会回到低帧率和不稳定。

可选但未默认启用的内存档位：单预览缓冲可再省 307,200 B，但有擕裂风险；将预览降为 256×150 可比当前省 460,800 B，但会明显降低画质。最大的未来收益是硬件直接输出 512×300，理论上可再省约 1.84 MB 采集缓冲，但当前 ISP 路径未接入缩放器，PPA 又在 rev1 上阻塞。

## 7. 当前缺陷与风险

### P0：360 MHz 启动不完整

360 MHz 固件能输出 `cpu freq: 360000000 Hz`，但随后停在 `Adding pool of 32768K of PSRAM memory to heap allocator`，不进入 NSH/desktop。这证明时钟硬件已切换，但 ECO2/rev1 Simple Boot 的配套时钟/计时/电压初始化不完整。不得仅因 Kconfig 默认值为 360 就作为可用结论。

当前规避：defconfig 锁定 180 MHz。360 MHz 需在 rev3 硬件或完成厂商级 rev1 时钟初始化移植后再测。

### P0：PPA 首次硬件事务锁死

Espressif upper HAL PPA/DMA2D 已能编译并注册，日志到达 `PPA RGB565 scaler ready`，但首次 blocking transaction 会使全系统无响应。实验调用已从正式源码回滚，不得烧录 `nuttx-camera-ppa-rgb565.bin`。

后续方向：不要再用 FreeRTOS 兼容头包装 IDF upper HAL；应实现 NuttX 原生 PPA lower-half，显式处理中断、DMA cache 一致性、时钟/复位、完成信号和超时回复，并优先在 rev3 芯片验证。

### P1：停止/面板切换附近偶发 QBUF `EINVAL`

一次 180 MHz 测试在首帧后输出 `[qpk] camera QBUF failed: 22`，随后内存正常回到基线；重新启动后可持续运行 30 秒并输出稳定阶段日志。从表现看更像 panel/QPK 销毁与采集线程返还缓冲重叠，而非持续采集故障。

建议在 `qpk_camera_stop()` 中增加状态机日志，并在 QBUF 错误时同时打印 `stop_requested`/`streaming`/fd/buffer index/userptr。若只在 stop 路径出现，应将其收敛为可预期退出；若运行中也出现，再检查 V4L2 container 状态。

### P1：源码镜像和复现脚本尚未完全统一

`camera-port/apply_final_overlays.sh` 当前引用 `camera-port/patches/*.patch`，但本工作区实际补丁位于 `camera-port/` 根目录，且并不包含当前全部应用/Home Assistant 变更。在重构补丁集之前，应以本日志列出的活跃文件和 WSL 构建树为准，不要直接运行该脚本覆盖现有源码。

### P2：构建警告

全量构建存在重复 LVGL target 警告；当前不影响链接和固件启动，但应在下次清理构建系统时去重。

## 8. 建议后续顺序

1. 保持 180 MHz + CPU 打包抽样作为参赛稳定基线，连续运行至少 30 分钟，每 5 分钟记录 FPS、`free` 和 QBUF 错误计数。
2. 补齐 QBUF 停止竞态的诊断日志，连续执行 50 次 `camera -> camera-stop`，确认错误是否仅发生在退出窗口。
3. 将 `camera-port` 重整为可从指定 NuttX/apps commit 一键应用的补丁集，不再依赖手工复制镜像文件。
4. 若必须继续提帧，优先在 rev3 板上实现 NuttX 原生 PPA lower-half；不要在当前 rev1 板上继续盲试 360 MHz/PPA 组合。
5. 只在明确接受画质或擕裂代价时，再提供单预览缓冲或 256×150 低内存档；不要把单 V4L2 采集缓冲作为默认优化。

## 9. 验收标志

启动日志必须同时包含：

```text
cpu freq: 180000000 Hz
Camera: first RGB565 frame OK, 1228800 bytes
Camera: SC2336 1024x600 RGB565 via ISP registered at /dev/video0
[qpk] camera first frame displayed: 1228800 bytes
```

持续运行期间应定期出现 `camera stages` 和 `camera preview fps`，`scale` 应约为 28–30 ms。执行 `desktop camera-stop` 后，Umem used 应从约 5.7 MB 回到约 2.6 MB。
