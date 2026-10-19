#!/usr/bin/env python3
"""Compare teacher and portable Chinese frontend on independent test texts."""
import argparse
import json
from pathlib import Path
import sys

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--prepared',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=a.prepared.resolve();a.output.parent.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(root/'upstream/pypkg'))
from piper import PiperVoice
from sanotts.frontend import load_phoneme_table
from sanotts.zh_g2p import text_to_phoneme_ids
config=root/'teacher/zh_CN-xiao_ya-medium.onnx.json'
voice=PiperVoice.load(root/'teacher/zh_CN-xiao_ya-medium.onnx',config_path=config)
table=load_phoneme_table(config)
rows=[json.loads(x) for x in (root/'corpora/acceptance.jsonl').read_text(encoding='utf-8').splitlines() if x.strip()]
results=[]
for row in rows:
    phonemes=[p for chunk in voice.phonemize(row['text']) for p in chunk]
    teacher=voice.phonemes_to_ids(phonemes)
    portable,unmapped=text_to_phoneme_ids(row['text'],table)
    portable=portable.tolist()
    results.append({**row,'ids_equal':teacher==portable,'unmapped':unmapped,
                    'teacher_ids':teacher,'portable_ids':portable})
result={'cases':len(results),'equal':sum(r['ids_equal'] for r in results),
        'all_symbols_mapped':all(not r['unmapped'] for r in results),
        'actual_device_execution':False,'rows':results}
a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('ZH_FRONTEND_AUDIT',json.dumps({k:v for k,v in result.items() if k!='rows'}),flush=True)
