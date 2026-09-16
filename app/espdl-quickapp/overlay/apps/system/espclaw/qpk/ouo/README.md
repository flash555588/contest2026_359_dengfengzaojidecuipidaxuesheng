# OuO：ESPClaw 参考项目

设备路径：`/data/qpk/.examples/ouo/`。

用途：表情状态、触摸三参数回调、动画、有限定时器。

源码来源：`desktop/ouo/app.js`，与当前固件对应源代码逐字节一致（LF、UTF-8）。

源码 SHA-256：`72ec0ffb36e5fcceb9d482b9c8b8760587e81ebcd31afe32ca54b8ba50aa4b0a`。

已有证据与边界：espdl-quickapp/tests/test_ouo_app.js 覆盖状态、定时器、触摸和帧工作量；evidence/ouo-touch-boot.json 等为实板专项记录。聆听、充电等文案只是表情状态。

本次生成器的固定源码/交互测试报告位于 `espdl-quickapp/evidence/qpk-authoring-validation.json`；只有报告实际存在且相应测试通过时才可引用其结论。这些文件不包含应用用户数据，也不读取 `.data` 或服务配置。

阅读顺序：先理解 `app.js` 的状态和渲染函数，再按新任务借用 API。示例 package 只用于说明来源，新应用由生成器分配 `ai.<slug>`，不要复用它。存储异常提示、按钮大小、主题和内容区尺寸应针对新应用再次检查。
