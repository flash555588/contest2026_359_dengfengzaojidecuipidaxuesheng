'use strict';
const info = app.getInfo();
let launches = Number(system.storage.get('launches') || 0) + 1;
system.storage.set('launches', String(launches));
const left = Math.floor((ui.getSize().width - 400) / 2);
const height=ui.getSize().height;
ui.background(ui.theme?ui.theme.background:ui.card);
ui.text(info.packageName + '  v' + info.versionName, left, 72, 16, ui.secondary);
ui.text(info.name, left, 28, 28, ui.primary);
ui.panel(left, 112, 400, 96, ui.theme?ui.theme.card:ui.surface, 18, 255);
ui.number(String(launches), left + 20, 126, 80, 64, ui.primary);
ui.text('累计启动次数', left + 120, 142, 20, ui.secondary);
ui.button('Toast 提示', left, height-168, 400, 44, () => {
  prompt.showToast({message: '来自真正 QuickJS QPK 的问候'});
}, ui.accent);
ui.button('对话框', left, height-112, 400, 44, () => {
  prompt.dialog({title: 'QPK 运行时',
    message: '界面由 JavaScript 创建，事件由 QuickJS 执行。'});
}, ui.surface);
let secs = 0;
const tick = ui.text('已运行 0 秒', left, height-44, 16, ui.secondary);
setInterval(() => { secs++; ui.setText(tick, '已运行 ' + secs + ' 秒'); }, 1000);
console.log('hello QPK initialized');
