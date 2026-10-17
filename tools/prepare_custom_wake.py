#!/usr/bin/env python3
"""Generate a versioned synthetic Mandarin/English wake-word corpus.

Only phrase text is sent to Edge TTS. No microphone recordings are uploaded.
This corpus is a bootstrap, not a substitute for held-out human recordings.
"""
import argparse,asyncio,hashlib,json,subprocess
from pathlib import Path
import edge_tts

VOICES={
 'train':['zh-CN-XiaoxiaoNeural','zh-CN-YunxiNeural','zh-CN-YunjianNeural','zh-CN-YunyangNeural',
          'zh-TW-HsiaoChenNeural','zh-TW-YunJheNeural'],
 'validation':['zh-CN-XiaoyiNeural','zh-CN-YunxiaNeural'],
 'test':['zh-CN-liaoning-XiaobeiNeural','zh-CN-shaanxi-XiaoniNeural']}
NEGATIVES=[
 '你好','OpenVela','你好 Open','你好 Vela','你好小爱','你好小智','你好朋友',
 '打开 OpenVela','关闭 OpenVela','再见 OpenVela','OpenVela 你好','我喜欢 OpenVela',
 '你好 OpenAI','你好 Opera','你好 OpenValue','你好 OpenWeather','你好贝拉',
 '您好，请问有什么需要帮助的','打开灯光','关闭灯光','打开窗帘','关闭窗帘','打开风扇',
 '关闭风扇','返回桌面','停止聆听','现在几点钟','今天天气怎么样','把声音调小一点',
 '请打开相机','开始录制视频','停止录像','你好，欢迎使用智能家居','我们明天一起出门',
 '这个功能很好用','系统正在更新，请稍候','播放音乐','音量大一点','请打开设备列表',
 '不用了谢谢','我想看一下照片','Hello world','Open the window','Hello Vela',
 '你好，欧拉','你好，维拉','你好，打开这个','Hello Open','Good morning',
 '在这里我们介绍开源系统','我现在正在说话','请不要唤醒设备','欢迎回来','谢谢大家',
 '这里没有任何指令','你好，朋友们大家好','你好，好久不见','Open source operating system']

async def build(output):
    output.mkdir(parents=True,exist_ok=True);rows=[];sem=asyncio.Semaphore(4)
    async def one(split,voice,text,label,rate):
        key=hashlib.sha256(json.dumps([voice,text,rate],ensure_ascii=False).encode()).hexdigest()[:20]
        mp3=output/(key+'.mp3');pcm=output/(key+'.pcm')
        async with sem:
            if not pcm.exists():
                for attempt in range(3):
                    try:
                        await edge_tts.Communicate(text,voice,rate=rate).save(str(mp3));break
                    except Exception:
                        if attempt==2:raise
                        await asyncio.sleep(2+attempt*2)
                subprocess.run(['ffmpeg','-v','error','-y','-i',str(mp3),'-ar','16000','-ac','1','-f','s16le',str(pcm)],check=True)
            data=pcm.read_bytes()
            if len(data)<3200 or len(data)%2:raise ValueError('Invalid TTS output')
        rows.append({'split':split,'speaker':voice,'text':text,'label':label,'rate':rate,
          'pcm':pcm.name,'sha256':hashlib.sha256(data).hexdigest(),'samples':len(data)//2,'source':'synthetic Edge TTS'})
        if len(rows)%25==0:print('WAKE_CORPUS',len(rows),flush=True)
    tasks=[]
    for split,voices in VOICES.items():
        for voice in voices:
            for text in ['你好 OpenVela','你好，Open Vela','你好，openvela。']:
                for rate in ['-15%','+0%','+15%']:tasks.append(one(split,voice,text,1,rate))
            for text in NEGATIVES:tasks.append(one(split,voice,text,0,'+0%'))
    await asyncio.gather(*tasks)
    rows.sort(key=lambda r:(r['split'],r['speaker'],r['pcm']))
    result={'format':1,'wake_phrase':'你好 openvela','sample_rate':16000,'synthetic_only':True,
      'split_policy':'disjoint TTS voices before augmentation; human evaluation still required','voices':VOICES,'records':rows}
    (output/'corpus.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('WAKE_CORPUS_READY',len(rows),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    asyncio.run(build(p.parse_args().output))
