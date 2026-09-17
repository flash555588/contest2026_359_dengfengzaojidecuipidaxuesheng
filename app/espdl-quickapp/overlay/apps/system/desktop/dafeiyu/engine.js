'use strict';

function createPet(options) {
  options = options || {};
  const LINES = [
    '梁白开，更适合国人的大硬鲸模型',
    '五梁威力，变身！',
    '我先去吃饭啦！这个你测一下~',
    '你这吃白饭的用户！',
    '大肥鱼坐的住',
    '去别的地方玩！不要耽误AGI训练！'
  ];
  const FOOD = {
    cake: '蛋糕！罪恶但快乐……',
    candy: '棒棒糖！转圈圈～',
    dango: '三色团子！软乎乎～',
    gem: '钻石？！这能吃吗……咕咚。真香！',
    fish: '小鱼干！我的最爱！'
  };
  let mode = 'wander';
  let dir = 'down';
  let size = 'medium';
  let x = 880;
  let y = 470;
  let w = 96;
  let h = 88;
  let mood = 72;
  let hunger = 28;
  let visible = true;
  let eventSeq = 0;
  let bubble = '';
  let inner = false;
  let lastEvent = '';
  let lastReply = '';
  let toast = '';
  let jump = 0;
  let rng = 1;

  function nextRand() {
    rng ^= rng << 13;
    rng ^= rng >>> 17;
    rng ^= rng << 5;
    rng >>>= 0;
    if (!rng) rng = 1;
    return rng;
  }

  function pick(list) {
    return list[nextRand() % list.length];
  }

  function emit(event, nextToast) {
    eventSeq += 1;
    lastEvent = event;
    toast = nextToast || '';
    if (options.toast && toast) options.toast(toast);
  }

  function say(text, isInner) {
    if (!text) return getState();
    bubble = text;
    inner = !!isInner;
    return getState();
  }

  function foodLine(food) {
    const key = String(food || 'fish');
    if (key.indexOf('cake') >= 0 || key.indexOf('蛋糕') >= 0) return FOOD.cake;
    if (key.indexOf('candy') >= 0 || key.indexOf('糖') >= 0) return FOOD.candy;
    if (key.indexOf('dango') >= 0 || key.indexOf('团子') >= 0) return FOOD.dango;
    if (key.indexOf('gem') >= 0 || key.indexOf('钻石') >= 0) return FOOD.gem;
    return FOOD.fish;
  }

  function getState() {
    return {
      mode: mode,
      dir: dir,
      size: size,
      facing: 1,
      x: x,
      y: y,
      w: w,
      h: h,
      mood: mood,
      hunger: hunger,
      visible: visible,
      dragging: false,
      jump: jump,
      eventSeq: eventSeq,
      bubble: bubble,
      inner: inner,
      event: lastEvent,
      reply: lastReply,
      toast: toast
    };
  }

  function persist() {
    if (!options.storage) return;
    options.storage.set('dafeiyu', JSON.stringify({
      mode: mode, size: size, visible: visible, mood: mood, hunger: hunger
    }));
  }

  function restore() {
    if (!options.storage) return;
    const raw = options.storage.get('dafeiyu');
    if (!raw) return;
    try {
      const data = JSON.parse(raw);
      if (data.mode) mode = data.mode;
      if (data.size) size = data.size;
      if (typeof data.visible === 'boolean') visible = data.visible;
      if (typeof data.mood === 'number') mood = data.mood;
      if (typeof data.hunger === 'number') hunger = data.hunger;
    } catch (err) {
    }
  }

  restore();

  return {
    getState: getState,
    setMode: function (next) {
      mode = next === 'follow' || next === 'still' ? next : 'wander';
      emit('mode', mode === 'follow' ? '跟着你' : (mode === 'still' ? '原地待着' : '自由散步'));
      persist();
      return getState();
    },
    weather: function () {
      return this.chat('天气');
    },
    show: function () {
      visible = true;
      emit('show', '我回来了');
      persist();
      return getState();
    },
    hide: function () {
      visible = false;
      emit('hide', '我隐身了');
      persist();
      return getState();
    },
    toggle: function () {
      return visible ? this.hide() : this.show();
    },
    poke: function () {
      jump = 500;
      mood = Math.min(100, mood + 3);
      say(pick(LINES));
      emit('poke', bubble);
      persist();
      return getState();
    },
    feed: function (food) {
      const line = foodLine(food);
      hunger = Math.max(0, hunger - 18);
      mood = Math.min(100, mood + 8);
      jump = 400;
      say(line);
      emit('feed', line);
      persist();
      return getState();
    },
    say: function (text, isInner) {
      say(text, isInner);
      emit('say', text);
      return getState();
    },
    speak: function (text, isInner) {
      return this.say(text, isInner);
    },
    chat: function (text) {
      let reply = pick(LINES);
      const msg = String(text || '');
      if (!msg) reply = '你倒是说话呀';
      else if (msg.indexOf('天气') >= 0) reply = '今天适合摸鱼，别问温度。';
      else if (msg.indexOf('你好') >= 0 || msg.indexOf('嗨') >= 0) reply = '来啦来啦，碳基生物。';
      else if (msg.indexOf('饿') >= 0 || msg.indexOf('吃') >= 0) reply = '投喂小鱼干，谢谢老板。';
      else if (msg.indexOf('困') >= 0 || msg.indexOf('睡') >= 0) reply = '我先去吃饭啦！模型以后再说。';
      lastReply = reply;
      say(reply);
      emit('chat', reply);
      return getState();
    },
    setSize: function (next) {
      size = next === 'small' || next === 'large' ? next : 'medium';
      w = size === 'small' ? 72 : (size === 'large' ? 120 : 96);
      h = size === 'small' ? 66 : (size === 'large' ? 110 : 88);
      emit('size', size);
      persist();
      return getState();
    },
    follow: function (nx, ny) {
      x = nx;
      y = ny;
      mode = 'follow';
      emit('mode', '跟着你');
      persist();
      return getState();
    },
    tick: function (dt) {
      if (jump > 0) jump = Math.max(0, jump - (dt || 50));
      if (!visible) return getState();
      if (mode === 'wander') {
        x += (nextRand() % 5) - 2;
      } else if (mode === 'follow') {
        if (x < 480) x += 4;
        else if (x > 480) x -= 4;
      }
      return getState();
    }
  };
}
