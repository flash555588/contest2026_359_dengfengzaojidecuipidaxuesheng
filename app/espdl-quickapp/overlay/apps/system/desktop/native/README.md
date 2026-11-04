# 原生模块加载

固件提供通用的原生模块（ELF）动态加载。模块随应用安装在 SD 卡或内部存储的应用目录里，启动时按需加载到 PSRAM 运行，不占 Flash，也不链接进固件。系统里没有任何针对特定应用的代码或包名。

## 应用目录

```
/sdcard/qpk/<应用目录>/        （或 /data/qpk/<应用目录>/）
  manifest.json               package 与模块声明的 package 必须一致
  app.js                      快应用外壳，可以只显示加载状态
  native/module.elf           原生模块（ELF 可重定位文件）
  native/host.abi             构建时的 OV_NATIVE_HOST_ABI，64 位十六进制
```

没有 `native/module.elf` 的应用就是普通快应用，加载器不做任何事。内置（`builtin:/`）应用和应用根目录以外的入口都不会加载原生代码。

## 模块接口

见 `native_module.h`。模块导出 `ov_native_module_query()`，返回 `struct ov_native_module`：

- `abi` / `bytes`：`OV_NATIVE_MODULE_ABI` 和结构体大小。
- `host_abi`：构建时的 `OV_NATIVE_HOST_ABI`，与 `native/host.abi` 一致。
- `package`：所属应用包名，必须等于启动它的应用。
- `attach(context, system, root, package)`：UI 线程调用；`root` 是应用内容区。
- `detach()`：UI 线程调用；释放 `attach` 拿到的所有 LVGL/JS 引用。
- `quiesce()`：可选。detach 之后调用；返回 true 表示模块的线程、定时器和回调都已结束，固件随即卸载代码并释放内存，下次启动重新加载文件。为 NULL 或返回 false 时模块常驻，再次启动直接 `attach`。

JS 外壳可读 `system.nativeLoading`（加载中为 true）和 `system.nativeError`（失败原因）。

模块只能调用 `exports.txt` 里列出的固件符号。修改该列表后运行 `python3 tools/generate_native_exports.py`，生成 `native_exports.c` 和 `native_host_abi.h`；不要手工编辑这两个文件。列表或 `OV_NATIVE_MODULE_ABI` 变化后，主机 ABI 摘要随之变化，旧模块会在执行任何代码前被拒绝，需要重新构建。

## 内存与并发

- 系统不对模块设内存、大小或功能上限。唯一边界是可执行别名窗口 `0x4a000000..0x4c000000`（32 MiB，开机不预留物理内存）。堆满足不了的分配会正常失败。各模块自行控制内存预算。
- 最多 8 个模块同时常驻。不同应用的模块可同时驻留；同一时刻只有当前前台应用处于 attach 状态。
- 加载在低于 UI 优先级的工作线程中进行，不调用 LVGL 或 QuickJS；完成后由 UI 定时器 attach。
- 加载器只做兼容性与归属校验，不是任意原生代码的沙箱。只应安装可信来源的模块。

## 固件构建

`tools/build_bilibili_v3.py` 启用 `CONFIG_OV_NATIVE_TEXT_POOL`（由 `tools/prepare_openvela_exec_pool.py` 加入），并用 `tools/build_native_loader.py` 编译本目录。宿主测试：`tools/test_native_text_heap.py`。诊断命令：`desktop native-heap-test`。
