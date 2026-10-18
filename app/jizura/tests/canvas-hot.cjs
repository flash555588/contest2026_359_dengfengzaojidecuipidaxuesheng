/* SPDX-License-Identifier: MIT */
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const hot=require('../port/canvas-hot.js');
const text=fs.readFileSync(__dirname+'/../upstream/src/03_text.js','utf8');
const source=text.slice(0,text.indexOf('/* layout: glyph centres'))+'})();';
const J={isSmallKana:c=>'ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ'.includes(c)};
vm.runInNewContext(source,{J});
let cases=0;
const lines=new hot.Lines(3,32);
for(const enabled of [false,true,false,true]){
 J.setTypeset(enabled);
 for(const line of ['光の中で','はがをにでとのへも','Hello 世界','カタカナ ぁッー','\n','🙂A々','',...Array.from({length:50},(_,i)=>'歌词'+i)]){
  const expected=JSON.stringify({arr:[...line],ts:J.typesetLine([...line])});
  assert.equal(JSON.stringify(lines.get(line,enabled,J.typesetLine)),expected);
  assert.equal(JSON.stringify(lines.get(line,enabled,J.typesetLine)),expected);
  assert(lines.map.size<=3&&lines.chars<=32);cases++;
 }
}
const before=lines.map.size;lines.get('A'.repeat(257),true,J.typesetLine);assert.equal(lines.map.size,before);cases++;
let patterns=0;const ctx={createPattern(){return {id:++patterns};}},r={},img={width:256,height:256};
const a=hot.pattern(r,ctx,img);assert.equal(hot.pattern(r,ctx,img),a);
img.width=128;assert.notEqual(hot.pattern(r,ctx,img),a);
assert.notEqual(hot.pattern(r,{...ctx},img),a);cases+=3;
console.log(JSON.stringify({pass:true,cases,patternCreations:patterns,textCacheStats:lines.stats,entries:lines.map.size,chars:lines.chars},null,2));
