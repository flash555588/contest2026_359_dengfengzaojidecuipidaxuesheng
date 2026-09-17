# 2048：ESPClaw 参考项目

设备路径：`/data/qpk/.examples/game2048/`。

用途：4×4 网格、控件复用、数字、手势、合并算法、最高分。

源码来源：`desktop_main.c: g_2048_qpk_js`，与当前固件对应源代码逐字节一致（LF、UTF-8）。

源码 SHA-256：`aef46fb6f70d4160a843a0e2a890c8b40b337dfbf3c3e9061c74e84d5bb8b2f8`。

已有证据与边界：ui-layout-fix/evidence/review-light/light-2048.json 和 review-dark/dark-2048.json 是已有页面审计。游戏边界和连续操作由本次宿主测试另行验证，不能将页面截图当作全部玩法通过。

本次生成器的固定源码/交互测试报告位于 `espdl-quickapp/evidence/qpk-authoring-validation.json`；只有报告实际存在且相应测试通过时才可引用其结论。这些文件不包含应用用户数据，也不读取 `.data` 或服务配置。

阅读顺序：先理解 `app.js` 的状态和渲染函数，再按新任务借用 API。示例 package 只用于说明来源，新应用由生成器分配 `ai.<slug>`，不要复用它。存储异常提示、按钮大小、主题和内容区尺寸应针对新应用再次检查。
