# Grok Ball / LVGL

**当前固件使用 SD 原始帧播放器**，见 [SD-PLAYBACK.md](SD-PLAYBACK.md)。以下记录描述此前实时 SVG 适配器和离线素材来源，不代表当前设备播放路径。

OuO 入口现使用 [tycoding/grok-ball](https://github.com/tycoding/grok-ball) 的表情引擎。上游固定在 de368ce3acdc5a70e871ee46ebbe8f60b11d3d6a，原文件未改写，MIT 许可和来源哈希在 vendor/ 保留。LVGL 适配代码为 Apache-2.0。

保留上游 32 种表情、48 点眼睛轮廓、动画、注视、自旋彩带和 AI emotionId 接口。浏览器 SVG 场景映射到原生 LVGL 多边形，没有 WebView。填充按完整轮廓扫描，避免三角形拼接细线和缺口。渐变保留全部色标并由原生 LVGL 分段插值，字体和抗锯齿由 LVGL 绘制，因此不是浏览器逐像素复刻。

触摸球体时眼睛跟随，轻点自旋，左右滑动切换表情。底部“表情”打开生命周期、情绪反应、工作状态三个分组，可直接选中全部 32 种表情；还可启动/停止巡演或进入思考状态。面板打开时停止巡演并暂停背景动画，选择/关闭后恢复；背景按键不会穿透触发。表情使用本包 storage 保存，跨重启保存取决于数据文件系统。

原引擎的 active 开关已启用，自动眨眼、眼环池轮换、待机小动作及进入状态时的自旋/撒花均运行；动画仍由单个原生定时器驱动。AI tips 从事件的 text 字段读取，状态切换会清除旧提示。暂停同时停止巡演。

## 文件和生成

- vendor/grok-ball.js：固定版本的上游引擎。
- grok_lvgl_adapter.js：SVG 节点、坐标和绘制适配。
- grok_app.js：主题、布局、触摸、存储和兼容接口。
- app.js、ouo_resource.c：由 python tools/generate_grok_ouo.py 生成，勿独立编辑。

生成器同步内置应用、ESPClaw 示例、清单和嵌入资源。保留原 OuO 包名、目录和 CLI 入口，显示名 Grok Ball，版本 2.0.0。状态 ID 改用上游定义，详见 DESIGN.md。

## 验证边界

tests/test_grok_ball_app.js 验证两主题全部状态、触摸、滑动、AI 文字/回退、彩带、撒花、分组选择面板、巡演和暂停。真实 QuickJS / LVGL 离线渲染器输出两主题全部 32 种表情、选择面板、AI 提示，以及环绕、自旋、睡眠、撒花和注视的连续帧；ASan / UBSan 检查原生绘制、点击和控件生命周期。动态审核页共 76 段 GIF，由原生连续帧打包。

本次仅编译 ESP32-P4 固件及离线审核截图，未刷机。实板帧率、触摸手感和长期稳定性尚未验证；旧 OuO 实板记录不代表本移植版已通过实板验证。
