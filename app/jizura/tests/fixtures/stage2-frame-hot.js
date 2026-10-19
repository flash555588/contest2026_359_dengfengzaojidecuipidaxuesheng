/* SPDX-License-Identifier: MIT */
/* No pixel buffers: recent-event search and two gradient slots per context. */
(function (root) {
  'use strict';
  function recentStart(events, time) {
    // The original camera loop already requires events ordered by .t (it
    // stops at the first future event). Read current times, without a stale
    // schedule cache. Use exactly its age arithmetic at floating boundaries.
    if (events.length < 32 || !Number.isFinite(time)) return 0;
    let lo = 0, hi = events.length;
    while (lo < hi) {
      const mid = (lo + hi) >>> 1;
      if ((time - events[mid].t) * 24 > 14) lo = mid + 1;
      else hi = mid;
    }
    return lo;
  }
  function activeEvents(events, time, fps) {
    // Keep the entire schedule and original order: a very long event can start
    // arbitrarily far in the past. Avoid Array.filter's interpreted callback.
    const out = [], count = events.length, frameStep = 1 / fps;
    for (let i = 0; i < count; i++) {
      const ev = events[i];
      if (time >= ev.t) {
        const duration = +ev.dur;
        const span = duration !== duration || frameStep !== frameStep ? NaN : duration > frameStep ? duration : frameStep;
        if (time < ev.t + span) out.push(ev);
      }
    }
    return out;
  }
  function slots(renderer, ctx) {
    if (!renderer.portGradients) renderer.portGradients = new WeakMap();
    let entry = renderer.portGradients.get(ctx);
    if (!entry) { entry = {}; renderer.portGradients.set(ctx, entry); }
    return entry;
  }
  function background(renderer, ctx, width, height, scale, lift) {
    const entry = slots(renderer, ctx), old = entry.background;
    if (old && old.width === width && old.height === height && old.scale === scale && old.lift === lift) return old.gradient;
    const gradient = ctx.createRadialGradient(width / 2, height * .45, 0, width / 2, height / 2, Math.hypot(width, height) * .6);
    gradient.addColorStop(0, lift); gradient.addColorStop(1, 'rgba(0,0,0,0)');
    entry.background = { width, height, scale, lift, gradient }; return gradient;
  }
  function vignette(renderer, ctx, width, height, alpha) {
    const entry = slots(renderer, ctx), old = entry.vignette;
    if (old && old.width === width && old.height === height && old.alpha === alpha) return old.gradient;
    const gradient = ctx.createRadialGradient(width / 2, height / 2, Math.min(width, height) * .35, width / 2, height / 2, Math.hypot(width, height) * .62);
    gradient.addColorStop(0, 'rgba(0,0,0,0)'); gradient.addColorStop(1, `rgba(0,0,0,${alpha})`);
    entry.vignette = { width, height, alpha, gradient }; return gradient;
  }
  const api = { recentStart, activeEvents, background, vignette };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.JizuraFrameHot = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
