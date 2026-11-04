"""Cumulative four-stage listening comparison with a fixed bank and checkpoints."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil

import soundfile as sf

from .audition import CASES
from .join_control import Controller
from .prosody import Predictor
from .selector import Selector
from .synthesize import Inventory,synthesize

STAGES=[('00-rule','纯规则'),('01-prosody','加韵律规划器'),
        ('02-selection','再加选段模型'),('03-splice','再加接缝控制模型')]


def run(root:Path,output:Path):
    if output.exists():raise FileExistsError('Ablation directory already exists')
    output.mkdir(parents=True)
    database=root/'bank-200m-v1/bank.sqlite'
    bank=json.loads((database.parent/'bank.json').read_text())
    for name,info in bank['files'].items():
        h=hashlib.sha256()
        with (database.parent/name).open('rb') as stream:
            for chunk in iter(lambda:stream.read(4*1024*1024),b''):h.update(chunk)
        if h.hexdigest()!=info['sha256']:raise ValueError('Frozen bank checksum mismatch: '+name)
    inventory=Inventory(database)
    planner=Predictor(root/'planner-v2/best.pt')
    selector=Selector(root/'selector-v1/best.pt')
    controller=Controller(root/'join-v1/best.pt')
    if selector.bank_id!=inventory.bank_id or controller.bank_id!=inventory.bank_id:
        raise ValueError('Checkpoint/bank identity mismatch')
    all_rows=[]
    for key,text,pinyin in CASES:
        case=output/key;case.mkdir()
        rows=[];paths=[]
        for i,(stage,label) in enumerate(STAGES):
            inventory.selector=selector if i>=2 else None
            result=synthesize(database,case/stage,pinyin=pinyin,inventory=inventory,
                              predictor=planner if i>=1 else None,controller=controller if i>=3 else None)
            selected=[[(s['utterance'],s['first'],s['last']) for s in p['spans']] for p in result['phrases']]
            paths.append(selected)
            audio=case/stage/'bounded_joins.wav'
            if sf.info(audio).frames<=0 or result['clipped_samples']:
                raise RuntimeError('Empty or clipped ablation audio')
            rows.append(dict(stage=stage,label=label,seconds=result['seconds'],
                parameters=result['active_parameters'],joins=len(result['joins']),
                path=audio.relative_to(output).as_posix(),sha256=hashlib.sha256(audio.read_bytes()).hexdigest(),
                actions=[j.get('name',j.get('method')) for j in result['joins']]))
        if paths[2]!=paths[3]:raise RuntimeError('Splice ablation changed source selection')
        teacher=root/'audition-heuristic-v1'/(key+'-teacher')/'teacher.wav'
        shutil.copy2(teacher,case/'teacher.wav')
        all_rows.append(dict(key=key,text=text,pinyin=pinyin,stages=rows,
                             teacher=(case/'teacher.wav').relative_to(output).as_posix(),
                             splice_selection_locked=True,
                             prosody_changed_selection=paths[0]!=paths[1],
                             selector_changed_selection=paths[1]!=paths[2]))
        print(json.dumps(all_rows[-1],ensure_ascii=False),flush=True)
    models={name:dict(bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            for name,path in [('prosody',root/'planner-v2/best.pt'),('selector',root/'selector-v1/best.pt'),
                              ('splice',root/'join-v1/best.pt')]}
    summary=dict(cases=all_rows,bank_id=inventory.bank_id,bank_bytes=bank['total_bytes'],
                 models=models,total_parameters=planner.parameters+selector.parameters+controller.parameters,
                 cumulative=True,all_stages_same_bank=True,splice_selection_locked=True,
                 controlled_explicit_pinyin=True,volume_normalized=False,
                 quality_passed=False,board_tested=False)
    (output/'comparison.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    return summary


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(a.root,a.output)
