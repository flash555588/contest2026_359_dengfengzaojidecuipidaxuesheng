# SD 素材与更多风格

完整引擎的新播放预览直接提供 27 套风格，增加「样式巡游」和「换一组构图」。每套风格使用不同的镜头构图组，入场和退场交给原规划器选择，取消旧演示中固定的 assemble／explode。

2026-10-18 的浏览器检查实际覆盖 16 种构图、13 种入场、13 种退场，累计 135 个取样时刻，无绘制警告／异常。所有资源请求均来自素材包所在的本机站点，没有外部字体请求。此预览仍是完整版浏览器引擎，不是 P4 完整 Canvas2D 后端。

## 存储

准备后的文件树可整体放到 SD 的 `/sdcard/jizura`，约 91 MB，包含：

```text
/sdcard/jizura/
  manifest.json              文件大小与 SHA-256
  styles.json                27 套风格名称、深浅配色、镜头构图组
  engine.js                  完整优化引擎源码；不是主机 QuickJS 字节码
  index.html / player.js     样式预览与连续播放
  fonts.css / fonts/         10 个字体文件及各自的 OFL 许可证
  projects/demo.json         原创演示歌词与项目
  textures/README.txt        纸纹、扫描线、颗粒使用原引擎生成
  vendor/                    原版 MP4 封装依赖
```

字体来自 [Google Fonts 官方仓库](https://github.com/google/fonts)，固定到 `47d54ca1ebb14fd864695f1d39dc734f39017c33`，原始 URL、大小和 SHA-256 在 [fonts.lock.json](fonts.lock.json)。包含 Noto Sans／Serif SC、原版字重动画所需的 Noto Sans／Serif JP、站酷庆科黄油体／快乐体／小薇体、马善政和 IBM Plex Mono 两个字重。TTF 未裁剪或改写，许可证随包保存。

离线字体覆盖范围为当前中文简体播放预览；不能据此宣称全部语言的离线字体已准备。字体存在于 SD，也不表示原生 TinyTTF 能同时加载所有大字体：当前原生音乐页继续使用系统字体。完整绘图后端后续仍需按需读取／字形缓存方案。

## 原生播放器

原生文字 PV 新增独立的 SD 风格按钮，「构图」按钮保留原来八种布局。配色表只在全屏播放器创建时读取，限制文件 32 KiB、32 套风格、JSON 深度与字段长度；配色损坏、缺失或没有 SD 时沿用已有本机配色，不阻断音频播放。

27 套 SD 配色在深浅主题下共完成 54 张真实 LVGL 宿主截图检查。原有音频时钟同步、暂停、UTF-8 打字机、减少动态效果、布局边界、对象复用和反复打开／关闭检查通过 ASan／UBSan；解析器另行检查错误 JSON、超深结构、超大文件和 NUL 字节。

这里接入的是 **SD 配色与选择入口**，不是把 860 个效果缩写为八种原生布局。完整 27 套样式和全部效果仍保留在 SD 的完整引擎源码中。原生入口代码需要进入后续固件；本轮素材部署不烧录固件。

## 准备与检查

仓库根目录：

```powershell
python tools/prepare_jizura_sd.py fonts --output ../output/jizura-font-cache
python tools/prepare_jizura_sd.py build --build ../output/jizura-optimized-stage3 --cache ../output/jizura-font-cache --output ../output/jizura-sd-20261018
python tools/prepare_jizura_sd.py verify --output ../output/jizura-sd-20261018/sdcard/jizura
python tools/jizura_full.py serve --directory ../output/jizura-sd-20261018/sdcard/jizura --port 8771
```

打开 <http://127.0.0.1:8771/>。输出目录必须为空，避免覆盖已有项目。字体首次锁定使用了 `fonts --update-lock`；日常构建应使用已有锁文件，不重新选择字体版本。

连接开发板后，可使用 `tools/deploy_jizura_sd.py`：它先检查完整主机清单、真实 FAT SD 挂载和剩余空间，再通过只服务指定开发板的临时局域网 HTTP 传输到新暂存目录；逐文件核对 SD 大小，读回并比对完整配色表，最后改名发布为 `jizura`。已有 `jizura` 目录会保留并报错，不自动覆盖；没有格式化或固件写入。当前板端没有通用文件 SHA-256 命令，因此字体的板端哈希尚未逐文件验收，部署报告明确保留这一边界。

本次完整本地证据位于工作区 `output/jizura-sd-20261018/`，原生界面记录在 `output/jizura-sd-native-review/`。

本次已实际发布到开发板 `/sdcard/jizura`：30 个文件，共 **90,770,585 B**，逐文件大小核对通过，7,844 B 的 `styles.json` 完整读回一致。摘要见 [SD-RESULTS.json](SD-RESULTS.json)。现有视频与语音目录保留，固件未更新。
