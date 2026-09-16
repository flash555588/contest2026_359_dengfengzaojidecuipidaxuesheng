from pathlib import Path
src=Path(__file__).resolve().parent.parent/'04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop'
p=src/'portal/app.js'
s=p.read_text(encoding='utf-8')
s=s.replace("entries.push({name,size:n,local});", "entries.push({name,size:n,local,compressed:v.getUint32(off+20,true),crc:v.getUint32(off+16,true)});")
s=s.replace("bytes.subarray(start),{out:output}", "bytes.subarray(start,start+item.compressed),{out:output}")
s=s.replace("if(actual.length!==expected)throw Error('解压大小与目录不符');", "if(actual.length!==expected||crc32(actual)!==item.crc)throw Error('文件解压校验失败，安装包可能已损坏');")
s=s.replace("function zipEntries(bytes)", "function crc32(bytes){let crc=0xffffffff;for(const b of bytes){crc^=b;for(let i=0;i<8;i++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (crc^0xffffffff)>>>0;}\nfunction zipEntries(bytes)")
p.write_text(s,encoding='utf-8')
