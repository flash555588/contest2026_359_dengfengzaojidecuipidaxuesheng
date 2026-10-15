# Grok Ball：ESPClaw 参考项目

设备路径 /data/qpk/.examples/ouo/，保留 OuO 示例 ID。显示名 Grok Ball，版本 2.0.0。

用途：32 种表情、触摸注视、自旋彩带、滑动切换、AI emotionId 与有限动画定时器。源码与 desktop/ouo/app.js 逐字节一致，由 tools/generate_grok_ouo.py 生成；SHA-256 在父目录 catalog.json 记录。

“表情”入口提供三组直接选择；打开时暂停背景动画，关闭后恢复。启用上游 active 状态以运行眨眼、眼环轮换、待机小动作和任务完成撒花。AI 提示使用 tips 事件的 text 字段，状态切换会清除旧文字。面板期间背景按键不响应，球体以外的触摸不触发自旋。

上游 https://github.com/tycoding/grok-ball ，固定版本 de368ce3acdc5a70e871ee46ebbe8f60b11d3d6a。MIT 许可保留于 app.js，适配代码 Apache-2.0。当前固件播放 `/sdcard/grok/` 中的 RGB565+A8 原始帧，不在设备上运行 SVG。素材保持 32 个表情 ID，八秒循环、10 fps；注视采用九方向素材，自旋播放预渲染片段。

状态 ID：00..07、10..21、30..41。00 睡眠，02 待机，10 开心，30 思考；未知 ID 回退 02。勿使用旧 OuO 含义。

`tools/test_grok_sd.py` 验证双缓冲、全部状态/配色、注视、自旋、暂停、缺失/损坏素材及退出生命周期。实板测试以对应烧录报告为准；旧 OuO 性能记录不适用于本版。

新应用应由生成器分配 ai.<slug> 包名。尺寸使用 ui.getSize() 内容区域，勿复制示例包名。
