'use strict';

const view = ui.getSize();
const left = 24;
const width = view.width - 48;
const actionsY = view.height - 58;
const statusY = actionsY - 36;
const modeY = statusY - 54;
ui.background(0x101820);
const countLabel = ui.text('正在检测摄像头', left, 16, 18, 0xa9bad1);
const rows = [];
let devices = [], selected = null, modes = [], modeIndex = 0, intervalIndex = 0;
let pending = false, running = false, requestAt = 0, message = '';
let signature = '', intervals = [];
for (let i = 0; i < 5; i++) {
  const button = ui.button('', left, 54 + i * 43, width, 38, () => select(i), 0x23384a);
  ui.setHidden(button, true);
  rows.push(button);
}
const modeButton = ui.button('分辨率', left, modeY, Math.floor((width - 12) / 2), 42,
  () => { if (pending || !modes.length) return; modeIndex = (modeIndex + 1) % modes.length; chooseIntervals(); }, 0x23384a);
const fpsButton = ui.button('帧率', left + Math.floor((width + 12) / 2), modeY, Math.floor((width - 12) / 2), 42,
  () => { if (pending || !intervals.length) return; intervalIndex = (intervalIndex + 1) % intervals.length; renderMode(); }, 0x23384a);
const status = ui.text('', left, statusY, 18, 0xa9bad1);
ui.button('开始预览', left, actionsY, 150, 44, start, 0x168a72);
ui.button('停止', left + 162, actionsY, 110, 44, stop, 0x824b46);
ui.button('刷新设备', left + 284, actionsY, 150, 44, () => refresh(true), 0x23384a);

function key(d) { return d.id + ':' + d.generation; }
function renderMode() {
  const m = modes[modeIndex];
  ui.setText(modeButton, selected && selected.transport === 'csi' ? '1024 x 600 · RGB565' :
    m ? m.width + ' x ' + m.height + ' · MJPEG' : '没有可用的 MJPEG 模式');
  const interval = intervals[intervalIndex];
  ui.setText(fpsButton, selected && selected.transport === 'csi' ? '30 fps' : interval ?
    (10000000 / interval).toFixed(1) + ' fps' : '帧率不可用');
}
function chooseIntervals() {
  intervals = []; intervalIndex = 0;
  const m = modes[modeIndex];
  if (m) {
    if (m.continuous) {
      const min = m.intervals[0], max = m.intervals[1], step = m.intervals[2];
      [m.defaultInterval, min, 333333, 666666, 1000000, max].forEach(v => {
        let n = Math.max(min, Math.min(max, v));
        if (step) n = min + Math.floor((n - min) / step) * step;
        if (!intervals.includes(n)) intervals.push(n);
      });
    } else intervals = m.intervals.slice();
    const preferred = intervals.findIndex(v => v >= 666666);
    intervalIndex = preferred >= 0 ? preferred : Math.max(0, intervals.indexOf(m.defaultInterval));
  }
  renderMode();
}
function select(index) {
  if (pending || running || system.camera.status().busy || !devices[index]) return;
  selected = devices[index]; modes = []; modeIndex = 0;
  if (selected.transport === 'usb') {
    try { modes = system.camera.modes(selected.id, selected.generation); }
    catch (_) { message = '设备已断开，请刷新'; selected = null; }
    let best = modes.findIndex(m => m.width === 640 && m.height === 480);
    if (best < 0) best = modes.findIndex(m => m.width <= 1280 && m.height <= 720);
    if (best >= 0) modeIndex = best;
  }
  chooseIntervals();
  message = selected ? '已选择：' + Array.from(selected.name).slice(0, 35).join('') : message;
  renderRows();
  ui.setText(status, message);
}
function renderRows() {
  for (let i = 0; i < rows.length; i++) {
    const d = devices[i];
    ui.setHidden(rows[i], !d);
    if (d) ui.setText(rows[i], (selected && key(d) === key(selected) ? '[当前] ' : '') +
      (i + 1) + '. ' + d.name + ' · ' + d.transport.toUpperCase() +
      (d.previewSupported ? '' : ' · 不支持 MJPEG'));
  }
  const csi = devices.filter(d => d.transport === 'csi').length;
  ui.setText(countLabel, '已连接 ' + devices.length + ' 个摄像头  ·  CSI ' + csi + '  /  USB ' + (devices.length - csi));
}
function refresh(force) {
  let current;
  try { current = system.camera.devices(); }
  catch (_) { ui.setText(status, '设备查询失败，请重试'); return; }
  const next = current.map(key).join(',');
  if (!force && next === signature) return;
  signature = next; devices = current;
  if (selected && !devices.some(d => key(d) === key(selected))) {
    pending = false; system.camera.stop(); running = false; selected = null;
    modes = []; intervals = []; message = '摄像头已断开';
  }
  renderRows(); renderMode();
  if (!devices.length) message = '未检测到摄像头，请连接 CSI 或 USB 摄像头';
  if (!selected && devices.length && !system.camera.status().busy) select(0);
  ui.setText(status, message);
}
function start() {
  if (pending || running) return;
  refresh(false);
  if (!selected || !selected.previewSupported) { ui.setText(status, '请选择可预览的摄像头'); return; }
  if (selected.transport === 'usb' && !modes.length) return;
  system.camera.stop(); pending = true; requestAt = Date.now();
  ui.setText(status, '正在准备预览…');
}
function stop() {
  pending = false; running = false; system.camera.stop();
  message = '预览已停止'; ui.setText(status, message);
}
function tick() {
  const state = system.camera.status();
  if (pending) {
    if (Date.now() - requestAt > 10000) { pending = false; ui.setText(status, '相机释放超时，请稍后重试'); return; }
    if (state.busy) return;
    pending = false;
    const m = modes[modeIndex];
    running = system.camera.startDevice(selected.id, selected.generation, m ? m.index : 0, intervals[intervalIndex] || 0);
    if (!running) ui.setText(status, '启动失败，请刷新设备后重试');
    return;
  }
  if (running && !state.busy) {
    running = false;
    message = state.error ? '预览已停止（' + state.error + '），请检查连接或更换视频模式' : '请选择摄像头，然后开始预览';
    ui.setText(status, message);
  }
  refresh(false);
}
refresh(true);
setInterval(tick, 500);
globalThis.CameraPreview = { start, stop, refresh: () => refresh(true), frames: () => system.camera.frames() };
