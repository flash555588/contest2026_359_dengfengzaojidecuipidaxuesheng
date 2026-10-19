/* SPDX-License-Identifier: MIT */
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const J={};const root=path.join(__dirname,'../upstream/src');
for(const name of ['04_styles.js','11p_horror3.js','11p_styles.js']){
 let source=fs.readFileSync(path.join(root,name),'utf8');
 if(name==='11p_horror3.js')source='(() => {\n'+source.slice(source.indexOf('const LAY ='));
 vm.runInNewContext(source,{J});
}
const names=['黑白铬影','赤红讯号','警示黄','流行洋红','纸上诗','暗色仪表','薄荷终端','字体标本','城市交通','蓝图','胭脂渐变','单色像差','废墟','深夜录像','旧信','樱花','深海','落日','森林手记','蒸汽波','报纸','合成器','牛皮纸','糖果','酸性绿','水墨','香槟金'];
const layouts=['center','vcols','huge','diag','circle','stack','gloss','mixed','pill','wave','labels','ring','type','marquee','condensed','tile'];
const brightness=s=>{const x=parseInt(s.bg.slice(1),16);return ((x>>16)*299+((x>>8)&255)*587+(x&255)*114)/1000;};
const colorSet=s=>({background:s.bg,foreground:s.fg,secondary:s.sub||s.fg,accent:s.accent||s.fg});
const styles=J.STYLE_ORDER.map((id,i)=>{
 const s=J.STYLES[id],schemes=[...s.schemes].sort((a,b)=>brightness(a)-brightness(b));
 return {id,name:names[i]||id,light:colorSet(schemes[schemes.length-1]),dark:colorSet(schemes[0]),
   mood:i%4===0?'graphic':i%4===1?'emotional':i%4===2?'pop':'editorial',
   layouts:Array.from({length:4},(_,j)=>layouts[(i*3+j*5)%layouts.length])};
});
if(styles.length!==27)throw Error('Pinned style order changed');
console.log(JSON.stringify({version:1,styles},null,2));
