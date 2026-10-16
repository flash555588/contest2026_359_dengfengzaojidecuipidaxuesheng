# v3 应用宿主测试

本目录保存 JS 回归、C 宿主用例与少量测试输入，不是实板测试结果目录。
返回[应用快照说明](../README.md)，已有输出见 [evidence](../evidence/README.md)。

## Node.js 回归

在仓库根目录、具备 `node --test` 的 Node.js 环境运行：

```bash
node --test app/espdl-quickapp/tests/dafeiyu.test.cjs app/espdl-quickapp/tests/package.test.cjs
node --test app/homeassistant/tests/homeassistant.test.cjs
```

打包测试会按字节检查 JS 与 C 资源。LF/CRLF 的变化可能导致资源一致性失败，特别是
Windows 自动转换换行符的检出；应核对实际源码字节和 Git 属性，不能删除断言掩盖差异。
根级 Home Assistant 的生成能力以[当前脚本说明](../../homeassistant/README.md)为准。

## C 与 QuickJS 用例

`run_security.py` 要求宿主 `cc`、AddressSanitizer/UndefinedBehaviorSanitizer 及匹配的
QuickJS 源码目录；`--quickjs` 指向源码而不是可执行程序。在满足依赖的 Linux/WSL 中，
可从仓库根目录运行：

```bash
python3 app/espdl-quickapp/tests/run_security.py --quickjs /path/to/quickjs-source
```

其他 `test_*.js` 和 C 用例可能需要额外参数或独立编译，应先阅读具体文件。
依赖缺失、用例跳过、构建失败和断言失败须分别记录；宿主通过不代表外设或真实服务已联通。

## SD 动画调度与画面对比

`grok_schedule_host.c` 直接运行真实 LVGL 定时器和 SD 读取线程，比较改动前后的
帧序列、RGB565+A8 像素哈希及最终 framebuffer 哈希。它覆盖五种展示方式、全部
32 种表情、自旋/注视、隐藏/暂停/减少动态效果、输入唤醒和 tick 回绕。
读取线程在每个虚拟毫秒后完成工作，以排除宿主磁盘速度和线程调度的随机影响。

先将改动前的 `glass_grok_player.inc` 保存到独立目录，再在 Linux/WSL 运行：

```bash
python3 tools/test_grok_schedule.py \
  --apps /path/to/prepared/apps \
  --cache /path/to/prepared/lvgl-host-cache \
  --assets /path/to/grok/sdcard \
  --before /path/to/before-player-directory \
  --output /path/to/comparison-output
```

需要已经准备好的带 ASan/UBSan 的 `lvgl-*.o`、匹配的 `lv_conf.h` 和五个 `.gbs`
素材文件。输出包括逐帧 trace、编译/执行日志和 `comparison.json`。回调和 seek
次数是确定性宿主工作量指标，不能作为实板 CPU 占用或功耗降幅。

## 原生交互动效

`glass_motion.h` 提供统一的按压、页面进入和提示条动效。它使用共享时长和定点 Hermite
插值，保留中途变更目标时的位置与速度，按压 80 ms、归位 180 ms、页面进入 240 ms；提示条进入
200 ms、退出 140 ms。这些时长是本工程对 ESP32-P4 的适配值。

控制器固定保留 12 个槽位，只在动效运行时唤醒共享定时器。页面使用短距离位移，
提示条只改变标签本身的背景/文字透明度；不创建整页截图或整组透明度缓冲。
快应用按钮通过可选原生 hook 复用反馈，原点击回调和触摸冒泡保持原有执行路径。
按压期间服务返回的新颜色会成为归位后的状态，避免还原旧的设备状态。
“减少动态效果”会立即结束现有反馈并禁用后续位移。页面删除会取消延后启动和活跃槽位。

```bash
python3 tools/test_interaction_motion.py \
  --apps /path/to/prepared/apps \
  --cache /path/to/prepared/lvgl-host-cache \
  --output /path/to/motion-test-output

python3 tools/render_all_ui.py \
  --apps /path/to/prepared/apps \
  --cache /path/to/prepared/lvgl-host-cache \
  --grok-assets /path/to/grok/sdcard \
  --harness-source app/espdl-quickapp/tests/render_interaction_motion.c \
  --output /path/to/motion-render-output
```

前者覆盖像素还原、状态颜色更新、100 次中断按压、禁用/取消、tick 回绕、槽位饱和、
对象删除和空闲暂停；后者在真实桌面与 QuickJS 桥上检查两种主题的页面、对话框、
提示条、快应用按钮和减少动态效果。宿主逐帧预览不代表实板帧率或长期稳定性验收。

原生应用页分页选中节点为 20×6 长条，其他节点为 6×6 圆点；240 ms 形状过渡和 160 ms 颜色过渡保持节点居中。应用页复用页面、网格和分页器，只重建当前页的网格内容；内容短距离移动时，分页长条保持连续。

`ui.setPos(id,x,y,true)`、`ui.setSize(id,w,h,true)`、`ui.setColor(id,color,true)` 仅对装饰对象启用动画；点击目标保持直接更新。重复目标不重置截止时间，几何反向保留速度。滑条和色盘拖动立即更新；设备回报、点击预设和开关状态可平滑过渡，数值文本保留真实值。

`ui.range(id,stop,accent,background,animate)` 对装饰轨道的 0–255 渐变边界插值，保证 0% 和 100% 端点颜色正确，复用现有 12 槽原生调度器。`ui.present(handles,offset)` 最多接受 128 个句柄和 ±24 px 位移，使用一个批量任务；只移动可见装饰对象，按钮和命中区域保持直接交互。两个新增绑定均在全部 JS 参数转换后验证句柄世代，拒绝删除、重用、超限及异常 getter；只提供显示能力，不增加设备控制权限。

详情打开、选项切换和返回采用相反方向的短位移。边缘返回在 72 px 内跟手最多 24 px，取消时 180 ms 归位，确认时 140 ms 完成后执行返回；页面删除取消手势与待执行回调，锁屏不拖动底层页面，对话框保留原有返回语义。

刷新节奏跟随 `LV_DEF_REFR_PERIOD` 并限于 16–33 ms（本板为 33 ms）。固定槽位、固定批量存储，无逐帧堆分配、截图、整页透明度或额外定时器；空闲暂停。关闭动效立即完成目标和复位。宿主 ASan/UBSan 测试覆盖双主题、原生指针分发、滑条端点和反向、批量成员删除、减少动效、参数转换期间删除及任务饱和；宿主预览不代替实板帧率测试。

第二批动效：侧栏复用第一个菜单底块，在固定文字和命中区域下移动；设备列表翻页使用 `ui.present(handles,offset,true)` 横向批量位移，页脚与分页器不加入该批次。省略第三参继续纵向过渡；轴向切换、中断和删除均还原对应轴的本地样式，不影响另一轴。

详情模式选项底色使用同一个 160 ms 装饰填色通道，不更改选项请求和设备回报逻辑；首次显示、设备复用与不可用状态直接呈现。滤芯余量条只对真实数值变化插值，零值隐藏填充并使用合法的最小目标宽度。

顶部快捷控制面板以 240 ms 展开、140 ms 收起；收起中可原位置反向展开，不重建面板。音乐、设置、锁屏等按钮立即执行原回调，系统/主题切换直接清理面板，销毁事件清空引用；减少动效时开关均立即生效。沿用原定时器与 12 个任务槽，未增加循环装饰动画、截图缓存或线程。

米家分页改为每个分页器保留一根 20×6 选中长条，背景圆点固定 6×6。主列表、详情及选项分页共用同一实现，切换时只对长条位置插值，不改变宽高或淡出/重建选中标记；快速反向保留位置和速度。详情/选项重绘保留分页器的逻辑可见状态，关闭视图时才复位，避免每次渲染都跳到终点。

新增验证覆盖双主题左→中→右→中→左、长条句柄不变与宽度不变、选项分页重复重绘、原生中途反向的连续性，以及减少动效下立即定位。普通切页的分页动画从两个几何加两个颜色任务降为一个几何任务；命中区域与服务调用保持原行为。

## 米家右侧瀑布流

当前原生运行时将主列表改为右侧连续瀑布流：侧栏、标题和页脚固定，按卡片高度向最短列排布，随宽度调整列数。主列表不再分页；详情、选项及桌面应用分页仍保留。缺少 viewport API 的旧运行时继续使用原分页布局。

原生 LVGL viewport 提供裁剪、惯性滚动和滚动条。前端复用 12 个卡片槽位及 64 个按钮槽位，不为每个设备建立原生对象；只在可见窗口变化时重新绑定内容。滑条拖动期间锁定滚动，释放或取消时解锁；打开详情再返回保留滚动位置，侧栏切换重置位置。

卡片滑条在触点移动达到阈值后判断方向：横向达到 5 px 开始实时调节，纵向达到 8 px 且超过横向的 1.2 倍则让给原生滚动，不发送设备指令。小幅抖动不更新亮度，点击松手仍可选值；快速纵向松手及取消不提交。详情滑条保持原有直接拖动。卡片实时数值保留开关状态文字，白色手柄保持在滑轨前方；双主题像素断言检查复用后的实际显示。

`ui.rangeLinked(track,stop,accent,background,animate,thumb)` 将填充和白色手柄交给同一个原生标量任务，保留反向时的位置与速度，每帧只根据同一插值计算两者位置；不增加定时器或槽位。两个对象必须为同父级的装饰对象，手柄句柄限定数值，并在所有参数转换之后检查双方世代及父级。删除手柄解除引用，减少动效直接完成目标；旧运行时继续使用独立的原有绑定。

同一主页、同一连接世代的布局刷新，以最靠近视口顶部且仍存在的设备维持屏幕高度；上方设备删除或高度变化不会直接顶走当前内容。手势期间不强制重定位，侧栏切换继续回到顶部。卡片显示等待执行、正在提交、正在核对及真实状态核对后的已确认；场景仅显示已受理，失败显示结果未确认。反馈最多保留 32 条，通过已有 tick 到期清理，无额外循环或重试请求。

滚动通知仅在虚拟窗口的设备映射改变时安排 JS 绘制，已有卡片由 LVGL 原生移动；小范围滚动不触发页面重绘。每轮仍按列数分批绘制，但新进入视口的卡片先于保留卡片及预加载卡片处理，池大小保持 12。所有右侧原生按钮共用按下目标检查：记录设备、连接世代和滚动位置，禁止在按压期间复用槽位后控制其他设备，也禁止滚动或取消手势触发场景及步进按钮。新按下手势重新捕获目标，不影响后续正常点击。

输入清理统一取消滑块、色盘及待提交手势，并立即释放原生滚动锁。断线、请求失败、设置页、详情切换、销毁及非法触点都会清理；后台同步导致设备移除、离线、滑条能力消失或量程/步长改变时，在下一轮渲染或输入事件中取消。迟到的松手不提交旧值。有效滑块及色盘手势期间暂停页面重绘，保留手指下的本地预览；取消后恢复最新服务器状态。回归覆盖设置按钮和接口两种入口，并在真实 LVGL 中检查切页及断线时的 SCROLLABLE 标志立即恢复。

弱网下对同一设备、同一操作键、同一服务和同一参数的尚未完成目标去重，包括亮度及模式参数；参数键按名称比较，参数值逐项比较。去重只针对最新目标：反向后再返回旧值仍替换尚未发送的操作，完成后新的显式操作可以再次提交。沿用原 16 项队列限制和串行请求，不增加权限或自动重试。等待达到 4 秒时，现有 tick 仅触发一次反馈更新：未发送显示等待前序操作，已发送显示等待设备回报；仍以服务器状态核对决定成功，超时及错误继续采用原失败路径。

设备查找复用本地同步数据：顶部提供名称/实体 ID 搜索、房间和关注筛选。房间来自设备注册表，搜索覆盖设备组全部成员；关注入口位于详情页，取消关注会清理该设备组成员的关注记录。搜索回调检查连接世代。筛选只影响本地展示，不改变同步范围和设备控制权限，清除筛选恢复完整列表。筛选面板复用固定按钮，打开时隐藏并锁定整个瀑布流父容器，所有底层卡片及滚动条一起隐藏；关闭后恢复原滚动位置。面板高度按实际选项收紧，最小屏幕分页显示两个房间，其他屏幕三个，避免按钮重叠。

无设备、未连接、无匹配结果及空日志提供不同说明；无匹配结果可清除筛选，未连接可进入连接设置。顶部四个入口均使用互不重叠的 44×44 触控区。无文字的原生命中按钮设为透明，避免筛选面板关闭后复用按钮遮住导航文字。通用详情优先展示主设备的调节和动作，附加字段通过“更多参数”展开；专用详情保留既有主控制布局，附加设备字段可收起。宿主测试验证最小尺寸、筛选分页、空态恢复、关注分组及参数展开都不产生设备控制指令。

`glass_ui_metrics.h` 在显示线程监听 LVGL 实际绘制/刷新事件，无采样定时器。部署后运行 `desktop ui render-metrics-reset`，操作一段时间，再运行 `desktop ui render-metrics`，可读取绘制次数、平均/最大 CPU 绘制耗时、平均/最大完整刷新耗时及原生控件数量；通过已有 UI 命令队列读取，不跨线程访问 LVGL。结合 `free` 检查持续运行内存。刷新包含显示等待，绘制次数不应直接当作面板刷新率，也不能据此声称测得手指到屏幕的实际延迟。未烧录时只能记录旧固件基线。

实板升级必须保留既有数据分区。当前板卡的 SMARTFS 格式扇区位于 `0xC00000`，大小 `0x400000`；把挂载起点改为 `0xE00000` 会读到原文件系统中间，导致挂载失败及本地设置不可读。不得通过格式化解决此问题。`build_bilibili_v3.py` 默认 `--storage-layout preserve`，保持准备好的配置；`12m-4m` 和 `14m-2m` 必须显式选择，选择布局本身不迁移数据。打包工具沿用构建元数据中的真实边界，固件末尾不得越过数据起点。`tools/tests/test_prepared_storage_layout.py` 覆盖两种既有布局保持、缺失/不一致配置拒绝和显式覆盖。

标题及状态为右侧电源按钮预留同样宽度，避免重叠和重复的开关文字；较长读数先缩至 24/20 字号，再按宽度省略。宿主输出 `WATERFALL_HOST_PERF` 记录 24 次窗口更新耗时和对象数，这是启用 ASan/UBSan 的宿主工作量指标，不是实板帧率。真实串口 `desktop cpu-status` 与 `free` 可用于部署前后基线对照；未经烧录的旧固件数据不能作为新布局的性能结果。

`ui.viewport`、`ui.attach`、`ui.scrollInfo`、`ui.scrollTo`、`ui.scrollExtent`、`ui.scrollLock` 限制父子关系及尺寸，保留装饰对象的逻辑坐标。滚动通知延后到 LVGL 异步队列，合并待处理通知，并在参数转换和回调之后检查对象世代；删除 viewport 会取消尚未执行的通知。滚动和布局不增加设备控制权限。

```bash
python3 tools/render_all_ui.py \
  --apps /path/to/prepared/apps \
  --cache /path/to/prepared/lvgl-host-cache \
  --grok-assets /path/to/grok/sdcard \
  --harness-source app/espdl-quickapp/tests/render_waterfall.c \
  --output /path/to/waterfall-render-output
```

该宿主检查使用真实 LVGL 与 QuickJS，覆盖双主题 512 张卡片、原生指针拖动、顶部/中部/底部、固定对象池、详情目标和返回位置、参数转换期间删除及滚动回调删除自身。ASan/UBSan 检查通过不代表实板帧率或功耗验收；固件仅编译，不自动烧录。

### Built-in HA compile cache

`run_compile_cache.py --quickjs <vendored-quickjs>` runs the production cache with real QuickJS under ASan/UBSan. It verifies fresh globals across 30 independent runtimes, ineligible-source bypass, invalid-version recovery, syntax errors, eviction before execution, the 512 KiB cap, and compilation/deserialization of the full reviewed HA source. Optional `--objects <render-output>` reuses sanitizer objects from `render_all_ui.py`.

Only a built-in HA launch with the exact reviewed SHA256 and all existing grants may use the process-local cache. Every launch still checks identity and source authorization and creates a fresh runtime/context. No disk bytecode is accepted, and credentials, live JS objects and UI state are never retained. First launch still parses source; repeat launches reuse at most 512 KiB of serialized code until reboot. Console startup logs distinguish `cache=hit/miss`; compile timing includes cache serialization/deserialization.

### Entity lookup and static frontend data

Live entity/scene lookups use null-prototype indexes rebuilt lazily on committed revision or array replacement. Source state remains authoritative: late callbacks resolve the latest row, and disconnect/configuration changes cannot reuse old entities. The 512-sensor regression checks bounded full-list traversal, refreshed reading provenance, removal and configuration reset. Static profile/reference catalogs use safely JSON-encoded data with identical contents, avoiding object-literal initialization bytecode. The common profile generator owns this encoding and still regenerates exact-source authorization. Commented obsolete layouts are removed from the shipped script.
