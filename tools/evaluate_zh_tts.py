#!/usr/bin/env python3
"""Independent Chinese ASR check; automated transcripts do not replace listening."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import time

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--audition',type=Path,required=True);p.add_argument('--model',type=Path,required=True)
    p.add_argument('--model-sha256',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--device',choices=['cpu','cuda'],default='cpu');p.add_argument('--indices',default='0,8,18,19,21,23')
    p.add_argument('--lanes',default='teacher,student')
    p.add_argument('--rescore-only',action='store_true')
    a=p.parse_args()
    if hashlib.sha256(a.model.read_bytes()).hexdigest()!=a.model_sha256:raise ValueError('ASR model hash differs')
    from opencc import OpenCC
    converter=OpenCC('t2s')
    def norm(text):return re.sub(r'[^\w]','',converter.convert(text).lower(),flags=re.UNICODE)
    def edits(a,b):
        prev=list(range(len(b)+1))
        for i,x in enumerate(a,1):
            cur=[i]
            for j,y in enumerate(b,1):cur.append(min(cur[-1]+1,prev[j]+1,prev[j-1]+(x!=y)))
            prev=cur
        return prev[-1]
    rows=[];indices={int(x) for x in a.indices.split(',')}
    report={'asr_model_sha256':a.model_sha256,'asr_model':'OpenAI Whisper base multilingual',
            'device':a.device,'human_listening_verified':False,'quality_passed':False,
            'normalization':'OpenCC t2s, Unicode letters/digits, lowercase, punctuation removed; numeric spelling differences retained',
            'rows':rows}
    def save():a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    if a.rescore_only:
        previous=json.loads(a.output.read_text(encoding='utf-8'));rows=previous['rows'];report['rows']=rows
        report['rescored_from_existing_transcripts']=True
        for row in rows:
            reference=norm(row['text']);distance=edits(reference,norm(row['heard']))
            row.update(cer=distance/max(1,len(reference)),edit_distance=distance,reference_characters=len(reference))
    else:
        import torch
        import whisper
        torch.set_num_threads(4)
        model=whisper.load_model(str(a.model),device=a.device)
    for lane in ([] if a.rescore_only else a.lanes.split(',')):
        manifest=[json.loads(x) for x in (a.audition/lane/'manifest.jsonl').read_text().splitlines() if x.strip()]
        for item in manifest:
            if item['index'] not in indices:continue
            if not item['ok']:raise ValueError('Failed synthesis: '+str(item))
            path=a.audition/lane/item['wav'];start=time.monotonic()
            heard=model.transcribe(str(path),language='zh',fp16=a.device=='cuda',temperature=0,
                condition_on_previous_text=False)['text'].strip()
            reference=norm(item['text']);hypothesis=norm(heard)
            distance=edits(reference,hypothesis)
            row={**item,'lane':lane,'heard':heard,'cer':distance/max(1,len(reference)),
                 'edit_distance':distance,'reference_characters':len(reference),
                 'elapsed_seconds':time.monotonic()-start,'wav_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
            rows.append(row);save();print(json.dumps(row,ensure_ascii=False),flush=True)
    report['summary']={lane:{'cases':sum(x['lane']==lane for x in rows),
        'cer':sum(x['edit_distance'] for x in rows if x['lane']==lane)/max(1,sum(x['reference_characters'] for x in rows if x['lane']==lane))}
        for lane in sorted({x['lane'] for x in rows})}
    report['status']='completed';save()
if __name__=='__main__':main()
