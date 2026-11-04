"""Frozen-bank-conditioned duration/F0 control, supervised by real recordings.

One acoustic encoder is shared by the center unit and both neighbors. Every
stored analysis frame is consumed; this model emits bounded control curves,
not audio. It does not yet include a trained splice head.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import random
import time

import numpy as np
import torch
from torch import nn
from torch.nn.utils.rnn import pad_sequence,pack_padded_sequence,pad_packed_sequence

from .bank import digest_file
from .bank_review import verify_files
from .controls import Predictor as Planner,F0_SCALE,DURATION_SCALE
from .controls_data import MAX_PHONES,KNOTS,sample_pitch
from .synthesize import Inventory

INITIALS={'b','p','m','f','d','t','n','l','g','k','h','j','q','x','zh','ch','sh','r','z','c','s'}
PITCH_LIMIT=2*np.log(2)/12


def bounds(labels):
    limits=[(.95,1.05) if p in INITIALS else (.85,1.18) for p in labels]
    limits.extend([(1.,1.)]*(MAX_PHONES-len(labels)))
    limits=np.log(np.array(limits))
    return limits.mean(1),(limits[:,1]-limits[:,0])/2


def phase_map(source_durations,target_durations):
    positions=(np.arange(KNOTS)+.5)/KNOTS*sum(source_durations)
    source_edges=np.r_[0.,np.cumsum(source_durations)]
    target_edges=np.r_[0.,np.cumsum(target_durations)]
    return np.interp(positions,source_edges,target_edges)


def acoustic_frames(acoustic):
    durations=acoustic['durations'];total=max(float(durations.sum()),.001)
    time=acoustic['frame_times'].astype(np.float32)/total
    f0=np.log(np.maximum(acoustic['frame_f0'],75)/220)
    vowel=np.zeros(len(time),np.float32);cursor=0.
    for p,d in zip(acoustic['phones'],durations):
        if not p:continue
        if p not in INITIALS:vowel[(time>=cursor/total)&(time<(cursor+d)/total)]=1
        cursor+=float(d)
    return np.column_stack((np.clip((acoustic['features'].astype(np.float32)+15)/8,-6,6),
                            f0,acoustic['frame_voiced'],time,vowel)).astype(np.float32)


class BankControlNet(nn.Module):
    def __init__(self,vocabulary):
        super().__init__()
        self.token=nn.Embedding(vocabulary,32,padding_idx=0)
        self.front=nn.Sequential(nn.Conv1d(33,48,5,padding=2),nn.SiLU())
        self.acoustic=nn.GRU(48,64,batch_first=True,bidirectional=True)
        self.layers=nn.Sequential(nn.Linear(512+3*128+9*32+54+30,384),nn.SiLU(),nn.Dropout(.1),
                                  nn.Linear(384,192),nn.SiLU(),nn.Linear(192,30))
        nn.init.zeros_(self.layers[-1].weight);nn.init.zeros_(self.layers[-1].bias)

    def forward(self,frames,lengths,tokens,context,observed,base):
        # Flatten three independently padded units. Shared encoder weights see
        # all frames; lengths prevent padding from becoming acoustic evidence.
        n=frames.shape[0]
        lens=lengths.flatten().cpu()
        flat=frames.flatten(0,1)
        input_mask=torch.arange(flat.shape[1],device=frames.device)[None,:]<lens.to(frames.device)[:,None]
        x=self.front((flat*input_mask[:,:,None]).transpose(1,2)).transpose(1,2)
        packed=pack_padded_sequence(x,lens,batch_first=True,enforce_sorted=False)
        hidden,_=self.acoustic(packed)
        hidden,_=pad_packed_sequence(hidden,batch_first=True,total_length=x.shape[1])
        mask=torch.arange(x.shape[1],device=frames.device)[None,:]<lens.to(frames.device)[:,None]
        pooled=(hidden*mask[:,:,None]).sum(1)/lens.to(frames.device)[:,None]
        pooled=pooled.reshape(n,3,128)*(tokens[:,:,0]!=0)[:,:,None]
        pooled=pooled.flatten(1)
        delta=self.layers(torch.cat((context,pooled,self.token(tokens).flatten(1),observed,base),1))
        # Zero initial delta exactly reproduces the bounded global-planner baseline.
        logits=torch.atanh(base.clamp(-.98,.98))+delta
        return torch.tanh(logits)


def unpack_inventory(inventory):
    units=sorted((u for seq in inventory.units.values() for u in seq),key=lambda u:u.uid)
    vocabulary={'<boundary>':0,**{t:i+1 for i,t in enumerate(sorted(inventory.by_token))}}
    frames=[np.zeros((1,33),np.float32)];tokens=[[0,0,0]];acoustics=[None];lookup={};groups=defaultdict(list)
    for index,unit in enumerate(units,1):
        acoustic=inventory.acoustic_unit(unit)
        context=inventory.source_context(unit)
        extra=np.array([context.ordinal/max(context.count,1),context.ordinal==0,
                        context.ordinal==context.count-1,min(unit.f['gap_before'],.5),
                        min(unit.f['gap_after'],.5)],np.float32)
        values=acoustic_frames(acoustic)
        frames.append(np.column_stack((values,np.broadcast_to(extra,(len(values),5)))))
        tokens.append([vocabulary[unit.token],vocabulary.get(context.previous,0),vocabulary.get(context.following,0)])
        acoustics.append(acoustic)
        lookup[unit.uid]=index
        groups[(unit.token,tuple(str(p) for p in acoustic['phones'] if p))].append(unit)
    return units,vocabulary,frames,np.array(tokens,np.int64),acoustics,lookup,groups


def targets_for(acoustic,reference,index,plan):
    labels=[str(p) for p in acoustic['phones'] if p];count=len(labels)
    source_d=acoustic['durations'][:count].astype(float)
    ref_d=reference['durations'][index,:count].astype(float)
    desired_d=np.array(plan['durations'][index,:count],float)
    center,spread=bounds(labels)
    raw=np.zeros(30);base=np.zeros(30);mask=np.zeros(30)
    raw[:count]=(np.log(ref_d/source_d)-center[:count])/spread[:count]
    base[:count]=(np.log(desired_d/source_d)-center[:count])/spread[:count]
    mask[:count]=1
    mapped_ref=phase_map(source_d,ref_d)+reference['starts'][index]
    ref_f0,ref_voiced=sample_pitch(reference['frame_times'],reference['frame_f0'],
                                  reference['frame_voiced'],mapped_ref)
    mapped_plan=phase_map(source_d,desired_d)/desired_d.sum()
    fbase=np.interp(mapped_plan,(np.arange(KNOTS)+.5)/KNOTS,plan['log_f0'][index])
    src=acoustic['log_f0'];voiced=acoustic['voiced']
    raw[6:]=(np.log(np.maximum(ref_f0,75))-src)/PITCH_LIMIT
    base[6:]=(fbase-src)/PITCH_LIMIT
    mask[6:]=voiced*ref_voiced
    observed=np.r_[acoustic['log_f0']-np.log(220),acoustic['voiced'],
                   np.log(np.maximum(acoustic['durations'],.001))].astype(np.float32)
    return np.clip(raw,-.98,.98).astype(np.float32),np.clip(base,-.98,.98).astype(np.float32),mask.astype(np.float32),observed,raw


def inference_base(acoustic,desired_d,planned_f0):
    labels=[str(p) for p in acoustic['phones'] if p];count=len(labels)
    source_d=acoustic['durations'][:count].astype(float);desired_d=np.array(desired_d[:count],float)
    center,spread=bounds(labels);base=np.zeros(30)
    base[:count]=(np.log(desired_d/source_d)-center[:count])/spread[:count]
    mapped=phase_map(source_d,desired_d)/desired_d.sum()
    f0=np.interp(mapped,(np.arange(KNOTS)+.5)/KNOTS,planned_f0)
    base[6:]=(f0-acoustic['log_f0'])/PITCH_LIMIT
    observed=np.r_[acoustic['log_f0']-np.log(220),acoustic['voiced'],
                   np.log(np.maximum(acoustic['durations'],.001))].astype(np.float32)
    return np.clip(base,-.98,.98).astype(np.float32),observed


def prepare(original_data,bank,planner,output,maximum=30000):
    if output.exists():raise FileExistsError('Choose a fresh bank-conditioned dataset')
    verify_files(bank);inventory=Inventory(bank/'bank.sqlite')
    if not inventory.context_complete:raise ValueError('Bank context is incomplete')
    model=Planner(planner);m=model.metadata;v=m['vocabulary'];model.model.cuda()
    manifest=json.loads((original_data/'dataset.json').read_text())
    if digest_file(original_data/'dataset.json')!=json.loads((bank/'bank.json').read_text())['control_dataset_sha256']:
        raise ValueError('Bank/data binding mismatch')
    units,vocabulary,frames,token_ids,acoustics,lookup,groups=unpack_inventory(inventory)
    records=[r for r in manifest['records'] if r.get('split') in ('train','validation') and 'skipped' not in r]
    raw={};plans={};contexts={};started=time.monotonic()
    with torch.inference_mode():
        for start in range(0,len(records),128):
            batch=records[start:start+128];ids=[];chars=[];phones=[]
            for r in batch:
                path=original_data/r['path']
                if digest_file(path)!=r['sha256']:raise ValueError('Original feature file changed')
                with np.load(path,allow_pickle=False) as values:raw[r['id']]=dict(values)
                d=raw[r['id']]
                ids.append(torch.tensor([v['tokens'].get(str(t),1) for t in d['tokens']]))
                chars.append(torch.tensor([v['characters'].get(str(c),1) if c else 0 for c in d['characters']]))
                phones.append(torch.tensor([[v['phones'].get(str(p),1) if p else 0 for p in row] for row in d['phones']]))
            lengths=torch.tensor([len(x) for x in ids])
            ti=pad_sequence(ids,batch_first=True).cuda();ci=pad_sequence(chars,batch_first=True).cuda();pi=pad_sequence(phones,batch_first=True).cuda()
            encoded=model.model.encode(ti,ci,pi,lengths)
            dr=model.model.duration(encoded).cpu().numpy();fr=model.model.pitch(encoded).cpu().numpy()
            encoded=encoded.cpu().numpy()
            for i,r in enumerate(batch):
                d=raw[r['id']];n=len(d['tokens']);ii=ids[i].numpy();pp=phones[i].numpy()
                plans[r['id']]=dict(durations=np.exp(np.array(m['phone_base'])[pp]+dr[i,:n]*DURATION_SCALE)*(pp>0),
                    log_f0=np.array(m['token_base'])[ii]+fr[i,:n]*F0_SCALE)
                contexts[r['id']]=encoded[i,:n].astype(np.float16)
    rng=random.Random(20261029);examples={};receipt={}
    for split,limit in [('train',maximum),('validation',min(4000,maximum//4))]:
        pool=[(r['id'],i) for r in records if r['split']==split for i in range(len(raw[r['id']]['tokens']))]
        rng.shuffle(pool);selected=[];identities=0;origins=set();limited=total=0
        def choose(uid,index,allow_identity=False):
            d=raw[uid];key=(str(d['tokens'][index]),tuple(str(p) for p in d['phones'][index] if p))
            candidates=groups.get(key,[])
            own=[u for u in candidates if inventory.sources[u.utterance]['origin']==uid]
            if allow_identity and own:
                candidates=[u for u in own if inventory.source_context(u).ordinal==index]
                if candidates:return candidates[0]
            candidates=[u for u in candidates if inventory.sources[u.utterance]['origin']!=uid]
            if not candidates:return None
            plan=plans[uid];desired_d=float(plan['durations'][index].sum())
            # Candidate variation includes near-plan choices and non-optimal
            # frozen-bank choices; the model must handle actual retrieval diversity.
            candidates=sorted(candidates,key=lambda u:abs(np.log(u.f['duration']/desired_d)))[:8]
            return rng.choice(candidates)
        for uid,index in pool:
            identity=split=='train' and rng.random()<.15
            unit=choose(uid,index,identity)
            if unit is None:continue
            actual_identity=(inventory.sources[unit.utterance]['origin']==uid)
            indices=[]
            for neighbor in (index-1,index,index+1):
                if neighbor==index:u=unit
                elif 0<=neighbor<len(raw[uid]['tokens']):
                    # Retain natural adjacent bank units when their labels match;
                    # otherwise use another exact-tone candidate from this bank.
                    source=inventory.units[unit.utterance];at=unit.ordinal+(neighbor-index)
                    u=source[at] if 0<=at<len(source) and source[at].token==raw[uid]['tokens'][neighbor] else choose(uid,neighbor)
                else:u=None
                indices.append(lookup[u.uid] if u else 0)
            acoustic=acoustics[lookup[unit.uid]]
            target,base,mask,observed,unbounded=targets_for(acoustic,raw[uid],index,plans[uid])
            if actual_identity:
                labels=[str(p) for p in acoustic['phones'] if p];center,spread=bounds(labels);count=len(labels)
                target[:count]=(-center[:count]/spread[:count]).astype(np.float32)
                target[6:]=0;mask[6:]=acoustic['voiced']
                unbounded[:count]=target[:count];unbounded[6:]=0
            selected.append((indices,contexts[uid][index],target,base,mask,observed))
            limited+=int(np.sum((abs(unbounded)>1)&(mask>0)));total+=int(np.sum(mask>0))
            identities+=actual_identity;origins.add(uid)
            if len(selected)>=limit:break
        if not selected:raise ValueError('No bank-conditioned '+split+' examples')
        examples[split]={name:np.array([row[i] for row in selected],dtype=dtype)
                         for i,(name,dtype) in enumerate([('indices',np.int64),('context',np.float16),
                             ('target',np.float32),('base',np.float32),('mask',np.float32),('observed',np.float32)])}
        receipt[split]=dict(examples=len(selected),identity_examples=identities,query_origins=sorted(origins),
                            reference_controls_outside_bounds=limited,valid_reference_controls=total)
        print(json.dumps(dict(split=split,examples=len(selected),identity_examples=identities)),flush=True)
    output.mkdir(parents=True)
    offsets=np.r_[0,np.cumsum([len(x) for x in frames])]
    np.savez_compressed(output/'acoustics.npz',frames=np.concatenate(frames),offsets=offsets,tokens=token_ids)
    for split,values in examples.items():np.savez_compressed(output/(split+'.npz'),**values)
    metadata=dict(bank_id=inventory.bank_id,bank_files=json.loads((bank/'bank.json').read_text())['files'],
        planner_sha256=model.sha256,vocabulary=vocabulary,source_dataset_sha256=digest_file(original_data/'dataset.json'),
        source_unit_ids=[0]+[u.uid for u in units],split_report=receipt,
        first_stage_parameters=model.parameters,teacher_supervised=False,full_unit_frames_consumed=True,
        waveform_generator=False,splice_head_trained=False,quality_passed=False,seconds=time.monotonic()-started)
    metadata['files']={p.name:dict(sha256=digest_file(p),bytes=p.stat().st_size) for p in output.glob('*.npz')}
    (output/'dataset.json').write_text(json.dumps(metadata,indent=2));return metadata


def batch_inputs(arrays,indices,frames,offsets,tokens,device):
    selected=arrays['indices'][indices];rows=[torch.from_numpy(frames[offsets[i]:offsets[i+1]]) for i in selected.flatten()]
    lengths=torch.tensor([len(x) for x in rows]).reshape(-1,3)
    padded=pad_sequence(rows,batch_first=True).reshape(len(indices),3,-1,33).to(device)
    return (padded,lengths,torch.from_numpy(tokens[selected]).to(device),
            torch.from_numpy(arrays['context'][indices].astype(np.float32)).to(device),
            torch.from_numpy(arrays['observed'][indices]).to(device),
            torch.from_numpy(arrays['base'][indices]).to(device))


def loss_function(prediction,target,mask):
    point=torch.nn.functional.smooth_l1_loss(prediction,target,reduction='none')
    duration=(point[:,:6]*mask[:,:6]).sum()/mask[:,:6].sum().clamp_min(1)
    pitch=(point[:,6:]*mask[:,6:]).sum()/mask[:,6:].sum().clamp_min(1)
    adjacent=mask[:,7:]*mask[:,6:-1]
    slope=(torch.nn.functional.smooth_l1_loss(torch.diff(prediction[:,6:],dim=1),torch.diff(target[:,6:],dim=1),reduction='none')*adjacent).sum()/adjacent.sum().clamp_min(1)
    return .6*duration+pitch+.2*slope


def train(dataset,output,epochs=60):
    if output.exists():raise FileExistsError('Choose a fresh bank-control training run')
    output.mkdir(parents=True);torch.manual_seed(20261029);torch.set_num_threads(4)
    metadata=json.loads((dataset/'dataset.json').read_text())
    for name,row in metadata['files'].items():
        if digest_file(dataset/name)!=row['sha256']:raise ValueError('Bank-control dataset changed')
    with np.load(dataset/'acoustics.npz') as x:frames=x['frames'];offsets=x['offsets'];tokens=x['tokens']
    arrays={}
    for split in ('train','validation'):
        with np.load(dataset/(split+'.npz')) as x:arrays[split]=dict(x)
    model=BankControlNet(len(metadata['vocabulary'])).cuda()
    count=sum(p.numel() for p in model.parameters())
    if count+metadata['first_stage_parameters']>25_000_000:raise ValueError('Combined active parameter budget exceeded')
    optimizer=torch.optim.AdamW(model.parameters(),lr=3e-4,weight_decay=1e-4)
    scheduler=torch.optim.lr_scheduler.ReduceLROnPlateau(optimizer,patience=3,factor=.5,min_lr=1e-5)
    base=arrays['validation'];baseline=float(loss_function(torch.from_numpy(base['base']),torch.from_numpy(base['target']),torch.from_numpy(base['mask'])))
    history=[];best=float('inf');best_epoch=0;started=time.monotonic()
    print(json.dumps(dict(parameters=count,total_parameters=count+metadata['first_stage_parameters'],baseline=baseline)),flush=True)
    for epoch in range(1,epochs+1):
        row=dict(epoch=epoch)
        for split,values in arrays.items():
            model.train(split=='train');order=np.random.default_rng(20261029+epoch).permutation(len(values['target'])) if split=='train' else np.arange(len(values['target']))
            total=n=0
            with torch.set_grad_enabled(split=='train'):
                for start in range(0,len(order),128):
                    indices=order[start:start+128]
                    inputs=batch_inputs(values,indices,frames,offsets,tokens,'cuda')
                    prediction=model(*inputs)
                    target=torch.from_numpy(values['target'][indices]).cuda();mask=torch.from_numpy(values['mask'][indices]).cuda()
                    loss=loss_function(prediction,target,mask)
                    if not torch.isfinite(loss):raise RuntimeError('Nonfinite bank-control loss')
                    if split=='train':
                        optimizer.zero_grad(set_to_none=True);loss.backward();nn.utils.clip_grad_norm_(model.parameters(),1.);optimizer.step()
                    total+=float(loss.detach())*len(indices);n+=len(indices)
            row[split]=total/n
        scheduler.step(row['validation']);row['seconds']=time.monotonic()-started
        history.append(row);print(json.dumps(row),flush=True)
        if row['validation']<best:
            best=row['validation'];best_epoch=epoch
            torch.save(dict(state={k:v.cpu() for k,v in model.state_dict().items()},metadata=metadata,parameters=count,
                            epoch=epoch,validation_loss=best,baseline_loss=baseline,quality_passed=False),output/'best.pt')
        (output/'history.json').write_text(json.dumps(history,indent=2))
        if epoch-best_epoch>=10:break
    report=dict(parameters=count,total_parameters=count+metadata['first_stage_parameters'],bank_id=metadata['bank_id'],
                epochs=len(history),best_epoch=best_epoch,best_validation_loss=best,baseline_loss=baseline,
                teacher_supervised=False,waveform_generator=False,full_neighbor_acoustics=True,
                splice_trained=False,quality_passed=False,seconds=time.monotonic()-started)
    (output/'report.json').write_text(json.dumps(report,indent=2));return report


class Predictor:
    def __init__(self,checkpoint,planner,inventory):
        torch.set_num_threads(4)
        data=torch.load(checkpoint,map_location='cpu',weights_only=True)
        self.metadata=data['metadata'];self.planner=Planner(planner)
        if self.metadata['bank_id']!=inventory.bank_id:raise ValueError('Bank-control checkpoint belongs to another frozen bank')
        if self.metadata['planner_sha256']!=self.planner.sha256:raise ValueError('Global planner checkpoint changed')
        manifest=verify_files(inventory.path.parent)
        if manifest['files']!=self.metadata['bank_files']:raise ValueError('Bank files changed after control training')
        self.model=BankControlNet(len(self.metadata['vocabulary']));self.model.load_state_dict(data['state']);self.model.eval()
        self.parameters=data['parameters']+self.planner.parameters
        self.sha256=digest_file(Path(checkpoint));self._inputs={}

    def source_inputs(self,inventory,unit):
        if unit.uid not in self._inputs:
            a=inventory.acoustic_unit(unit);c=inventory.source_context(unit);v=self.metadata['vocabulary']
            x=acoustic_frames(a)
            extra=np.array([c.ordinal/max(c.count,1),c.ordinal==0,c.ordinal==c.count-1,
                            min(unit.f['gap_before'],.5),min(unit.f['gap_after'],.5)],np.float32)
            x=np.column_stack((x,np.broadcast_to(extra,(len(x),5))))
            tokens=np.array([v[unit.token],v.get(c.previous,0),v.get(c.following,0)],np.int64)
            if len(self._inputs)>=128:self._inputs.pop(next(iter(self._inputs)))
            self._inputs[unit.uid]=(a,x,tokens)
        return self._inputs[unit.uid]

    @torch.inference_mode()
    def __call__(self,inventory,tokens,text,selected):
        from .controls_data import text_alignment
        if inventory.bank_id!=self.metadata['bank_id']:raise ValueError('Bank changed during inference')
        if len(selected)!=len(tokens) or any(u.token!=t for u,t in zip(selected,tokens)):
            raise ValueError('Control inputs do not match the exact selected syllables/tones')
        p=self.planner;m=p.metadata;v=m['vocabulary'];characters,_,aligned=text_alignment(text,len(tokens))
        source=[self.source_inputs(inventory,u) for u in selected]
        ids=torch.tensor([[v['tokens'].get(t,1) for t in tokens]])
        chars=torch.tensor([[v['characters'].get(c,1) if c else 0 for c in characters]])
        phones=torch.tensor([[[v['phones'].get(str(label),1) if label else 0 for label in a['phones']] for a,_,_ in source]])
        context=p.model.encode(ids,chars,phones,torch.tensor([len(tokens)]))
        durations=np.exp(np.array(m['phone_base'])[phones.numpy()[0]]+p.model.duration(context)[0].numpy()*DURATION_SCALE)*(phones.numpy()[0]>0)
        planned=np.array(m['token_base'])[ids.numpy()[0]]+p.model.pitch(context)[0].numpy()*F0_SCALE
        results=[]
        for i,(a,_,_) in enumerate(source):
            base,observed=inference_base(a,durations[i],planned[i])
            triplet=[source[j] if 0<=j<len(tokens) else (None,np.zeros((1,33),np.float32),np.zeros(3,np.int64))
                     for j in (i-1,i,i+1)]
            rows=[torch.from_numpy(row[1]) for row in triplet]
            frames=pad_sequence(rows,batch_first=True)[None,:,:,:]
            lengths=torch.tensor([[len(row) for row in rows]])
            names=torch.from_numpy(np.stack([row[2] for row in triplet]))[None,:,:]
            prediction=self.model(frames,lengths,names,context[:,i,:],torch.from_numpy(observed)[None,:],torch.from_numpy(base)[None,:])[0].numpy()
            labels=[str(x) for x in a['phones'] if x];center,spread=bounds(labels);count=len(labels)
            factor=np.exp(center[:count]+spread[:count]*prediction[:count])
            results.append(dict(token=tokens[i],unit_id=selected[i].uid,phones=labels,
                                duration_factors=factor.tolist(),pitch_delta_semitones=(prediction[6:]*2*a['voiced']).tolist(),
                                source_durations=a['durations'][:count].tolist(),source_voiced=a['voiced'].tolist()))
        return dict(units=results,bank_id=inventory.bank_id,parameters=self.parameters,
                    planner_sha256=p.sha256,controller_sha256=self.sha256,characters_aligned=aligned)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='mode',required=True)
    a=sub.add_parser('prepare');a.add_argument('--data',type=Path,required=True);a.add_argument('--bank',type=Path,required=True)
    a.add_argument('--planner',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--maximum',type=int,default=30000)
    a=sub.add_parser('train');a.add_argument('--dataset',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--epochs',type=int,default=60)
    a=p.parse_args()
    if a.mode=='prepare':prepare(a.data,a.bank,a.planner,a.output,a.maximum)
    else:train(a.dataset,a.output,a.epochs)
