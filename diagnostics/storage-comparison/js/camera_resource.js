'use strict';

const view = ui.getSize();
const previewX = Math.max(0, Math.floor((view.width - 512) / 2));
const previewY = 8;

ui.background(0x0b0f10);
const status = ui.text('正在启动摄像头…', previewX, 316, 18, 0xa9bad1);
let running = false;
let selected = null;
let pending = false;
let switchStarted = 0;
const switchTimeoutMs = 10000;
let message = '';

function usbDevices() {
  if (typeof system.camera.devices !== 'function' ||
      typeof system.camera.startDevice !== 'function') return [];
  const snapshot = system.camera.devices();
  if (!Array.isArray(snapshot) || snapshot.length > 32) throw Error('devices');
  const ids = new Set();
  return snapshot.filter(device => {
    if (!device || device.transport !== 'usb') return false;
    if (typeof device.id !== 'string' || !device.id.length || device.id.length > 64 ||
        !Number.isSafeInteger(device.generation) || device.generation < 0 ||
        typeof device.previewSupported !== 'boolean' ||
        ids.has(device.id)) throw Error('identity');
    ids.add(device.id);
    return device.previewSupported === true;
  }).map(device => ({id: device.id, generation: device.generation}));
}

function switchCamera() {
  if (pending) return;
  let devices;
  try { devices = usbDevices(); }
  catch (_) {
    if (!selected) { prompt.showToast('摄像头查询失败'); return; }
    devices = [];
    prompt.showToast('USB 查询失败，正在返回内置摄像头');
  }
  if (!devices.length && !selected) {
    prompt.showToast('没有可切换的 USB 摄像头，UVC 驱动尚未接入或设备不可用');
    return;
  }
  const index = selected ? devices.findIndex(d =>
    d.id === selected.id && d.generation === selected.generation) : -1;
  const next = selected && (index < 0 || index === devices.length - 1) ?
    null : devices[index + 1];
  try {
    if (system.camera.stop() === false) throw Error('stop');
  } catch (_) { prompt.showToast('停止失败，未切换摄像头'); return; }
  selected = next;
  running = false;
  pending = true;
  switchStarted = Date.now();
  message = '';
  ui.setText(status, '正在切换摄像头…');
}

function updateStatus() {
  if (pending) {
    const elapsed = Date.now() - switchStarted;
    if (elapsed < 0 || elapsed >= switchTimeoutMs) {
      pending = false;
      message = '切换等待超时，请停止后重试';
      ui.setText(status, message);
      return;
    }
    if (system.camera.active()) return;
    pending = false;
    startCamera();
    return;
  }
  const frames = system.camera.frames();
  if (running && !system.camera.active()) running = false;
  if (message) {
    ui.setText(status, message);
    return;
  }
  const photo = system.camera.photoStatus();
  if (photo.status === 2) {
    ui.setText(status, '已保存 ' + photo.path);
    return;
  }
  if (photo.status < 0) {
    ui.setText(status, '照片未保存 (' + photo.status + ')');
    return;
  }
  ui.setText(status, running ? '实时取景 · 已显示 ' + frames + ' 帧' : '预览已停止');
}

function startCamera() {
  if (running || pending) return;
  if (system.camera.active()) {
    message = '摄像头尚未释放，请稍后重试';
    ui.setText(status, message);
    return;
  }
  message = '';
  try {
    if (selected) {
      const current = usbDevices().find(d => d.id === selected.id &&
        d.generation === selected.generation);
      if (!current) {
        message = '所选 USB 摄像头已断开，请切换摄像头';
        ui.setText(status, message);
        return;
      }
      running = system.camera.startDevice(current.id, current.generation, previewX, previewY);
    } else running = system.camera.start(previewX, previewY);
  } catch (_) { running = false; }
  if (!running) message = '摄像头启动失败，请重试';
  updateStatus();
}

function stopCamera() {
  pending = false;
  if (running || system.camera.active()) {
    try {
      if (system.camera.stop() === false) throw Error('stop');
    } catch (_) {
      message = '停止相机失败，请重试';
      ui.setText(status, message);
      return;
    }
  }
  running = false;
  message = '预览已停止';
  ui.setText(status, message);
}

startCamera();

/* Create controls after the native canvas so they stay above the preview. */
ui.button('取景 / 重试', previewX, 350, 140, 44,
          startCamera, 0x168a72);
ui.button('拍照', previewX + 152, 350, 100, 44,
          () => {
            if (!system.camera.capture()) prompt.showToast('相机未就绪或正在保存');
          }, 0x4f6fa8);
ui.button('退出', previewX + 260, 350, 100, 44,
          stopCamera, 0x965f45);
ui.button('切换摄像头', previewX + 372, 350, 140, 44,
          switchCamera, 0x168a72);

setInterval(updateStatus, 500);

globalThis.CameraPreview = {
  start: startCamera,
  stop: stopCamera,
  switch: switchCamera,
  frames: () => system.camera.frames()
};
