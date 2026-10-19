'use strict';

const view = ui.getSize();
const rowCount = 3;
const sx = v => Math.round(v * view.width / 1024);
const sy = v => Math.round(v * view.height / 600);
const colors = {
  page: 0x000000, card: 0x202022,
  primary: 0xf5f5f7, secondary: 0x9c9ca1,
  accent: 0xf13c2e, onAccent: 0xffffff,
  border: 0x38383a, selected: 0x362321
};
const labels = new Map(), enabled = new Map();
let devices = [], selected = null, modes = [], modeIndex = 0, intervalIndex = 0;
let pending = false, running = false, requestAt = 0, message = '请选择摄像头后开始预览';
let signature = '', intervals = [], photoBaseline = '';
let devicePage = 0, lastScan = -Infinity;
let settingsOpen = false, resumeAfterSelect = false, colorStyle = 0;
let controlSignature = "";
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
  const id = ui.text(value, sx(x), sy(y), font, color);
  ui.setSize(id, sx(w), sy(h)); ui.setStyle(id, {center: !!center, ellipsis: true});
  labels.set(id, value); return id;
}
function card(x, y, w, h, radius, fill) {
  const id = ui.panel(sx(x), sy(y), sx(w), sy(h), fill === undefined ? colors.card : fill);
  ui.setStyle(id, {radius: radius, borderColor: colors.border, borderWidth: 1, borderOpacity: 100});
  return id;
}
function button(value, x, y, w, h, action, primary) {
  const id = ui.button(value, sx(x), sy(y), sx(w), sy(h), action, primary ? colors.accent : colors.card);
  ui.setStyle(id, {radius: 12, borderColor: colors.border, borderWidth: primary ? 0 : 1,
    borderOpacity: 110, textColor: primary ? colors.onAccent : colors.primary, fontSize: 16});
  labels.set(id, value); return id;
}
function enable(id, value) {
  if (enabled.get(id) !== value) { ui.setStyle(id, {enabled: value}); enabled.set(id, value); }
}

ui.background(0x000000);
// The native scene uses the user's unmodified screenshot as its sprite atlas.
// It owns only small read-only tiles, never a second full-screen framebuffer.
const cameraScene=ui.cameraReference();
const zoomValues=[100,150,200,300];
let zoomPercent=100,zoomGesture=null;
function applyZoom(index){
  index=Math.max(0,Math.min(3,index));
  const percent=zoomValues[index];
  if(typeof system.camera.setZoom==='function'&&!system.camera.setZoom(percent))return;
  zoomPercent=percent;ui.cameraReferenceZoom(cameraScene,percent);
}
function syncZoom(){
  const percent=typeof system.camera.zoom==='function'?system.camera.zoom():zoomPercent;
  if(zoomValues.includes(percent)&&percent!==zoomPercent){zoomPercent=percent;ui.cameraReferenceZoom(cameraScene,percent);}
}
ui.onTouch((state,nx,ny)=>{
  const x=nx*1024,y=ny*600;
  if(settingsOpen){zoomGesture=null;return;}
  if(state==='down'){
    zoomGesture=x>=700&&x<764&&y>=160&&y<440?{y,index:zoomValues.indexOf(zoomPercent)}:null;
    return;
  }
  if(!zoomGesture)return;
  if(state==='cancel'||x<680||x>=784){zoomGesture=null;return;}
  const delta=y-zoomGesture.y;
  if(state==='move')applyZoom(zoomGesture.index+Math.trunc(-delta/48));
  if(state==='up'){
    applyZoom(Math.abs(delta)<10?3-Math.trunc((y-172)/64):zoomGesture.index+Math.trunc(-delta/48));
    zoomGesture=null;
  }
});
syncZoom();
function hit(value,x,y,w,h,action) {
  const id=button(value,x,y,w,h,action,false);
  ui.setOpacity(id,0);return id;
}
function hiddenLabel(value) {
  const id=label(value,156,30,500,22,14,colors.secondary);
  ui.setHidden(id,true);return id;
}
const deviceName=hiddenLabel('尚未选择设备');
const countLabel=hiddenLabel('正在检测摄像头');
const previewTitle=hiddenLabel('连接摄像头');
const status=label(message,140,550,544,18,12,colors.secondary);
ui.setHidden(status,true);
function syncControls(){
  const next=typeof system.camera.style==='function'?system.camera.style():0;
  if(Number.isInteger(next)&&next>=0&&next<=2)colorStyle=next;
  const signature=colorStyle+':'+videoMode;
  if(signature!==controlSignature){ui.cameraReferenceControls(cameraScene,colorStyle,videoMode);controlSignature=signature;}
}
const deviceButton=hit('切换相机',856,24,78,72,()=>setSettingsOpen(!settingsOpen));
const colorButton=hit('色彩风格',700,28,64,72,()=>{
  const next=(colorStyle+1)%3;
  if(typeof system.camera.setStyle==='function'&&system.camera.setStyle(next)){
    syncControls();setStatus('色彩：'+['原色','鲜艳','黑白'][colorStyle]);
  }
});
const albumButton=hit('预览相册',856,504,78,78,()=>{
  pending=false;running=false;resumeAfterSelect=false;setSettingsOpen(false);
  if(typeof system.camera.openAlbum!=='function'||!system.camera.openAlbum())setStatus('相册暂时无法打开');
});
const captureMode=hit('切换录制',856,128,78,74,toggleVideo);
const startButton=hit('开始预览',836,244,112,112,start);
// The thin system-return edge is retained in the landscape translation.
const returnEdge=ui.panel(sx(1007),sy(234),Math.max(2,sx(3)),sy(132),0x999999);
ui.setStyle(returnEdge,{radius:2,borderWidth:0});

// Expanded choices use the reference's black field and plain white/red type.
// There are no desktop cards, outlined rows, or unrelated control colours.
const choices=part(card(140,188,544,294,0,0x000000));
ui.setStyle(choices,{borderWidth:0});
part(label('摄像头',156,204,350,24,16,colors.secondary));
const doneButton=part(button('完成',568,196,96,40,()=>closeSettings(),false));
const rows=[];
for(let i=0;i<rowCount;i++){
  const row=part(button('',156,244+i*40,508,36,()=>{
    if(select(devicePage*rowCount+i))closeSettings();
  },false));
  ui.setColor(row,0x000000);
  ui.setStyle(row,{radius:0,borderWidth:0,fontSize:16,center:false,ellipsis:true});
  rows.push(row);
}
const empty=[];
const emptyTitle=part(label('未检测到摄像头',156,254,508,26,16,colors.primary,true));
const emptyHint=part(label('连接 CSI 或 USB 摄像头后刷新',156,288,508,22,12,colors.secondary,true));
empty.push(emptyTitle,emptyHint);
function showList(list){empty.forEach(id=>ui.setHidden(id,list||!settingsOpen));}
const previousButton=part(button('上一页',156,372,112,36,()=>turnPage(-1),false));
const nextButton=part(button('下一页',552,372,112,36,()=>turnPage(1),false));
const pageLabel=part(label('',282,382,256,22,12,colors.secondary,true));
const modeButton=part(button('分辨率',156,426,244,40,()=>{
  if(pending||running||!modes.length)return;
  modeIndex=(modeIndex+1)%modes.length;chooseIntervals();
},false));
const fpsButton=part(button('帧率',404,426,120,40,()=>{
  if(pending||running||!intervals.length)return;
  intervalIndex=(intervalIndex+1)%intervals.length;renderMode();
},false));
const refreshButton=part(button('刷新设备',528,426,136,40,()=>refresh(true),false));
const recordingHint=part(label('点选设备后收起',156,470,508,18,12,colors.secondary));
for(const id of [previousButton,nextButton,modeButton,fpsButton,refreshButton,doneButton]){
  ui.setColor(id,0x000000);ui.setStyle(id,{radius:0,borderWidth:0,fontSize:14,textColor:colors.primary});
}

function closeSettings(){
  setSettingsOpen(false);
  if(resumeAfterSelect){resumeAfterSelect=false;start();}
}
function recordingBusy() { return recording.status >= 1 && recording.status <= 3; }
function syncRecording() {
  if (!hasVideo) return;
  const next = system.camera.recordStatus() || {status: 0};
  const changed = next.status !== recording.status || next.revision !== recording.revision;
  recording = next;
  videoMode = !!next.video;
  syncControls();
  write(recordingHint, videoMode ? '录像含麦克风声音，保存到 SD 卡' : '点选摄像头，按快门开始取景');

  if (changed && next.status === 4 && next.path) setStatus('录像已保存 · ' + next.path);
  else if (changed && next.status === 3) setStatus('正在保存录像，请勿拔出 SD 卡');
  else if (changed && next.status < 0) setStatus(next.status === -28 ? 'SD 卡空间不足' :
    next.status === -19 || next.status === -2 ? '录像需要可用的 SD 卡和麦克风' : '录像失败（' + next.status + '）');
}
function toggleVideo() {
  if (!hasVideo || pending || running || system.camera.status().busy || recordingBusy()) return;
  if (system.camera.setVideoMode(!videoMode)) {
    syncRecording();
    setStatus(videoMode ? '视频 · 含麦克风声音，进入取景后轻触快门录制' : '照片 · 进入取景后轻触快门拍摄');
  }
}

function turnPage(delta) {
  if (pending || running || system.camera.status().busy) return;
  devicePage = Math.max(0, Math.min(Math.ceil(devices.length / rowCount) - 1, devicePage + delta));
  renderRows();
}

function key(d) { return d.id + ':' + d.generation; }
function setStatus(value) {
  message=value;write(status,value);
  ui.setHidden(status,!/失败|超时|断开|未检测|已保存|取消|色彩：|无法打开/.test(value));
}
function controls(busy) {
  busy = busy || pending || running || system.camera.status().busy || recordingBusy();
  const ready = !!selected && !!selected.previewSupported && (selected.transport === 'csi' || (modes.length > 0 && intervals.length > 0));
  enable(startButton, !busy && ready);
  enable(deviceButton, !busy);
  enable(captureMode, !busy && hasVideo);
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
  if (pending || running || system.camera.status().busy || !devices[index]) return false;
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
  return !!selected && !!selected.previewSupported;
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
    ui.setColor(rows[i], 0x000000);
    ui.setStyle(rows[i], {borderWidth: 0, textColor: current ? colors.accent : colors.primary});
    write(rows[i], shortName(d.name) + (d.previewSupported ? '' : ' · 不支持'));
  }
  showList(devices.length > 0);
  write(pageLabel, devices.length ? (devicePage + 1) + ' / ' + Math.ceil(devices.length / rowCount) : '暂无设备');
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
  syncZoom();
  const state = system.camera.status();
  syncRecording();
  if(!state.busy&&typeof system.camera.takeAction==='function'&&system.camera.takeAction()===3){
    pending=false;running=false;resumeAfterSelect=true;refresh(true);setSettingsOpen(true);return;
  }
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
globalThis.CameraPreview = { start, stop, refresh: () => refresh(true), frames: () => system.camera.frames(), zoom: () => zoomPercent, style: () => colorStyle, video: () => videoMode };
