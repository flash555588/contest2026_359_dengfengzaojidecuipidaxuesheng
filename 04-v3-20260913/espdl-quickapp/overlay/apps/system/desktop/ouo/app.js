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
let phase = 0;
let blinkUntil = 0, nextBlink = 35;
let popPhase = -99, pokePhase = -99, eyeRadius = -1;
/* The gaze springs towards its target, so a drag feels alive instead of
 * snapping between positions. */
let gazeX = 0, gazeY = 0, gazeTargetX = 0, gazeTargetY = 0;
let touching = false;
let nextGlance = 70, glanceUntil = 0;

ui.background(ui.card);
/* A rounded backdrop keeps the desktop card shape instead of painting a
 * square colour over its corners. */
const backdrop = ui.panel(0, 0, W, H, states[currentId][1]);
ui.setStyle(backdrop, {radius: 24, borderWidth: 0});
const anchorL = 349, anchorR = 675, eyeCenterY = 262;
const eyeL = ui.panel(anchorL - 27, eyeCenterY - 64, 54, 128, states[currentId][2]);
const eyeR = ui.panel(anchorR - 27, eyeCenterY - 64, 54, 128, states[currentId][2]);

function clamp(value, limit) {
  return value > limit ? limit : value < -limit ? -limit : value;
}

function setEmotion(id, automatic) {
  if (!states[id]) id = fallbackId;
  currentId = id;
  const s = states[id];
  tourIndex = ids.indexOf(id);
  ui.setColor(backdrop, s[1]);
  ui.setColor(eyeL, s[2]); ui.setColor(eyeR, s[2]);
  ui.setSize(eyeL, Math.max(20, s[4] * 3), Math.max(36, s[3] * 2));
  ui.setSize(eyeR, Math.max(20, s[4] * 3), Math.max(36, s[3] * 2));
  if (!automatic) system.storage.set('emotion', id);
  popPhase = phase;          /* one short pop so a change is felt, not read */
  pokePhase = -99;
}

function setGaze(nx, ny) {
  gazeTargetX = clamp(Number(nx) || 0, 1);
  gazeTargetY = clamp(Number(ny) || 0, 1);
}

function handleAIMessage(message) {
  let data = message;
  try {
    if (typeof message === 'string') data = JSON.parse(message);
    if (!data || !states[String(data.emotionId)]) throw new Error('invalid emotionId');
    setEmotion(String(data.emotionId), false);
    return true;
  } catch (error) {
    setEmotion(fallbackId, false);
    return false;
  }
}

function startTour(period) {
  stopTour();
  const delay = Math.max(500, Number(period) || 1800);
  tourTimer = setInterval(() => {
    tourIndex = (tourIndex + 1) % ids.length;
    setEmotion(ids[tourIndex], true);
  }, delay);
}

function stopTour() {
  if (tourTimer) clearInterval(tourTimer);
  tourTimer = 0;
}

function setActive(value) {
  active = !!value;
}

function renderStatic() {
  renderFrame();
}

function glance() {
  if (touching || glanceUntil > phase) return;
  if (nextGlance > phase) return;
  gazeTargetX = (Math.sin(phase * 0.7) + Math.sin(phase * 0.23)) * 0.28;
  gazeTargetY = Math.sin(phase * 0.41) * 0.18;
  glanceUntil = phase + 12;
  nextGlance = phase + 70 + (phase % 60);
}

function renderFrame() {
  if (!active) return;
  const s = states[currentId];
  phase++;
  const t = phase / 5;
  const motion = s[8];
  glance();
  gazeX += (gazeTargetX - gazeX) * 0.32;
  gazeY += (gazeTargetY - gazeY) * 0.32;

  let ox = gazeX * 30;
  let oy = gazeY * 16;
  const breath = Math.sin(t * 0.22) * 3;
  if (motion === 'jitter') ox += ((phase % 3) - 1) * 5;
  if (motion === 'scan') ox += Math.sin(t * 0.55) * 18;
  if (motion === 'peek') ox += Math.sin(t * 0.16) * 12;
  if (motion === 'sad') oy += 8;
  if (motion === 'proud') oy -= 5;
  if (motion === 'bounce' || motion === 'celebrate') oy += Math.sin(t * 0.42) * 7;

  if (phase >= nextBlink) {
    blinkUntil = phase + 2;
    nextBlink = phase + 30 + (phase % 25);
  }

  const pop = Math.max(0, 1 - (phase - popPhase) / 3);
  /* Held down: wide, curious eyes. Released: a quick squint and wobble. */
  const poke = touching ? 1 : Math.max(0, 1 - (phase - pokePhase) / 5);
  const grow = 1 + 0.16 * pop + (touching ? 0.10 : 0);
  let eyeW = Math.max(24, s[4] * 3) * grow;
  let eyeH = Math.max(36, s[3] * 2) * grow;
  if (!touching && poke > 0) {
    eyeH *= 0.45 + 0.55 * (1 - poke);
    eyeW *= 1.05;
    ox += Math.sin(phase * 1.7) * 7 * poke;
  }
  const blinking = !touching && poke === 0 &&
                   (phase < blinkUntil || motion === 'sleep');
  if (blinking) { eyeW = 54; eyeH = 6; }
  const height = Math.max(6, Math.round(eyeH));
  const width = Math.max(12, Math.round(eyeW));
  const y = Math.round(eyeCenterY - height / 2 + breath + oy +
                       (touching ? -4 : 0));
  const xl = Math.round(anchorL - width / 2 + ox);
  const xr = Math.round(anchorR - width / 2 + ox);
  const radius = blinking ? 4 : Math.round(Math.min(width, height) / 2);
  ui.setPos(eyeL, xl, y); ui.setPos(eyeR, xr, y);
  ui.setSize(eyeL, width, height); ui.setSize(eyeR, width, height);
  if (radius !== eyeRadius) {
    eyeRadius = radius;
    ui.setStyle(eyeL, {radius: radius, borderWidth: 0});
    ui.setStyle(eyeR, {radius: radius, borderWidth: 0});
  }
}

/* Touching anywhere makes OuO look at the finger; letting go answers with a
 * squint. Swiping keeps switching emotions, as in the original app. */
ui.onTouch((state, nx, ny) => {
  if (state === 'down') {
    touching = true;
    pokePhase = -99;
    nextGlance = phase + 70;
    setGaze((nx - 0.5) * 2.4, (ny - 0.5) * 1.6);
  } else if (state === 'move') {
    setGaze((nx - 0.5) * 2.4, (ny - 0.5) * 1.6);
  } else {
    touching = false;
    pokePhase = phase;
    blinkUntil = phase + 2;
    setGaze(0, 0);
  }
});

ui.onSwipe(direction => {
  stopTour();
  const delta = direction === 'left' || direction === 'down' ? 1 : -1;
  tourIndex = (tourIndex + delta + ids.length) % ids.length;
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
  isTourRunning: () => tourTimer !== 0,
  states: ids.slice()
};

setEmotion(currentId, true);
setInterval(renderFrame, 50);
console.log('Vela Mood Console initialized with ' + ids.length + ' original states');
