'use strict';

const view = ui.getSize();
const left = 24, width = view.width - 48;
// The runtime publishes the desktop's card, surface, text and accent colours.
const light = ((ui.card >> 16) & 255) + ((ui.card >> 8) & 255) + (ui.card & 255) > 384;
const colors = {
  page: ui.card, card: ui.surface, primary: ui.primary, secondary: ui.secondary,
  accent: ui.accent, onAccent: light ? 0xffffff : 0x252d46,
  border: light ? 0xcbd0e6 : 0x505975
};
const labels = new Map(), enabled = new Map();
let devices = [], selected = null, modes = [], modeIndex = 0, intervalIndex = 0;
let pending = false, running = false, requestAt = 0, message = '请选择摄像头后开始预览';
let signature = '', intervals = [], photoBaseline = '';

function write(id, value) {
  value = String(value);
  if (labels.get(id) !== value) { ui.setText(id, value); labels.set(id, value); }
}
function label(value, x, y, w, h, font, color, center) {
  const id = ui.text(value, x, y, font, color);
  ui.setSize(id, w, h); ui.setStyle(id, {center: !!center, ellipsis: true});
  labels.set(id, value); return id;
}
function card(x, y, w, h, radius, fill) {
  const id = ui.panel(x, y, w, h, fill === undefined ? colors.card : fill);
  ui.setStyle(id, {radius: radius, borderColor: colors.border, borderWidth: 1, borderOpacity: 100});
  return id;
}
function button(value, x, y, w, h, action, primary) {
  const id = ui.button(value, x, y, w, h, action, primary ? colors.accent : colors.card);
  ui.setStyle(id, {radius: 12, borderColor: colors.border, borderWidth: primary ? 0 : 1,
    borderOpacity: 110, textColor: primary ? colors.onAccent : colors.primary, fontSize: 20});
  labels.set(id, value); return id;
}
function enable(id, value) {
  if (enabled.get(id) !== value) { ui.setStyle(id, {enabled: value}); enabled.set(id, value); }
}

ui.background(colors.page);
const hint = label('选择摄像头，然后开始预览', left, 8, 520, 28, 16, colors.secondary);
card(view.width - 324, 4, 300, 36, 18);
const countLabel = label('正在检测摄像头', view.width - 316, 10, 284, 24, 16, colors.accent, true);
const rows = [];
for (let i = 0; i < 5; i++) {
  rows.push(button('', left, 56 + i * 62, width, 54, () => select(i), false));
  ui.setHidden(rows[i], true);
}
const empty = [card(left, 56, width, 300, 24, colors.page)];
const emptyIcon = ui.text('\uf03e', view.width / 2 - 13, 158, 24, colors.accent, 1);
const emptyTitle = label('未检测到摄像头', left, 220, width, 32, 20, colors.primary, true);
const emptyHint = label('请连接 CSI 或支持 MJPEG 的 USB 摄像头', left, 258, width, 28, 16, colors.secondary, true);
empty.push(emptyIcon, emptyTitle, emptyHint);
function showList(list) {
  empty.forEach(id => ui.setHidden(id, list));
}

card(left, 372, width, 96, 24);
label('视频参数', left + 24, 384, 200, 24, 16, colors.secondary);
const half = Math.floor((width - 64) / 2);
const modeButton = button('分辨率', left + 24, 412, half, 44,
  () => { if (pending || running || !modes.length) return;
    modeIndex = (modeIndex + 1) % modes.length; chooseIntervals(); }, false);
const fpsButton = button('帧率', left + 40 + half, 412, half, 44,
  () => { if (pending || running || !intervals.length) return;
    intervalIndex = (intervalIndex + 1) % intervals.length; renderMode(); }, false);
const status = label(message, left, 478, 520, 32, 16, colors.secondary);
const startButton = button('开始预览', 486, 488, 200, 48, start, true);
const refreshButton = button('刷新设备', 702, 488, 160, 48, () => refresh(true), false);
const stopButton = button('停止', 878, 488, 120, 48, stop, false);

function key(d) { return d.id + ':' + d.generation; }
function setStatus(value) { message = value; write(status, value); }
function controls(busy) {
  busy = busy || pending || running || system.camera.status().busy;
  enable(startButton, !busy && !!selected && !!selected.previewSupported);
  enable(stopButton, !!busy);
  enable(refreshButton, !busy);
  enable(modeButton, !busy && modes.length > 0);
  enable(fpsButton, !busy && intervals.length > 0);
  rows.forEach(row => enable(row, !busy));
}
function renderMode() {
  const m = modes[modeIndex];
  write(modeButton, selected && selected.transport === 'csi' ? '1024 x 600 · RGB565' :
    m ? m.width + ' x ' + m.height + ' · MJPEG' : '没有可用的 MJPEG 模式');
  const interval = intervals[intervalIndex];
  write(fpsButton, selected && selected.transport === 'csi' ? '30 fps' : interval ?
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
    catch (_) { setStatus('设备已断开，请刷新'); selected = null; }
    let best = modes.findIndex(m => m.width === 640 && m.height === 480);
    if (best < 0) best = modes.findIndex(m => m.width <= 1280 && m.height <= 720);
    if (best >= 0) modeIndex = best;
  }
  chooseIntervals();
  if (selected) setStatus('已选择 ' + shortName(selected.name));
  renderRows(); controls(); renderMode();
}
function shortName(name) {
  const text = Array.from(name || '');
  return (text.length > 28 ? text.slice(0, 27).join('') + '…' : text.join(''));
}
function renderRows() {
  for (let i = 0; i < rows.length; i++) {
    const d = devices[i];
    ui.setHidden(rows[i], !d);
    if (!d) continue;
    const current = selected && key(d) === key(selected);
    ui.setColor(rows[i], current ? colors.accent : colors.card);
    ui.setStyle(rows[i], {borderWidth: current ? 0 : 1,
      textColor: current ? colors.onAccent : colors.primary});
    write(rows[i], shortName(d.name) + ' · ' + d.transport.toUpperCase() +
      (d.previewSupported ? '' : ' · 不支持预览'));
  }
  showList(devices.length > 0);
  const csi = devices.filter(d => d.transport === 'csi').length;
  write(countLabel, devices.length ? '已连接 ' + devices.length + ' 个摄像头 · USB ' +
    (devices.length - csi) + ' / CSI ' + csi : '未连接摄像头');
  write(emptyTitle, '未检测到摄像头');
  controls();
}
function refresh(force) {
  let current;
  try { current = system.camera.devices(); }
  catch (_) { setStatus('设备查询失败，请重试'); return; }
  const next = current.map(key).join(',');
  if (!force && next === signature) return;
  signature = next; devices = current;
  if (selected && !devices.some(d => key(d) === key(selected))) {
    pending = false; system.camera.stop(); running = false; selected = null;
    modes = []; intervals = []; setStatus('摄像头已断开，请重新选择');
  }
  if (!selected && devices.length && !system.camera.status().busy) select(0);
  renderRows(); renderMode();
  if (!devices.length && !pending) setStatus('未检测到摄像头，请连接 CSI 或 USB 摄像头');
  controls();
}
function start() {
  if (pending || running || system.camera.status().busy) return;
  refresh(false);
  if (!selected || !selected.previewSupported) { setStatus('请选择可预览的摄像头'); return; }
  if (selected.transport === 'usb' && !modes.length) { setStatus('该摄像头没有可用的 MJPEG 模式'); return; }
  const photo = system.camera.photoStatus();
  photoBaseline = photo && photo.path ? photo.path : '';
  system.camera.stop(); pending = true; requestAt = Date.now();
  setStatus('正在准备预览…'); controls();
}
function stop() {
  pending = false; running = false; system.camera.stop();
  setStatus(stoppedStatus(0)); controls();
}
// The live preview paints its own toolbar, so the saved path is reported on
// the page after the preview closes.
function stoppedStatus(error) {
  if (error) return '预览已停止（' + error + '），请检查连接或更换视频模式';
  const photo = system.camera.photoStatus() || {};
  if (photo.status === 2 && photo.path && photo.path !== photoBaseline)
    return '照片已保存 · ' + photo.path;
  if (photo.status === -125) return '已取消拍照';
  if (photo.status < 0 && !photo.path) return '拍照失败（' + photo.status + '）';
  return '预览已停止';
}
function tick() {
  const state = system.camera.status();
  if (pending) {
    if (Date.now() - requestAt > 10000) {
      pending = false; setStatus('相机释放超时，请稍后重试'); controls(); return;
    }
    if (state.busy) return;
    pending = false;
    const m = modes[modeIndex];
    running = system.camera.startDevice(selected.id, selected.generation,
      m ? m.index : 0, intervals[intervalIndex] || 0);
    if (!running) setStatus('启动失败，请刷新设备后重试');
    controls();
    return;
  }
  if (running && !state.busy) {
    running = false;
    setStatus(stoppedStatus(state.error));
    controls();
  }
  refresh(false);
}
refresh(true);
setInterval(tick, 500);
globalThis.CameraPreview = { start, stop, refresh: () => refresh(true), frames: () => system.camera.frames() };
