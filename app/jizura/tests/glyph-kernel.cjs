/* SPDX-License-Identifier: MIT */
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const {performance} = require('node:perf_hooks');
const root = path.resolve(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'upstream/src/02_fonts.js'), 'utf8');
const begin = source.indexOf('  const N = S * S, A = new Uint8Array(N);');
const end = source.indexOf('  const pieces = [];', begin);
assert(begin > 0 && end > begin);
// Oracle is the exact pinned algorithm, not a second hand-written reference.
const oracle = new Function('img', 'S', 'res', source.slice(begin, end) + '\nreturn {labels:L,boxes,remap,nl};');
const {segment} = require('../port/glyph-kernel.js');
let seed = 852, cases = 0, large;
const random = () => { seed = (Math.imul(seed,1664525)+1013904223) >>> 0; return seed / 4294967296; };
function check(side, res, make) {
  const rgba = new Uint8Array(side * side * 4);
  for (let y=0;y<side;y++) for(let x=0;x<side;x++) rgba[(y*side+x)*4+3] = make(x,y);
  const ref = oracle(rgba,side,res), got = segment(rgba,side,side,res);
  assert.equal(got.nl,ref.nl); assert.deepEqual(got.boxes,ref.boxes);
  assert.deepEqual(Array.from(got.labels),Array.from(ref.labels));
  assert.deepEqual(Array.from(got.remap),Array.from(ref.remap));
  cases++; return got;
}
for (const side of [1,2,3,8,31,64]) {
  check(side,side/1.45,()=>0); check(side,side/1.45,()=>255);
  check(side,side/1.45,(x,y)=>x===y?60:59);
  check(side,side/1.45,(x,y)=>(x===0||y===side-1)?255:1);
}
for (let n=0;n<240;n++) {
  const side=1+(random()*72|0), values=[0,1,40,59,60,61,127,254,255];
  check(side,side/1.45,()=>values[random()*values.length|0]);
}
// More than 65,535 components must promote labels instead of corrupting them.
large=check(514,0,(x,y)=>(x%2===0&&y%2===0)?255:0);
assert.equal(large.labels.BYTES_PER_ELEMENT,4); assert.equal(large.nl,66049);
const side=743, rgba=new Uint8Array(side*side*4);
for(let y=0;y<side;y++)for(let x=0;x<side;x++)rgba[(y*side+x)*4+3]=((x%97<23&&y>30&&y<710)||(y%131<22&&x>60&&x<690))?255:0;
function bench(fn) { const samples=[];for(let i=0;i<9;i++){const t=performance.now();fn();samples.push(performance.now()-t)}samples.sort((a,b)=>a-b);return samples[4]; }
const refMs=bench(()=>oracle(rgba,side,512)), optMs=bench(()=>segment(rgba,side,side,512));
const got=segment(rgba,side,side,512);
const report={pass:true,cases,oracle:'exact pinned 02_fonts.js segmentation block',host_only:true,
  benchmark:{kind:'synthetic connected-stroke mask',side,reference_median_ms:refMs,optimized_median_ms:optMs},
  buffers:{reference_array_bytes:side*side*13,optimized_scratch_bytes:got.stats.scratchBytes,
    note:'Includes segmentation arrays; excludes existing RGBA input/canvas, JS objects and output sprites.'}};
console.log(JSON.stringify(report,null,2));
