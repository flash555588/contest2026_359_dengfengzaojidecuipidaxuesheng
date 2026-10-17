'use strict';

const view = ui.getSize();
const left = 20, width = view.width - left * 2;
const railW = 128, previewW = view.width - railW;
const bodyY = 64, bodyH = view.height - 128;
const panelW = Math.min(600, previewW - 40), panelX = previewW - panelW - left;
const listW = Math.floor((panelW - 48) * 0.52), detailX = panelX + listW + 32;
const detailW = panelW - listW - 48;
const rowCount = Math.max(1, Math.min(5, Math.floor((bodyH - 78) / 52)));
// The runtime publishes the desktop's card, surface, text and accent colours.
const light = ((ui.card >> 16) & 255) + ((ui.card >> 8) & 255) + (ui.card & 255) > 384;
const colors = {
  page: 0x000000, card: 0x202022,
  primary: 0xf5f5f7, secondary: 0x9c9ca1,
  accent: 0xffd60a, onAccent: 0x111111,
  border: 0x38383a, selected: 0x383322
};
const labels = new Map(), enabled = new Map();
let devices = [], selected = null, modes = [], modeIndex = 0, intervalIndex = 0;
let pending = false, running = false, requestAt = 0, message = '请选择摄像头后开始预览';
let signature = '', intervals = [], photoBaseline = '';
let devicePage = 0, lastScan = -Infinity;
let settingsOpen = false;
let videoMode = false, recording = {status: 0, revision: 0};
const hasVideo = typeof system.camera.recordStatus === 'function' && typeof system.camera.setVideoMode === 'function';
const settingsWidgets = [];
function part(id) { settingsWidgets.push(id); ui.setHidden(id, true); return id; }
function setSettingsOpen(open) {
  settingsOpen = !!open;
  settingsWidgets.forEach(id => ui.setHidden(id, !settingsOpen));
  renderRows();
}

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
    borderOpacity: 110, textColor: primary ? colors.onAccent : colors.primary, fontSize: 16});
  labels.set(id, value); return id;
}
function enable(id, value) {
  if (enabled.get(id) !== value) { ui.setStyle(id, {enabled: value}); enabled.set(id, value); }
}

ui.background(colors.page);
const preview = card(0, 0, previewW, view.height, 0, 0x101112);
ui.setStyle(preview, {borderWidth: 0});
const deviceName = label('尚未选择设备', left, 18, previewW - 40, 24, 14, 0xa0a0a5);
const countLabel = label('正在检测摄像头', left, view.height - 50, previewW - 40, 22, 13, colors.secondary);
button('设备', previewW + 14, 16, 100, 40, () => setSettingsOpen(!settingsOpen), false);
const centerY = Math.floor(view.height / 2), shutterX = previewW + Math.floor(railW / 2);
const previewTitle = label('连接摄像头', left, centerY - 22, previewW - 40, 30, 22, 0xe5e5e7, true);
label('轻触右侧快门，开启全屏取景', left, centerY + 18, previewW - 40, 24, 14, colors.secondary, true);
const captureMode = button('照片', previewW + 14, centerY + 64, 100, 32, toggleVideo, false);
ui.setColor(captureMode, colors.page);
ui.setStyle(captureMode, {borderWidth: 0, textColor: colors.accent, fontSize: 15});
const actionY = centerY - 42, startW = 84;
const ring = ui.panel(shutterX - 48, actionY - 6, 96, 96, 0xffffff);
ui.setStyle(ring, {radius: 48, borderWidth: 0});
const gap = ui.panel(shutterX - 45, actionY - 3, 90, 90, 0x000000);
ui.setStyle(gap, {radius: 45, borderWidth: 0});
const startButton = button('开始预览', shutterX - 42, actionY, startW, startW, start, true);
ui.setColor(startButton, 0xffffff);ui.setStyle(startButton, {radius: 42, fontSize: 12, textColor: 0xffffff});
const stopButton = button('停止', previewW + 14, 72, 100, 44, stop, false);
ui.setStyle(stopButton, {radius: 22, fontSize: 13});
const status = label(message, left, view.height - 24, previewW - 40, 18, 12, colors.secondary);

part(card(panelX, bodyY, panelW, bodyH, 20, 0x242426));
part(label('摄像头', panelX + 16, bodyY + 12, listW - 16, 24, 14, colors.secondary));
part(label('取景参数', detailX, bodyY + 12, detailW, 24, 14, colors.secondary));
const refreshButton = part(button('刷新设备', detailX, bodyY + bodyH - 54, detailW, 40, () => refresh(true), false));
ui.setStyle(refreshButton, {radius: 12, fontSize: 14});
const rows = [];
for (let i = 0; i < rowCount; i++) {
  rows.push(part(button('', panelX + 12, bodyY + 46 + i * 52, listW, 44,
    () => select(devicePage * rowCount + i), false)));
  ui.setHidden(rows[i], true);
}
const empty = [];
const emptyTitle = part(label('未检测到摄像头', panelX + 16, bodyY + 62, listW - 16, 30, 16, colors.primary, true));
const emptyHint = part(label('支持 CSI / USB MJPEG', panelX + 16, bodyY + 100, listW - 16, 24, 12, colors.secondary, true));
empty.push(emptyTitle, emptyHint);
function showList(list) {
  empty.forEach(id => ui.setHidden(id, list || !settingsOpen));
}

const pageW = Math.floor((listW - 12) / 2);
const previousButton = part(button('上一页', panelX + 12, bodyY + bodyH - 38, pageW, 28, () => turnPage(-1), false));
const nextButton = part(button('下一页', panelX + 24 + pageW, bodyY + bodyH - 38, pageW, 28, () => turnPage(1), false));
const modeButton = part(button('分辨率', detailX, bodyY + 48, detailW, 44,
  () => { if (pending || running || !modes.length) return;
    modeIndex = (modeIndex + 1) % modes.length; chooseIntervals(); }, false));
const fpsButton = part(button('帧率', detailX, bodyY + 106, detailW, 44,
  () => { if (pending || running || !intervals.length) return;
    intervalIndex = (intervalIndex + 1) % intervals.length; renderMode(); }, false));
ui.setStyle(modeButton, {fontSize: 14});ui.setStyle(fpsButton, {fontSize: 14});
const recordingHint = part(label('录像含麦克风声音，保存到 SD 卡', detailX, bodyY + 162, detailW, 42, 12, colors.secondary));

function recordingBusy() { return recording.status >= 1 && recording.status <= 3; }
function syncRecording() {
  if (!hasVideo) return;
  const next = system.camera.recordStatus() || {status: 0};
  const changed = next.status !== recording.status || next.revision !== recording.revision;
  recording = next;
  videoMode = !!next.video;
  write(captureMode, videoMode ? '视频' : '照片');
  ui.setColor(startButton, videoMode ? 0xff453a : 0xffffff);
  ui.setStyle(startButton, {textColor: videoMode ? 0xff453a : 0xffffff});
  if (changed && next.status === 4 && next.path) setStatus('录像已保存 · ' + next.path);
  else if (changed && next.status === 3) setStatus('正在保存录像，请勿拔出 SD 卡');
  else if (changed && next.status < 0) setStatus(next.status === -28 ? 'SD 卡空间不足' :
    next.status === -19 || next.status === -2 ? '录像需要可用的 SD 卡和麦克风' : '录像失败（' + next.status + '）');
}
function toggleVideo() {
  if (!hasVideo || pending || running || system.camera.status().busy || recordingBusy()) return;
  if (system.camera.setVideoMode(!videoMode)) {
    syncRecording();
    setStatus(videoMode ? '视频 · 含麦克风声音，进入取景后轻触红色快门录制' : '照片 · 进入取景后轻触白色快门拍摄');
  }
}

function turnPage(delta) {
  if (pending || running || system.camera.status().busy) return;
  devicePage = Math.max(0, Math.min(Math.ceil(devices.length / rowCount) - 1, devicePage + delta));
  renderRows();
}

function key(d) { return d.id + ':' + d.generation; }
function setStatus(value) { message = value; write(status, value); }
function controls(busy) {
  busy = busy || pending || running || system.camera.status().busy || recordingBusy();
  const ready = !!selected && !!selected.previewSupported && (selected.transport === 'csi' || (modes.length > 0 && intervals.length > 0));
  enable(startButton, !busy && ready);
  enable(captureMode, !busy && hasVideo);
  enable(stopButton, !!busy);
  ui.setHidden(stopButton, !busy);
  enable(refreshButton, !busy);
  enable(modeButton, !busy && modes.length > 1);
  enable(fpsButton, !busy && intervals.length > 1);
  enable(previousButton, !busy && devicePage > 0);
  enable(nextButton, !busy && (devicePage + 1) * rowCount < devices.length);
  rows.forEach(row => enable(row, !busy));
}
function renderMode() {
  write(deviceName, selected ? shortName(selected.name) : '尚未选择设备');
  write(previewTitle, selected && selected.previewSupported ? '准备取景' : '连接摄像头');
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
    if (m.continuous && m.intervals.length >= 3) {
      const min = m.intervals[0], max = m.intervals[1], step = m.intervals[2];
      if (Number.isFinite(min) && Number.isFinite(max) && min > 0 && max >= min && Number.isFinite(step) && step >= 0)
      [m.defaultInterval, min, 333333, 666666, 1000000, max].filter(v => Number.isFinite(v) && v > 0).forEach(v => {
        let n = Math.max(min, Math.min(max, v));
        if (step) n = min + Math.floor((n - min) / step) * step;
        if (!intervals.includes(n)) intervals.push(n);
      });
    } else intervals = m.intervals.filter(v => Number.isFinite(v) && v > 0);
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
    const d = devices[devicePage * rowCount + i];
    ui.setHidden(rows[i], !d || !settingsOpen);
    if (!d) continue;
    const current = selected && key(d) === key(selected);
    ui.setColor(rows[i], current ? colors.selected : colors.card);
    ui.setStyle(rows[i], {borderWidth: current ? 0 : 1,
      textColor: current ? colors.accent : colors.primary});
    write(rows[i], shortName(d.name) + ' · ' + d.transport.toUpperCase() +
      (d.previewSupported ? '' : ' · 不支持预览'));
  }
  showList(devices.length > 0);
  ui.setHidden(previousButton, !settingsOpen || devices.length <= rowCount);
  ui.setHidden(nextButton, !settingsOpen || devices.length <= rowCount);
  const csi = devices.filter(d => d.transport === 'csi').length;
  write(countLabel, devices.length ? '已连接 ' + devices.length + ' 个摄像头 · USB ' +
    (devices.length - csi) + ' / CSI ' + csi : '未连接摄像头');
  write(emptyTitle, '未检测到摄像头');
  controls();
}
function refresh(force) {
  if (pending || running || system.camera.status().busy) return;
  lastScan = Date.now();
  let current;
  try { current = system.camera.devices(); }
  catch (_) { setStatus('设备查询失败，请重试'); return; }
  const next = current.map(key).join(',');
  if (!force && next === signature) return;
  signature = next; devices = current;
  devicePage = Math.max(0, Math.min(devicePage, Math.ceil(devices.length / rowCount) - 1));
  if (selected && !devices.some(d => key(d) === key(selected))) {
    pending = false; system.camera.stop(); running = false; selected = null;
    modes = []; intervals = []; setStatus('摄像头已断开，请重新选择');
  }
  if (!selected && devices.length) {
    const supported = devices.findIndex(d => d.previewSupported);
    select(supported >= 0 ? supported : 0);
    devicePage = Math.floor(Math.max(0, supported) / rowCount);
  }
  renderRows(); renderMode();
  if (!devices.length && !pending) setStatus('未检测到摄像头，请连接 CSI 或 USB 摄像头');
  controls();
}
function start() {
  syncRecording();
  if (pending || running || system.camera.status().busy || recordingBusy()) return;
  setSettingsOpen(false);
  refresh(false);
  if (!selected || !selected.previewSupported) { setStatus('请选择可预览的摄像头'); return; }
  if (selected.transport === 'usb' && (!modes.length || !intervals.length)) { setStatus('该摄像头没有可用的视频模式'); return; }
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
  if (hasVideo) {
    const video = system.camera.recordStatus() || {};
    if (video.status === 3) return '正在保存录像，请勿拔出 SD 卡';
    if (video.status === 4 && video.path) return '录像已保存 · ' + video.path;
    if (video.status < 0) return '录像失败（' + video.status + '）';
  }
  const photo = system.camera.photoStatus() || {};
  if (photo.status === 2 && photo.path && photo.path !== photoBaseline)
    return '照片已保存 · ' + photo.path;
  if (photo.status === -125) return '已取消拍照';
  if (photo.status < 0 && !photo.path) return '拍照失败（' + photo.status + '）';
  return '预览已停止';
}
function tick() {
  const state = system.camera.status();
  syncRecording();
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
  if (!state.busy && Date.now() - lastScan >= 2000) refresh(false);
  controls();
}
syncRecording();
refresh(true);
setInterval(tick, 500);
globalThis.CameraPreview = { start, stop, refresh: () => refresh(true), frames: () => system.camera.frames() };
