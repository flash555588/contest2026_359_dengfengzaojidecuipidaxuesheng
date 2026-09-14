# ESP32-P4 v3.x → openvela NSH 工作交接

交接日期：2026-09-07。本文是工作状态记录，不是新固件的硬件验收报告。

## 1. 最新用户决定：下一位执行者必须先读

用户明确要求：基于提供的 streetartist NuttX 源码包，开发成面向 ESP32-P4 v3.x 的真正 openvela NSH。

用户进一步确认：“当前，他的 nuttx 版本已经验证完全稳定，我需要你沿着这个开发到 openvela”。这已经回答了是否需要重复原版实板验收的问题。

因此，后续直接推进本地移植，不再以“streetartist 原版没有在本轮重新实板验收”为理由阻断工作，也不要重复询问同一个问题。原版稳定性属于用户确认；新 openvela 固件是否稳定，仍须由其自身的实测证明。

当前范围：单核、UART0 控制台、NSH。暂不加入 PSRAM、显示、触摸、SMP、相机、desktop 或 ESPHome/network。最终必须使用 openvela 的内核与 apps，不能把 streetartist 整个内核改名冒充 openvela。

目标已从此前 v1.0 改为 v3.x。不得沿用 v1.0 的移植目标；但这不意味着旧实板已实际更换，后续烧录前仍须确认实际芯片版本。

本次执行者选择两个内容完全相同的 master ZIP 作为最小 NSH 来源，并使用已核验匹配的 apps `(1)` 包作为参考。feature 分支保留为后续差异参考，不混入当前最小 NSH。

## 2. 当前准确进度

已经完成源码包身份核验、独立目录创建、六个精确 Git 对象的检出、HAL/工具链构建依据读取，以及部分接口差异分析。

**尚未把 BSP/启动/中断代码迁入新的 openvela 源树。新的 openvela 内核和 apps 均无工作区改动，尚无 `.config`、ELF 或 BIN。没有执行新固件烧录、复位、冷启动或 NSH 实板验收。**

交接复核时，新的 `golden/nuttx/.config` 已存在，大小 46,773 字节，显示 P4 Function EV Board、400 MHz、Simple Boot、REV_MIN=301、REV_MAX=399、`nsh_main`。当前会话没有可核验的该配置生成命令/成功日志，不能把它当作已验证的构建结果，也不能假设它完整等于原版 nsh defconfig。后续应核对后再用。golden 目录没有 ELF/BIN。

已启动过的 bootstrap 和工具链下载工具会话，交接时无法再通过会话 ID 查询退出码。通过文件和 Git 实际状态确认六个检出已到位；通过进程检查未发现对应 bootstrap、下载或构建任务仍在运行。不要在交接后假定后台正在继续编译。

## 3. 工作区与关键路径

Windows 工作区：`C:/Users/flash/Desktop/新建文件夹 (2)/vlae`

WSL 发行版：`Ubuntu-22.04`

唯一的新开发根目录：`/home/flash/openvela-p4-v3-nsh-20260907`

| 子目录 | 精确 HEAD | 交接时状态 |
|---|---|---|
| `golden/nuttx` | `2f1387d56eb04ad2599baca58a3fa2380cdaaedb` | Git clean；另有未受 Git 跟踪的生成配置 |
| `golden/apps` | `88827afd368d4bbb4802b96ed44d9582f85b2f92` | Git clean |
| `openvela/nuttx` | `dd92bcf425738734d1b8aed09c2bd4dbe3f2e438` | Git clean；未移植 |
| `openvela/apps` | `dcc6a95c3b323e533c98fde8fb209f99e24f0fdd` | Git clean；未构建 |
| `hal-reference` | `8d0a898910084206721a0892ab093021bca1496a` | Git clean；未应用包内 HAL patch |
| `hal-reference/components/mbedtls/mbedtls` | `582ff482038db6e4010dbf6f943d97b05ad06ea5` | Git clean |

这些仓库均从现有本地仓库的精确 Git 对象检出，没有复制旧实验工作区的脏文件。streetartist 两个对象事先已逐文件匹配 ZIP。HAL 的 mbedTLS SHA 由 HAL 精确对象的 gitlink 反向得到，不是借用 apps 的 mbedTLS 版本。

`cases/` 已创建，交接时为空。不要为未执行的实验伪造日志或填充占位固件。

## 4. 包身份与重复文件

所有包位于 `C:/Users/flash/Downloads`。下面 SHA 先从 ZIP 注释读出，再进行本地对象核验，未先套用旧交接中的 SHA。

| 文件 | ZIP 注释中的 commit | 核验 |
|---|---|---|
| `esp32p4_nuttx-master (3).zip`、`esp32p4_nuttx-master (2).zip` | `2f1387d56eb04ad2599baca58a3fa2380cdaaedb` | 两包字节相同；27,096 个文件内容和模式全部匹配本地该 Git 对象 |
| `esp32p4_nuttx-feature-esp-hosted-c6.zip` 及其 `(1)`、`(2)` | `88f2644ee73207961fad3a9de14dda9c654abe13` | 三包字节相同；CRC 完整读取无错误；未取得本地对应 commit 对象作独立匹配 |
| `esp32p4_nuttx_apps-master (1).zip` | `88827afd368d4bbb4802b96ed44d9582f85b2f92` | 5,854 个文件内容和模式全部匹配本地该 Git 对象 |
| `esp32p4_nuttx_apps-master.zip` | `d6a64c404ee5c277fd7ab709e0ba11e9ce9f2b88` | 与 `(1)` 不同；CRC 完整读取无错误；无本地对应 commit 对象 |

包 SHA-256：

```text
master kernel:
0cb78e69886e7c3332f9eda97a2135d6a53c7b9e3a1b227ef520aae859e0beff
feature kernel:
f9e7ade0eda96628dc11fa287d76c03215039f1625d01b7ff1dc7e872bf32345
apps master (1):
0be03619d616b7fd8d73896966126835bc4d38f82ee90097327aa54d4883d8a9
apps master:
b94e440d256e811dda413afd2801f17d78a2b08655a826a5afc9e006d360800b
```

两版内核的完整内容比较：feature 新增 9 个文件，修改 16 个文件，无删除。涉及 SDMMC、中断、I2C、SMP、DSI、相机、board bringup、Flash 与 v4l2，不是单纯的 C6 网络差异。

之前 GitHub 公开页面返回 404，但授权 `gh api` 能读取两个 streetartist 私有仓库元数据。现在本地包及检出已可用，不应再因公开页面 404 而停止，也不得改用其他 fork 替代。

## 5. 已创建的 Windows 文件

源码核验清单：

`C:/Users/flash/Desktop/新建文件夹 (2)/vlae/openvela-golden-baseline/intake-20260907-01/source.json`

该清单保存全部包哈希、Git 文件比较、源码构建依据和当时的阻断项。**其中 G0 的 BLOCKED 是用户最后确认稳定性之前的历史快照，不代表当前授权状态；当前以本文第 1 节为准。** 不要为了消除旧状态而篡改该历史清单。

独立目录初始化脚本：

`C:/Users/flash/Desktop/新建文件夹 (2)/vlae/openvela-golden-baseline/v3-nsh/bootstrap.sh`

脚本已经启动执行，其预期的六个检出均已复核到位。它故意拒绝覆盖已存在的根目录，因此不要原样重跑来“继续”。后续应在现有新目录中推进，或为另一轮实验创建另一个明确命名的新目录。

## 6. 工具链：有一个未完成下载，必须处理

包内 CI 配方：`golden/nuttx/tools/ci/docker/linux/Dockerfile:253` 指定 xPack RISC-V GCC `14.3.0-1`；第 411 行指定 `esptool==5.2.0`。P4 文档的 `14.2.0-3` 只是另一个示例，不要混为同一套工具链。

本轮选择准备源码 CI 使用的 xPack 14.3.0-1，以减少现有 GCC 15.2 带来的语言默认值等差异。这是本轮的可复现工具选择，不应冒称已独立核实作者实板运行时用的正是这一套。

官方来源：[xPack v14.3.0-1 release](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/tag/v14.3.0-1)。发布资产的元数据已实际查询。

```text
下载目标：
/home/flash/openvela-p4-v3-nsh-20260907/downloads/xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz

官方完整大小：414461685 字节
交接时实际大小：188364939 字节
完整文件预期 SHA-256：
be1768ef22789f4d9c41384e0261996f51724b84c2efa940d975dd7d9938c726

下载 URL：
https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v14.3.0-1/xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz
```

**该 tar.gz 是不完整下载，尚未解压或安装。继续下载或另存完整文件后，必须校验完整大小和 SHA-256，再用于构建。不要把当前 180 MiB 文件当作完整工具链。** esptool 5.2.0 尚未安装；应使用实验专属 Python 虚拟环境，不改全局环境。

现有可用工具仅作为环境事实：`/home/flash/vela-p4/riscv32-esp-elf/bin/riscv32-esp-elf-gcc` 为 `15.2.0`，发行标记 `esp-15.2.0_20251204`；`/home/flash/.local/bin/esptool.py` 为 `4.8.1`。另有 `/home/flash/xpack-riscv-none-elf-gcc-15.2.0-1`，不是本轮选定版本。

## 7. 已核实的源码构建依据

参考目标为 `esp32p4-function-ev-board:nsh`，不是旧实验树里的 `esp32p4x-function-ev-board:nsh`。

原 defconfig：`golden/nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board/configs/nsh/defconfig`。SHA-256 为 `4824e0d4bef0815ae874137f35ed91d2960f3d3a8687ffe201f1134c7662bb59`。它指定 UART0 控制台，NSH 入口 `nsh_main`，dynamic minimal vector table，17 个 user interrupts。

master 缺省最小芯片版本是 **v3.1**，最大值 399。不能将它描述为已覆盖所有 v3.x，尤其 v3.0。若需 v3.0，必须独立定义 profile 并核对相应 MSPI workaround，不能只改镜像头的 REV_MIN。

以下路径都相对于新的 `golden/nuttx`：

| 依据 | 位置 |
|---|---|
| HAL URL/默认 SHA | `arch/risc-v/src/common/espressif/Make.defs:244`，CMake 也指定相同 SHA |
| 原版 HAL patch 应用 | 同文件第 292 行起 |
| P4 ROM/register 版本分支 | `arch/risc-v/src/esp32p4/hal_esp32p4.mk:151` 与第 176 行起 |
| section linker script 版本选择 | `boards/risc-v/esp32p4/esp32p4-function-ev-board/scripts/Make.defs:39` |
| 芯片 revision 与范围 | `arch/risc-v/src/esp32p4/Kconfig:10` 起 |
| 默认 CPU 频率 | `arch/risc-v/src/common/espressif/Kconfig:158` 起，v3.x 默认 400 MHz |
| Simple Boot 镜像生成 | `tools/espressif/Config.mk:251` 起，使用 `--ram-only-header` |
| P4 Simple Boot 偏移 | `tools/espressif/Config.mk:134` 起，`0x2000` |
| flash 模板 | `tools/espressif/Config.mk:282` 起，缺省波特率 921600，没有显式 `--before`/`--after` |

包内自带 HAL patch：`arch/risc-v/src/esp32p4/patches/esp-hal-3rdparty/0002-openvela-esp32p4-smp-start.patch`。SHA-256 为 `43657ffea1bd9fa3dbe806ec6ae1b9a7693a453262cc677343815d96b21396d1`。它包含 HAL sdkconfig、critical section 和 RTC calibration 初始化改动。虽然文件名含 openvela，它是输入包自带内容，不是本轮新编写的迁移补丁。新的 `hal-reference` 目前仍为未打 patch 的干净对象。

Make 原配方会在 HAL 获取路径调用 `clean/reset`，flash target 也有条件启用的 eFuse 操作。不要不经检查运行这些副作用路径；预先准备独立 HAL、保留原版要求的 patch，并确保安全配置未启用。任何修改 eFuse/安全状态均不在授权范围。

apps 的 LVGL 默认版本为 9.2.2，apps mbedTLS 默认 3.6.2；它们不是 HAL mbedTLS gitlink 的替代值，当前最小 NSH 也不需要为了桌面去启用这些库。

## 8. 最小移植的技术方向与已发现风险

建议保持新的 openvela 通用内核和 apps，针对 P4 导入包内 SoC/BSP，以及匹配的最小 Espressif 驱动实现。可放在 P4 专属子目录中，仅让 P4 编译选用，以避免替换整个 `common/espressif` 后影响已有 C3/C6 等芯片。该目录方案尚未实施。

参考 NSH 的固定编译单元包括：`esp_allocateheap.c`、`esp_start.c`、`esp_idle.c`、`esp_irq.c`、`esp_gpio.c`、`esp_rtc_gpio.c`、`esp_libc_stubs.c`、`esp_lowputc.c`、`esp_serial.c`、`esp_systemreset.c`；周期 tick 使用 `esp_timerisr.c`；入口/向量为 `esp_head.S`、`esp_vectors.S`；P4 专有文件包括 `esp_chip_rev.c`。还需逐个跟进匹配头文件、链接脚本和配置依赖，不能仅复制这个名称列表即宣称完整。

关键接口差异：参考 IRQ 已采用五参数 `esp_setup_irq`、HAL interrupt handle 与 minimal DEMUX；旧 openvela 使用三参数分配器与 `irq_attach` 模型。不得只换 HAL os.c 而保留不匹配的 IRQ glue，也不要直接移入上一轮实验性 handle-map 重写。

只读子任务还报告了两项需要下一位亲自复核的线索，尚无实现或构建验证：

1. 部分参考文件引用 `<nuttx/debug.h>`，目标 openvela 只有 `<debug.h>`；应做小范围 include/API 兼容。
2. 参考通用 `riscv_initialize.c` 具有 `riscv_soc_initialize()` 启动 hook，连接 P4 `esp_start.c` 的 HAL startup；目标 openvela 的通用文件缺少此 hook。应核对调用顺序，并只加 P4 必需的声明/调用，不能整份覆盖 openvela 的 per-CPU 初始化实现。

原版 `esp_start.c` 的 MMU/extmem/cache 接口也必须对照所锁定 HAL 实际构建验证。不要复用旧实验里跳过 clock、内存保护或强行补镜像 digest 的办法来掩盖问题。

只读子任务因执行资源额度耗尽而未交付完整差异报告；前述线索不是完成的代码审查，也不能标记为已修复。

## 9. 下一步具体执行顺序

1. 阅读本文，按用户确认的原版稳定性继续，不再卡原版重新实板验收。复核新目录状态及 golden `.config`，不要覆盖其他会话/用户的新改动。
2. 完成工具链下载校验，准备隔离 esptool 环境，固定所有有效构建输入。新的 openvela 使用上述精确内核/apps 对象，不复用旧脏树。
3. 列出最小 P4 source import manifest，每个文件记录原仓库、commit、原路径、目标路径和内容 SHA。以包内代码为来源，用最小补丁接入 openvela 的 Kconfig、Make、board target 和 HAL。
4. 逐项解决启动 hook、头文件和 OS API 差异；IRQ/UART/timer 必须成套兼容。不要同时改显示、网络、SMP 等。
5. 首先构建 rev3.1–3.99 的单核 UART0 NSH profile；若明确需要 v3.0，再另立 profile。每个失败保存日志、定位真实编译错误后再修改，不提前编造 diff。
6. 做 fresh build、diff/style 检查及适配层测试。创建独立 `case-0001/`，保存 source manifest、patches、resolved.config、build.log、ELF、BIN、map、readelf、nm 和 SHA-256；不得用旧 ELF 解析新故障。
7. 解析新镜像的 entry/header/offset/digest/ROM linker 与实际配置，给出可审查的刷写方案。只有具体设备与操作获准后才能执行；下载模式进入与应用启动分别验收。
8. 验收结果分别记录 DOWNLOAD_ENTRY、FLASH_WRITE、FLASH_VERIFY、APP_COLD_BOOT、APP_RESET_BOOT、NSH，不用编译成功或 Hash verified 代替启动成功。

后续阶段依次为 PSRAM、Display、Touch、SMP、CSI、Display+Camera、desktop/LVGL、ESPHome/network；当前均不启用。

## 10. 旧目录与安全边界

`/home/flash/openvela-p4-port-clean-20260907/openvela/nuttx` 是此前实验树，含未提交改动。它仅作为本轮 Git 对象的本地来源；不得继续在那里叠补丁。Windows `openvela-port-clean/build-l0.sh` 仍指向该旧树，不能当作新目录的构建脚本执行。

`/home/flash/openvela/nuttx`、`/home/flash/vela-p4/nuttx`、`/home/flash/vela-p4-dev` 及其他旧试验目录均不要清理、覆盖或重置。旧 ELF/BIN 和所谓编译成功记录不构成本轮成果。

本轮没有硬件连接、烧录、GDB load/reset、eFuse、安全启动或下载安全配置变更；没有 commit/push/merge/rebase/reset 操作。工具链只下载了部分数据，没有全局安装。

此前远端 COM23、localhost:13333 等信息没有在本轮重新核验，不能直接作为现用连接参数。用户已授权本地移植，但不要把它扩大成未明确确认的远端设备操作。

文件编辑使用 apply_patch；保留所有现存脏改动。所有联网先读取并遵循 `web-access` skill；不要把凭据写入文件。驱动与构建继续遵循适用 skills，但 S3 专属的引脚、ROM 和启动经验不能直接用于 P4。

## 11. 执行记录摘要

实际执行：ZIP 哈希与完整内容核验；本地 Git 精确对象/状态检查；新隔离目录 bootstrap；包内构建配方读取；官方工具链发布页与资产元数据查询；工具链部分下载；新目录交接复核；本文生成。

尚未执行：新 openvela BSP 导入、API 适配 patch、配置生成、编译、链接、elf2image、烧录、复位、断电冷启动及 NSH 验收。golden `.config` 虽在交接时存在，其生成过程未在本轮可靠记录中确认。

下一位的工作不是重新解释“可以移植”，而是在上述新目录中，沿已确认的 streetartist 软件参考，完成真正的 openvela v3.x 最小 NSH 移植，并如实区分软件验证与新固件的硬件验收。
