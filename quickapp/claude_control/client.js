'use strict';
// Pure request state machine. Used by the native QuickJS page and Node tests.
class CliController {
  constructor(api, cfg, persist, options) {
    this.api = api; this.cfg = cfg; this.persist = persist;
    const opts = options || {};
    this.now = opts.now || (() => Date.now());
    this.changed = opts.changed || (() => {}); this.message = opts.message || (() => {});
    this.validUrl = opts.validUrl || CliController.validUrl;
    this.flight = null; this.queued = null; this.connected = false; this.nextPoll = 0;
    this.authRequired = false;
    this.failures = 0;
    this.recovered = opts.recovered || (() => {});
  }
  get pending() { return this.cfg.pending || null; }
  get idle() { return !this.flight && !this.queued; }
  nextId() { return String(this.now()) + '-' + Math.random().toString(36).slice(2); }
  static validUrl(value) {
    const match = /^http:\/\/(\d+)\.(\d+)\.(\d+)\.(\d+):(\d+)$/.exec(value || '');
    if (!match) return false;
    const n = match.slice(1).map(Number);
    if (n.some((x,i) => !Number.isInteger(x) || x < (i === 4 ? 1 : 0) || x > (i === 4 ? 65535 : 255))) return false;
    if (value !== 'http://' + n.slice(0,4).join('.') + ':' + n[4]) return false;
    return n[0] === 10 || n[0] === 192 && n[1] === 168 || n[0] === 172 && n[1] >= 16 && n[1] <= 31;
  }
  send(action, data, done, target) {
    if (!this.api) return false;
    const url = target || this.cfg.url;
    if (!this.validUrl(url)) {this.message('电脑地址无效'); return false;}
    if (this.flight) {
      if (this.flight.action === 'status' && !this.queued && (action !== 'status' || data.id !== this.flight.data.id)) {
        this.queued = {action,data,done,target:url}; this.changed(); return true;
      }
      this.message('上一项操作仍在处理'); return false;
    }
    const mutation = action === 'submit' || action === 'input';
    if (mutation && this.pending && (this.pending.action !== action || this.pending.data.request_id !== data.request_id)) {
      this.message('请先核对上一次操作'); return false;
    }
    if (mutation) {this.cfg.pending = {action,data,url}; this.persist();}
    let id;
    try {id = this.api.request(url,action === 'pair' ? '' : this.cfg.token,action,JSON.stringify(data || {}));}
    catch (_) {id = -1;}
    if (id <= 0) {
      // The native layer did not accept this attempt. A prior uncertain receipt stays.
      if(action !== 'status') this.message('连接暂不可用；任务草稿已保留');
      this.connected=false;this.failures++;
      this.nextPoll = this.now()+Math.min(15000,1000*Math.pow(2,Math.min(4,this.failures-1)));
      this.changed(); return false;
    }
    this.flight = {id,action,data,done,url}; this.changed(); return true;
  }
  reconcile(done) {
    const p = this.pending;
    if (!p) return false;
    if (p.url !== this.cfg.url) {this.message('请连接原电脑后核对操作'); return false;}
    return this.send(p.action,p.data,done,p.url);
  }
  tick(receive) {
    if (!this.api) return;
    if (!this.flight) {
      if (this.cfg.token && !this.authRequired && this.now() >= this.nextPoll) this.send('status',{id:this.cfg.selected},receive);
      return;
    }
    let result;
    try {result = this.api.poll(this.flight.id);} catch (_) {result={done:true,error:-1};}
    if (!result || !result.done) return;
    const sent = this.flight; this.flight = null;
    const body = result.body || {};
    const ok = !result.error && result.status === 200 && body.ok;
    this.connected = ok || !!result.status && result.status < 500 && result.status !== 403;
    const wasAuthRequired=this.authRequired;
    this.authRequired = result.status === 403;
    if (ok) {
      if(this.failures)this.recovered();
      this.failures=0;
      const pending = this.pending;
      const observed = pending && sent.action === 'status' && body.task &&
        (pending.action === 'submit' && body.task.request_id === pending.data.request_id ||
         pending.action === 'input' && body.task.last_input && body.task.last_input.request_id === pending.data.request_id);
      if (this.pending && this.pending.action === sent.action && this.pending.data.request_id === sent.data.request_id) {
        delete this.cfg.pending; this.cfg.draft=''; this.persist();
      }
      if (observed) {delete this.cfg.pending; this.cfg.draft=''; this.persist();}
      if (sent.done && (sent.action !== 'status' || !sent.data.id || sent.data.id === this.cfg.selected)) sent.done(body);
    } else {
      if (this.pending && this.pending.action === sent.action && (body.accepted === false || body.accepted === true)) {
        delete this.cfg.pending; this.persist();
      }
      if(sent.action !== 'status') this.message(body.error || '连接中断；请核对操作，避免重复发送');
      else if(this.authRequired && !wasAuthRequired)this.message('配对已失效，请连接电脑');
      if(!this.connected)this.failures++;
    }
    this.nextPoll = this.now() + (this.connected ? 1500 : Math.min(15000,1000*Math.pow(2,Math.min(4,this.failures-1))));
    const queued = this.queued; this.queued = null;
    this.changed();
    if (queued) this.send(queued.action,queued.data,queued.done,queued.target);
  }
}
globalThis.CliController = CliController;
if (typeof module !== 'undefined') module.exports = CliController;
