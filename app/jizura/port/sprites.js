/* SPDX-License-Identifier: MIT */
/* Lossless A8/RLE storage and one byte-limited LRU for white + tinted canvases.
 * Piece identities live outside the raster cache, preserving shatter seeds.
 */
(function (root) {
  'use strict';
  const littleEndian = new Uint8Array(new Uint16Array([1]).buffer)[0] === 1;
  function fillMask(mask, data) {
    // A view only: no second image buffer. TypedArray.fill runs in the VM's
    // native implementation, avoiding one interpreted call per RLE pixel.
    const words = new Uint32Array(data.buffer, data.byteOffset, data.byteLength / 4);
    if (mask.rle) {
      for (let o = 0, i = 0; o < mask.data.length; o += 3) {
        const n = mask.data[o] | mask.data[o + 1] << 8, a = mask.data[o + 2];
        if (a) words.fill(littleEndian ? (a << 24 | 0xffffff) >>> 0 : (0xffffff00 | a) >>> 0, i, i + n);
        i += n;
      }
    } else {
      for (let i = 0; i < mask.data.length; i++) {
        const a = mask.data[i];
        if (a) words[i] = littleEndian ? (a << 24 | 0xffffff) >>> 0 : (0xffffff00 | a) >>> 0;
      }
    }
  }
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
    raster(mask) {
      const c = document.createElement('canvas'); c.width = mask.width; c.height = mask.height;
      const x = c.getContext('2d'), image = x.createImageData(c.width, c.height), d = image.data;
      fillMask(mask, d);
      x.putImageData(image, 0, 0); return c;
    }
    white(mask) {
      const key = 'w' + mask.id, hit = this.get(key);
      return hit || this.put(key, this.raster(mask));
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
      let c;
      if (piece.portMask) c = this.raster(piece.portMask);
      else {
        const source = piece.cv; c = document.createElement('canvas');
        c.width = source.width; c.height = source.height;
        c.getContext('2d').drawImage(source, 0, 0);
      }
      const x = c.getContext('2d');
      x.globalCompositeOperation = 'source-in'; x.fillStyle = color; x.fillRect(0, 0, c.width, c.height);
      return this.put(key, c);
    }
  }
  Sprites.fillMask = fillMask;
  root.JizuraPortSprites = Sprites;
})(typeof globalThis !== 'undefined' ? globalThis : this);
