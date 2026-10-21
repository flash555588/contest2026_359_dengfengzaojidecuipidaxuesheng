# 大肥鱼桌宠

当前接入树：`04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop/`。

把 [dafeiyu-pet](https://github.com/1190fasheqi/dafeiyu-pet) 做成 OpenVela 本地应用：

- **本地原生层**一直在桌面上走（`pet_engine` + `pet_lvgl`）
- **快应用**只做控制面板
- 其他快应用通过 `system.pet` 调用同一只鱼

台词来自原项目社区梗，协议 MIT，许可证正文见 `LICENSE.upstream`。设备上不内置 DeepSeek Key，聊天走本地词库。

## 统一控制面板

使用现有 Quick App `ui` 运行时和桌面明暗主题，侧栏尺寸与 `glass_sidebar.h`
保持一致。陪伴、喂食、外观、设置四个分区复用控件；状态卡持续显示台词、心情和
饥饿值，模式、体型与显隐状态以原生桌宠回读为准。相同快照不重复更新文字和样式。
支持 640×400、800×450、1024×504 和 1024×600 内容区。

`preview.html` 直接运行同一份 `engine.js` 和 `app.js`，可通过 `?theme=dark`
或 `?width=640&height=400` 检查布局。浏览器侧使用本地词库和本地状态；旁边的
女仆装立绘是素材预览，不表示已在设备加载。预览正面图对应
`preview-assets/sources.json` 的 `正面.png`，原始字节与 SHA256 保持一致，许可见
`LICENSE.upstream`。

## SD 卡原版形象资源

原版正面、背面、侧面 PNG 来自上游仓库的 `sprites/` 目录。固件不在设备上解码 PNG，
而是优先加载 SD 卡中的 `/sdcard/dafeiyu/dafeiyu.lvbin`。该文件包含小、中、大三种
尺寸的正面、背面、左侧面和右侧面，共 12 张 LVGL `RGB565A8` 图像；颜色与透明度分
平面保存，并带文件级及逐图 CRC32 校验。资源缺失、版本不符、截断或校验失败时，
桌宠自动使用固件内置的几何鲸鱼，不阻止桌面启动。

现有三视图本身就是蓝发女仆装。将生成的 `dafeiyu.lvbin` 与 `LICENSE.upstream`
放到 SD 卡的 `dafeiyu/` 目录；设备首次创建桌宠时自动加载，插卡后也可在
**外观 → 加载女仆装** 手动重读。`system.pet.loadAppearance()` 返回布尔结果，
`getState().appearance` 为 `maid` 或 `fallback`。该接口需要配套的新固件；独立预览或
旧固件不具备接口时会明确提示。载入失败保留当前形象，不清除已加载的有效资源。
重新加载在 GUI 线程上完成，整包校验成功后先解除旧图像及缓存引用，再释放旧像素。

在 `espdl-quickapp` 目录转换上游 PNG：

```sh
python tools/convert_dafeiyu_sprites.py \
  --sprites path/to/dafeiyu-pet/sprites \
  --output path/to/sdcard/dafeiyu/dafeiyu.lvbin
```

当前转换基于上游提交 `5b0e01856116bd2bae82df1f43c32faa5f056196`。发布资源时须连同
本目录的 `LICENSE.upstream` 保留上游 MIT 许可说明。

## 桌面直接语音

无需打开桌宠控制面板：在桌面上长按大肥鱼开始语音识别，按住说话，松手后自动把
识别结果发送给 ESPClaw，流式回复和最终回复直接显示在桌宠气泡中。录音期间轻微移动
不会误触发拖拽，长按也不会继续触发原有单击或双击动作。首次使用仍需在系统服务配置
中填写语音识别凭据和 AI 服务配置；未配置、服务忙碌或网络失败时会直接冒泡提示。

## system.pet

任何快应用都可以用：

```js
if (system.pet && system.pet.getState) {
  system.pet.speak('番茄完成啦');
  system.pet.poke();
  system.pet.feed('fish');
  system.pet.setMode('wander'); // wander | follow | still
  system.pet.follow(200, 300);
  system.pet.chat('你好');
  system.pet.hide();
}
```

| 方法 | 作用 |
| --- | --- |
| `getState()` | 位置、模式、心情、气泡 |
| `setMode(mode)` | `wander` / `follow` / `still` |
| `poke()` | 蹦跳 + 回嘴 |
| `feed(food)` | `fish` `cake` `candy` `dango` `gem` |
| `speak(text)` / `say(text)` | 冒泡 |
| `chat(text)` | 本地回复并冒泡 |
| `weather()` | 播报桌面已缓存的天气；没有数据时摸鱼台词 |
| `follow(x, y)` | 跟随目标点；只 `setMode('follow')` 时走向屏幕中心 |
| `show()` `hide()` `toggle()` | 显隐桌面鱼 |
| `setSize(size)` | `small` `medium` `large` |
| `loadAppearance()` | 重读 SD 女仆装精灵，成功返回 `true`，失败返回 `false` |

## 验证

在仓库根目录运行 `node --test app/espdl-quickapp/tests/dafeiyu.test.cjs app/espdl-quickapp/tests/package.test.cjs`。
`render_dafeiyu.c` 经 `tools/render_all_ui.py --harness-source` 检查真实 LVGL/QuickJS
四个分区、双主题和 60 次复开，服务状态使用离线夹具。
`tools/test_pet_appearance.py --apps <prepared-apps> --cache <lvgl-host-cache> --bundle <dafeiyu.lvbin> --output <host-output>`
运行生产精灵加载／布局／同步代码，检查 12 张图、插卡后加载、隐藏时加载、60 次替换及
CRC 损坏／文件缺失时保留旧像素，启用 ASan/UBSan，并编译真实 QuickJS 接口。
这些是宿主验证，不代表已完成固件链接、刷机或实板屏幕验收。

无原生后端时，快应用会用 `engine.js` 自己跑一份状态，方便预览和打包成独立 QPK。

## 打包

独立 QPK 应以 `dafeiyu/qpk/` 为源目录，使用支持该工程格式的 QPK 打包工具
打包其中的 `app.js` 和 `manifest.json`。本源码包不附带打包工具；
直接以 `dafeiyu/` 为源目录会因为入口脚本缺少 `createPet()` 而失败。
包名 `org.flash.dafeiyu`，版本 1.0.1。固件源码内已嵌入同一套脚本。
