// Compare compact on-device lexicon with the pinned, unmodified JS frontend.
import fs from 'node:fs';
import {pathToFileURL} from 'node:url';
import path from 'node:path';
const [cache,prepared,output]=process.argv.slice(2);
const {createZhG2P}=await import(pathToFileURL(path.resolve(cache,'sanoTTS/web/g2p-lazy/zh/zh_g2p.mjs')));
const root=path.join(prepared,'sdcard/speech/sanotts-zh');
const tables=Object.fromEntries(['chars','phrases','phrase_keys'].map(n=>[n,fs.readFileSync(path.join(root,n+'.tsv'),'utf8')]));
const map=JSON.parse(fs.readFileSync(path.join(root,'phonemes.json'),'utf8'));
const g2p=createZhG2P(tables);
const cases=['你好。','你好，欢迎使用智能家居。','打开灯光','关闭窗帘','温度二十六度，湿度百分之六十。','现在是2026年10月15日，16点30分。','今天气温23.5度。','重庆银行行长重新检查重量。','一个，一天，一百；不是，不对，不好。','你好！请打开客厅的灯。','WiFi 已连接，IP 192.168.1.10。','百分之0，百分之100。','价格12元5角，电话123456789。','长大以后，音乐快乐，长长的头发。'];
const corpus=path.join(cache,'sanoTTS/pypkg/tests/data/zh-pinyin-g2pw-reference.jsonl');
if(fs.existsSync(corpus))for(const line of fs.readFileSync(corpus,'utf8').trim().split('\n').slice(0,100))cases.push(JSON.parse(line).text);
const vectors=cases.map(text=>({text,ids:Array.from(g2p.textToIds(text,map).ids)}));
fs.writeFileSync(output,vectors.map(v=>v.text+'\t'+v.ids.join(',')).join('\n')+'\n');
console.log('UPSTREAM_G2P_VECTORS',vectors.length);
