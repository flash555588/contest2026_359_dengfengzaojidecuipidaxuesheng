'use strict';

const view = ui.getSize();
const W = view.width;
const H = view.height;
const fallbackId = '02';

const states = {
  '00': ['离线',       0x29313d, 0x6f7b88, 34, 5,  70, 6, 0, 'offline'],
  '01': ['启动中',     0x102b36, 0x50d5b7, 40, 10, 68, 8, 1, 'scan'],
  '02': ['待机',       0x10222c, 0x73dfc0, 42, 12, 58, 8, 1, 'breathe'],
  '03': ['休眠',       0x171b25, 0x637080, 48, 3,  48, 4, 0, 'sleep'],
  '04': ['唤醒',       0x16293a, 0x9fd9ff, 46, 16, 72, 7, 1, 'bounce'],
  '05': ['聆听',       0x112d35, 0x57d7ca, 38, 14, 50, 6, 1, 'listen'],
  '06': ['表达',       0x2d2239, 0xd6a4ff, 44, 13, 76, 9, 1, 'speak'],
  '07': ['充电',       0x173026, 0x7ee787, 42, 12, 56, 8, 1, 'charge'],
  '08': ['低电量',     0x38231f, 0xff9b72, 46, 7,  44, 5, 1, 'slow'],
  '09': ['更新系统',   0x1b2840, 0x80b6ff, 40, 10, 64, 6, 1, 'scan'],
  '10': ['开心',       0x143329, 0x70e5a3, 46, 15, 78, 9, 1, 'bounce'],
  '11': ['兴奋',       0x32272a, 0xffbf69, 52, 18, 82, 10, 1, 'spark'],
  '12': ['害羞',       0x35252f, 0xf4a6c1, 38, 11, 52, 7, 1, 'shy'],
  '13': ['平静',       0x192c32, 0x83c5be, 46, 9,  62, 5, 1, 'breathe'],
  '14': ['难过',       0x20293c, 0x86a9d6, 38, 8,  48, 4, 1, 'sad'],
  '15': ['生气',       0x3b2223, 0xff716c, 50, 9,  70, 5, 1, 'jitter'],
  '16': ['惊讶',       0x2b293c, 0xc9a7ff, 30, 22, 32, 18, 1, 'pop'],
  '17': ['好奇',       0x183239, 0x64d8e6, 40, 13, 56, 7, 1, 'peek'],
  '18': ['困惑',       0x302a22, 0xe6c36a, 44, 8,  36, 5, 1, 'tilt'],
  '19': ['自信',       0x222c3b, 0x8db4ff, 50, 11, 72, 7, 1, 'proud'],
  '30': ['思考中',     0x20233a, 0x9f9cff, 40, 9,  42, 5, 1, 'think'],
  '31': ['检索资料',   0x122e37, 0x4fd9c8, 36, 10, 50, 6, 1, 'scan'],
  '32': ['阅读内容',   0x243128, 0x8bd17c, 46, 6,  66, 4, 1, 'read'],
  '33': ['编写代码',   0x19293a, 0x6eb7ff, 38, 11, 58, 7, 1, 'code'],
  '34': ['规划步骤',   0x2c2838, 0xc6a3ff, 42, 10, 62, 6, 1, 'plan'],
  '35': ['等待响应',   0x2d2d31, 0xb4bac3, 44, 8,  46, 5, 1, 'slow'],
  '36': ['任务成功',   0x153426, 0x69e39b, 50, 15, 82, 9, 1, 'celebrate'],
  '37': ['需要注意',   0x3b3120, 0xffcc66, 40, 12, 54, 8, 1, 'pulse'],
  '38': ['发生错误',   0x3b2026, 0xff6f86, 34, 8,  32, 5, 1, 'jitter'],
  '39': ['正在重试',   0x302a22, 0xf0b768, 40, 10, 56, 6, 1, 'spin'],
  '40': ['连接服务',   0x192b3a, 0x6bbcff, 38, 12, 60, 7, 1, 'connect'],
  '41': ['同步数据',   0x162f35, 0x58d4d0, 42, 10, 68, 6, 1, 'scan']
};

const ids = Object.keys(states);
let currentId = system.storage.get('emotion') || fallbackId;
if (!states[currentId]) currentId = fallbackId;
let active = true;
let tourTimer = 0;
let tourIndex = ids.indexOf(currentId);
let gazeX = 0;
let gazeY = 0;
let phase = 0;
let blinkUntil = 0;
let nextBlink = 35;

ui.background(0xfff7f1);
const face = ui.panel(150, 90, W - 300, H - 260, 0xfffffb, 42, 255);
const eyeL = ui.panel(286, 205, 112, 42, 0x7ddfc3, 21, 255);
const eyeR = ui.panel(W - 398, 205, 112, 42, 0x7ddfc3, 21, 255);
const cheekL = ui.panel(260, 300, 58, 22, 0xffb5b0, 11, 150);
const cheekR = ui.panel(W - 318, 300, 58, 22, 0xffb5b0, 11, 150);
const mouthL = ui.panel(W / 2 - 34, 292, 12, 32, 0x657080, 6, 255);
const mouthB = ui.panel(W / 2 - 22, 312, 44, 12, 0x657080, 6, 255);
const mouthR = ui.panel(W / 2 + 22, 292, 12, 32, 0x657080, 6, 255);
const stateLabel = ui.text('', 210, H - 98, 24, 0x4f5a6b);
const tipsLabel = ui.text('滑动切换状态', 210, H - 64, 16, 0x9a8490);
const idLabel = ui.text('', W - 280, H - 92, 20, 0x9a8490);

ui.button('自动巡演', 90, H - 42, 154, 38, () => startTour(1800), 0xf0b59f);
ui.button('停止', 256, H - 42, 100, 38, stopTour, 0xe0d5d2);
ui.button('AI 消息', W - 220, H - 42, 130, 38, () => {
  handleAIMessage({emotionId: '30', tips: '正在分析本地任务'});
}, 0x255d66);

function setEmotion(id, automatic) {
  if (!states[id]) id = fallbackId;
  currentId = id;
  const s = states[id];
  ui.setColor(face, s[1]);
  ui.setColor(eyeL, s[2]); ui.setColor(eyeR, s[2]);
  ui.setColor(mouthL, s[2]); ui.setColor(mouthB, s[2]); ui.setColor(mouthR, s[2]);
  ui.setSize(eyeL, s[3] * 2, s[4] * 2);
  ui.setSize(eyeR, s[3] * 2, s[4] * 2);
  ui.setSize(mouthB, s[5] * 2, Math.max(8, s[6]));
  ui.setText(stateLabel, s[0]);
  ui.setText(idLabel, 'ID ' + id);
  ui.setOpacity(cheekL, s[7] ? 190 : 120);
  ui.setOpacity(cheekR, s[7] ? 190 : 120);
  if (!automatic) system.storage.set('emotion', id);
  phase = 0;
}

function setGaze(nx, ny) {
  gazeX = Math.max(-1, Math.min(1, Number(nx) || 0));
  gazeY = Math.max(-1, Math.min(1, Number(ny) || 0));
}

function handleAIMessage(message) {
  let data = message;
  try {
    if (typeof message === 'string') data = JSON.parse(message);
    if (!data || !states[String(data.emotionId)]) throw new Error('invalid emotionId');
    setEmotion(String(data.emotionId), false);
    ui.setText(tipsLabel, data.tips ? String(data.tips) : '状态已更新');
    return true;
  } catch (error) {
    setEmotion(fallbackId, false);
    ui.setText(tipsLabel, '消息无效，已回到待机');
    return false;
  }
}

function startTour(period) {
  stopTour();
  const delay = Math.max(500, Number(period) || 1800);
  tourTimer = setInterval(() => {
    tourIndex = (tourIndex + 1) % ids.length;
    setEmotion(ids[tourIndex], true);
    ui.setText(tipsLabel, '自动巡演 · ' + (tourIndex + 1) + '/' + ids.length);
  }, delay);
}

function stopTour() {
  if (tourTimer) clearInterval(tourTimer);
  tourTimer = 0;
  ui.setText(tipsLabel, '滑动切换状态');
}

function setActive(value) {
  active = !!value;
}

function renderStatic() {
  renderFrame();
}

function renderFrame() {
  if (!active) return;
  const s = states[currentId];
  phase++;
  const t = phase / 5;
  const motion = s[8];
  let ox = gazeX * 20;
  let oy = gazeY * 10;
  let breath = Math.round(Math.sin(t * 0.22) * 3);
  if (motion === 'jitter') ox += ((phase % 3) - 1) * 5;
  if (motion === 'scan') ox += Math.round(Math.sin(t * 0.55) * 18);
  if (motion === 'peek') ox += Math.round(Math.sin(t * 0.16) * 12);
  if (motion === 'sad') oy += 8;
  if (motion === 'proud') oy -= 5;
  if (motion === 'bounce' || motion === 'celebrate') oy += Math.round(Math.sin(t * 0.42) * 7);

  if (phase >= nextBlink) {
    blinkUntil = phase + 2;
    nextBlink = phase + 30 + (phase % 25);
  }
  const blinking = phase < blinkUntil || motion === 'sleep';
  const eyeH = blinking ? 4 : s[4] * 2;
  const eyeY = 170 + breath + oy;
  ui.setPos(eyeL, 286 + ox, eyeY); ui.setPos(eyeR, W - 398 + ox, eyeY);
  ui.setSize(eyeL, s[3] * 2, eyeH); ui.setSize(eyeR, s[3] * 2, eyeH);
  ui.setPos(mouthL, W / 2 - 34, 292 + breath);
  ui.setPos(mouthB, W / 2 - 22, 312 + breath);
  ui.setPos(mouthR, W / 2 + 22, 292 + breath);
  ui.setOpacity(mouthL, blinking ? 0 : 255);
  ui.setOpacity(mouthB, blinking ? 0 : 255);
  ui.setOpacity(mouthR, blinking ? 0 : 255);
}

ui.onSwipe(direction => {
  if (direction === 'left' || direction === 'down') tourIndex++;
  else tourIndex--;
  if (tourIndex < 0) tourIndex = ids.length - 1;
  if (tourIndex >= ids.length) tourIndex = 0;
  stopTour();
  setEmotion(ids[tourIndex], false);
});

globalThis.VelaMood = {
  setEmotion,
  setGaze,
  handleAIMessage,
  startTour,
  stopTour,
  setActive,
  renderStatic,
  states: ids.slice()
};

setEmotion(currentId, true);
setInterval(renderFrame, 80);
console.log('Vela Mood Console initialized with ' + ids.length + ' original states');
