'use strict';
const info = app.getInfo();
let launches = Number(system.storage.get('launches') || 0) + 1;
system.storage.set('launches', String(launches));
const left = Math.floor((ui.getSize().width - 400) / 2);
ui.text(info.packageName + '  v' + info.versionName, left, 104, 16, ui.secondary);
ui.text(info.name, left, 52, 28, ui.primary);
ui.panel(left, 150, 400, 96, ui.surface, 16, 255);
ui.number(String(launches), left + 20, 164, 80, 64, ui.primary);
ui.text('累计启动次数', left + 120, 180, 20, ui.secondary);
ui.button('Toast 提示', left, 274, 400, 54, () => {
  prompt.showToast({message: '来自真正 QuickJS QPK 的问候'});
}, 0x6677f5);
ui.button('对话框', left, 344, 400, 54, () => {
  prompt.dialog({title: 'QPK 运行时',
    message: '界面由 JavaScript 创建，事件由 QuickJS 执行。'});
}, ui.surface);
let secs = 0;
const tick = ui.text('已运行 0 秒', left, 426, 16, ui.secondary);
setInterval(() => { secs++; ui.setText(tick, '已运行 ' + secs + ' 秒'); }, 1000);
console.log('hello QPK initialized');
