/* SPDX-License-Identifier: Apache-2.0 */
'use strict';

(function () {
  const nativeService = system.homeAssistantService;
  const sharedReady = nativeService && nativeService.apiVersion === 1 &&
    ['configure', 'get', 'getState', 'control', 'poll', 'close', 'status']
      .every(name => typeof nativeService[name] === 'function');
  const bridge = nativeService ? (sharedReady ? sharedAdapter() : {}) : system.homeAssistant;
  const storage = system.storage;
  const bridgeReady = bridge && ['get', 'getState', 'callService', 'poll']
    .every(name => typeof bridge[name] === 'function');
  const MAX_ENTITIES = 128;
  const MAX_FAVORITES = 16;
  const RESPONSE_LIMIT = 65536;
  const REQUEST_TIMEOUT = 12000;
  const REFRESH_INTERVAL = 10000;
  const VERIFY_INTERVAL = 1000;
  const VERIFY_WINDOW = 8000;
  const VERIFY_ATTEMPTS = 4;
  const BUTTON_SLOTS = 16;
  const buttons = [];
  const buttonPool = [];
  const size = ui.getSize();
  const W = size.width, H = size.height;
  const dark = ui.primary === 0xffffff;
  const colors = {
    background: dark ? 0x101318 : 0xf3f5f8,
    surface: dark ? 0x1d222b : 0xffffff,
    active: dark ? 0x3c2d20 : 0xfff0e4,
    hero: dark ? 0x32271f : 0xffe4cf,
    heroMuted: dark ? 0xcab39d : 0x8b5b36,
    sensor: dark ? 0x1c2f33 : 0xe3f1ef,
    detail: dark ? 0x171c24 : 0xf7f8fa,
    text: dark ? 0xf7f8fa : 0x15171a,
    muted: dark ? 0x9da5b2 : 0x6f7782,
    accent: dark ? 0xff7a1a : 0xff6900,
    selected: dark ? 0x4b321f : 0xffe2cc,
    good: dark ? 0x68d39b : 0x148054,
    warning: dark ? 0xf2be63 : 0xa36100
  };
  const styles = {
    icon: { radius: 18, borderWidth: 0 },
    tab: { radius: 16, borderWidth: 0, fontSize: 15 },
    card: { radius: 18, borderWidth: 0 },
    control: { radius: 14, borderWidth: 0, fontSize: 16 },
    setting: { radius: 14, borderWidth: 0, fontSize: 15 },
    panel: { radius: 18, borderWidth: 0 }
  };
  const glyph = {
    search: '\uf002', refresh: '\uf021', settings: '\uf013',
    previous: '\uf053', next: '\uf054', favorite: '\uf067', followed: '\uf00c',
    light: '\uf0e7', switch: '\uf011', input_boolean: '\uf205', fan: '\uf2dc',
    sensor: '\uf2c9', binary_sensor: '\uf06a', home: '\uf015'
  };
  let url = 'http://homeassistant.local:8123';
  let token = '';
  let httpAllowed = false;
  let favorites = [];
  let scope = 'all';
  let entities = [];
  let services = Object.create(null);
  let serviceDiscoveryLimited = false;
  let selectedId = '';
  let filter = 'all';
  let search = '';
  let page = 0;
  let settingsOpen = false;
  let detailOpen = false;
  let connected = false;
  let phase = 'disconnected';
  let message = '未连接';
  let generation = 0;
  let pending = null;
  let queued = null;
  let nativeBusy = false;
  let autoRefresh = false;
  let failures = 0;
  let nextRefresh = Infinity;
  let lastSync = 0;
  let lastCached = false;
  let disposed = false;
  let limited = false;
  let watchQueue = [];
  let watchResults = [];
  let timer;
  let widgets;
  let nativeManaged = false;

  function sharedAdapter() {
    let requestId = 0, detached = false;
    function prepare() {
      detached = false;
      if (token) {
        if (nativeService.configure(url, token, httpAllowed) !== 0) return false;
        token = '';
        httpAllowed = false;
        nativeManaged = true;
      }
      const status = nativeService.status();
      nativeManaged = status.configured && parseUrl(status.url) === url;
      return nativeManaged;
    }
    function submit(action) {
      if (!prepare()) return false;
      const id = action();
      if (!Number.isInteger(id) || id <= 0) return false;
      requestId = id;
      return true;
    }
    function close() {
      detached = true;
      requestId = 0;
      nativeService.close();
    }
    return {
      get: (_url, _token, resource) => submit(() => nativeService.get(resource)),
      getState: (_url, _token, entity) => submit(() => nativeService.getState(entity)),
      callService: (_url, _token, domain, service, data) => {
        const body = JSON.parse(data);
        if (!validEntity(body.entity_id) || body.entity_id.split('.')[0] !== domain ||
            !['turn_on', 'turn_off'].includes(service) ||
            Object.keys(body).some(key => !['entity_id', 'brightness_pct'].includes(key))) return false;
        return submit(() => nativeService.control(body.entity_id, service === 'turn_on',
          body.brightness_pct === undefined ? -1 : body.brightness_pct));
      },
      poll: () => {
        if (detached) return { busy: false, done: false };
        const result = nativeService.poll();
        if (result.done && result.requestId !== requestId)
          return { busy: false, done: true, error: -22, status: 0, body: '' };
        return result;
      },
      close, stop: close
    };
  }

  function sharedConnection() {
    if (!sharedReady) return false;
    try {
      const status = nativeService.status();
      nativeManaged = status.configured && parseUrl(status.url) === url;
      return nativeManaged;
    } catch (_) {
      nativeManaged = false;
      return false;
    }
  }

  function clean(value, limit) {
    return typeof value === 'string' ?
      Array.from(value.replace(/[\u0000-\u001f\u007f]/g, ' '))
        .slice(0, limit).join('') : '';
  }

  function validEntity(value) {
    return typeof value === 'string' && value.length < 96 &&
      /^[a-z][a-z0-9_]*\.[a-z0-9_]+$/.test(value);
  }

  function parseUrl(value) {
    if (typeof value !== 'string' || value.length > 120) return '';
    const match = /^http:\/\/([a-zA-Z0-9.-]+)(?::([0-9]{1,5}))?\/?$/.exec(value.trim());
    if (!match) return '';
    const host = match[1].toLowerCase();
    const port = match[2] ? Number(match[2]) : 8123;
    if (port < 1 || port > 65535) return '';
    let local = host === 'localhost';
    if (/^[0-9.]+$/.test(host)) {
      const pieces = host.split('.');
      if (pieces.length !== 4 ||
          pieces.some(x => String(Number(x)) !== x || Number(x) > 255)) return '';
      const a = pieces.map(Number);
      local = a[0] === 10 || a[0] === 127 ||
        (a[0] === 192 && a[1] === 168) ||
        (a[0] === 172 && a[1] >= 16 && a[1] <= 31) ||
        (a[0] === 169 && a[1] === 254);
    } else if (host.endsWith('.local') || host.endsWith('.lan')) {
      local = host.split('.').every(x =>
        x.length > 0 && x.length < 64 && /^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?$/.test(x));
    }
    return local ? 'http://' + host + ':' + port : '';
  }

  function save(key, value) {
    try {
      if (storage && typeof storage.set === 'function') storage.set(key, value);
      return true;
    } catch (_) {
      message = '本地保存失败，本次会话仍可使用';
      return false;
    }
  }

  try {
    if (storage && typeof storage.get === 'function') {
      url = parseUrl(storage.get('eh_url')) || url;
      const saved = JSON.parse(storage.get('eh_favs') || '[]');
      if (Array.isArray(saved)) {
        favorites = saved.filter(validEntity)
          .filter((id, index, list) => list.indexOf(id) === index)
          .slice(0, MAX_FAVORITES);
      }
      scope = storage.get('eh_scope') === 'favorites' ? 'favorites' : 'all';
    }
  } catch (_) {
    message = '本地设置无法读取';
  }

  if (sharedReady) {
    try {
      const status = nativeService.status();
      if (status.configured && parseUrl(status.url)) {
        url = parseUrl(status.url);
        nativeManaged = true;
        message = '本地 HA 服务已配置，尚未同步';
      }
    } catch (_) { message = '本地 HA 服务暂不可用'; }
  }
  // A remembered native connection should open directly to the home view and
  // begin its first bounded sync. New connections start with the setup view.
  settingsOpen = !nativeManaged;
  autoRefresh = nativeManaged;

  function staleAll() {
    entities.forEach(item => { item.stale = true; });
  }

  function invalidate() {
    generation++;
    queued = null;
    if (pending) pending.abandoned = true;
    connected = false;
    autoRefresh = false;
    nextRefresh = Infinity;
    lastCached = false;
    phase = 'disconnected';
    services = Object.create(null);
    serviceDiscoveryLimited = false;
    staleAll();
  }

  function disconnect() {
    invalidate();
    token = '';
    httpAllowed = false;
    message = '已断开，令牌已从会话移除';
    if (sharedReady) {
      try { bridge.close(); } catch (_) { /* UI still invalidates its session. */ }
      nativeManaged = false;
      message = '界面已断开，本地服务配置保留';
    }
    render();
  }

  function credentialsReady() {
    if (!bridgeReady) message = '固件未提供 Home Assistant 接口';
    else if (!parseUrl(url)) message = '需要局域网 HTTP 地址';
    else if (!token && sharedConnection()) return true;
    else if (!token) message = '尚未输入访问令牌';
    else if (!httpAllowed) message = 'HTTP 未加密，尚未授权连接';
    else return true;
    render();
    return false;
  }

  function schedule(kind, entityId, data) {
    queued = { kind, entityId: entityId || '', data, generation };
  }

  function startQueued() {
    if (!queued || pending || nativeBusy || disposed) return;
    if (queued.notBefore && Date.now() < queued.notBefore) return;
    const request = queued;
    queued = null;
    if (request.generation !== generation || !credentialsReady()) return;
    let accepted = false;
    try {
      if (request.kind === 'service') {
        accepted = bridge.callService(url, token, request.data.domain,
          request.data.service, JSON.stringify(request.data.body));
      } else if (request.kind === 'verify' || request.kind === 'watch') {
        accepted = bridge.getState(url, token, request.entityId);
      } else {
        accepted = bridge.get(url, token, request.kind);
      }
    } catch (_) {
      accepted = false;
    }
    if (!accepted) {
      fail(request, '接口忙或参数被拒绝', false);
      return;
    }
    pending = request;
    pending.started = Date.now();
    nativeBusy = true;
    render();
  }

  function connect() {
    if (!credentialsReady()) return;
    if (pending || nativeBusy || queued) {
      message = '仍有请求未结束';
      render();
      return;
    }
    generation++;
    connected = false;
    phase = 'connecting';
    settingsOpen = false;
    detailOpen = false;
    selectedId = '';
    autoRefresh = true;
    failures = 0;
    services = Object.create(null);
    serviceDiscoveryLimited = false;
    staleAll();
    message = '正在连接 Home Assistant';
    schedule('config');
    startQueued();
  }

  function refresh() {
    if (disposed || pending || nativeBusy || queued) return;
    if (!connected) { connect(); return; }
    if (!credentialsReady()) return;
    message = '正在同步状态';
    beginStates();
    startQueued();
  }

  function beginStates() {
    if (scope === 'favorites') {
      watchQueue = favorites.slice();
      watchResults = [];
      nextWatch();
    } else {
      schedule('states');
    }
  }

  function nextWatch() {
    if (watchQueue.length) schedule('watch', watchQueue.shift());
    else commitStates(watchResults, false, false);
  }

  function normalize(raw, expectedId) {
    if (!raw || typeof raw !== 'object' || !validEntity(raw.entity_id) ||
        typeof raw.state !== 'string' ||
        (expectedId && raw.entity_id !== expectedId)) return null;
    const attributes = raw.attributes && typeof raw.attributes === 'object' ?
      raw.attributes : {};
    const modes = Array.isArray(attributes.supported_color_modes) ?
      attributes.supported_color_modes : [];
    const brightness = typeof attributes.brightness === 'number' &&
      Number.isFinite(attributes.brightness) &&
      attributes.brightness >= 0 && attributes.brightness <= 255 ?
      Math.round(attributes.brightness * 100 / 255) : null;
    return {
      entity_id: raw.entity_id,
      domain: raw.entity_id.split('.')[0],
      name: clean(attributes.friendly_name, 64) || raw.entity_id,
      state: clean(raw.state, 64),
      unit: clean(attributes.unit_of_measurement, 16),
      brightness,
      dimmable: raw.entity_id.startsWith('light.') &&
        (brightness !== null || modes.some(mode =>
          ['brightness', 'color_temp', 'hs', 'xy', 'rgb', 'rgbw', 'rgbww', 'white'].includes(mode))),
      stale: false
    };
  }

  function commitStates(values, wasLimited, fromCache) {
    entities = values;
    limited = wasLimited;
    lastCached = !!fromCache;
    if (!entities.some(item => item.entity_id === selectedId)) selectedId = '';
    connected = true;
    phase = 'ready';
    failures = 0;
    lastSync = Date.now();
    nextRefresh = lastSync + REFRESH_INTERVAL;
    message = limited ? '已达 128 个实体上限，可切换关注同步' :
      '已同步 ' + entities.length + ' 个实体' +
      (lastCached ? ' · 本地缓存' : '');
    if (serviceDiscoveryLimited) message = '只读 · 服务目录超过 64 KiB' +
      (lastCached ? ' · 本地缓存' : '');
    render();
  }

  function failureMessage(result, request) {
    if (result.error === -75 || result.error === -7) {
      if (request.kind === 'states') return '响应超过 64 KiB，请切换关注同步';
      if (request.kind === 'service' || request.kind === 'verify')
        return '控制结果未确认，请刷新；不会重试';
      return '该响应超过 64 KiB，当前固件无法读取';
    }
    if (result.error) return request.kind === 'service' ?
      '控制结果未确认，请刷新；不会重试' : '网络连接失败，状态已过期';
    if (result.status === 401 || result.status === 403) return '认证失败，请重新输入令牌';
    return request.kind === 'service' ? '控制被拒绝，HTTP ' + result.status :
      '服务返回 HTTP ' + result.status;
  }

  function fail(request, text, auth) {
    queued = null;
    connected = false;
    phase = 'error';
    staleAll();
    message = text;
    failures++;
    if (auth) {
      generation++;
      token = '';
      httpAllowed = false;
      nativeManaged = false;
      autoRefresh = false;
    }
    const uncertain = request.kind === 'service' || request.kind === 'verify';
    nextRefresh = auth || uncertain ? Infinity :
      Date.now() + Math.min(60000, REFRESH_INTERVAL * Math.pow(2, Math.min(failures - 1, 3)));
    if (request.kind === 'verify') message = '控制结果未确认，请刷新；不会重试';
    render();
  }

  function complete(request, result) {
    if (request.generation !== generation || request.abandoned || disposed) return;
    if (request.kind === 'services' && (result.error === -75 || result.error === -7 ||
        (!result.error && result.status >= 200 && result.status < 300 &&
         typeof result.body === 'string' && result.body.length > RESPONSE_LIMIT))) {
      services = Object.create(null);
      serviceDiscoveryLimited = true;
      message = '服务目录超限，正在只读同步';
      beginStates();
      return;
    }
    if (request.kind === 'watch' && !result.error && result.status === 404) {
      watchResults.push({
        entity_id: request.entityId, domain: request.entityId.split('.')[0],
        name: request.entityId, state: 'unavailable', unit: '',
        brightness: null, dimmable: false, stale: false
      });
      nextWatch();
      return;
    }
    if (result.error || result.status < 200 || result.status >= 300) {
      fail(request, failureMessage(result, request),
        result.status === 401 || result.status === 403);
      return;
    }
    if (request.kind === 'service') {
      message = '控制已受理，正在核对状态';
      phase = 'verifying';
      nextRefresh = Infinity;
      schedule('verify', request.entityId, {
        state: request.data.service === 'turn_on' ? 'on' : 'off',
        brightness: request.data.body.brightness_pct,
        attempt: 1,
        deadline: Date.now() + VERIFY_WINDOW
      });
      return;
    }
    try {
      if (typeof result.body !== 'string' || result.body.length > RESPONSE_LIMIT)
        throw new Error('response limit');
      const value = JSON.parse(result.body);
      if (request.kind === 'config') {
        if (!value || typeof value !== 'object' || Array.isArray(value))
          throw new Error('invalid config');
        schedule('services');
      } else if (request.kind === 'services') {
        if (!Array.isArray(value)) throw new Error('invalid services');
        const next = Object.create(null);
        value.forEach(group => {
          if (!group || typeof group.domain !== 'string' ||
              !/^[a-z_]+$/.test(group.domain) ||
              !group.services || typeof group.services !== 'object') return;
          const names = Array.isArray(group.services) ?
            group.services : Object.keys(group.services);
          names.forEach(name => {
            if (typeof name === 'string' && /^[a-z_]+$/.test(name))
              next[group.domain + '.' + name] = true;
          });
        });
        services = next;
        beginStates();
      } else if (request.kind === 'states') {
        if (!Array.isArray(value)) throw new Error('invalid states');
        const next = [];
        const seen = Object.create(null);
        let overflow = false;
        value.forEach(raw => {
          const item = normalize(raw);
          if (!item || seen[item.entity_id]) return;
          seen[item.entity_id] = true;
          if (next.length < MAX_ENTITIES) next.push(item);
          else overflow = true;
        });
        commitStates(next, overflow, !!result.cached);
      } else {
        const item = normalize(value, request.entityId);
        if (!item) throw new Error('entity mismatch');
        if (request.kind === 'watch') {
          watchResults.push(item);
          nextWatch();
        } else {
          const index = entities.findIndex(row => row.entity_id === request.entityId);
          if (index >= 0) entities[index] = item;
          const target = request.data;
          const matched = item.state === target.state &&
            (target.brightness === undefined || (item.brightness !== null &&
             Math.abs(item.brightness - target.brightness) <= 1));
          if (!matched) {
            if (target.attempt >= VERIFY_ATTEMPTS || Date.now() >= target.deadline) {
              fail(request, '控制结果未确认，请刷新；不会重试', false);
              return;
            }
            message = '等待设备达到目标状态';
            schedule('verify', request.entityId, {
              state: target.state, brightness: target.brightness,
              attempt: target.attempt + 1, deadline: target.deadline
            });
            queued.notBefore = Date.now() + VERIFY_INTERVAL;
            render();
            return;
          }
          connected = true;
          phase = 'ready';
          message = '状态已达到目标：' + item.name;
          failures = 0;
          nextRefresh = Date.now() + REFRESH_INTERVAL;
          render();
        }
      }
    } catch (_) {
      fail(request, '响应格式错误，未应用数据', false);
    }
  }

  function tick() {
    if (disposed || !bridgeReady) return;
    if (pending && !pending.abandoned &&
        Date.now() - pending.started >= REQUEST_TIMEOUT) {
      pending.abandoned = true;
      fail(pending, pending.kind === 'service' ?
        '控制结果未确认，请刷新；不会重试' : '请求超时，等待旧请求结束', false);
    }
    // Enforce the whole confirmation window before consuming a late result.
    const verification = pending || queued;
    if (verification && verification.kind === 'verify' &&
        !verification.abandoned && Date.now() >= verification.data.deadline) {
      verification.abandoned = true;
      fail(verification, '控制结果未确认，请刷新；不会重试', false);
    }
    let result;
    try { result = bridge.poll(); }
    catch (_) {
      if (pending && !pending.abandoned) {
        pending.abandoned = true;
        fail(pending, '接口异常，等待旧请求结束', false);
      }
      return;
    }
    if (!result || typeof result !== 'object') return;
    nativeBusy = !!result.busy;
    if (pending && result.done) {
      const request = pending;
      pending = null;
      complete(request, result);
      render();
    } else if (pending && !nativeBusy && pending.abandoned) {
      pending = null;
      render();
    }
    startQueued();
    if (!pending && !queued && !nativeBusy && !settingsOpen &&
        autoRefresh && (token || sharedConnection()) && Date.now() >= nextRefresh) {
      if (connected) refresh();
      else {
        phase = 'connecting';
        message = '正在重新读取连接状态';
        schedule('config');
        startQueued();
      }
    }
  }

  function current() {
    return entities.find(item => item.entity_id === selectedId) || null;
  }

  function available(item) {
    return item && !item.stale && item.state !== 'unavailable' &&
      item.state !== 'unknown';
  }

  function supports(item, service) {
    if (sharedReady) {
      try { if (!nativeService.status().canControl) return false; }
      catch (_) { return false; }
    }
    return item && ['light', 'switch', 'input_boolean', 'fan'].includes(item.domain) &&
      !!services[item.domain + '.' + service];
  }

  function control(service, brightness) {
    const item = current();
    if (!connected || !available(item) || pending || queued || nativeBusy ||
        !supports(item, service)) return;
    const body = { entity_id: item.entity_id };
    if (brightness !== undefined) {
      if (!item.dimmable || !Number.isFinite(brightness)) return;
      body.brightness_pct = Math.max(1, Math.min(100, Math.round(brightness)));
    }
    message = '正在发送控制请求';
    schedule('service', item.entity_id, { domain: item.domain, service, body });
    startQueued();
  }

  function changeBrightness(delta) {
    const item = current();
    if (!item || item.brightness === null) return;
    control('turn_on', item.brightness + delta);
  }

  function toggleFavorite() {
    const item = current();
    if (!item) return;
    const index = favorites.indexOf(item.entity_id);
    if (index >= 0) favorites.splice(index, 1);
    else if (favorites.length < MAX_FAVORITES) favorites.push(item.entity_id);
    else { message = '关注列表已达 16 个实体'; render(); return; }
    save('eh_favs', JSON.stringify(favorites));
    render();
  }

  function promptUrl() {
    const openedGeneration = generation;
    prompt.input({ title: 'Home Assistant 地址', value: url, maxLength: 120 }, value => {
      if (disposed || openedGeneration !== generation ||
          value === null || value === undefined || value === '') return;
      const next = parseUrl(value);
      if (!next) {
        message = /^https:/i.test(String(value)) ?
          '当前 HA 接口不支持 HTTPS，不会降级连接' : '仅支持局域网 HTTP 地址，不允许路径或账号';
      } else if (next !== url) {
        invalidate();
        token = '';
        httpAllowed = false;
        url = next;
        favorites = [];
        entities = [];
        selectedId = '';
        message = '服务端已变更，请重新输入令牌';
        save('eh_url', url);
        save('eh_favs', '[]');
      }
      render();
    });
  }

  function promptToken() {
    const openedGeneration = generation;
    prompt.input({ title: '访问令牌（仅本次会话）', value: '',
      password: true, maxLength: 511 }, value => {
      if (disposed || openedGeneration !== generation ||
          value === null || value === undefined || value === '') return;
      if (typeof value !== 'string' || value.length > 511 || !/^[\x21-\x7e]+$/.test(value)) {
        message = '令牌含空白、控制字符或超过长度限制';
      } else {
        invalidate();
        token = value;
        message = '令牌已输入，仅保留在本次会话';
      }
      render();
    });
  }

  function addFavorite() {
    const openedGeneration = generation;
    prompt.input({ title: '关注实体 ID', value: '', maxLength: 95 }, value => {
      if (disposed || openedGeneration !== generation ||
          value === null || value === undefined || value === '') return;
      if (!validEntity(value)) message = '实体 ID 格式无效';
      else if (favorites.includes(value)) message = '该实体已在关注列表';
      else if (favorites.length >= MAX_FAVORITES) message = '关注列表已达 16 个实体';
      else {
        favorites.push(value);
        message = '已关注 ' + value;
        save('eh_favs', JSON.stringify(favorites));
      }
      render();
    });
  }

  function promptSearch() {
    prompt.input({ title: '搜索名称或实体 ID', value: search, maxLength: 64 }, value => {
      if (disposed || value === null || value === undefined) return;
      search = clean(value, 64).trim().toLowerCase();
      selectedId = '';
      page = 0;
      settingsOpen = false;
      detailOpen = false;
      render();
    });
  }

  function visibleEntities() {
    return entities.filter(item => {
      if (filter === 'favorites' && !favorites.includes(item.entity_id)) return false;
      if (filter === 'controls' &&
          !['light', 'switch', 'input_boolean', 'fan'].includes(item.domain)) return false;
      if (filter === 'sensors' && !['sensor', 'binary_sensor'].includes(item.domain)) return false;
      return !search || (item.name + ' ' + item.entity_id).toLowerCase().includes(search);
    });
  }

  function stateText(item) {
    const translated = { on: '开启', off: '关闭', unavailable: '离线', unknown: '未知' };
    return (item.stale ? '上次：' : '') +
      (translated[item.state] || item.state) + (item.unit ? ' ' + item.unit : '');
  }

  function activeDevices() {
    return entities.filter(item => available(item) && item.state === 'on' &&
      ['light', 'switch', 'input_boolean', 'fan'].includes(item.domain)).length;
  }

  function controlDevices() {
    return entities.filter(item => ['light', 'switch', 'input_boolean', 'fan']
      .includes(item.domain)).length;
  }

  function cardStatus(item) {
    if (!available(item)) return item.state === 'unavailable' ? '离线' : '未知';
    if (item.state === 'on') return item.brightness === null ? '开启' : item.brightness + '%';
    if (item.state === 'off') return '关闭';
    return fit(stateText(item), 80, 14);
  }

  function fit(value, width, font) {
    const text = clean(value, 160);
    let used = 0, output = '';
    for (const char of Array.from(text)) {
      const step = char.charCodeAt(0) > 127 ? font : font * 0.66;
      if (used + step > width - font) return output + '…';
      used += step;
      output += char;
    }
    return output;
  }

  function hide(id, hidden) {
    if (typeof id === 'object') id.hidden = hidden;
    else ui.setHidden(id, hidden);
  }

  function buttonColor(button, color) { button.color = color; }
  function buttonText(button, text) { button.text = text; }

  function createButton(text, x, y, w, h, action, color, style) {
    const button = { text, x, y, w, h, action, color, style: style || styles.control,
      hidden: false, section: 'common' };
    buttons.push(button);
    return button;
  }

  function initializeButtonPool() {
    for (let i = 0; i < BUTTON_SLOTS; i++) {
      const slot = { id: 0, target: null };
      slot.id = ui.button('', 0, 0, 1, 1, () => {
        const target = slot.target;
        if (!disposed && target && !target.hidden) target.action();
      }, colors.surface);
      ui.setHidden(slot.id, true);
      buttonPool.push(slot);
    }
  }

  function renderButtons() {
    const view = settingsOpen ? 'settings' : detailOpen ? 'detail' : 'list';
    const active = buttons.filter(button => button.section === 'common' ||
      button.section === view || (button.section === 'main' && view === 'list') ||
      (button.section === 'navigation' && view !== 'settings'));
    if (active.length > BUTTON_SLOTS) throw new Error('Button pool capacity exceeded');
    // Hidden controls retain their slots until an explicit page navigation.
    buttonPool.forEach(slot => {
      if (slot.target && !active.includes(slot.target)) {
        slot.target = null;
        ui.setHidden(slot.id, true);
      }
    });
    active.forEach(button => {
      let slot = buttonPool.find(entry => entry.target === button);
      if (!slot) {
        slot = buttonPool.find(entry => !entry.target);
        slot.target = button;
      }
      ui.setPos(slot.id, button.x, button.y);
      ui.setSize(slot.id, button.w, button.h);
      ui.setText(slot.id, button.text);
      ui.setColor(slot.id, button.color);
      ui.setStyle(slot.id, button.style);
      ui.setHidden(slot.id, button.hidden);
    });
  }
  function label(text, x, y, w, h, font, color, icon) {
    const id = ui.text(text, x, y, font, color, icon ? 1 : 0);
    ui.setSize(id, w, h);
    return id;
  }

  function panel(x, y, w, h, color, radius) {
    const id = ui.panel(x, y, w, h, color);
    ui.setStyle(id, { radius: radius || 18, borderWidth: 0 });
    return id;
  }

  function iconButton(symbol, x, y, action) {
    const button = createButton('', x, y, 40, 36, action, colors.surface, styles.icon);
    const icon = label(symbol, x + 8, y + 5, 26, 26, 24, colors.accent, true);
    return { button, icon };
  }

  function placeIconButton(pair, x, y) {
    pair.button.x = x;
    pair.button.y = y;
    ui.setPos(pair.icon, x + 8, y + 5);
  }

  function pairHidden(pair, value) {
    hide(pair.button, value);
    hide(pair.icon, value);
  }

  function buildUi() {
    ui.background(colors.background);
    const pad = 12;
    const compact = W < 480;
    const tabColumns = compact ? 2 : 4;
    const tabWidth = Math.floor((W - pad * 2 - 6 * (tabColumns - 1)) / tabColumns);
    const tabY = compact ? 88 : 102;
    const gridY = compact ? 168 : 150;
    const columns = W >= 800 ? 3 : W >= 560 ? 2 : 1;
    const footerY = H - 54;
    const rowCount = Math.max(1, Math.min(2,
      Math.floor((footerY - gridY + 10) / 88)));
    const cardWidth = Math.floor((W - pad * 2 - (columns - 1) * 10) / columns);
    const cardHeight = Math.max(72, Math.floor((footerY - gridY -
      (rowCount - 1) * 10) / rowCount));
    const settingsY = compact ? 92 : 96;
    const settingsStep = compact ? 40 : 44;
    const out = { cards: [], tabs: [], settings: [], pageSize: columns * rowCount,
      compact, footerY, gridY };
    // Panels are allocated before interactive controls so the controls remain
    // above them in the LVGL draw order.
    out.heroPanel = panel(pad, 8, W - pad * 2, 72, colors.hero, 20);
    out.summaryPanel = panel(pad, compact ? 140 : 112, W - pad * 2, 24,
      colors.surface, 12);
    out.detailPanel = panel(pad, gridY - 6, W - pad * 2, H - gridY - 64,
      colors.detail, 18);
    out.setupPanel = panel(pad, settingsY - 8, W - pad * 2,
      130 + settingsStep * 3, colors.detail, 18);
    initializeButtonPool();
    out.kicker = label('MIJIA HOME', pad + 16, 16, W - 172, 18, 13, colors.heroMuted);
    out.title = label('我的家', pad + 16, 34, W - 172, 30, 24, colors.text);
    out.status = label('', pad + 16, 60, W - 178, 18, 14, colors.heroMuted);
    out.summary = label('', pad, compact ? 144 : 120, W - pad * 2, 22, 15, colors.muted);
    out.search = iconButton(glyph.search, W - 142, 16, promptSearch);
    out.refresh = iconButton(glyph.refresh, W - 96, 16, refresh);
    out.settingsButton = iconButton(glyph.settings, W - 50, 16, () => {
      settingsOpen = !settingsOpen;
      render();
    });
    [['all', '全屋'], ['favorites', '常用'], ['controls', '设备'], ['sensors', '环境']]
      .forEach((tab, index) => {
        const button = createButton(tab[1], pad + index % tabColumns * (tabWidth + 6),
          tabY + Math.floor(index / tabColumns) * 38, tabWidth, 32, () => {
            filter = tab[0]; page = 0; selectedId = ''; detailOpen = false; settingsOpen = false; render();
          }, colors.surface, styles.tab);
        button.section = 'main';
        out.tabs.push({ button, key: tab[0] });
      });
    for (let i = 0; i < out.pageSize; i++) {
      const x = pad + i % columns * (cardWidth + 10);
      const y = gridY + Math.floor(i / columns) * (cardHeight + 10);
      const button = createButton('', x, y, cardWidth, cardHeight, () => {
        const item = visibleEntities()[page * out.pageSize + i];
        if (!item) return;
        selectedId = item.entity_id;
        detailOpen = true;
        render();
      }, colors.surface, styles.card);
      button.section = 'list';
      out.cards.push({
        button,
        icon: label('', x + 16, y + 15, 30, 30, 25, colors.accent, true),
        name: label('', x + 56, y + 12, cardWidth - 132, 24, 18, colors.text),
        state: label('', x + 56, y + 40, cardWidth - 132, 22, 15, colors.muted),
        badge: label('', x + cardWidth - 74, y + 18, 62, 24, 14, colors.muted),
        textWidth: cardWidth - 132
      });
    }
    out.empty = label('', pad, gridY + 28, W - pad * 2, 54, 18, colors.muted);
    out.entityIcon = label('', pad + 8, gridY + 10, 34, 34, 28, colors.accent, true);
    out.entityName = label('', pad + 56, gridY + 4, W - 92, 28, 22, colors.text);
    out.entityId = label('', pad + 56, gridY + 36, W - 92, 24, 15, colors.muted);
    out.detail = label('', pad + 56, gridY + 72, W - 80, 28, 18, colors.text);
    out.previous = iconButton(glyph.previous, pad, footerY, () => {
      if (detailOpen) { selectedId = ''; detailOpen = false; }
      else if (page > 0) page--;
      render();
    });
    out.next = iconButton(glyph.next, W - 52, footerY, () => {
      if ((page + 1) * out.pageSize < visibleEntities().length) page++;
      render();
    });
    out.pages = label('', 62, footerY + 7, W - 124, 24, 15, colors.muted);
    out.previous.button.section = 'navigation';
    out.next.button.section = 'list';
    out.favorite = iconButton(glyph.favorite, W - 52, gridY + 4, toggleFavorite);
    out.on = createButton('开启', pad, H - 46, 76, 36, () => control('turn_on'), colors.selected, styles.control);
    out.off = createButton('关闭', pad + 84, H - 46, 76, 36, () => control('turn_off'), colors.surface, styles.control);
    out.minus = createButton('-', W - 144, H - 46, 36, 36, () => changeBrightness(-10), colors.surface, styles.icon);
    out.level = label('', W - 103, H - 40, 54, 24, 16, colors.text);
    out.plus = createButton('+', W - 48, H - 46, 36, 36, () => changeBrightness(10), colors.surface, styles.icon);
    [out.favorite.button, out.on, out.off, out.minus, out.plus].forEach(button => {
      button.section = 'detail';
    });
    const settingsWidth = Math.floor((W - 34) / 2);
    out.url = label('', pad, settingsY, W - 24, 24, 16, colors.text);
    out.secret = label('', pad, settingsY + 28, W - 24, 24, 16, colors.muted);
    out.transport = label('HTTP · 未加密', pad, settingsY + 56, W - 24, 24, 16, colors.warning);
    const definitions = [
      ['服务端', promptUrl], ['访问令牌', promptToken],
      ['允许 HTTP', () => {
        if (httpAllowed) {
          invalidate();
          httpAllowed = false;
        } else httpAllowed = true;
        message = httpAllowed ? '已允许本次局域网明文连接' : '已撤销 HTTP 授权';
        render();
      }],
      ['全部实体', () => {
        if (pending || nativeBusy || queued) { message = '请求结束后才能切换范围'; render(); return; }
        scope = scope === 'all' ? 'favorites' : 'all';
        save('eh_scope', scope);
        selectedId = '';
        if (connected) { beginStates(); startQueued(); }
        render();
      }],
      ['添加关注', addFavorite], ['连接', connect], ['断开', disconnect]
    ];
    definitions.forEach((entry, index) => {
      out.settings.push(createButton(entry[0], pad + index % 2 * (settingsWidth + 10),
        settingsY + 88 + Math.floor(index / 2) * settingsStep,
        settingsWidth, 34, entry[1], index === 5 ? colors.selected : colors.surface, styles.setting));
    });
    out.settings.forEach(button => { button.section = 'settings'; });
    return out;
  }

  function render() {
    if (!widgets || disposed) return;
    const item = current();
    ui.setText(widgets.kicker, settingsOpen ? 'HOME ASSISTANT' :
      detailOpen ? 'DEVICE CONTROL' : 'MIJIA HOME');
    ui.setText(widgets.title, settingsOpen ?
      (nativeManaged ? '家庭设置' : '连接我的家') : '我的家');
    ui.setText(widgets.status, fit(message, W - 178, 14));
    ui.setColor(widgets.status, phase === 'error' ? colors.warning : colors.heroMuted);
    const filtered = visibleEntities();
    page = Math.max(0, Math.min(page, Math.max(0, Math.ceil(filtered.length / widgets.pageSize) - 1)));
    if (!settingsOpen && !filtered.slice(page * widgets.pageSize,
        (page + 1) * widgets.pageSize).some(item => item.entity_id === selectedId)) {
      selectedId = '';
    }
    widgets.tabs.forEach(tab => {
      hide(tab.button, settingsOpen || detailOpen);
      buttonColor(tab.button, tab.key === filter ? colors.selected : colors.surface);
    });
    pairHidden(widgets.search, settingsOpen || detailOpen);
    pairHidden(widgets.refresh, settingsOpen || detailOpen);
    pairHidden(widgets.settingsButton, settingsOpen);
    hide(widgets.summaryPanel, settingsOpen || detailOpen);
    hide(widgets.detailPanel, settingsOpen || !detailOpen || !item);
    hide(widgets.setupPanel, !settingsOpen);
    hide(widgets.summary, settingsOpen || detailOpen);
    ui.setText(widgets.summary, connected ?
      controlDevices() + ' 个设备 · ' + activeDevices() + ' 个运行中 · ' +
        (lastCached ? '本地缓存' : '实时状态') :
      nativeManaged ? '正在读取家庭设备' : '连接后集中查看和控制家庭设备');
    widgets.cards.forEach((card, index) => {
      const item = filtered[page * widgets.pageSize + index];
      [card.button, card.icon, card.name, card.state, card.badge].forEach(id =>
        hide(id, settingsOpen || detailOpen || !item));
      if (!item) return;
      buttonColor(card.button, available(item) && item.state === 'on' ? colors.active :
        ['sensor', 'binary_sensor'].includes(item.domain) ? colors.sensor : colors.surface);
      ui.setText(card.icon, glyph[item.domain] || glyph.home);
      ui.setText(card.name, fit(item.name, card.textWidth, 18));
      ui.setText(card.state, fit(stateText(item), card.textWidth, 16));
      ui.setText(card.badge, cardStatus(item));
      ui.setColor(card.icon, !available(item) ? colors.muted :
        item.state === 'on' ? colors.accent : colors.muted);
      ui.setColor(card.state, !available(item) ? colors.muted :
        item.state === 'on' ? colors.accent : colors.muted);
      ui.setColor(card.badge, !available(item) ? colors.muted :
        item.state === 'on' ? colors.accent : colors.muted);
    });
    hide(widgets.empty, settingsOpen || (detailOpen ? !!current() : filtered.length > 0));
    ui.setText(widgets.empty, detailOpen ? '实体已不在当前列表' : search ? '没有匹配的实体' :
      filter === 'favorites' ? '没有关注实体' :
      connected ? '没有实体' : '尚未同步实体');
    pairHidden(widgets.previous, settingsOpen || (!detailOpen && page === 0));
    pairHidden(widgets.next, settingsOpen || detailOpen ||
      (page + 1) * widgets.pageSize >= filtered.length);
    hide(widgets.pages, settingsOpen || detailOpen);
    ui.setText(widgets.pages, detailOpen ? '设备详情' : fit((page + 1) + ' / ' +
      Math.max(1, Math.ceil(filtered.length / widgets.pageSize)) + '  ·  ' +
      filtered.length + ' 个实体' + (search ? '  ·  搜索中' : ''), W - 124, 16));
    placeIconButton(widgets.previous, 12, detailOpen ? H - 96 : widgets.footerY);
    [widgets.entityIcon, widgets.entityName, widgets.entityId]
      .forEach(id => hide(id, settingsOpen || !item));
    hide(widgets.detail, settingsOpen || !item);
    pairHidden(widgets.favorite, settingsOpen || !item);
    if (item) {
      ui.setText(widgets.entityIcon, glyph[item.domain] || glyph.home);
      ui.setColor(widgets.entityIcon, available(item) && item.state === 'on' ?
        colors.accent : colors.muted);
      ui.setText(widgets.entityName, fit(item.name, W - 92, 22));
      ui.setText(widgets.entityId, fit(item.entity_id, W - 92, 15));
      ui.setText(widgets.detail, fit('当前状态 · ' + stateText(item), W - 80, 18));
      ui.setText(widgets.favorite.icon, favorites.includes(item.entity_id) ? glyph.followed : glyph.favorite);
      ui.setColor(widgets.favorite.icon, favorites.includes(item.entity_id) ? colors.warning : colors.muted);
    }
    const usable = !settingsOpen && connected && available(item) &&
      !pending && !queued && !nativeBusy;
    hide(widgets.on, !usable || !supports(item, 'turn_on'));
    hide(widgets.off, !usable || !supports(item, 'turn_off'));
    const dimmer = usable && item.dimmable &&
      item.brightness !== null && supports(item, 'turn_on');
    [widgets.minus, widgets.level, widgets.plus].forEach(id => hide(id, !dimmer));
    if (dimmer) ui.setText(widgets.level, item.brightness + '%');
    [widgets.url, widgets.secret, widgets.transport].forEach(id => hide(id, !settingsOpen));
    widgets.settings.forEach(id => hide(id, !settingsOpen));
    ui.setText(widgets.url, fit('家庭中枢 · ' + url, W - 24, 16));
    ui.setText(widgets.secret, token ? '令牌：已输入（仅本次会话）' :
      nativeManaged ? '令牌：由本地服务管理' : '令牌：未输入');
    ui.setText(widgets.transport, httpAllowed || nativeManaged ?
      '本地网络 · HTTP 明文连接' : '连接前需要确认本地 HTTP 风险');
    buttonText(widgets.settings[2], httpAllowed ? 'HTTP 已允许' : '允许 HTTP');
    buttonText(widgets.settings[3], scope === 'favorites' ? '仅关注' : '全部实体');
    buttonText(widgets.settings[5], connected ? '重新同步' : '连接家庭');
    buttonColor(widgets.settings[5], colors.selected);
    renderButtons();
  }

  function snapshot() {
    return {
      phase, message, url, connected, hasToken: !!token, httpAllowed, scope,
      backend: sharedReady ? 'native-service' : nativeService ? 'unsupported-service' : 'legacy',
      nativeManaged,
      favorites: favorites.slice(), filter, search, page, settingsOpen, selectedId,
      limited, serviceDiscoveryLimited, lastSync, lastCached, nextRefresh,
      nativeBusy, disposed,
      view: settingsOpen ? 'settings' : detailOpen ? 'detail' : 'list',
      pending: pending ? { kind: pending.kind, entityId: pending.entityId,
        abandoned: !!pending.abandoned } : null,
      entities: entities.map(item => Object.assign({}, item))
    };
  }

  if (W < 320 || H < 360) {
    ui.background(colors.background);
    label('米家 HA', 12, 12, Math.max(1, W - 24), 28, 20, colors.text);
    label('显示区域至少需要 320 × 360', 12, 56, Math.max(1, W - 24), 64, 16, colors.muted);
    return;
  }
  widgets = buildUi();
  if (!bridgeReady) message = '固件未提供 Home Assistant 接口';
  render();
  if (nativeManaged && bridgeReady) connect();
  ui.onSwipe(direction => {
    if (disposed || settingsOpen) return;
    if (detailOpen) {
      if (direction === 'left' || direction === 'right') {
        selectedId = '';
        detailOpen = false;
        render();
      }
      return;
    }
    if (direction === 'left' && (page + 1) * widgets.pageSize < visibleEntities().length) page++;
    else if (direction === 'right' && page > 0) page--;
    render();
  });
  timer = setInterval(tick, 250);
  globalThis.ESPHomeHA = {
    refresh, disconnect,
    settings: () => { settingsOpen = true; render(); },
    snapshot,
    dispose: () => {
      disconnect();
      disposed = true;
      buttonPool.forEach(slot => {
        slot.target = null;
        ui.setHidden(slot.id, true);
      });
      clearInterval(timer);
      if (bridge && typeof bridge.stop === 'function') bridge.stop();
    }
  };
})();
