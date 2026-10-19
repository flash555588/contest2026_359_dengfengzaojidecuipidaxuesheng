/* SPDX-License-Identifier: MIT */
/* Reuse texture handles and read-only typesetting factors, never frame pixels. */
(function (root) {
  'use strict';
  class Lines {
    constructor(maxEntries=128,maxChars=4096) {
      this.maxEntries=maxEntries;this.maxChars=maxChars;this.map=new Map();this.chars=0;
      this.stats={hits:0,misses:0,evictions:0};
    }
    get(line, enabled, typeset) {
      const key=(enabled?'1':'0')+line,old=this.map.get(key);
      if(old){this.map.delete(key);this.map.set(key,old);this.stats.hits++;return old;}
      this.stats.misses++;
      const arr=[...line],value={arr,ts:typeset(arr)};
      // Long generated labels do not displace the small lyric working set.
      if(arr.length>256||arr.length>this.maxChars||this.maxEntries<=0)return value;
      while(this.map.size&&(this.map.size>=this.maxEntries||this.chars+arr.length>this.maxChars)){
        const k=this.map.keys().next().value;this.chars-=this.map.get(k).arr.length;this.map.delete(k);this.stats.evictions++;
      }
      this.map.set(key,value);this.chars+=arr.length;return value;
    }
  }
  function pattern(renderer, ctx, image) {
    if (!renderer.portPatterns) renderer.portPatterns = new WeakMap();
    let images = renderer.portPatterns.get(ctx);
    if (!images) { images = new WeakMap(); renderer.portPatterns.set(ctx, images); }
    let entry = images.get(image);
    if (!entry || entry.width !== image.width || entry.height !== image.height) {
      entry = { width: image.width, height: image.height, pattern: ctx.createPattern(image, 'repeat') };
      images.set(image, entry);
    }
    return entry.pattern;
  }
  const api = { Lines, pattern };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.JizuraCanvasHot = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
