# ESP32-P4 openvela 系统移植工作交接

交接日期：2026-09-07。此文件是工作记录，不是硬件验收报告。

## 最新增量（优先于下文历史状态）

最新快照 `openvela-port-clean/l0-compile-only-H9ADcZ`：新增 `irq-demux-required.patch`，P4 DEMUX 挂接失败时停止初始化，不继续开启全局中断；完整构建和失败注入测试通过。这是错误路径修复，不是原始 WDT 故障根因证明。UART0 遗留路由清理仍待证据，未执行远端硬件操作。

当前最新快照：`openvela-port-clean/l0-compile-only-EkFORn`。新增 `irq-enable-order.patch`，去掉 P4 CPU 中断初始化中的提前全局启用，保留异常处理与 DEMUX 挂接后的统一启用。完整构建、中断生命周期测试和实际 C 初始化顺序测试通过；快照内有 `irq-init-order-test.txt`。UART0 遗留路由清理仍未确认、未加入，不宣称目标板故障修复。本轮没有远端硬件操作。

最新快照为 `openvela-port-clean/l0-compile-only-I5WJyF`。根据对方导出的工作记录进一步核对后，已补齐 P4 的 SoC 初始化调用：openvela `up_initialize` 在串口/网络初始化后调用 `esp32p4_soc_initialize`，后者进入 HAL `g_startup_fn`。`test_soc_init.py` 验证了实际 ELF 调用链，完整构建及快照检查通过。UART0 遗留路由清理仍需继续核对，未因此宣称启动问题已解决。独立 `boot-candidate-301-399` 是旧产物，不包含此修复，不能拿它验证新路径。本轮没有操作远端设备。

最新快照为 `openvela-port-clean/l0-compile-only-lqqbKI`（2026-09-08），在 P4 Simple Boot 解析器中加入映射段缺失即返回失败的检查，防止错误镜像继续配置 MMU。完整构建、启动顺序、TCM、真实 C 扫描、摘要布局和中断主机测试通过。当前 BIN SHA256 `a234ee8b592d248e2d3f2f63f7e2fb8a40e25b9826f10bab0655b8a952811341`，ELF SHA256 `896ca99b4ce87cab5b46f884aca1a27cf205b4f8c0d8e4b4c058cb548ad71459`，入口 `0x4ff44820`。仍未进行远端实板测试，不宣称解决 ECO7 启动问题。

最新快照更新为 `openvela-port-clean/l0-compile-only-Tl4y4f`：P4 启动解析器新增按 header 标志跳过可选 32 字节摘要的支持，完整构建通过。保留原始 BIN，同时生成独立 `DIGEST-EXPERIMENT-NOT-QUALIFIED.bin`（SHA256 `b34fb2cb75a11753b165a3ff1506624ebfd1a187140a7d75bd581b47a6dbe2ef`）。两者来自同一 ELF，主机测试确认映射偏移与内容不变、损坏摘要被拒绝，esptool 对实验 BIN 报告有效校验和及摘要。生产构建规则仍输出原始格式；转换仅作为显式实验。未进行实板测试，不宣称 ECO7 WDT 问题已修复。下述快照均为历史版本。

更新：最新快照为 `openvela-port-clean/l0-compile-only-XF0XzT`。新增 `start-clic.patch`，限定 P4 设置 CLIC mtvec/mtvt，并在 Simple Boot 的首个 HAL 调用前清零 BSS；没有跳过时钟或内存保护。完整构建、实际机器码初始化顺序测试、TCM 测试、中断测试和映射检查通过。BIN SHA256 为 `4e0b870d8f3c8cfc24149338bd74742450307e846c8650e2d2b0546822a8745f`，入口 `0x4ff44820`。仍未修订原始无附加摘要的打包方式，未进行目标板测试，不能宣称旧 WDT 故障已修复。下面的 oDC0Wa 是前一快照。

交接后工作区已完成 P4 HAL 中断句柄分配、DEMUX、tick/UART 调用者及直接 HAL free 清理接入；`openvela-port-clean/test_irq_bridge.py` 主机测试已重新执行通过。具体当前实现和补丁清单以 `openvela-port-clean/README.md`、`build-l0.sh` 和实际源码为准。

随后发现并修复 openvela Simple Boot 对 P4 TCM 地址分类的遗漏：`start-tcm.patch` 将 TCM 纳入 RAM 计数并从未知地址排除。`test_start_tcm.py` 的 P4 边界及非 P4 分支测试通过。完整 Make 编译/链接已成功，不再是下文记录的中断编译阻断状态。

当前快照：`C:/Users/flash/Desktop/新建文件夹 (2)/vlae/openvela-port-clean/l0-compile-only-oDC0Wa`，包含 ELF、BIN、实际配置、构建日志、内核/HAL diff 和未跟踪移植文件归档。BIN 268540 字节，SHA256 `350bf325b8508851444d8258485c97c15613721f93dfaf2bedc9af47846faeda`；ELF SHA256 `fc1462cac2c7752bc530ef9dda29bf3e9fc2fa1a19b64306672b3a9b68f5905a`，入口 `0x4ff44824`。

实际配置已启用 `CONFIG_ESPRESSIF_USBSERIAL` 与 `CONFIG_OTHER_SERIAL_CONSOLE`，UART0 console 关闭；修订下限仍为 301。不要沿用下文旧 UART0 描述。`check_image_layout.py` 检查两个 TCM RAM 段、RAM XOR、入口与 ELF 一致、DROM Flash 偏移 `0x10020`、IROM 偏移 `0x20000` 及对齐，全部通过。

该 BIN 仍为 `hash_appended=0` 的原始 Simple Boot 格式，未完成 ECO7 摘要兼容性及实板冷启动验证。快照不是批准烧录的发布包；没有远端设备操作。下一步应针对启动代码和 ROM 格式做验证，不把本地编译成功当作移植验收。

## 1. 用户目标与最终确定的方向

目标是在用户朋友的 ESP32-P4 设备上完成真正的 openvela 系统移植，并最终集成用户当前工作区中的新桌面 UI、相机交互、Home Assistant、番茄钟、ESPHome 等软件。

不要把 Apache NuttX 加 UI 或修改品牌名称当作 openvela 移植完成。openvela 是目标系统；streetartist 的两个仓库是用户确认曾在目标设备上运行的硬件参考。具体可运行固件对应的完整配置、工具链、HAL 工作树和 ELF 尚未取得并复现。

参考仓库：

- https://github.com/streetartist/esp32p4_nuttx
- https://github.com/streetartist/esp32p4_nuttx_apps
- 用户项目：https://github.com/flash555588/contest2026_359_dengfengzaojidecuipidaxuesheng

当前优先级：openvela 最小 NSH 的正确启动、中断和调度适配；然后冷启动及重复复位验收；随后 PSRAM、SMP、显示、触摸，最后相机与新桌面。不要继续先扩 UI、再要求对方反复试刷。

## 2. 本机与隔离源码位置

Windows 工作区：`C:/Users/flash/Desktop/新建文件夹 (2)/vlae`。

WSL 发行版：`Ubuntu-22.04`。

当前实际移植隔离区：`/home/flash/openvela-p4-port-clean-20260907`。

| 目录（相对隔离区） | 用途 | 固定提交 |
| --- | --- | --- |
| `openvela/nuttx` | 目标 openvela 内核，目前有移植修改 | `dd92bcf425738734d1b8aed09c2bd4dbe3f2e438` |
| `openvela/apps` | 配套 openvela apps | `dcc6a95c3b323e533c98fde8fb209f99e24f0fdd` |
| `reference/nuttx-pinned` | streetartist 对应的干净参考 | `2f1387d56eb04ad2599baca58a3fa2380cdaaedb` |
| `reference/apps-pinned` | streetartist 对应的干净参考 | `88827afd368d4bbb4802b96ed44d9582f85b2f92` |

openvela 提交来自本地 `dev-ai-contest-2026` 来源，不是最新 upstream 的声明。四棵树最初均干净检出；现在目标内核和其 HAL 已有本任务修改。

当前 HAL：目标内核下 `arch/risc-v/src/esp32p4/esp-hal-3rdparty`，固定 `8d0a898910084206721a0892ab093021bca1496a`，另有本任务兼容补丁。该 SHA 是 streetartist 原始构建规则的默认值。早期失败候选使用过不同的 `78c092909fca38d1e2ccf767b5eff66bddc5c789`，不得混淆。

Windows 侧脚本、补丁及日志目录：`C:/Users/flash/Desktop/新建文件夹 (2)/vlae/openvela-port-clean`。

其它树的性质：

- `/home/flash/openvela/nuttx`：原始工作树，已有约 79 个文件的未提交修改，不要清理或直接覆盖。
- Windows `openvela_port`：历史 Git worktree，提交 `95eb9ac7fc9c3f601b49e10ff013ff6a03531383`，父提交 openvela/dev `322ad9f11c333315356e3e4c8f3b64e5121cfc6d`。
- `/home/flash/vela-p4-dev`：历史构建副本，Git 链接失效，不能仅凭目录名推断可追溯基线。
- 隔离区 `reference/nuttx` 可能留有一次被中止的复制半成品；实际参考请用 `nuttx-pinned`。

## 3. 当前移植已经做了什么

`historical-l0-reference.patch` 从历史 L0 提交导出；在目标内核应用时排除了：

- `ESP32P4_L0.md`。
- `arch/risc-v/src/common/espressif/esp_start.c` 的历史修改。
- `arch/risc-v/src/esp32p4/patches/esp-hal-3rdparty/*`。

排除原因：历史启动补丁跳过了 P4 时钟初始化、内存保护等流程；旧 HAL 补丁有提前返回和绑核语义改变。不要为了编译通过重新整体应用它们。

当前已做的小范围兼容修改：

1. `esp_start.c`：P4 不包含不存在的旧芯片 `soc/extmem_reg.h`、`soc/mmu.h`；P4 Cache HAL enable/disable 传入层级参数。没有因此跳过时钟或内存保护。
2. `drivers/drivers_initialize.c`：`usrsock_rpmsg.h` 的包含与其调用点使用相同的 `CONFIG_NET_USRSOCK_RPMSG_SERVER` 条件。
3. `esp_allocateheap.c`：将未声明的 `ets_printf` 改为已有头文件声明的 `esp_rom_printf`。
4. P4 `Make.defs`：显式 `-std=gnu17`，解决 GCC15 默认标准下 HAL `ATOMIC_VAR_INIT` 错误，未直接改写原子实现。
5. HAL `platform/os.h`：不可用的 `nxsched_usleep` 改为局部零信号量上的 `nxsem_tickwait_uninterruptible` 超时等待，保留 ticks 单位；不是普通可被信号提前结束的 `usleep`。仅编译验证，运行时等待语义及边界仍需测试。
6. HAL `platform/os.c`：补 `<fcntl.h>`；`nxtask_init` 改用 openvela 的 `posix_spawnattr_t` 签名，保留设置 affinity 后再 `nxtask_activate` 的顺序。属性初始化的错误码转换已加入，setter 的错误处理仍值得审查。
7. `esp_irq.c` 旧桥接函数：检查 `irq_attach` 返回值，失败后 `esp_teardown_irq` 回收 CPU 中断，不继续启用。

对应独立补丁：`heap-rom-api.patch`、`gnu17.patch`、`hal-delay.patch`、`hal-task-api.patch`、`hal-fcntl.patch`、`irq-attach-rollback.patch`。

注意：部分内核修改直接位于 WSL 工作树，`l0-tracked.diff` 不保证包含最后一轮全部修改；新加的未跟踪移植文件也不会出现在普通 `git diff` 中。移交或搬迁前必须重新核对 `git status --short`，分别保存目标内核、HAL 的 diff 和未跟踪文件。不要把一个旧 diff 文件当作完整可复现交付。

## 4. 当前配置、构建方法与结果

板级配置：`esp32p4x-function-ev-board:nsh`。当前解析结果为单核、CPU 400MHz、UART0 控制台（GPIO37 TX / GPIO38 RX）、修订下限 301。它不是已经覆盖 v3.0 的配置，也不是 USB Serial/JTAG 控制台配置。

工具链：`/home/flash/vela-p4/riscv32-esp-elf/bin/riscv32-esp-elf-gcc`，15.2.0。主机已有 Kconfiglib `olddefconfig`。显式设置 PATH，避免把含空格括号的 Windows PATH 非引用展开进 bash。

在 Windows 工作区执行实际构建：

```powershell
wsl -d Ubuntu-22.04 -- bash openvela-port-clean/build-l0.sh
```

构建输出在 `openvela-port-clean/build-l0.log`。脚本禁用网络 Git 协议及下载命令，使用本地固定 HAL 缓存，并检查/应用三个 HAL 兼容补丁。

不要直接重跑 `configure-l0.sh`：它目前是首次应用脚本，不保证重复执行幂等。`prepare.sh` 也不是完整的恢复/重建工具；源码已存在时先检查状态，不能覆盖当前移植工作。

最近一次完整构建仍失败，错误集中为：

- `struct intr_adapter_to_nuttx` 未定义。
- `esp_get_handle` 无声明/实现，并导致返回类型错误。

此前延时接口、原子初始化、任务创建签名和文件常量相关报错在后续构建中已消失。**没有成功生成该 openvela 移植树的 ELF/BIN，没有实板启动验收。**

最后一轮只修改了中断挂接失败回收，并运行了主机测试，未再次完成固件构建。

## 5. 当前最重要的技术阻断：中断模型不一致

不要只补结构体或写一个假的 `esp_get_handle`。

参考树 `arch/risc-v/src/common/espressif/esp_irq.c`：

- 通过 HAL `esp_intr_alloc_intrstatus` 分配中断，建立 `g_handle_map[cpu][irq]`。
- 使用 HAL 的 CPU handler table 和 `ESP_IRQ_DEMUX` 分发。
- `up_enable_irq` / `up_disable_irq` 经 HAL handle 操作。
- `esp_get_cpuint` 从 handle 查询；释放需要与分配体系一致。

当前 openvela 移植树的旧实现：

- 用 `g_cpuint_map` / `g_irq_map` 自己管理分配。
- `esp_setup_irq_with_flags_intrstatus` 忽略 `intrstatusreg`、`intrstatusmask`，不是完整的状态过滤实现。
- `esp_timerisr.c` 和 `esp_serial.c` 仍采用先分配、再 `irq_attach` 的旧调用方式。
- HAL 的 `intr_alloc.c` 虽已编入，但直接让 OS wrapper 使用它，会和旧分配器混用。

HAL `nuttx/src/platform/os.c` 还需重点审查：`esp_os_intr_free` 将 `intr_handle_t` 转成 `esp_os_intr_handle_t`；分配失败后的 adapter 内存释放、可选 ret_handle 处理以及中断启用时机，不能默认原实现全部正确。

推荐下一步：先写出一套一致的分配/分发/释放合同，再将 P4 的分配器、demux、头文件、定时器/串口调用者成套适配。尽量限制为 P4，不无条件改变其它 Espressif 芯片。逐项验证失败回滚、重复分配、状态掩码、禁用后启用、释放、SMP core ownership；不要用全局开关绕过这些语义。

已新增测试：

```powershell
wsl -d Ubuntu-22.04 -- python3 openvela-port-clean/test_irq_bridge.py
```

它提取实际桥接函数，以主机 mock 验证分配失败、挂接失败回收、禁用标志和启用顺序，四个场景通过。不覆盖真正硬件、HAL handle 生命周期、共享中断或 SMP。测试与函数边界绑定，后续重构需同步调整。

## 6. 之前的目标板故障与调试证据

目标 ROM 日志：`ESP-ROM:esp32p4-eco7-20260109`。不能仅凭 ECO7 banner 确定产品 silicon revision 或完整板卡 BOM。

早期候选在对方板上表现为：第一次在另一正常固件运行后刷入可能亮屏，随后复位反复 `HP_SYS_HP_WDT_RESET`；自动进入下载困难，需要 BOOT 强制下载。没有证据证明硬件损坏或 eFuse 被修改。

早期失败工程是 NuttX/apps 快照加用户项目 27 个补丁，HAL 换成 `78c0929...`，并非当前正在做的纯 openvela 移植树。

摘要问题已有独立证据：ROM 报出的 Calculated 与交付镜像 RAM 部分 SHA256 完全一致，Expected 是后续映射填充字节。后来在填充空间中插入摘要并同步修改启动解析器跳过摘要，保持映射段偏移不变，ROM 摘要报错消失，但 WDT 循环未解决。该实验不能被默认为已验证的正式打包流程。

旧实验目录与性质：

- `isolated-v3-20260906`：旧桌面，301–399 候选。
- `isolated-v3plus-20260906`：旧桌面，300–399 候选；桌面 BIN 已因 ECO7 启动失败撤回，见 `BOOT-FAILURE.md`。
- `boot-diagnostic`：摘要实验及 D2/D3 启动探针，未解决启动问题。
- `isolated-current-v3plus`：当前应用快照的实验集成，曾因 SC2336 驱动未启用链接失败，不是可交付固件。

这些旧 BIN、压缩包仍可能留在 Windows 工作区。不要当作“已移植成功”或继续让对方盲刷。`intermediate.bin` / `ram-only-intermediate.bin` 不可作为交付镜像。

JTAG 曾读到的可靠寄存器：

```text
pc      = 4ff40000
sp      = 4ff469d0
mstatus = 00001801
mtvec   = 4ff40003
mcause  = 30000001
mepc    = 4ff40000
mtval   = 4ff40000
dcsr    = 400090c3
pmpcfg0 = 9d9d9b9b
pmpcfg1 = 808b8d80
pmpcfg2 = 00008b8d
pmpcfg3 = 00000000
pmpaddr0..9 = 09ffffff 0ffc3fff 13f03fff 23f03fff
               13fc0000 13fd0e60 13ff0000
               23fc0000 23fd0e60 23ff0000
```

按普通 PMP TOR 解码，SRAM `0x4ff00000..0x4ff43980` 为 RX，后续到 `0x4ffc0000` 为 RW；向量表和入口都落在 RX 区。不能因此简单归因为“SRAM 没有执行权限”，也不能清空 PMP 试错。可能是重复异常覆盖了首个错误，仍未定位根因。

实际内存入口 `0x4ff43514` 的 16 个字与 D2 完全一致，而不是 D3。对方下载的 D3 文件哈希虽正确，但未取得证明已烧入并重新加载 D3 的完整日志。不要拿 D3 ELF 解释该 D2 现场。

## 7. 远程硬件连接与权限边界

对方 Windows 用户目录：`C:/Users/wenji`，串口曾为 `COM23`，USB `VID_303A/PID_1001`，JTAG 接口 `MI_02`，串口 `MI_00`。

对方 OpenOCD：
`D:/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250707/openocd-esp32/bin/openocd.exe`。

UU 曾配置映射：本机 `127.0.0.1:13333` -> 对方 `127.0.0.1:3333`。本机实际检查确认只监听 loopback。映射、服务、设备当前是否在线必须重新检查，不能假设持续有效。

本机 GDB：`C:/Users/flash/.espressif/tools/riscv32-esp-elf-gdb/17.1_20260402/riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb.exe`。

必须注意 OpenOCD 默认 gdb-attach 会因内存保护自动复位并探测 Flash，曾运行 flasher stub 失败并拒绝连接。后改成单核、禁用 GDB memory map/flash program、attach 仅 halt，通道建立过，但寄存器协商与内存读取仍不稳定；一次受控复位后读到零 PC/SP，不能作为固件根因。最后一次断电后的远程连接被关闭，未重新拿到稳定现场。

用户此前授权过暂停、断点、单步和受控复位；不要把这理解为无限期烧录授权。当前阶段是本地移植。硬件试验前重新确认对方在线与设备状态，烧录单独确认。不整片擦除，不改 eFuse、安全设置，不写凭据到文件，不开放无认证 GDB 到公网，不运行 `load` 或复位命令冒充只读。

## 8. 新桌面与应用来源

用户 GitHub 项目本地 HEAD 曾为 `c9dd8960563328d3fad388c79413ee9772c56e48`，其中 `3035384` 新增部分 Home Assistant、番茄钟和相机资源。用户明确说明仓库未同步所有功能，不能只用旧提交 `10bc10d4...`。

实际额外代码在 Windows `camera-app`：桌面入口、运行时、ESPHome、Home Assistant、番茄钟等。`build-product.sh` 标明是实验 overlay 工具，不是已验收系统基线。

`camera-app/desktop-mac/ROLLBACK.md` 记录一组重建源码启动崩溃；`build-restore.sh` 已禁用。历史 `artifacts/desktop-polish/nuttx.bin` 是 v1.0/180MHz 回滚产物，不是目标 ECO7 可用基线，其精确源码复现尚未建立。

`camera-app/native/qpk_runtime_userptr.c` 相机要求三页 framebuffer，而旧 v3 显示路径只提供一页。不能只开 CSI 就宣称相机可用。当前桌面启动相机方式也与旧 native-mode 测试不同；测试运行曾 23 项中 21 通过、2 失败，失败路径的触摸恢复值得审查。

本任务应先完成系统层，不整体复制实验应用/驱动组合掩盖启动问题。

## 9. 下一位工程师的起步清单

1. 阅读本文件和 `openvela-port-clean/README.md`，确认最新用户要求仍是 openvela 系统移植。
2. 检查目标内核、apps、HAL 的 HEAD、dirty diff、未跟踪文件和当前 `.config`；不要 reset/clean 用户已有改动。
3. 运行中断桥接主机测试，读取最近构建日志；如需再次构建使用 `build-l0.sh`，先确认脚本补丁重复检查仍适用。
4. 解决 P4 中断分配与分发合同，不增加假的句柄查询；与系统 tick、串口、HAL ISR 安装、enable/disable/free 调用链成套验证。
5. 后续处理启动地址分类、Cache/MMU、时钟、PSRAM 相关接口，保留正确初始化；不得把历史 bypass 补丁整体塞回去。
6. 编译成功后检查 ELF 段、入口、映射格式和配置，不仅看生成了 BIN。再选择与对方接线匹配的控制台。
7. 明确授权和版本核验后才做实板试验，记录冷启动、重复复位、NSH 响应、自动下载恢复；失败保留完整 ELF/BIN/配置/日志。
8. 系统基线通过后再移植当前 UI/相机/软件，逐层验收。

有一项每小时只读跟进自动化，ID `esp32-p4-openvela`；它只观察变化，不会代替工程实现。当前没有需要继续轮询的构建 exec 会话。

## 10. 交接结论

已经完成可追溯的隔离基线、部分芯片接入及 HAL API 适配，最近完成了中断挂接失败回收测试。

尚未完成中断模型迁移、最终编译链接、目标板 openvela 冷启动、显示或相机验收。没有可诚实标记为“移植成功”的新固件。所有进度陈述必须区分源码修改、编译通过、主机测试通过与实板通过。
