# 应用存储修复版 — 2026-09-14

本目录的 `nuttx.bin` 已烧录到 COM23 的 ESP32-P4 v3.2，镜像回读摘要校验、实际 SmartFS 存储自检和 Wi-Fi DHCP 均通过。原有黑屏、启动和 DHCP 修复全部保留，配置与 `../wifi-dhcp-fix/resolved.config` 逐字节一致。

## 故障原因与修复

`/data` 已正常挂载为 SmartFS，容量 4 MiB，使用 26 KiB、空闲 4070 KiB。应用共用的 `storage.get/set/delete` 把包名转成十六进制后，与键名拼为一个文件名。例如 Hello 的 `launches` 文件名有 47 字节，临时文件还要追加 `.tmp`。

现有 SmartFS 卷的名称字段为 32 字节。其写入实现使用 `strlcpy(..., 32)`，安全名称长度是 31 个字符；默认目录栈包含根目录共 8 层。长文件名导致存储接口失败，应用显示“存储无效”。

现在直接将包名和键名分段，示例为：

```text
/data/qpk/.data/@2pcom.example.hello/klaunches/value
```

每个名称最多 31 个字符；最长 47 字节包名和 64 字节键名仍不超过 7 层父目录。包名和键名边界有独立前缀，不截断、不使用摘要映射。值上限仍为 8192 字节，键名规则保持 ASCII 字母、数字、下划线和连字符。

读取优先使用新格式，找不到时依次读取之前的十六进制扁平格式和原版的“包名/键名.txt”格式。读取旧文件不会修改或删除它；之后保存该键会使用新路径。显式删除该键时同时删除兼容路径中的同键文件，避免旧值重新出现。写入临时文件并关闭后使用 `rename` 完成替换。

没有修改 SmartFS 卷格式、重新格式化或擦除 `/data`。原版 WSL `/home/streetartist/nuttxspace` 未修改。

## 固件与源码

| 项目 | 值 |
| --- | --- |
| 固件 | `nuttx.bin`，3,926,764 字节 |
| SHA-256 | `2159a0adca9a7e76680233064d907aa3676b6d10da12b1e35dae3901f7b65932` |
| 烧录偏移 | `0x2000` |
| 擦除范围终点，不包含 | `0x3c1000` |
| `/data` 起点 | `0x400000` |
| ELF / MAP | `nuttx.elf` / `nuttx.map` |
| 配置 | `resolved.config` |
| 新增应用存储改动 | `storage.patch` |
| 相对原导出源码的全部修复 | `full-fix.patch` |
| 可覆盖源码 | `overlay/` |

回退镜像保留在 `../wifi-dhcp-fix/nuttx.bin`。该旧固件不会读取新格式中新增的应用值；原有旧文件仍保留，除非用户显式删除对应键。

## 验证结果

- 主机测试以真实限制约束文件系统调用，并开启 AddressSanitizer、UndefinedBehaviorSanitizer：长名称与目录深度、命名隔离、空值、8192/8193 字节边界、含 NUL 的值、非法键、两种旧格式读取、新值优先、覆盖与删除，以及调用 `rename` 前注入失败后的旧值保留均通过。
- 板上 `desktop storage-test`：Hello、2048 和 47 字节包名/64 字节键名共三个独立命名空间，创建、覆盖、读回比对、删除及删除后未找到均通过。测试只操作独占创建的临时目录，结束后全部清理。
- 板上测试前后旧应用目录和 OuO 文件列表一致；使用空间均为 26 KiB、空闲均为 4070 KiB。
- C6 使用自身保存的 Wi-Fi 配置关联成功；`c6probe dhcp eth0` 返回 0，IPv4 为 `192.168.0.105`，网关为 `192.168.0.1`。
- JTAG 使用本次 ELF 确认桌面任务存活、显示驱动 `video_running=true`、1024×600 RGB565；随后 NSH 串口继续正常响应，串口已释放。

没有逐个操作应用界面、目视确认实体屏幕或进行断电恢复测试。旧格式回读兼容性已做主机回归测试，板上现有旧文件进行了保留检查。

`evidence/hardware-validation.json` 是最终实板结论；`evidence/postflash.log` 包含实际输出。`evidence/attempt1/` 仅保留曾被边界测试拒绝的中间版本及清理记录，不是交付固件。

可在 NSH 复查：

```text
desktop storage-test
df -h
```

## 构建

本机独立构建树为 `/tmp/v3-desktop-app-storage-20260914`，由已验证的 `/tmp/v3-desktop-wifi-dhcp-20260914` 复制。首次构建先清理，边界修正后增量编译并重新验证镜像；未运行会重置其他配置的 `olddefconfig`。

在本工作区使用 WSL 执行 `diagnostics/build_storage_fix.py` 可按当前环境重建，`--test-only` 只运行主机回归测试。依赖现有独立 Wi-Fi 构建树、已锁定 TLS 下载和相同工具链。`build-scripts/` 为这些脚本的审阅副本；可覆盖源码和两份补丁保留在本目录。
