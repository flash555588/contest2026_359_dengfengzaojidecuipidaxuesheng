'use strict';

const storage = system.storage;
const hosted = !!(system.pet && system.pet.getState);

function notifyToast(text) {
  if (text) prompt.showToast(text);
}

const backend = hosted ? system.pet : createPet({
  storage: storage,
  toast: notifyToast
});

let lastSeq = -1;

ui.background(0x0b0f10);

const title = ui.text('大肥鱼桌宠', 36, 16, 22, 0x8ec8ea);
const hint = ui.text('本地宠物 · 其他快应用可调用 system.pet', 36, 46, 14, 0x8b989c);
const status = ui.text('自由散步', 36, 78, 16, 0xf4ead6);
const bubble = ui.text('戳我一下，或者让别的应用喊我', 36, 108, 16, 0xc5d0d3);
const meters = ui.text('心情 72  饥饿 28', 36, 140, 14, 0x8b989c);

ui.button('散步', 36, 178, 100, 44, function () { setMode('wander'); }, 0x3d9ad1);
ui.button('跟随', 148, 178, 100, 44, function () { setMode('follow'); }, 0x2b6f9c);
ui.button('待着', 260, 178, 100, 44, function () { setMode('still'); }, 0x1e272a);

ui.button('戳一下', 36, 236, 100, 44, poke, 0x1fa97a);
ui.button('说句话', 148, 236, 100, 44, talk, 0x4f6fa8);
ui.button('聊天', 260, 236, 100, 44, chat, 0x5686fe);
ui.button('显隐', 372, 236, 100, 44, toggle, 0x1e272a);

ui.button('小鱼干', 36, 294, 88, 44, function () { feed('fish'); }, 0x3d9ad1);
ui.button('蛋糕', 136, 294, 88, 44, function () { feed('cake'); }, 0xe07a5f);
ui.button('棒棒糖', 236, 294, 88, 44, function () { feed('candy'); }, 0xd8a843);
ui.button('团子', 336, 294, 88, 44, function () { feed('dango'); }, 0xe26d7f);
ui.button('钻石', 436, 294, 88, 44, function () { feed('gem'); }, 0x7aa2e3);

ui.button('小', 36, 352, 72, 40, function () { setSize('small'); }, 0x1e272a);
ui.button('中', 120, 352, 72, 40, function () { setSize('medium'); }, 0x1e272a);
ui.button('大', 204, 352, 72, 40, function () { setSize('large'); }, 0x1e272a);
ui.button('天气', 288, 352, 88, 40, weather, 0x3d9ad1);

const apiHint = ui.text('其他应用: system.pet.speak("番茄完成") / poke() / feed("fish")',
                        36, 408, 13, 0x6b787c);

function modeLabel(mode) {
  if (mode === 'follow') return '跟随指针';
  if (mode === 'still') return '原地待着';
  return '自由散步';
}

function render() {
  const state = backend.getState();
  ui.setText(status, modeLabel(state.mode) + (state.visible ? '' : ' · 已隐藏'));
  ui.setText(bubble, state.bubble ? state.bubble : '（安静地呼吸）');
  ui.setText(meters, '心情 ' + state.mood + '  饥饿 ' + state.hunger +
             '  朝向 ' + state.dir);
  if (hosted && state.eventSeq !== lastSeq) {
    lastSeq = state.eventSeq;
    if (state.eventSeq > 0 && state.toast) notifyToast(state.toast);
  }
  return state;
}

function setMode(mode) {
  backend.setMode(mode);
  render();
}

function poke() {
  backend.poke();
  render();
}

function feed(food) {
  backend.feed(food);
  render();
}

function talk() {
  backend.say('梁白开，更适合国人的大硬鲸模型');
  render();
}

function chat() {
  prompt.input({title: '给大肥鱼发消息', placeholder: '说点什么', maxLength: 40},
    function (value) {
      if (value) backend.chat(value);
      render();
    });
}

function toggle() {
  backend.toggle();
  render();
}

function setSize(size) {
  backend.setSize(size);
  render();
}

function weather() {
  if (backend.weather) backend.weather();
  else backend.chat('天气');
  render();
}

function tick() {
  if (!hosted && backend.tick) backend.tick(1000);
  render();
}

render();
setInterval(tick, 1000);

globalThis.Dafeiyu = {
  getState: function () { return backend.getState(); },
  setMode: setMode,
  poke: poke,
  feed: feed,
  talk: talk,
  chat: function (text) { backend.chat(text); return render(); },
  toggle: toggle,
  setSize: setSize,
  weather: weather,
  tick: tick
};
