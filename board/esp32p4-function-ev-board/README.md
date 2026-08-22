# ESP32-P4 Function-EV-Board NSH L0 移植指南

本文记录 ESP32-P4 Function-EV-Board 上 NuttShell（NSH）L0 移植的现状：硬件约束、构建烧录流程、当前配置与已知限制。所有结论均来自实板验证，事实依据见 `logs/flash555588/2026-08-21/work-record-esp32p4-nsh.md`。

## 硬件与限制说明

| 项目 | 实况 |
|------|------|
| 开发板 | 乐鑫 ESP32-P4 Function-EV-Board **v1.4** |
| 芯片 revision | **v1.0**（不是官方文档里的 P4X v3.1） |
| Kconfig 前提 | 必须 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` |
| 烧录对象 | **只烧 P4**，不烧 C6（板上 C6 保持原样未启用） |
| 串口 | COM7（CP2102N），UART0 TX=GPIO37 / RX=GPIO38，115200 |
| Flash | 16MB，DIO 模式 |

注意：网上多数 ESP32-P4 资料按 P4X v3.1 编写，本板是 v1.0，勘误表共 4 条（APM-560 / ECDSA_DS-837 / I2C-308 / RMT-176），其中仅 APM-560 与启动路径相关，正常启动下无未授权主机访问条件，已排除为主因。

## 目录结构

```
contest2026_359_dengfengzaojidecuipidaxuesheng/
├── chip/esp32p4/common-espressif/   # 芯片 overlay，源真值（esp_start.c 的关狗/时钟/外设 init 都在这里）
├── board/                           # 板级目录（esp32p4-function-ev-board 等）
├── tools/                           # WSL 构建/补丁/拷贝脚本 + Windows 烧录脚本
├── firmware/esp32p4-nsh/            # 可烧录产物：nuttx（ELF）/ nuttx.bin / nuttx.hex / nuttx.map
└── logs/                            # 工作记录与串口日志
```

改动以 `chip/esp32p4/common-espressif/` 为源真值；板级差异放 `board/`。

## 构建流程

编译在 WSL 树内完成：

- 编译树位置：WSL `/home/flash/vela-p4/nuttx`
- 构建脚本：`tools/wsl_make_p4_nsh.sh`（内部 `make -j2`）。注意：构建末尾 WSL 侧的 esptool 版本报错**可忽略**——真正的 elf2image 在 Windows 侧做（见下一节）
- 产物拷贝：`tools/wsl_copy_firmware.py`，把 ELF / map / hex 从 WSL 树拷回仓内 `firmware/esp32p4-nsh/`

对 WSL 树打 Simple Boot 补丁用 `tools/wsl_patch_p4_simple_boot.py`（改树内 `bootloader_esp32p4.c`）。

## 生成镜像与烧录

**生成镜像**（Windows 侧，esptool 5.3.1，在仓根目录执行）：

```
python -m esptool --chip esp32p4 elf2image --ram-only-header --flash_mode dio --flash_freq 80m --flash_size 16MB -o firmware\esp32p4-nsh\nuttx.bin firmware\esp32p4-nsh\nuttx
```

关键点：必须用 `--ram-only-header`。**不要烧 objcopy 出来的 bin**——那里面带着 64MB 的空洞。

**烧录**：

```
powershell -File tools\flash_p4_nsh.ps1
```

脚本行为（见 `tools/flash_p4_nsh.ps1`）：把 `firmware\esp32p4-nsh\nuttx.bin` 写到 COM7 @ 460800，偏移 **0x2000**（Apache NuttX Config.mk 规定的 ESP32-P4 simple-boot app offset，ROM 从这里加载），`--before default_reset --after hard_reset`，flash 参数与 elf2image 一致（dio / 80m / 16MB）。

**进下载模式**：按住 BOOT 键，点一下 RST，再跑烧录脚本。连接失败时重复此动作重试。

## 串口验证

打开 COM7，115200。预期启动序列：

1. ROM 加载 Simple Boot 镜像后打印 **SHA-256 Expected 全 0 —— 这是 `--ram-only-header` 的正常现象**，不是错误；
2. 随后出现 N 系面包屑和 XIP 段表（`map_rom_segments` 成功的标志）；
3. 最后进入 NuttShell：

```
NuttShell (NSH)
nsh> uname -a
NuttX 0.0.0 dd92bcf4-dirty Aug 21 2026 19:38:54 risc-v esp32p4-function-ev-board
```

看到 `nsh>` 且 `uname -a` 显示 `risc-v esp32p4-function-ev-board` 即 L0 通过。完整串口日志：`logs/flash555588/2026-08-21/bootlog-esp32p4-nsh.txt`。

## 当前时钟与外设配置

`chip/esp32p4/common-espressif/esp_start.c` 的 SIMPLE_BOOT 路径当前配置（每项均单独构建 + 全套压测验证）：

| 配置项 | 当前值 | 说明 |
|--------|--------|------|
| CPU 时钟 | **CPLL 90MHz** | v1.0 实测稳定档（SELECTS_REV_LESS_V3 分频表最低档），比 ROM 复位时钟 ~40MHz 快一倍多；不调用 `esp_clk_init()` 全量切换 |
| regi2c / bias | 保活 + I2C_BIAS_DREG_1P1(_PVT)=10 | 提频前置条件 |
| XTAL 频率寄存器 | `rtc_clk_xtal_freq_update(40MHz)` | 消掉启动假警告链 |
| IRQ 栈 | 2048 → **8192** | `CONFIG_ARCH_INTERRUPTSTACK`，防御性加大（-O0 肥帧） |
| Brownout | `esp_brownout_init()`，LVL7 | NuttX 无中断变体，纯检测复位 |
| MSPI 引脚 | `esp_mspi_pin_reserve()` | MSPI 引脚占用登记，防止被外设复用抢走 |
| 外设时钟 | `esp_perip_clk_init()` | 未用外设时钟门控，UART0 / MSPI 保留 |
| 看门狗 | `__esp_start` 入口 MMIO 关 TG0 MWDT / LP WDT flashboot，Super WDT auto-feed | ROM flashboot 打开的狗超时远短于 bootloader 的 9 秒 |

## 已知限制

1. **360MHz 不可用**：Phase-2-1b 实验证实，在 Simple Boot 的 ROM 时钟阶段对 MSPI/SPLL 做重配（含先强制 SPLL=480M）会立即挂死 XIP；正确路径需完整时钟树重建（rtc_clk_init 级别），列为后续专项。当前 CPLL 90MHz 为实测稳定档位。
2. **procfs 未自动挂载**：`free` / `ps` 前需手动执行 `mount -t procfs /proc /proc`，否则报 procfs 未挂载属预期行为。
3. **面包屑已部分收编**：esp_start.c 的 N 系列已收进 CONFIG_DEBUG_FEATURES（默认静默）；bootloader 阶段的 B0-B3 四个字符因 HAL 编译单元看不到 NuttX 调试配置而保留常开。
4. **ESP32-C6 保持原样未启用**：本移植只针对 P4 核。

## 根因记录摘要

`help` / `uname` 等命令随机触发非法指令异常（MCAUSE=2）的排查结论：

- 排除了取指映射问题：MMU 换算、镜像与 ELF 逐字节一致性、链接脚本、march 均验证正确；且崩溃只在串口输入到达后发生，EPC 处执行的编码 ≠ 链接视图 → 是内存/取指流被污染，非确定性偏移。
- **根因**：Simple Boot 跳过 `bootloader_clock_configure` 时，`esp_clk_init()` 把 CPU 切到 PLL 后 MSPI 时序失配，XIP 取指进入边缘状态 → 随机指令污染。
- 实验证据（同一块板、COM7、压测脚本 p4_stress）：

| 配置 | 结果 |
|------|------|
| `esp_clk_init()` 全量 + 360MHz | ❌ 数秒内随机非法指令 |
| 同上 + I2C_BIAS 两个修调 | ❌ 同样崩溃（bias 不是全部答案） |
| 只切 CPU 频率 + 360MHz | ❌ 复现（LP 域无罪，频率切换即翻车） |
| 只切 CPU 频率 + **90MHz** | ✅ 全绿，冷启动可重复 |
| ROM 时钟不切（CLKSKIP） | ✅ 全绿（40MHz 档基线） |

完整过程详见 `logs/flash555588/2026-08-21/work-record-esp32p4-nsh.md`。
