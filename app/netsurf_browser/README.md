# NetSurf 浏览器（openvela 原生模块应用）

基于 NetSurf 官方源码的轻量网页浏览器。浏览器以普通应用的形式安装在 SD 卡或内部存储上，启动时由固件的通用原生模块加载器（`desktop/native/`）载入 `native/module.elf`，不占 Flash、不链接进固件。系统里没有浏览器专用代码。

## 功能

- HTML、CSS 排版（NetSurf 核心：libcss、libdom、libhubbub）。
- 中文显示与断行：字形来自系统 SD 卡字体服务（`glass_sd_font_get`，`/sdcard/fonts/VelaGlobal-Regular.ttf`）；字体未就绪时退回内置 Montserrat（仅拉丁字符）。
- HTTP 与 HTTPS：固件的 webclient 加 mbedTLS 传输（`glass_https`），证书使用 `/sdcard/netsurf/ca-bundle.pem`（SD 资源包 `browser-ca.zip`）。
- 图片：基线 JPEG 用 P4 硬件解码器，失败时用固件 TJpgDec；PNG 用系统相册同一份 LodePNG；GIF、BMP 用 NetSurf 的 libnsgif、libnsbmp。
- 操作：点击链接、手指拖动滚动、后退、前进、刷新、主页、地址栏（系统输入法 `prompt.input`）、网页文本框输入、Cookie、重定向、表单 GET/POST。
- 不支持：网页 JavaScript、渐进式 JPEG、WebP、SVG、gzip 压缩传输（请求中声明 `Accept-Encoding: identity`）、下载、多标签。

## 目录

```
app/netsurf_browser/
  build.py                 构建脚本（WSL/Linux）
  pins.json                上游 NetSurf 各仓库提交与 zlib 1.3 摘要
  frontend/openvela/       NetSurf openvela 前端（复制到 netsurf/frontends/openvela）
  frontend/res/            内置欢迎页
  port/compat/             NuttX 头文件兼容（强制包含，不改上游源码）
  port/patches/            上游小补丁：加入 openvela 目标、NuttX 缺少的 libc 功能、libcss int32_t 联合体转换
  package/                 manifest.json、app.js（快应用外壳：工具栏与输入转发）
```

## 构建

需要已准备好的 openvela 工作区（含 `nuttx/include/nuttx/config.h`）和 `riscv32-esp-elf` 工具链：

```sh
python3 app/netsurf_browser/build.py \
  --workspace <prepared-workspace> \
  --toolchain <dir-with-riscv32-esp-elf-gcc> \
  --output <out-dir> \
  --firmware-elf <prepared-workspace>/nuttx/nuttx
```

首次运行从 GitHub 拉取 `pins.json` 中固定提交的源码到 `~/netsurf-openvela/src`（可用 `--work` 改位置），之后可加 `--offline`。只改前端时加 `--reuse-libs` 跳过依赖库重编。

构建依次完成：应用补丁并重新生成 libcss 选择器代码；按固件同一套 NuttX 编译参数交叉编译 8 个 NetSurf 库和 zlib 1.3（openvela `apps/system/zlib` 用的同一版本）；用 NetSurf 自带的 `split-messages` 生成中文（英文后备）消息表并与样式表、欢迎页一起内嵌；`ld -r` 部分链接成可重定位 ELF。

链接后脚本检查并写入 `BUILD.json`，任何一项不过即返回非零：

- 重定位只含 NuttX RISC-V 加载器支持的类型（`R_RISCV_32/HI20/LO12_I/LO12_S/CALL_PLT`）。代码使用 `-mcmodel=medlow -mno-relax -msmall-data-limit=0`，因此没有 PC 相对配对、分支重定位或小数据段。
- 加载器对 `CALL`、`LO12_S` 是“加到原字段上”，脚本按 psABI 把这些字段清零（RELA 的加数在 `r_addend` 中），并校验指令形态。
- 每个导入符号都在 `desktop/native/exports.txt` 中；给了 `--firmware-elf` 时还确认固件确实定义了它们。
- 模块只导出 `ov_native_module_query`，`native/host.abi` 与模块内嵌的主机 ABI 一致。

输出 `com.netsurf.browser/` 目录和 `NetSurfBrowser-<版本>.zip`。

## 安装

把 `com.netsurf.browser/` 整个目录放到 `/sdcard/qpk/` 下（或内部 `/data/qpk/`），同时准备 SD 资源包中的字体与 `netsurf/ca-bundle.pem`。固件必须带有相同 `OV_NATIVE_HOST_ABI` 的原生模块加载器；`exports.txt` 变化后需重新构建固件和本模块。

## 内存

系统不限制本应用；预算由应用在核心启动时按当时的空闲堆自行计算（`gui.c` 的 `compute_budget`）：

- 页面缓存为空闲堆的 1/6，限定在 2–12 MiB 之间；图片缓存为其 1/4（NetSurf 默认比例）。
- 单张解码图片不超过缓存的 1/3（至少 1 MiB），超出的图片保留占位、不解码。
- 单个响应正文不超过空闲堆的 1/4（最多 16 MiB）；每个请求排队未处理的数据不超过 256 KiB，网络线程在此等待界面线程消费。
- 最多 4 个并发请求（同一主机 2 个），无磁盘缓存。

## 线程与生命周期

NetSurf 核心、绘制和全部 LVGL/JS 调用都在界面线程，由 10 ms 的 LVGL 定时器驱动。每个网络请求一个低于界面优先级的可 join 工作线程，只往请求队列追加数据，从不调用 NetSurf 或 LVGL。

退出应用时 `detach` 销毁浏览窗口并通知所有请求中止；`quiesce` 等待最多约 200 ms，所有工作线程都结束后关闭核心，固件随即卸载模块并释放内存。若仍有线程卡在 DNS 等阻塞调用里，模块保持驻留，下次打开直接复用。

## 验证状态

已完成：宿主侧原生加载器测试；固件同参数交叉编译、部分链接；上述重定位、导入、导出与 ABI 检查。

未完成：真实板卡加载、渲染、触摸、网络与 HTTPS 验收，以及物理屏幕观察。`BUILD.json` 中 `deployable` 保持 `false`，在实板验收前不能认为可以部署。

## 许可

NetSurf 及其库为 GPL-2.0 / MIT，本前端随 NetSurf 采用 GPL-2.0。zlib 为 zlib 许可，LodePNG 为 zlib 许可。分发模块时须同时提供对应源码（本目录、`pins.json` 指向的上游提交与补丁）。
