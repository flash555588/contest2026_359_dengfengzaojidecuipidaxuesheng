/* SPDX-License-Identifier: Apache-2.0 */
'use strict';

const size = ui.getSize(), W = size.width, H = size.height;
const left = 24, top = 24, leftWidth = Math.round((W - 68) * 0.41);
const right = left + leftWidth + 20, rightWidth = W - right - 24;
const red = 0xd95068;
let task = 0, operation = '', stopping = false, pendingClip = '';
let clips = [], selected = '', page = 0, nextNumber = 1, loaded = false;
let preferred = '', afterList = '', confirmClip = '', confirmTicks = 0;
let elapsed = 0, history = Array(28).fill(0);

function text(value, x, y, width, height, font, color) {
  const id = ui.text(value, x, y, font || 20, color === undefined ? ui.primary : color);
  ui.setSize(id, width, height); ui.setStyle(id, {ellipsis: 1}); return id;
}
function panel(x, y, width, height) {
  const id = ui.panel(x, y, width, height, ui.card);
  ui.setStyle(id, {radius: 22, borderWidth: 0}); return id;
}
function time(ms) {
  const seconds = Math.floor(ms / 1000);
  return String(Math.floor(seconds / 60)).padStart(2, '0') + ':' + String(seconds % 60).padStart(2, '0');
}
function title(clip) {
  const match = /^rec_(\d+)$/.exec(clip);
  return match ? '录音 ' + String(Number(match[1])).padStart(3, '0') : clip;
}
function errorText(error, kind) {
  const code = Math.abs(Number(error.error) || 0);
  if (code === 61 || code === 110) return kind === 'play' ?
    '播放超时，音频设备未返回数据，请重试' : '麦克风没有返回数据，请重试';
  return ({16: '音频设备正忙，请稍后重试', 19: '音频设备未就绪',
    28: '存储空间不足，请删除旧录音', 5: '音频读写失败，请重试',
    12: '内存不足，请稍后重试'})[code] || '操作失败，请重试';
}

ui.background(ui.surface);
panel(left, top, leftWidth, H - 48);
panel(right, top, rightWidth, H - 48);
text('新录音', left + 22, 44, leftWidth - 44, 32, 28);
const status = text('正在读取录音…', left + 22, 88, leftWidth - 44, 48, 20, ui.secondary);
const clock = ui.number('00:00', left + 22, 142, leftWidth - 44, 64, ui.primary);
ui.setStyle(clock, {borderWidth: 0});
text('输入音量', left + 22, 218, leftWidth - 44, 24, 16, ui.secondary);
const bars = [];
const step = (leftWidth - 44) / history.length;
for (let i = 0; i < history.length; i++) {
  const bar = ui.rect(Math.round(left + 22 + i * step), 277, Math.max(3, Math.floor(step - 4)), 4, ui.surface);
  ui.setStyle(bar, {radius: 2, borderWidth: 0}); bars.push(bar);
}

const recordButton = ui.button('开始录音', left + 22, H - 202, leftWidth - 44, 56, record, red);
const stopButton = ui.button('停止并保存', left + 22, H - 132, leftWidth - 188, 52, stop, ui.accent);
const cancelButton = ui.button('放弃本次', left + leftWidth - 154, H - 132, 132, 52, cancel, ui.surface);
text('返回将放弃尚未保存的录音', left + 22, H - 66, leftWidth - 44, 24, 16, ui.secondary);

text('录音列表', right + 22, 44, rightWidth - 150, 32, 28);
const countLabel = text('0 条', right + rightWidth - 110, 51, 88, 24, 16, ui.secondary);
ui.setStyle(countLabel, {center: 1});
const empty = text('还没有录音\n点击左侧开始录音', right + 38, 206, rightWidth - 76, 100, 20, ui.secondary);
ui.setStyle(empty, {center: 1, ellipsis: 0});
const rows = [];
for (let i = 0; i < 4; i++) {
  const y = 94 + i * 74;
  const button = ui.button('', right + 16, y, rightWidth - 32, 66, () => select(page * 4 + i), ui.surface);
  ui.setStyle(button, {radius: 12, borderWidth: 0});
  rows.push({button,
    name: text('', right + 32, y + 8, rightWidth - 64, 27, 20),
    detail: text('', right + 32, y + 39, rightWidth - 64, 22, 16, ui.secondary)});
}
const selectionHint = text('选择录音后可以播放或删除', right + 22, H - 130, rightWidth - 44, 24, 16, ui.secondary);
const playButton = ui.button('播放', right + 16, H - 80, 154, 44, play, ui.accent);
const deleteButton = ui.button('删除', right + 182, H - 80, 144, 44, remove, ui.surface);
const previousButton = ui.button('<', right + rightWidth - 224, H - 80, 64, 44, () => turnPage(-1), ui.surface);
const pageLabel = text('1 / 1', right + rightWidth - 152, H - 72, 60, 28, 16, ui.secondary);
ui.setStyle(pageLabel, {center: 1});
const nextButton = ui.button('>', right + rightWidth - 84, H - 80, 64, 44, () => turnPage(1), ui.surface);

function enable(button, enabled, color, bright) {
  ui.setStyle(button, {enabled: !!enabled, textColor: enabled && bright ? 0xffffff : ui.secondary});
  ui.setColor(button, enabled ? color : ui.surface);
}
function current() { return clips.find(clip => clip.clip === selected); }
function controls() {
  const busy = task !== 0, entry = current();
  const audio = operation === 'record' || operation === 'play';
  enable(recordButton, !busy, red, true);
  enable(stopButton, busy && audio && !stopping, ui.accent, true);
  enable(cancelButton, busy && operation === 'record' && !stopping, ui.surface, false);
  enable(playButton, !busy && !!entry && entry.valid !== false, ui.accent, true);
  enable(deleteButton, !busy && !!entry, confirmClip ? red : ui.surface, !!confirmClip);
  enable(previousButton, !busy && page > 0, ui.surface, false);
  enable(nextButton, !busy && (page + 1) * 4 < clips.length, ui.surface, false);
  ui.setText(recordButton, operation === 'record' ? '录音中' : loaded ? '开始录音' : '重试读取');
  ui.setText(stopButton, operation === 'play' ? '停止播放' : '停止并保存');
  ui.setText(deleteButton, confirmClip ? '确认删除' : '删除');
  for (let i = 0; i < rows.length; i++) ui.setStyle(rows[i].button, {enabled: !busy});
}
function drawList() {
  page = Math.min(page, Math.max(0, Math.ceil(clips.length / 4) - 1));
  ui.setText(countLabel, clips.length + ' 条');
  ui.setText(pageLabel, (page + 1) + ' / ' + Math.max(1, Math.ceil(clips.length / 4)));
  ui.setHidden(empty, clips.length > 0);
  for (let i = 0; i < rows.length; i++) {
    const entry = clips[page * 4 + i], row = rows[i];
    for (const key of ['button', 'name', 'detail']) ui.setHidden(row[key], !entry);
    if (!entry) continue;
    ui.setText(row.name, title(entry.clip));
    ui.setText(row.detail, entry.valid === false ? '文件不完整 · 可删除' :
      time(entry.durationMs || 0) + '  ·  ' + Math.max(1, Math.round(entry.fileBytes / 1024)) + ' KB');
    ui.setStyle(row.button, {borderWidth: entry.clip === selected ? 2 : 0, borderColor: ui.accent});
  }
  ui.setText(selectionHint, confirmClip ? '再次点击“确认删除”将删除这条录音' :
    selected ? '已选择：' + title(selected) : '选择录音后可以播放或删除');
  controls();
}
function meter(peak) {
  history.shift(); history.push(Math.min(1, Math.max(0, Number(peak) / 32768 || 0)));
  for (let i = 0; i < bars.length; i++) {
    const height = Math.max(4, Math.round(Math.sqrt(history[i]) * 56));
    ui.setPos(bars[i], Math.round(left + 22 + i * step), 279 - Math.floor(height / 2));
    ui.setSize(bars[i], Math.max(3, Math.floor(step - 4)), height);
    ui.setColor(bars[i], history[i] > 0 ? red : ui.surface);
  }
}
function begin(kind, submit, message) {
  if (task) return false;
  try {
    task = submit(); operation = kind; stopping = false;
    confirmClip = ''; ui.setText(status, message); controls(); return true;
  } catch (error) {
    task = 0; operation = ''; ui.setText(status, '无法开始操作，请稍后重试'); controls(); return false;
  }
}
function refresh(clip, notice) {
  preferred = clip || ''; afterList = notice || '准备录音';
  loaded = false;
  begin('list', () => system.audio.list(), '正在读取录音…');
}
function record() {
  if (task) return;
  if (!loaded) { refresh('', '准备录音'); return; }
  pendingClip = 'rec_' + String(nextNumber).padStart(6, '0');
  if (begin('record', () => system.audio.record({clip: pendingClip, durationMs: 0}), '正在录音')) {
    elapsed = 0; ui.setText(clock, '00:00'); history.fill(0); meter(0);
  }
}
function stop() {
  if (!task || stopping || (operation !== 'record' && operation !== 'play')) return;
  try {
    system.audio.stop(task); stopping = true;
    ui.setText(status, operation === 'record' ? '正在保存录音…' : '正在停止播放…'); controls();
  } catch (error) { ui.setText(status, '停止请求失败，请重试'); }
}
function cancel() {
  if (!task || operation !== 'record' || stopping) return;
  try { system.hardware.cancel(task); stopping = true; ui.setText(status, '正在放弃录音…'); controls(); }
  catch (error) { ui.setText(status, '取消请求失败，请重试'); }
}
function select(index) {
  if (task || !clips[index]) return;
  selected = clips[index].clip; confirmClip = ''; drawList();
}
function turnPage(delta) {
  if (task) return;
  page = Math.max(0, Math.min(Math.ceil(clips.length / 4) - 1, page + delta));
  confirmClip = ''; drawList();
}
function play() {
  const entry = current(); if (task || !entry || entry.valid === false) return;
  if (begin('play', () => system.audio.play({clip: selected, volume: 40}), '正在播放：' + title(selected))) {
    elapsed = 0; ui.setText(clock, '00:00'); history.fill(0); meter(0);
  }
}
function remove() {
  if (task || !current()) return;
  if (confirmClip !== selected) { confirmClip = selected; confirmTicks = 50; drawList(); return; }
  begin('remove', () => system.audio.remove({clip: selected}), '正在删除录音…');
}
function poll() {
  if (confirmTicks > 0 && --confirmTicks === 0) { confirmClip = ''; drawList(); }
  if (!task) return;
  let state;
  try { state = system.hardware.poll(task); }
  catch (error) { ui.setText(status, '读取状态失败，正在重试…'); return; }
  if (!state.done) {
    if (operation === 'record' || operation === 'play') {
      elapsed = Number(state.elapsedMs) || 0; ui.setText(clock, time(elapsed));
      if (operation === 'record') meter(state.peak);
    }
    return;
  }
  const finished = operation;
  task = 0; operation = ''; stopping = false;
  if (finished === 'list' && state.ok) {
    loaded = true;
    clips = state.result.clips || [];
    clips.sort((a, b) => {
      const an = /^rec_(\d+)$/.exec(a.clip), bn = /^rec_(\d+)$/.exec(b.clip);
      return an && bn ? Number(bn[1]) - Number(an[1]) : b.clip.localeCompare(a.clip);
    });
    nextNumber = 1;
    for (const clip of clips) {
      const number = /^rec_(\d+)$/.exec(clip.clip);
      if (number) nextNumber = Math.max(nextNumber, Number(number[1]) + 1);
    }
    if (preferred && clips.some(clip => clip.clip === preferred)) selected = preferred;
    if (!clips.some(clip => clip.clip === selected)) selected = clips.length ? clips[0].clip : '';
    if (preferred) page = Math.max(0, Math.floor(clips.findIndex(clip => clip.clip === selected) / 4));
    ui.setText(status, afterList); drawList(); return;
  }
  if (finished === 'record') {
    elapsed = Number(state.elapsedMs) || elapsed; ui.setText(clock, time(elapsed));
    history.fill(0); meter(0);
    refresh(state.ok ? pendingClip : selected, state.ok ? '录音已保存' :
      Math.abs(state.error) === 125 ? '已放弃本次录音' : errorText(state, 'record')); return;
  }
  if (finished === 'remove' && state.ok) { confirmClip = ''; refresh('', '录音已删除'); return; }
  ui.setText(status, state.ok ? '播放已结束' : errorText(state, finished));
  drawList();
}

globalThis.RecorderApp = {record, stop, cancel, play, remove, select, turnPage,
  state: () => ({task, operation, selected, count: clips.length, elapsed, stopping})};
drawList(); refresh('', '准备录音'); setInterval(poll, 100);
