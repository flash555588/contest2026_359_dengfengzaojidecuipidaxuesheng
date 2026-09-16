# 你好快应用：ESPClaw 参考项目

设备路径：`/data/qpk/.examples/hello/`。

用途：启动计数、本包存储、按钮、提示、定时更新标签。

源码来源：`desktop_main.c: g_hello_qpk_js`，与当前固件对应源代码逐字节一致（LF、UTF-8）。

源码 SHA-256：`5afb533340f6a57f8b27af13177d326f6497e62ab8fbde904793a0b7f87db16b`。

已有证据与边界：ui-layout-fix/evidence/review-light/light-hello.json 与 review-dark/dark-hello.json；espdl-quickapp/evidence/ui-before-13/light-hello.json 保留了后续版页面记录。这些是对应旧固件的界面验证，不能代替新应用验收。

本次生成器的固定源码/交互测试报告位于 `espdl-quickapp/evidence/qpk-authoring-validation.json`；只有报告实际存在且相应测试通过时才可引用其结论。这些文件不包含应用用户数据，也不读取 `.data` 或服务配置。

阅读顺序：先理解 `app.js` 的状态和渲染函数，再按新任务借用 API。示例 package 只用于说明来源，新应用由生成器分配 `ai.<slug>`，不要复用它。存储异常提示、按钮大小、主题和内容区尺寸应针对新应用再次检查。
