# 大肥鱼桌宠

当前接入树：`04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop/`。

把 [dafeiyu-pet](https://github.com/1190fasheqi/dafeiyu-pet) 做成 OpenVela 本地应用：

- **本地原生层**一直在桌面上走（`pet_engine` + `pet_lvgl`）
- **快应用**只做控制面板
- 其他快应用通过 `system.pet` 调用同一只鱼

台词来自原项目社区梗，协议 MIT，许可证正文见 `LICENSE.upstream`。设备上不内置 DeepSeek Key，聊天走本地词库。

## SD 卡原版形象资源

原版正面、背面、侧面 PNG 来自上游仓库的 `sprites/` 目录。固件不在设备上解码 PNG，
而是优先加载 SD 卡中的 `/sdcard/dafeiyu/dafeiyu.lvbin`。该文件包含小、中、大三种
尺寸的正面、背面、左侧面和右侧面，共 12 张 LVGL `RGB565A8` 图像；颜色与透明度分
平面保存，并带文件级及逐图 CRC32 校验。资源缺失、版本不符、截断或校验失败时，
桌宠自动使用固件内置的几何鲸鱼，不阻止桌面启动。

在 `espdl-quickapp` 目录转换上游 PNG：

```sh
python tools/convert_dafeiyu_sprites.py \
  --sprites path/to/dafeiyu-pet/sprites \
  --output path/to/sdcard/dafeiyu/dafeiyu.lvbin
```

当前转换基于上游提交 `5b0e01856116bd2bae82df1f43c32faa5f056196`。发布资源时须连同
本目录的 `LICENSE.upstream` 保留上游 MIT 许可说明。

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

无原生后端时，快应用会用 `engine.js` 自己跑一份状态，方便预览和打包成独立 QPK。

## 打包

独立 QPK 应以 `dafeiyu/qpk/` 为源目录，使用支持该工程格式的 QPK 打包工具
打包其中的 `app.js` 和 `manifest.json`。本源码包不附带打包工具；
直接以 `dafeiyu/` 为源目录会因为入口脚本缺少 `createPet()` 而失败。
包名 `org.flash.dafeiyu`，版本 1.0.1。固件源码内已嵌入同一套脚本。
