/* SPDX-License-Identifier: MIT
 * Equivalent connected components for JIZURA's pinned 02_fonts.js algorithm.
 * Keep this independent of Canvas/DOM so the same kernel can run in QuickJS.
 */
(function (root) {
  'use strict';
  function segment(rgba, width, height, resolution) {
    const n = width * height;
    if (!Number.isSafeInteger(n) || n < 1 || rgba.length !== n * 4) throw Error('Invalid glyph dimensions');
    let labels = new Uint16Array(n), parent = new Uint32Array(256), count = 0;
    let peak = labels.byteLength + parent.byteLength;
    const allocated = extra => { peak = Math.max(peak, labels.byteLength + parent.byteLength + extra); };
    const find = p => {
      let r = p;
      while (parent[r] !== r) r = parent[r];
      while (p !== r) { const next = parent[p]; parent[p] = r; p = next; }
      return r;
    };
    const join = (a, b) => {
      a = find(a); b = find(b);
      if (a > b) { const t = a; a = b; b = t; }
      parent[b] = a; return a;
    };
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const p = y * width + x;
        if (rgba[p * 4 + 3] < 60) continue;
        let lab = x ? labels[p - 1] : 0;
        if (y) {
          for (let dx = -1; dx <= 1; dx++) {
            if (x + dx < 0 || x + dx >= width) continue;
            const other = labels[p - width + dx];
            if (other && other !== lab) lab = lab ? join(lab, other) : other;
          }
        }
        if (!lab) {
          lab = ++count;
          if (count >= parent.length) {
            const next = new Uint32Array(parent.length * 2);
            allocated(next.byteLength); next.set(parent); parent = next;
          }
          if (count > 65535 && labels.BYTES_PER_ELEMENT === 2) {
            const next = new Uint32Array(n);
            allocated(next.byteLength); next.set(labels); labels = next;
          }
          parent[lab] = lab;
        }
        labels[p] = lab;
      }
    }
    // Roots increase with the first foreground pixel, exactly the original DFS
    // component order. Reuse parent for the provisional -> canonical mapping.
    let nl = 0;
    for (let l = 1; l <= count; l++) {
      const p = parent[l];
      parent[l] = p === l ? ++nl : parent[p];
    }
    const boxes = [];
    for (let y = 0; y < height; y++) {
      for (let x = 0, p = y * width; x < width; x++, p++) {
        if (!labels[p]) continue;
        const lab = labels[p] = parent[labels[p]];
        let b = boxes[lab];
        if (!b) boxes[lab] = b = { x0: x, x1: x, y0: y, y1: y, area: 0 };
        b.area++;
        if (x < b.x0) b.x0 = x;
        if (x > b.x1) b.x1 = x;
        if (y < b.y0) b.y0 = y;
        if (y > b.y1) b.y1 = y;
      }
    }
    // Three original rows give simultaneous dilation without an image-sized copy.
    const rows = [new labels.constructor(width), new labels.constructor(width), new labels.constructor(width)];
    allocated(rows.reduce((s, a) => s + a.byteLength, 0));
    for (let pass = 0; pass < 2; pass++) {
      let prev = rows[0], cur = rows[1], next = rows[2];
      prev.fill(0); cur.set(labels.subarray(0, width)); next.fill(0);
      if (height > 1) next.set(labels.subarray(width, width * 2));
      for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
          const p = y * width + x;
          if (cur[x] || !rgba[p * 4 + 3]) continue;
          const lab = (x ? cur[x - 1] : 0) || (x + 1 < width ? cur[x + 1] : 0) || prev[x] || next[x];
          if (!lab) continue;
          labels[p] = lab;
          const b = boxes[lab];
          if (x < b.x0) b.x0 = x;
          if (x > b.x1) b.x1 = x;
          if (y < b.y0) b.y0 = y;
          if (y > b.y1) b.y1 = y;
        }
        const old = prev; prev = cur; cur = next; next = old;
        if (y + 2 < height) next.set(labels.subarray((y + 2) * width, (y + 3) * width));
        else next.fill(0);
      }
    }
    // Preserve the original ordered, single-level speck remap, including ties.
    const remap = new Uint32Array(nl + 1), minA = resolution * resolution * 0.0012;
    for (let l = 1; l <= nl; l++) remap[l] = l;
    for (let l = 1; l <= nl; l++) {
      const b = boxes[l]; if (b.area >= minA) continue;
      let best = 0, bd = 1e9;
      const cx = (b.x0 + b.x1) / 2, cy = (b.y0 + b.y1) / 2;
      for (let m = 1; m <= nl; m++) {
        if (m === l || boxes[m].area < minA) continue;
        const o = boxes[m], dx = Math.max(o.x0 - cx, 0, cx - o.x1), dy = Math.max(o.y0 - cy, 0, cy - o.y1);
        const d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = m; }
      }
      if (best) {
        remap[l] = best;
        const o = boxes[best];
        o.x0 = Math.min(o.x0, b.x0); o.y0 = Math.min(o.y0, b.y0);
        o.x1 = Math.max(o.x1, b.x1); o.y1 = Math.max(o.y1, b.y1);
      }
    }
    allocated(rows.reduce((s, a) => s + a.byteLength, 0) + remap.byteLength);
    return { labels, boxes, remap, nl, stats: { scratchBytes: peak, labelBytes: labels.byteLength, provisional: count, components: nl } };
  }
  const api = { segment };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.JizuraGlyphKernel = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
