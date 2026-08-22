# 工作记录：ESP32-P4 Function-EV-Board L0 NSH

日期：2026-08-21
工具：Cursor（session `c9148026-9f28-4399-bae9-b25ba504b854`）
选手：flash555588 / 队伍仓 contest2026_359_dengfengzaojidecuipidaxuesheng
赛道：新硬件适配（ESP32-P4）

## 硬件

乐鑫 ESP32-P4 Function-EV-Board v1.4，芯片 revision v1.0（不是文档里的 P4X v3.1）。串口 COM7（CP2102N），UART0 TX GPIO37 / RX GPIO38，115200。只烧 P4，不烧 C6。必须 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`、HP 核 360 MHz。

## 结果

2026-08-21 约 19:38（UTC+8）板上出现 NuttShell：

```
NuttShell (NSH)
nsh>
uname -a
NuttX 0.0.0 dd92bcf4-dirty Aug 21 2026 19:38:54 risc-v esp32p4-function-ev-board
```

完整串口日志见同目录 `bootlog-esp32p4-nsh.txt`。可烧录镜像：`firmware/esp32p4-nsh/nuttx.bin`（esptool `--ram-only-header`，烧录偏移 0x2000）。

`help` 随后触发非法指令异常（MCAUSE=2，EPC 在 IROM `0x4000d7aa`）。L0 的 `nsh>` 已通，后续命令稳定性仍需修。

## 启动问题与处理

ROM 从 0x2000 加载 Simple Boot 镜像后会打印 SHA-256 Expected 全 0，这是 `--ram-only-header` 的正常现象。最早只打出 `N1` 就 `HP_SYS_HP_WDT_RESET` / `SUPER_WDT_RESET`：ROM flashboot 打开了 TG0 MWDT 与 LP WDT 的 flashboot 模式，超时远短于 bootloader 的 9 秒。在 `__esp_start` 入口用 MMIO 关掉 TG0 / LP WDT flashboot，并给 Super WDT 开 auto-feed 之后，启动才能继续。

关掉看门狗后，卡点依次是：`bootloader_hardware_init`（regi2c / MSPI 时钟）、`bootloader_clock_configure`、XIP 成功之后的 `esp_mspi_pin_reserve`、`bootloader_init_mem`（APM/PMP）、`esp_perip_clk_init`。Simple Boot 下 ROM 已经配过 Flash 与时钟，这些步骤暂时跳过。XIP（`map_rom_segments`）本身是成功的，日志里能看到 N2/N3 和段表。

## 关键路径

芯片公共层：`chip/esp32p4/common-espressif/esp_start.c`（关狗、面包屑、跳过危险 init）。
HAL 补丁脚本：`tools/wsl_patch_p4_simple_boot.py`（改 WSL 树里的 `bootloader_esp32p4.c`）。
编译树：WSL `/home/flash/vela-p4/nuttx`。产物拷贝：`tools/wsl_copy_firmware.py`。烧录：`tools/flash_p4_nsh.ps1`。
Windows esptool 5.3.1 做 `elf2image --ram-only-header`；不要烧 objcopy 出来的带 64MB 空洞的 bin。

## 下一步

芯片层稳定后按手册向 `open-vela/nuttx` 提 PR，板级向 `vendor_espressif` 提 PR。

## L0 稳定化（2026-08-21 晚，OpenCode 会话）

### 排查结论（help 非法指令根因）

`help`/`uname` 等命令的随机崩溃不是取指映射问题——字节级验证过四层：

1. MMU 换算正确：SIMPLE_BOOT 下解析出的 paddr 已含且只含一次 0x2000 基址，VMA 页 0x40000000 ↔ flash 页 0x20000 恒等平移；
2. 烧录镜像与 ELF 逐字节一致（两处抽样比对吻合）；
3. sections.ld / CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y / march=rv32imac_zicsr_zifencei 全部正确；
4. 多次崩溃 EPC 处「执行的编码 ≠ 链接视图」，且崩溃只在**串口输入到达后**发生 → 内存/取指流被污染，而非确定性偏移。

**根因**：Simple Boot 跳过 `bootloader_clock_configure` 时，`esp_clk_init()` 把 CPU 切到 PLL 后 MSPI 时序失配，XIP 取指进入边缘状态 → 随机指令污染。实验证据：在 SIMPLE_BOOT 分支跳过 `esp_clk_init()`、完全留在 ROM 时钟后，help/uname/free/ps/ls 连续压测零异常，多次冷启动可重复稳定（见 bootlog）。

### 当前实现（L0 基线）

- `esp_start.c` SIMPLE_BOOT 分支跳过 `esp_clk_init()`（面包屑 `CLKSKIP`），CPU 停留在 ROM 复位时钟（低于 360MHz，Phase-2 恢复精简版 clock configure + MSPI tuning 后再提频）。
- IRQ 栈 2048→8192（`CONFIG_ARCH_INTERRUPTSTACK`，防御性加大，-O0 肥帧）。
- 曾试验「drom 区间 cache invalidate + fence.i」补丁会确定性挂在 N4c，已回退；v1.0 上 ROM `Cache_Invalidate_Addr` 对 drom 窗口的行为待查。
- 勘误表核对：v1.0 共 4 条（APM-560/ECDSA_DS-837/I2C-308/RMT-176），仅 APM-560 相关但正常启动下无未授权主机访问，暂不构成主因；已记录备查。
- free/ps 报 procfs 未挂载为预期行为，`mount -t procfs /proc /proc` 即可。

### Phase-2 待办（一次一项）

1. 精简版 `bootloader_clock_configure`：PLL 提频前先把 MSPI 时钟源/分频配好（对照 IDF `bootloader_init_spi_flash` 的最小集），目标恢复 360MHz 且 XIP 稳定；
2. RTC_XTAL_FREQ_REG 写入 40MHz（消掉假警告链）；
3. `esp_mspi_pin_reserve` / `esp_perip_clk_init` / brownout 逐项加回；
4. N1/B0 调试打印收进 `CONFIG_DEBUG` 才输出。

## Phase-2 第一轮实验（同日晚，OpenCode 会话续）

### 实验矩阵（同一块板、COM7、压测脚本 p4_stress）

| 配置 | 结果 |
|------|------|
| `esp_clk_init()` 全量 + 360MHz | ❌ 数秒内随机非法指令（IDLE/nxsem_post_slow 等） |
| 同上 + I2C_BIAS 两个修调（K1） | ❌ 同样崩溃 → bias 不是全部答案 |
| 只做 CPU 频率切换（BISECT A，无 LP 域操作）+ 360MHz | ❌ 复现 → LP 域无罪，频率切换即翻车 |
| 只做 CPU 频率切换 + **90MHz**（CPLL/4, MEM90/APB90） | ✅ **全绿**：uname/free/help/ps/ls ×2 零异常，冷启动可重复 |
| ROM 时钟不切（CLKSKIP） | ✅ 全绿（40MHz 档基线） |

### 结论与 L0 决策

- `rtc_clk_cpu_freq_set_config()` 的切换机制本身在 v1.0 上没问题；**360MHz 是边际状态**——缺 IDF `bootloader_hardware_init` 的完整模拟序列（regi2c master init、PMU/PVT），只补两个 bias 寄存器不够。
- **L0 定档 CPLL 90MHz**（SELECTS_REV_LESS_V3 分频表最低档）：比 ROM 的 ~40MHz 快一倍多，且经过完整压测验证。
- `esp_start.c` 已收敛为正式实现并注释清楚限制与 TODO(P2)：SIMPLE_BOOT 下不再调用 `esp_clk_init()`，改为「regi2c 保活 + 1.1V bias + set_config(90)」。
- P4 v1.0 寄存器快照里没有 tee_reg.h（TEE 是 v3.x 特性），勘误 APM-560 在正常启动下无未授权主机触发条件，排除为主因。
- 实验备份存 WSL `/tmp/p4_backups/esp_start.c.pre-*`。

### Phase-2 剩余

1. 恢复 `bootloader_hardware_init` 完整序列后重测 180/360MHz；
2. ~~RTC_XTAL_FREQ_REG 写入 40MHz~~ ✅ 已完成（`rtc_clk_xtal_freq_update(SOC_XTAL_FREQ_40M)`，启动零假警告）；
3. ~~`esp_mspi_pin_reserve` / `esp_perip_clk_init` / brownout~~ ✅ 三项全部加回并逐项压测通过（23:29 / 23:32 / 23:36 三个构建）；
4. N1/K 面包屑收进 `CONFIG_DEBUG`。

### Phase-2 累计改动（esp_start.c SIMPLE_BOOT 路径）

- regi2c master 保活 + I2C_BIAS_DREG_1P1(_PVT)=10（提频前置条件）
- `rtc_clk_xtal_freq_update(SOC_XTAL_FREQ_40M)`
- CPU 提至 CPLL@90MHz（v1.0 分频表最低档，实测稳定上限档位）
- `esp_mspi_pin_reserve()`（MSPI 引脚占用登记）
- `esp_perip_clk_init()`（未用外设时钟门控，UART0/MSPI 保留）
- `esp_brownout_init()`（BOD 检测，NuttX 无中断变体，LVL7）
- IRQ 栈 2048→8192
- 每项均单独构建 + 全套压测验证

## Phase-2-1b：180/360MHz 冲击（22 日凌晨，负结果）

按 IDF `bootloader_hardware_init` 对 rev≥1 的流程补齐缺失项后重试 360MHz：

| 实验 | 内容 | 结果 |
|------|------|------|
| 1 | + `bootloader_init_mspi_clock()`（flash src=SPLL, core=80M） | ❌ 早期挂死（pmu_pvt 警告后无输出） |
| 2 | 实验前先 `REGI2C_WRITE_MASK(I2C_SYSPLL, OC_DIV=8)` 强制 SPLL=480M | ❌ 同点位挂死 |

结论：
- 在 Simple Boot 的 ROM 时钟阶段对 MSPI 时钟源/SPLL 做任何重配都会立即破坏 XIP——v1.0 POR 下 SPLL 域不可假设可用（与 ECO0 注释「POR PLL 频率异常」同族问题，但 v1.0 表现是「动就死」而非「频率高」）。
- 正确路径应是完整重建时钟树（等价 `rtc_clk_init`：CPLL/SPLL 使能+校准+MSPI 时序调优一体），散件 LL 调用不可行；需要 JTAG/寄存器快照辅助定位，列入后续专项。
- 已回滚到 90MHz 已知良好基线（overlay 为准），压测复验全绿（00:09 构建）。
- 实验备份：WSL `/tmp/p4_backups/`。

## Phase-3 整理（同日深夜）

- `esp_start.c` 的 N 系列面包屑收进 `CONFIG_DEBUG_FEATURES`（`sb_progress` 宏，默认静默，错误信息保留）；bootloader 阶段 B0-B3 因 HAL 编译单元看不到 NuttX 调试配置保留常开（4 字符，无碍）。收敛后固件压测全绿（23:47 构建）。
- 新增 `board/esp32p4-function-ev-board/README.md`：构建/镜像/烧录/串口验证/当前配置/已知限制完整指南。
- `.gitignore` 增补：`firmware/**/nuttx`（8MB ELF）、`.depend`、`Make.dep`、`logs/your-github-login/`。
- 清理 `board/esp32p4-common/` 内 .o/libboard.a/.depend 编译垃圾。
- 提交由选手手动执行：`git add` chip/ tools/ board/ firmware/*/(bin+bootlog) logs/flash555588/ .gitignore xml；确认无 ELF/map/.o 混入后 commit+push。
