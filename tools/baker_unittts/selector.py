"""Bank-bound, soft-target candidate ranking learned from held-out source contexts.

This distills an acoustic/context reference oracle, not human preference or MOS.
All candidates have the exact requested syllable and tone. A query's own source
utterance is excluded from candidates even during training.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import sqlite3
import time

import numpy as np
import torch
from torch import nn
from torch.nn.utils.rnn import pad_sequence

from .corpus import decode_features
from .prosody import Predictor
from .synthesize import Inventory, Unit


class Ranker(nn.Module):
    def __init__(self, vocabulary: int, features: int = 70):
        super().__init__()
        self.embedding=nn.Embedding(vocabulary,24,padding_idx=0)
        self.layers=nn.Sequential(nn.Linear(5*24+features,192),nn.SiLU(),
                                  nn.Linear(192,96),nn.SiLU(),nn.Linear(96,1),nn.Softplus())

    def forward(self, ids, values):
        return self.layers(torch.cat((self.embedding(ids).flatten(-2),values),dim=-1)).squeeze(-1)


def candidate_inputs(inventory, candidates, tokens, position, desired, vocabulary):
    names=[]
    features=[]
    query_previous=tokens[position-1] if position else '<boundary>'
    query_next=tokens[position+1] if position+1<len(tokens) else '<boundary>'
    for unit in candidates:
        context=inventory.source_context(unit)
        previous=context.previous or '<boundary>'
        following=context.following or '<boundary>'
        names.append([vocabulary.get(t,0) for t in (unit.token,query_previous,query_next,previous,following)])
        vector=inventory.target_vector(unit)
        qpos=position/max(len(tokens),1)
        cpos=context.ordinal/max(context.count,1)
        features.append([*desired,*vector,*(vector-desired),
                         *np.asarray(unit.f['left'])/6,*np.asarray(unit.f['right'])/6,
                         qpos,1-qpos,cpos,1-cpos,unit.f['voiced'],
                         min(unit.f['gap_before'],.5),min(unit.f['gap_after'],.5)])
    return np.asarray(names,np.int64),np.asarray(features,np.float32)


def source_queries(database):
    db=sqlite3.connect(f'file:{database.resolve().as_posix()}?mode=ro',uri=True)
    queries=[]
    for uid,split in db.execute("SELECT id,split FROM utterances WHERE split IN ('train','validation') ORDER BY id"):
        units=[]
        for i,row in enumerate(db.execute('SELECT id,token,start,end,features FROM units WHERE utterance=? ORDER BY ordinal',(uid,))):
            units.append(Unit(row[0],uid,i,row[1],row[2],row[3],decode_features(row[4])))
        queries.append((uid,split,units))
    db.close()
    return queries


def prepare_examples(original, bank, planner, output, maximum=30000, device='cuda', seed=20261029):
    if output.exists():
        raise FileExistsError('Selector dataset already exists')
    output.mkdir(parents=True)
    random.seed(seed)
    inventory=Inventory(bank)
    if not inventory.bank_id:
        raise ValueError('Selector training requires a frozen compiled bank')
    vocabulary={'<boundary>':0,**{t:i+1 for i,t in enumerate(sorted(inventory.by_token))}}
    predictor=Predictor(planner)
    predictor.model.to(device)
    queries=source_queries(original)
    predicted={}
    with torch.inference_mode():
        for start in range(0,len(queries),128):
            batch=queries[start:start+128]
            ids=[torch.tensor([predictor.vocabulary.get(u.token,1) for u in units]) for _,_,units in batch]
            lengths=torch.tensor([len(x) for x in ids])
            padded=pad_sequence(ids,batch_first=True).to(device)
            values=predictor.model(padded,lengths).cpu().numpy()*predictor.std+predictor.mean
            for i,(uid,_,units) in enumerate(batch):
                predicted[uid]=values[i,:len(units)]
    pool={'train':[],'validation':[]}
    for uid,split,units in queries:
        pool[split].extend((uid,units,i) for i in range(len(units)))
    report={}
    for split,limit in [('train',maximum),('validation',min(4000,maximum//4))]:
        random.shuffle(pool[split])
        examples=[]
        query_origins=set()
        for uid,units,position in pool[split]:
            token=units[position].token
            candidates=[u for u in inventory.by_token.get(token,[]) if inventory.sources[u.utterance]['origin']!=uid]
            if len(candidates)<2:
                continue
            desired=predicted[uid][position]
            candidates=sorted(candidates,key=lambda u:inventory.target_cost(u,desired))[:12]
            tokens=[u.token for u in units]
            ids,values=candidate_inputs(inventory,candidates,tokens,position,desired,vocabulary)
            reference=units[position]
            truth=inventory.target_vector(reference)
            costs=[]
            for candidate in candidates:
                cost=inventory.target_cost(candidate,truth)
                spectral=(np.mean((np.array(candidate.f['left'])-reference.f['left'])**2)+
                          np.mean((np.array(candidate.f['right'])-reference.f['right'])**2))
                costs.append(min(cost+.018*spectral,12.))
            examples.append((ids,values,np.asarray(costs,np.float32)))
            query_origins.add(uid)
            if len(examples)>=limit:
                break
        if not examples:
            raise ValueError('No selector examples for '+split)
        count=len(examples)
        ids=np.zeros((count,12,5),np.int64)
        values=np.zeros((count,12,70),np.float32)
        costs=np.zeros((count,12),np.float32)
        mask=np.zeros((count,12),bool)
        for i,(a,b,c) in enumerate(examples):
            n=len(c);ids[i,:n]=a;values[i,:n]=b;costs[i,:n]=c;mask[i,:n]=True
        np.savez_compressed(output/(split+'.npz'),ids=ids,values=values,costs=costs,mask=mask)
        report[split]=dict(queries=count,candidates=int(mask.sum()),query_origins=sorted(query_origins))
        print(json.dumps(dict(split=split,queries=count,candidates=int(mask.sum()))),flush=True)
    manifest=dict(bank_id=inventory.bank_id,vocabulary=vocabulary,planner_sha256=predictor.sha256,
                  seed=seed,own_source_candidate_excluded=True,oracle='reference duration/F0/energy and edge spectra',
                  split_report=report,human_preference_supervision=False,quality_passed=False)
    (output/'dataset.json').write_text(json.dumps(manifest,indent=2))
    return manifest


def train(dataset, output, epochs=35, device='cuda'):
    if output.exists():
        raise FileExistsError('Selector run already exists')
    output.mkdir(parents=True)
    torch.manual_seed(20261029)
    torch.set_num_threads(4)
    metadata=json.loads((dataset/'dataset.json').read_text())
    arrays={s:dict(np.load(dataset/(s+'.npz'))) for s in ('train','validation')}
    raw=arrays['train']['values'][arrays['train']['mask']]
    mean=raw.mean(0);std=raw.std(0).clip(.1)
    tensors={}
    for split,a in arrays.items():
        tensors[split]=(torch.tensor(a['ids'],device=device),
                        torch.tensor((a['values']-mean)/std,device=device),
                        torch.tensor(a['costs'],device=device),torch.tensor(a['mask'],device=device))
    model=Ranker(len(metadata['vocabulary'])).to(device)
    optimizer=torch.optim.AdamW(model.parameters(),lr=4e-4,weight_decay=1e-4)
    best=float('inf');history=[];start=time.monotonic()
    for epoch in range(1,epochs+1):
        metrics={}
        for split,values in tensors.items():
            model.train(split=='train')
            order=torch.randperm(len(values[0]),device=device) if split=='train' else torch.arange(len(values[0]),device=device)
            total=count=0
            with torch.set_grad_enabled(split=='train'):
                for indices in order.split(256):
                    ids,x,cost,mask=[v[indices] for v in values]
                    prediction=model(ids,x)
                    target_prob=torch.softmax((-cost/.35).masked_fill(~mask,-1e4),dim=-1)
                    logprob=torch.log_softmax((-prediction/.35).masked_fill(~mask,-1e4),dim=-1)
                    ranking=-(target_prob*logprob).sum(-1).mean()
                    calibration=((prediction-cost).square()*mask).sum()/mask.sum().clamp_min(1)
                    loss=ranking+.2*calibration
                    if not torch.isfinite(loss):raise RuntimeError('Nonfinite selector loss')
                    if split=='train':
                        optimizer.zero_grad(set_to_none=True);loss.backward()
                        nn.utils.clip_grad_norm_(model.parameters(),1.);optimizer.step()
                    total+=loss.item()*len(indices);count+=len(indices)
            metrics[split]=total/count
        row=dict(epoch=epoch,**metrics,elapsed_seconds=time.monotonic()-start)
        history.append(row);print(json.dumps(row),flush=True)
        if metrics['validation']<best:
            best=metrics['validation']
            torch.save(dict(state={k:v.cpu() for k,v in model.state_dict().items()},
                vocabulary=metadata['vocabulary'],mean=mean.tolist(),std=std.tolist(),
                bank_id=metadata['bank_id'],parameters=sum(p.numel() for p in model.parameters()),
                epoch=epoch,validation_loss=best,oracle=metadata['oracle'],quality_passed=False),output/'best.pt')
        (output/'history.json').write_text(json.dumps(history,indent=2))
    report=dict(parameters=sum(p.numel() for p in model.parameters()),bank_id=metadata['bank_id'],
                epochs=epochs,best_validation_loss=best,elapsed_seconds=time.monotonic()-start,
                train_queries=len(arrays['train']['ids']),validation_queries=len(arrays['validation']['ids']),
                oracle_supervised=True,human_preference_supervision=False,quality_passed=False)
    (output/'report.json').write_text(json.dumps(report,indent=2))


class Selector:
    def __init__(self, checkpoint):
        torch.set_num_threads(4)
        data=torch.load(checkpoint,map_location='cpu',weights_only=True)
        self.bank_id=data['bank_id'];self.vocabulary=data['vocabulary'];self.parameters=data['parameters']
        self.mean=np.array(data['mean'],np.float32);self.std=np.array(data['std'],np.float32)
        self.model=Ranker(len(self.vocabulary));self.model.load_state_dict(data['state']);self.model.eval()

    @torch.inference_mode()
    def costs(self, inventory, tokens, position, targets):
        if inventory.bank_id!=self.bank_id:raise ValueError('Selector is bound to a different frozen bank')
        candidates=inventory.by_token[tokens[position]]
        ids,features=candidate_inputs(inventory,candidates,tokens,position,targets[position],self.vocabulary)
        values=self.model(torch.from_numpy(ids),torch.from_numpy((features-self.mean)/self.std)).numpy()
        return {u.uid:float(max(0.,v)) for u,v in zip(candidates,values)}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='mode',required=True)
    a=sub.add_parser('prepare');a.add_argument('--original',type=Path,required=True);a.add_argument('--bank',type=Path,required=True)
    a.add_argument('--planner',type=Path,required=True);a.add_argument('--output',type=Path,required=True)
    a.add_argument('--maximum',type=int,default=30000)
    b=sub.add_parser('train');b.add_argument('--dataset',type=Path,required=True);b.add_argument('--output',type=Path,required=True)
    b.add_argument('--epochs',type=int,default=35)
    args=p.parse_args()
    if args.mode=='prepare':prepare_examples(args.original,args.bank,args.planner,args.output,args.maximum)
    else:train(args.dataset,args.output,args.epochs)
