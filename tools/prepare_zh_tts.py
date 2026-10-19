#!/usr/bin/env python3
"""Prepare a pinned Chinese TTS distillation experiment, without board writes.

Outputs are research students, not official SanoTTS quantized voice packages.
Teacher licensing and upstream notices are preserved beside the downloaded data.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import urllib.request


def prepare(output, cache=None, extra_corpus=None):
    root=Path(__file__).resolve().parents[1]
    spec=root/'app/espdl-quickapp/speech/zh-distill'
    lock=json.loads((spec/'upstream.lock.json').read_text(encoding='utf-8'))
    teacher=json.loads((spec/'teacher.lock.json').read_text(encoding='utf-8'))
    output.mkdir(parents=True,exist_ok=True)

    def fetch(job):
        url,target,record,cached=job
        data=None
        for source in [target,cached]:
            if source and source.is_file():
                candidate=source.read_bytes()
                if len(candidate)==record['bytes'] and hashlib.sha256(candidate).hexdigest()==record['sha256']:
                    data=candidate;break
        if data is None:data=urllib.request.urlopen(url,timeout=180).read()
        if len(data)!=record['bytes'] or hashlib.sha256(data).hexdigest()!=record['sha256']:
            raise ValueError('Locked Chinese training input differs: '+str(target))
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)

    jobs=[]
    for row in lock['sources']:
        relative=Path(row['path'])
        if relative.is_absolute() or '..' in relative.parts:raise ValueError('Unsafe input path')
        jobs.append((f"https://raw.githubusercontent.com/{lock['repo']}/{lock['revision']}/{relative.as_posix()}",
                     output/'upstream'/relative,row,cache/'upstream'/relative if cache else None))
    for row in teacher['sources']:
        name=Path(row['path']).name
        jobs.append((f"https://huggingface.co/{teacher['repo']}/resolve/{teacher['revision']}/{row['path']}",
                     output/'teacher'/name,row,cache/'teacher'/name if cache else None))
    with ThreadPoolExecutor(6) as pool:list(pool.map(fetch,jobs))
    support=[]
    for path in sorted((root/'tools/zh_tts_support').glob('*.py')):
        target=output/'upstream/tools'/path.name
        data=path.read_bytes();target.write_bytes(data)
        support.append({'path':'tools/'+path.name,'sha256':hashlib.sha256(data).hexdigest(),
                        'origin':'project-owned bridge for an unpublished upstream dependency'})
    data=output/'upstream/data/textsets/multilang-distill-v1'
    train=[json.loads(x) for x in (data/'zh_CN.train.jsonl').read_text(encoding='utf-8').splitlines() if x.strip()]
    evaluate=[json.loads(x) for x in (data/'zh_CN.eval.jsonl').read_text(encoding='utf-8').splitlines() if x.strip()]
    if len(evaluate)!=128 or len(train)!=2048:raise ValueError('Changed corpus shape')
    # The synthetic source repeats base texts with an English "Variant N."
    # suffix. Raw-string separation hides leakage and creates extra sentences.
    # Split by the normalized spoken body, never by its decoration or row id.
    def normalize(row):
        return {**row,'text':re.sub(r'\s+Variant\s+\d+\.?\s*$','',row['text']).strip()}
    clean_eval=[normalize(row) for row in evaluate]
    eval_text={row['text'] for row in clean_eval}
    if len(eval_text)!=128:raise ValueError('Duplicate evaluation bodies')
    clean_train=[];seen=set(eval_text)
    for raw in train:
        row=normalize(raw)
        if row['text'] in seen:continue
        seen.add(row['text']);clean_train.append(row)
    rows=clean_train[:12]+clean_eval+clean_train[12:]
    if len(rows)!=768:raise ValueError('Unexpected normalized corpus size')
    extra_count=0
    if extra_corpus:
        extras=[json.loads(x) for x in extra_corpus.read_text(encoding='utf-8').splitlines() if x.strip()]
        for row in extras:
            row=normalize(row)
            if row['text'] in seen:raise ValueError('Extra corpus duplicates a fixed input: '+row['text'])
            seen.add(row['text']);rows.append(row);extra_count+=1
    heldout={r['text'].strip() for r in rows[:140]}
    if len(heldout)!=140 or heldout & {r['text'].strip() for r in rows[140:]}:
        raise ValueError('Training/evaluation text leakage')
    if len({r['id'] for r in rows})!=len(rows):raise ValueError('Duplicate corpus ids')
    acceptance_data=(spec/'acceptance.jsonl').read_bytes()
    acceptance=[json.loads(x) for x in acceptance_data.decode('utf-8').splitlines() if x.strip()]
    all_text={r['text'].strip() for r in rows}
    if any(r['text'].strip() in all_text for r in acceptance):
        raise ValueError('Acceptance text leaked into train/calibration corpus')
    corpora=output/'corpora';corpora.mkdir(exist_ok=True)
    manifests={}
    for name,count in [('smoke',396),('full',len(rows))]:
        target=corpora/(name+'.jsonl')
        target.write_bytes(('\n'.join(json.dumps(r,ensure_ascii=False) for r in rows[:count])+'\n').encode('utf-8'))
        manifests[name]={'rows':count,'held_out_rows':140,'training_rows':count-140,
                         'sha256':hashlib.sha256(target.read_bytes()).hexdigest()}
    (corpora/'acceptance.jsonl').write_bytes(acceptance_data)
    manifests['acceptance']={'rows':len(acceptance),'training_rows':0,'calibration_rows':0,
                             'sha256':hashlib.sha256(acceptance_data).hexdigest()}
    result={'upstream':lock,'teacher':teacher,'corpora':manifests,'board_written':False,
            'text_sets_are_synthetic_templates':True,'quality_claimed':False,
            'preprocessing_version':2,'raw_training_rows':len(train),'raw_evaluation_rows':len(evaluate),
            'normalized_unique_rows':len(rows),'removed_duplicate_or_leaked_rows':len(train)+len(evaluate)-(len(rows)-extra_count),
            'support_sources':support}
    if extra_corpus:result['extra_training_corpus']={'rows':extra_count,'sha256':hashlib.sha256(extra_corpus.read_bytes()).hexdigest(),'origin':'project-authored synthetic coverage texts'}
    (output/'prepared.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    (output/'experiment.json').write_bytes((spec/'experiment.json').read_bytes())
    print('CHINESE_DISTILLATION_INPUTS_READY',json.dumps(manifests),flush=True)
    return output


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cache',type=Path,help='Previously verified download tree')
    p.add_argument('--extra-corpus',type=Path,help='Author-owned additional training texts; never acceptance rows')
    a=p.parse_args();prepare(a.output.resolve(),a.cache.resolve() if a.cache else None,a.extra_corpus.resolve() if a.extra_corpus else None)
