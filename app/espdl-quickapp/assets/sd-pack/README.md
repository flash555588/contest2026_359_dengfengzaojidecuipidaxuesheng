# App SD 素材包

这里提供可直接解压的成品资源，只收录识别模型、多语言字体、JIZURA 离线素材、Grok 动画和大肥鱼精灵五类。共 50 个文件，压缩后约 73 MiB，展开约 688 MiB。测试截图、日志、用户配置和固件不在包内。

| 资源包 | 解压到 SD 根目录后的路径 | 内容 |
| --- | --- | --- |
| [espdl.zip](espdl.zip) | `espdl/` | 清单指定的 3 个识别模型、模型清单和许可证 |
| [fonts.zip](fonts.zip) | `fonts/` | 成品 `VelaGlobal-Regular.ttf`、字体清单和 4 份 OFL 许可证 |
| [grok.zip](grok.zip) | `grok/` | 首页深浅主题、聊天头像、独立应用深浅主题，共 5 个 `.gbs` 文件及清单、许可证 |
| [jizura.zip](jizura.zip) | `jizura/` | 完整离线引擎、27 套风格、10 个字体文件、预览入口、原创演示项目和许可证 |
| [dafeiyu.zip](dafeiyu.zip) | `dafeiyu/` | 已转换的 `dafeiyu.lvbin` 和上游许可证；含 4 个方向、3 种尺寸，共 12 张 RGB565A8 图像 |

## 组装与安装

在本目录运行，仅需 Python 标准库：

```sh
python assemble.py --verify
python assemble.py --output /path/to/sdcard-ready
```

`--output` 必须是新目录或空目录。工具先核对各 ZIP 的 SHA-256，再逐文件核对长度和摘要；实际组装会在写出时再次校验。将 `sdcard-ready` 里的 `espdl`、`fonts`、`grok`、`jizura`、`dafeiyu` 目录复制到 SD 根目录。也可直接把五个 ZIP 解压到同一个空目录。

设备挂载后，路径应为 `/sdcard/espdl/`、`/sdcard/fonts/VelaGlobal-Regular.ttf`、`/sdcard/grok/`、`/sdcard/jizura/` 和 `/sdcard/dafeiyu/dafeiyu.lvbin`。复制到已有 SD 卡时请自行保留原有同名资源和个人文件。

## 版本与兼容范围

- 模型采用仓库 `models/manifest.json` 中的原始布局和摘要。模型清单的 Flash 偏移是历史信息；本包只提供 SD 文件，不执行烧录。
- 全局字体与 `glass_sd_font_asset.h` 的长度和 SHA-256 一致，覆盖 45,695 个 Unicode 映射。字体覆盖不等于复杂文字排版已实现。
- Grok 文件按清单逐一核对 SHA-256，并检查 `GROKSD1` 文件头、状态表和全部帧区间。播放器需要这些未经压缩的 `.gbs`，不能把 ZIP 直接放到设备上使用。
- JIZURA 包是完整离线预览资源，可在电脑上通过本地 HTTP 服务器打开 `jizura/index.html`。当前工作区原生音乐 UI 已移除旧 SD PV 帧库入口，使用聚焦、强调、逐字三套实时 LRC 效果；此包不会启用已移除的入口，也不包含历史渲染帧库。固件是否读取离线目录取决于所用版本。
- 大肥鱼由上游固定提交 `5b0e01856116bd2bae82df1f43c32faa5f056196` 的正面、背面、侧面 PNG 转换，转换程序检查整包与各图片 CRC32。

[manifest.json](manifest.json) 包含全部安装路径、字节数、SHA-256 和上游来源。已完成离线完整性检查与实际解压复核；本次没有操作开发板或替换板上文件。
