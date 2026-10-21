# 统一应用 UI

`overlay/apps/system/desktop/glass_app_theme.h` 是原生页面、媒体工具、QuickApp 和网页配置的主题入口。深浅主题共享语义：canvas、paper、soft、line、ink、muted、accent、on_accent、success、warning、danger。

原生页面通过 `glass_app_controls.h` 设置已有按钮、卡片、输入框；保留已有事件、对象所有权与服务生命周期。侧栏使用 164px 宽、140×44px 条目、12px 内边距和 8px 间距；普通卡片圆角 18px，控件圆角 12px，推荐触控高度 44px。

QuickJS 通过 `qpk_app_theme.h` 发布 `ui.theme`，包含上述颜色和尺寸。`ui.primary`、`ui.secondary`、`ui.card`、`ui.surface`、`ui.accent` 继续可用。独立预览没有主题对象时应用保留兼容配色。相机预览、视频画面、天气插画、歌词和设备功能色继续表达其内容语义。

当前覆盖系统设置、应用管理、网络、蓝牙、鼠标、文件及其对话框、性能、天气、环境、网页配置入口、音乐及播放视图、聊天/创作/助手/Bot及其弹窗、语音、相机宿主、录音、家居、端侧识别、大肥鱼、2048、示例应用、终端、相册和直播。录音与识别页补充小高度布局，录音分页根据可用空间决定条目数。

修改脚本后同步资源：

```sh
python tools/generate_app_ui.py
python tools/generate_homeassistant_ui.py
python app/espdl-quickapp/overlay/apps/system/desktop/generate_dafeiyu_resource.py
python tools/generate_portal_resource.py
```

第一个命令同步录音/识别 C 资源、Hello/2048 原生与分发源码、示例目录源码哈希，并从共享 C 调色板生成网页 CSS 覆盖层。不要手改生成的 C 字符串或 CSS 覆盖层。

行为和布局回归：`tests/unified_ui.test.cjs` 覆盖四个应用、四种尺寸、双主题的控件边界与相交检查；原生 `tests/render_all_page_lifetime.c` 覆盖 47 个页面、双主题和 282 次开关，并输出 94 张页面截图。另运行 `render_other_pages.c`、`render_ime_ui.c` 和 `render_grok_conversation.c` 检查侧栏/文件操作、输入法和聊天/语音生命周期。均通过 `tools/render_all_ui.py --harness-source <测试源文件>` 调用现有宿主 LVGL/QuickJS 环境。

本次离线证据位于工作区 `output/unified-ui-all-20261021/`。宿主夹具替代网络、录音和设备控制；这些结果不等同固件构建、烧录或实板验收。
