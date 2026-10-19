# 本地助手工具

助手模式新增八个 `assistant_*` 工具，直接随模型请求提供 schema，也可从 `board` MCP 发现并调用。
完整桌面构建注册 34 个 board 工具：11 个应用／硬件工具、7 个 home 工具、8 个 music 工具、8 个本地助手工具。
发现结果按 16 KiB 上限分页，有 `nextCursor` 时继续；未链接 Home Assistant 的构建按实际注册能力返回。
普通聊天和创作模式不能调用这些助手工具。

| 工具 | 用途 | 成功依据 |
| --- | --- | --- |
| `assistant_status` | 时钟、时区默认值、调度运行、本地资料数量、播报请求状态 | 时钟有效、对应服务或请求状态 |
| `assistant_diagnostics` | Wi-Fi 链路／IPv4 租约、堆、存储、电池 | 实际采样；未取得的字段标为 unavailable |
| `assistant_weather` | 当前定位的天气、缓存年龄、请求刷新、播报 | `valid`、`stale`、`report_time`；播报另看 `speech` |
| `assistant_speak` | 中文文本播报 | 受理返回 `speech_request`，再核对播报状态 |
| `assistant_camera` | 摄像头清单、拍照、本地分类、结果查询及按请求取消 | 对应 request_id 完成；保存另需 path 且无保存错误 |
| `assistant_records` | 本地笔记、待办、日程的增删改查 | 完成持久化后返回 item |
| `assistant_schedule` | 一次性／每天的提醒、播报、停音乐和家居动作 | 保存后查任务 state/result；家居还需 verified |
| `assistant_alias` | 用户自定义设备／应用／歌曲别名、查询与删除 | 返回匹配结果，本工具不执行控制 |

## 本地笔记、待办和日程

资料保存在 `/data/assistant`，不需要外部账号，不上传云端。每种资料最多 16 条，每个快照最多 8 KiB；
每条 title 最多 96 个 UTF-8 字节，text 最多 512 字节。日程时间使用 UTC Unix 秒，界面和模型需按用户时区转换。
默认提示使用北京时间（UTC+8）；可通过 `assistant_status` 读取 now，不能依赖模型记忆的当前时间。

```javascript
// “记一下，买牛奶和面包。”
assistant_records({operation:"add",kind:"note",request_id:"shopping-001",title:"购物清单",text:"牛奶、面包"})
// “看看我的购物笔记。”
assistant_records({operation:"list",kind:"note",query:"购物"})
// “把买牛奶记成待办。”
assistant_records({operation:"add",kind:"todo",request_id:"milk-001",title:"买牛奶"})
// id 必须替换为刚查到的实际编号。
assistant_records({operation:"update",kind:"todo",id:1,done:true})
assistant_records({operation:"delete",kind:"note",id:1})
```

新增 request_id 在保留记录期间用于去重；相同编号和内容返回已有 item，内容冲突明确拒绝。
修改或删除前先查询真实记录，不能假设第一条就是用户指代的内容。列表每页最多 8 条，按 next_offset 继续。
日程 `kind:"event"` 必须提供 due_at，可附 end_at；用 from_at/until_at 按时间范围查询，跨越查询窗口的日程也会匹配。
保存日程／待办不会隐式创建提醒，需要提醒时另建定时任务。

## 聊天结束后继续执行的任务

调度器由桌面启动，独立工作线程执行，不调用模型、不依赖聊天页面存在。
无临近任务时缓存下次检查时间，避免每秒读取闪存。用户修改定时任务后立即使缓存失效。

```javascript
// “20 分钟后提醒我喝水。”
assistant_schedule({operation:"add",request_id:"water-001",title:"喝水提醒",delay_seconds:1200,
  action:"notify",arguments:{text:"该喝水了"}})
// “20 分钟后停止音乐。”
assistant_schedule({operation:"add",request_id:"music-stop-001",title:"停止音乐",delay_seconds:1200,
  action:"music_stop",arguments:{}})
// “每天晚上 10 点关闭客厅灯。”先 home_list 得到实际实体。
assistant_schedule({operation:"add",request_id:"night-light-001",title:"客厅灯定时关闭",daily_at:"22:00",
  timezone_offset_minutes:480,action:"home_control",arguments:{entity:"light.living_room",on:false}})
assistant_schedule({operation:"list"})
assistant_schedule({operation:"cancel",id:1})
```

时间只能选择 delay_seconds、due_at 或 daily_at 之一；每日任务按固定 UTC 偏移运行，不实现夏令时区规则。
一次性时间须在未来一年以内，系统时钟未校准时拒绝创建。支持 notify、speak、weather_speak、music_stop、home_control、home_service，
没有任意 shell、HTTP、模型提示或硬件脚本执行入口。家居动作复用共享白名单、参数校验及执行时的权限开关和状态回读。

任务最多 16 条，并受 8 KiB 快照限制。已完成记录保留在列表中，用户可明确删除，服务不自动删除其他任务腾出空间。
状态包括 pending、executing、done、failed、missed、interrupted、cancelled。done 表示动作函数已返回，
结果中的 accepted／verified 仍须分别判断；播报受理后需继续核对 speech_request，不代表已经播完。

每次动作执行前先持久化 executing。执行中崩溃或完成结果写入失败，重启后标为 interrupted，
不会再次执行。超过到期时间两分钟的任务标为 missed，每日任务跳至未来下一次，不补发过去的动作。
设备必须开机运行，不能在断电期间唤醒自己执行。通知在桌面线程显示；相机独占显示期间暂缓取出通知。
最多暂存最近八条提示，完整任务及结果仍可查询。

每类数据使用两个独立快照轮换，临时文件 flush/fsync 后提交，适配 SmartFS 的目标文件覆盖行为。
无效或不可读资料不会静默清空；定时快照损坏时拒绝执行，避免退回旧的 pending 记录造成重放。
该设计不承诺外设动作与闪存提交之间的分布式事务；回执不明确时优先报告中断，不能自动补做。

## 天气、播报与诊断

```javascript
assistant_weather({})                         // 当前定位的实测天气
assistant_weather({refresh:true})             // 请求更新，仍可能返回旧缓存
assistant_weather({speak:true})               // 按当前有效数据播报
assistant_speak({text:"会议十分钟后开始"})
assistant_status({})                          // 核对 speech_request 与 active/error
assistant_diagnostics({})
```

天气复用系统服务的每分钟手动刷新限制，不绕过配额，不新增常驻网络轮询。
不提供任意城市和未来降雨预报；未接相应外部服务时，不能把当前天气当作未来预测。
缓存数据会标记 stale、age_ms 和 report_time，播报缓存也会明确说“缓存天气”。
音频忙碌或缺少端侧语音构建／模型时明确返回错误，不自动停止正在播放的音乐或录音。

网络诊断提供链路和租约，不将其解释为互联网已可达。堆和存储取真实统计，
电池没有驱动读数时不猜电量。诊断不会修改网络配置或清理用户文件，也不输出配置令牌。

## 拍照与本地识别

```javascript
assistant_camera({operation:"list"})
// USB 的 device 与 generation 必须取自清单；不能使用断开前的旧代次。
assistant_camera({operation:"capture",device:0,generation:1})
assistant_camera({operation:"photo_status",request_id:1})
assistant_camera({operation:"recognize",device:0,generation:1})
assistant_camera({operation:"recognition_status",request_id:2})
assistant_camera({operation:"cancel",request_id:2})
```

USB 拍照直接复用单次采集线程，普通拍照不加载分类模型；识别拍一帧后用已有 ESP-DL 模型分类。
结果轮询完成时保存 320×240 BMP 到 `/data/photos`，重复查询复用同一照片；返回的是设备文件路径，不上传图像。
CSI 拍照复用原生相机预览：先打开相机、按实际按钮启动预览，再调用不带 device 的 capture，按返回编号查询 photo_status。
预览未就绪会明确失败；打开相机本身不等于拍照成功。

请求编号由助手封装，旧请求、别的页面替换的请求不会被误认。取消只针对自己的底层识别请求。
识别成功和照片保存成功分别返回，photo_save_error 时不能声称照片已保存。
当前能力是本地分类，不包含通用视觉问答、OCR 或人脸身份识别；USB 模型识别须有实际摄像头及已部署模型。

## 精确别名

```javascript
// 只有用户明确说“以后把客厅灯叫大灯”才保存；目标须先发现并核实。
assistant_alias({operation:"save",alias:"大灯",kind:"home",target:"light.living_room"})
assistant_alias({operation:"lookup",alias:"大灯",kind:"home"})
assistant_alias({operation:"list"})
assistant_alias({operation:"delete",alias:"大灯"})
```

lookup 仅接受已保存别名的完整名称；“灯”不会匹配“大灯”，“关一下大灯”也不会匹配“大灯”。
每设备最多保存 16 个别名，现有用户命名数据保留。灯光控制只解析完整设备名或精确实体 ID；
同名设备必须澄清。只读设备搜索仍可列出候选，但不据近似名称执行控制。

已移除口语动作映射、历史目标推断和默认增减值等规则。
必要的目标、动作、参数或时间不明确时先询问，不执行补全后的指令。
语音转文字直接保留完整识别结果；别名服务和模型提示不改写识别文字。

## 开发验证

```bash
python3 tools/generate_assistant_tools.py
python3 tools/generate_agent_profiles.py
python3 tools/test_assistant_service.py --apps <prepared-apps> --output <output>/service
python3 tools/test_assistant_platform.py --baseline <prepared-workspace> --output <output>/platform
python3 tools/test_agent.py --apps <prepared-apps> --output <output>/agent
python3 tools/cross_compile_assistant.py --baseline <prepared-workspace> --output <output>/cross
```

服务测试使用真实 JSON、双槽持久化、定时逻辑与新进程恢复；平台测试使用真实天气／播报格式化、
相机采集／分类工作线程和 BMP 保存。硬件、模型和网络边界使用离线替身。
本次输出目录为工作区 `output/assistant-all-20261019`；整机编译、实板验证和离线回归的结果分别记录。
若 SD 卡存在 `/sdcard/espclaw/agents/assistant/agent.md` 覆写文件，还需更新它，才能使用新的默认运行提示。

## 设备内部功能

`home_device({entity:"fan.example",query:"童锁"})` 按实际设备注册关联搜索其内部功能。entity 必须是已发现的精确实体；query 是只读筛选，不建立别名或模糊控制映射。省略 query 分页列出全部功能，每页最多 8 项，并受 12KiB 内容预算限制；使用 next_offset 继续。member_total 是同设备状态实体数，total 是匹配数。关联不可用时只返回当前实体，不能按名称合并。

每个 function 保留自己的 entity、raw_state、经过筛选的 attributes、实际已注册的 actions 和参数范围/选项。设备控制器、童锁、指示灯、工作模式、数值设定等不能互相替代。传感器没有写入口；AI 的门锁和安防排除规则保持不变。

写入继续使用 home_service，非灯光状态型控制会先读取最新能力，拒绝超范围、非法步长、猜测选项和不支持的服务。这里只校验本轮参数，不添加默认数值或联动动作。低温/高温必须成对且顺序有效。HTTP 受理后按相应属性回读；没有可观察完成属性的动作只显示已受理，不宣称 verified。

`tools/test_home_functions.py --apps <prepared-apps> --output <目录>` 检查真实 AI/共享 HA 服务代码，HTTP 使用脚本替身，覆盖同设备筛选、同名隔离、风向、叶片、水平摆风、温度区间、数值步长、枚举拒绝和写后确认，不操作家中设备。
