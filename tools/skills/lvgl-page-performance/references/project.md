# 入口、资源和验证

路径相对项目根。已有分析：`docs/FRONTEND_PERFORMANCE_2026-10-19.md`、`docs/PAGE_LIFECYCLE_AUDIT_2026-10-20.md`。

先核对的生产入口：

- `app/espdl-quickapp/overlay/apps/system/desktop/` 中的 Makefile、`glass_ui_visibility.h`、原生页面及小球。
- 同目录下 `qpk_runtime.c`：QuickJS/LVGL 共用桥接。
- `quickapp/homeassistant/app.js` 和 desktop 下同名 JS：核对源与生成关系；修改后重建资源和授权摘要。

三份 `homeassistant_resource.c` 不代表三套前端同时运行；旧 JS 球体、历史语音页面或上游输入必须从当前链接/包含关系判断。不能按目录名猜运行负载。

在 Linux/WSL 使用匹配的准备树和 sanitizer LVGL 缓存：

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
PAGE_AUDIT_MOTION=1 python3 tools/render_all_ui.py \
  --apps /path/to/prepared/apps --cache /path/to/lvgl-host-cache \
  --output /path/to/output/page-audit --grok-assets /short/path/to/sd-banks \
  --harness-source app/espdl-quickapp/tests/render_all_page_lifetime.c
```

不设 `PAGE_AUDIT_MOTION` 检查减少动效模式；两套主题由对应夹具覆盖，检查输出实际范围。定向性能检查可使用同目录的 `render_frontend_performance.c`；动效变更使用 `tools/test_interaction_motion.py --apps ... --cache ... --output ...`。浮球交接使用 `tools/test_wake_mini.py` 的相关夹具，先核对支持参数。

页面审计产物包括 `lifetime.jsonl` 和 `render-sources.json`。网络、解码和录音替身只能说明 UI 生命周期；实板帧率、触摸延迟及音视频压力另行验收。

历史验证基线：47 个页面/临时状态、两套主题和两种动效共 564 次切换未发现退出累积；发现的是完整遮挡后仍刷新。另一次 UI 修复发现池化设置按钮被后建面板盖住、零帧截图误报入口空白。这些是可复现机制，不是永久保证所有页面无泄漏。
