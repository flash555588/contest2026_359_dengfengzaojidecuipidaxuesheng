/* SPDX-License-Identifier: MIT */
/* Lossless A8/RLE storage and one byte-limited LRU for white + tinted canvases.
 * Piece identities live outside the raster cache, preserving shatter seeds.
 */
(function (root) {
  'use strict';
  class Sprites {
    constructor(budget = 4 * 1024 * 1024) {
      this.budget = budget; this.cache = new Map(); this.bytes = 0; this.next = 0;
      this.colors = new WeakMap(); this.nextColor = 0;
      this.stats = { hits: 0, misses: 0, evictions: 0, peakRasterBytes: 0, masks: 0, maskBytesCreated: 0, rawMaskBytesCreated: 0 };
    }
    clear() {
      // drawImage captures the source pixels synchronously; resetting an evicted
      // canvas releases its backing storage, not the pixels already drawn.
      for (const c of this.cache.values()) c.width = c.height = 1;
      this.cache.clear(); this.bytes = 0;
    }
    get(key) {
      const c = this.cache.get(key);
      if (!c) { this.stats.misses++; return null; }
      this.cache.delete(key); this.cache.set(key, c); this.stats.hits++; return c;
    }
    put(key, c) {
      const bytes = c.width * c.height * 4;
      // A single oversized sprite is used transiently, without retaining it.
      if (bytes > this.budget) return c;
      while (this.bytes + bytes > this.budget && this.cache.size) {
        const k = this.cache.keys().next().value, old = this.cache.get(k);
        this.bytes -= old.width * old.height * 4; this.cache.delete(k);
        old.width = old.height = 1; this.stats.evictions++;
      }
      this.cache.set(key, c); this.bytes += bytes;
      this.stats.peakRasterBytes = Math.max(this.stats.peakRasterBytes, this.bytes);
      return c;
    }
    registerMask(alpha, width, height) {
      let runs = 0;
      for (let i = 0; i < alpha.length;) {
        const a = alpha[i]; let end = i + 1;
        while (end < alpha.length && end - i < 65535 && alpha[end] === a) end++;
        runs++; i = end;
      }
      let data = alpha, rle = false;
      if (runs * 3 < alpha.length) {
        data = new Uint8Array(runs * 3); rle = true;
        for (let i = 0, o = 0; i < alpha.length;) {
          const a = alpha[i]; let end = i + 1;
          while (end < alpha.length && end - i < 65535 && alpha[end] === a) end++;
          const n = end - i; data[o++] = n & 255; data[o++] = n >>> 8; data[o++] = a; i = end;
        }
      }
      const id = ++this.next, self = this;
      const mask = { id, width, height, data, rle, get() { return self.white(mask); } };
      this.stats.masks++; this.stats.maskBytesCreated += data.byteLength;
      this.stats.rawMaskBytesCreated += alpha.byteLength;
      return mask;
    }
    white(mask) {
      const key = 'w' + mask.id, hit = this.get(key);
      if (hit) return hit;
      const c = document.createElement('canvas'); c.width = mask.width; c.height = mask.height;
      const x = c.getContext('2d'), image = x.createImageData(c.width, c.height), d = image.data;
      const write = (i, a) => { if (a) { const o = i * 4; d[o] = d[o + 1] = d[o + 2] = 255; d[o + 3] = a; } };
      if (mask.rle) {
        for (let o = 0, i = 0; o < mask.data.length; o += 3) {
          const n = mask.data[o] | mask.data[o + 1] << 8, a = mask.data[o + 2];
          for (let end = i + n; i < end; i++) write(i, a);
        }
      } else for (let i = 0; i < mask.data.length; i++) write(i, mask.data[i]);
      x.putImageData(image, 0, 0); return this.put(key, c);
    }
    tint(piece, color) {
      if (color === '#ffffff' || color === '#fff') return piece.cv;
      let colorKey = color;
      if (color && typeof color === 'object') {
        if (!this.colors.has(color)) this.colors.set(color, ++this.nextColor);
        colorKey = '@' + this.colors.get(color);
      }
      const key = 't' + piece.id + ':' + colorKey, hit = this.get(key);
      if (hit) return hit;
      const source = piece.cv, c = document.createElement('canvas');
      c.width = source.width; c.height = source.height;
      const x = c.getContext('2d'); x.drawImage(source, 0, 0);
      x.globalCompositeOperation = 'source-in'; x.fillStyle = color; x.fillRect(0, 0, c.width, c.height);
      return this.put(key, c);
    }
  }
  root.JizuraPortSprites = Sprites;
})(typeof globalThis !== 'undefined' ? globalThis : this);
