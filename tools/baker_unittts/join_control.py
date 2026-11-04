"""Learn bounded splice actions over a frozen PCM bank; no waveform generator.

Supervision is an explicitly recorded DSP surrogate, not human audio preference.
Clean recorded boundaries are included to discourage needless processing.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import time

import numpy as np
from scipy import signal
import torch
from torch import nn

from .selector import source_queries
from .synthesize import Inventory

ACTION_NAMES=('unchanged','fade_025ms','fade_1ms','fade_2ms',
              'offset_05ms','offset_1ms','offset_2ms','offset_4ms','low_energy_cut_075ms')


def action_join(left, right, rate, action):
    if left.dtype!=np.int16 or right.dtype!=np.int16 or min(len(left),len(right))<8:
        raise ValueError('Splice actions require nonempty PCM16 context')
    a=left.astype(np.float64);b=right.astype(np.float64)
    cut_left=cut_right=0
    milliseconds=0.
    if action==8:
        width=min(round(rate*.00075),len(a)//8,len(b)//8)
        # Do not trim an onset whose first millisecond is an energy transient.
        n=max(2,round(rate*.001))
        onset=np.sqrt(np.mean(b[:n]**2))
        following=np.sqrt(np.mean(b[n:8*n]**2)) if len(b)>2*n else onset
        if onset>1.8*max(following,1):
            action=5
        elif width:
            cut_left=width-1-int(np.argmin(np.abs(a[-width:])))
            cut_right=int(np.argmin(np.abs(b[:width])))
            if cut_left:a=a[:-cut_left]
            if cut_right:b=b[cut_right:]
    if action in (1,2,3):
        milliseconds={1:.25,2:1.,3:2.}[action]
        n=min(max(1,round(rate*milliseconds/1000)),len(a)//8,len(b)//8)
        t=np.linspace(0.,1.,n)
        a[-n:]*=np.cos(t*np.pi/2)**2
        b[:n]*=np.sin(t*np.pi/2)**2
    elif action in (4,5,6,7,8):
        milliseconds={4:.5,5:1.,6:2.,7:4.,8:1.}[action]
        n=min(max(2,round(rate*milliseconds/1000)),len(a)//8,len(b)//8)
        jump=(b[0]-a[-1])/2
        t=np.linspace(0.,1.,n)
        ramp=t*t*(3-2*t)
        a[-n:]+=jump*ramp
        b[:n]-=jump*ramp[::-1]
    elif action!=0:
        raise ValueError('Unknown splice action')
    joined=np.concatenate((a,b))
    clipped=int(np.sum((joined>32767)|(joined<-32768)))
    if clipped:
        # A rejected action never silently clips or normalizes the utterance.
        return np.concatenate((left,right)),dict(action=0,requested_action=action,rejected='clipping',
                 seam=len(left),modified_input_samples=0,removed_samples=0)
    return np.rint(joined).astype(np.int16),dict(action=action,requested_action=action,
        name=ACTION_NAMES[action],seam=len(a),milliseconds=milliseconds,
        modified_input_samples=2*round(rate*milliseconds/1000)+cut_left+cut_right,
        removed_samples=cut_left+cut_right,cut_left_samples=cut_left,cut_right_samples=cut_right)


def inputs(left,right,rate,vocabulary,tokens):
    width=round(rate*.024)
    a=left[-width:].astype(np.float64);b=right[:width].astype(np.float64)
    if len(a)<width:a=np.pad(a,(width-len(a),0))
    if len(b)<width:b=np.pad(b,(0,width-len(b)))
    x=np.concatenate((a,b))
    scale=max(float(np.sqrt(np.mean(x*x))),64.)
    g=math.gcd(rate,16000)
    resampled=signal.resample_poly(x,16000//g,rate//g)
    if len(resampled)!=768:
        resampled=np.interp(np.linspace(0,len(resampled)-1,768),np.arange(len(resampled)),resampled)
    wave=np.clip(resampled/scale,-8,8).astype(np.float32)[None,:]
    ar=max(float(np.sqrt(np.mean(a*a))),1.);br=max(float(np.sqrt(np.mean(b*b))),1.)
    metrics=np.array([(b[0]-a[-1])/scale,
        ((b[1]-b[0])-(a[-1]-a[-2]))/scale,np.log(br/ar),np.max(np.abs(x))/scale,
        np.sqrt(np.mean(np.diff(a)**2))/scale,np.sqrt(np.mean(np.diff(b)**2))/scale,
        np.log(scale/32768),float(scale<=64.)],np.float32)
    ids=np.array([vocabulary.get(t,0) for t in tokens],np.int64)
    return wave,metrics,ids


def surrogate_cost(left,right,joined,detail,rate):
    seam=detail['seam']
    width=min(round(rate*.012),seam,len(joined)-seam)
    x=joined[seam-width:seam+width].astype(np.float64)
    raw=np.concatenate((left[-width:],right[:width])).astype(np.float64)
    scale=max(float(np.sqrt(np.mean(raw**2))),64.)
    jump=(x[width]-x[width-1])/scale
    curvature=((x[width+1]-x[width])-(x[width-1]-x[width-2]))/scale
    region=max(3,round(rate*.001))
    second=np.diff(x[width-region:width+region],n=2)/scale
    spikes=float(np.sort(second*second)[-min(5,len(second)):].mean())
    # Different-length actions get an explicit deletion penalty and preserve
    # the corresponding original samples in the distortion comparison.
    ca=detail.get('cut_left_samples',0);cb=detail.get('cut_right_samples',0)
    original=np.concatenate((left[max(0,len(left)-ca-width):len(left)-ca if ca else len(left)],right[cb:cb+width])).astype(np.float64)
    distortion=float(np.mean(((x-original)/scale)**2)) if len(original)==len(x) else 1.
    return .16*min(jump*jump,100)+.025*min(curvature*curvature,100)+.035*min(spikes,100)+6*distortion+.035*(ca+cb)/rate*1000


class JoinNet(nn.Module):
    def __init__(self,vocabulary):
        super().__init__()
        self.embedding=nn.Embedding(vocabulary,16,padding_idx=0)
        self.encoder=nn.Sequential(nn.Conv1d(1,24,9,2,4),nn.SiLU(),
            nn.Conv1d(24,48,7,2,3),nn.SiLU(),nn.Conv1d(48,64,5,2,2),nn.SiLU(),
            nn.Conv1d(64,96,5,2,2),nn.SiLU(),nn.AdaptiveAvgPool1d(4))
        self.head=nn.Sequential(nn.Linear(96*4+32+8,128),nn.SiLU(),nn.Linear(128,len(ACTION_NAMES)))

    def forward(self,wave,metrics,ids):
        return self.head(torch.cat((self.encoder(wave).flatten(1),metrics,self.embedding(ids).flatten(1)),dim=1))


def origin_split(origin):
    n=int(hashlib.sha256(('join-v1:'+origin).encode()).hexdigest()[:8],16)%10
    return 'validation' if n==0 else 'train'


def prepare(original,bank,output,maximum=12000,seed=20261029):
    if output.exists():raise FileExistsError('Join dataset already exists')
    output.mkdir(parents=True)
    rng=random.Random(seed)
    inv=Inventory(bank)
    if not inv.bank_id:raise ValueError('Join training requires a frozen PCM bank')
    vocabulary={'<boundary>':0,**{t:i+1 for i,t in enumerate(sorted(inv.by_token))}}
    groups={s:{} for s in ('train','validation')}
    all_units={s:[] for s in groups}
    for token,units in inv.by_token.items():
        for split in groups:
            chosen=[u for u in units if origin_split(inv.sources[u.utterance]['origin'])==split]
            groups[split][token]=chosen;all_units[split].extend(chosen)
    queries=source_queries(original)
    manifest={}
    for split,limit in [('train',maximum),('validation',max(1000,maximum//6))]:
        pool=[(uid,units,i) for uid,s,units in queries if s==split for i in range(len(units)-1)]
        rng.shuffle(pool)
        examples=[];origins=set();query_ids=set();pairs=set();clean_count=0
        def fragment(u,side):
            wave,rate=inv.wave(u.utterance)
            n=round(rate*.035)
            if side=='left':return wave[max(u.start,u.end-n):u.end].copy(),rate
            return wave[u.start:min(u.end,u.start+n)].copy(),rate
        for uid,sequence,position in pool:
            clean=rng.random()<.2
            if clean:
                u=rng.choice(all_units[split]);wave,rate=inv.wave(u.utterance)
                n=round(rate*.035)
                if len(wave)<2*n+4:continue
                point=rng.randrange(n,len(wave)-n)
                left,right=wave[point-n:point].copy(),wave[point:point+n].copy()
                tokens=(u.token,u.token);used=[u];clean_count+=1
            else:
                lu,ru=sequence[position],sequence[position+1]
                a=groups[split].get(lu.token,[]);b=groups[split].get(ru.token,[])
                if not a or not b:continue
                l=rng.choice(a);r=rng.choice(b)
                pair=(l.uid,r.uid)
                if pair in pairs:continue
                pairs.add(pair)
                left,rate=fragment(l,'left');right,rrate=fragment(r,'right')
                if rate!=rrate or min(len(left),len(right))<round(rate*.012):continue
                tokens=(l.token,r.token);used=[l,r]
            features=inputs(left,right,rate,vocabulary,tokens)
            costs=[]
            for action in range(len(ACTION_NAMES)):
                joined,detail=action_join(left,right,rate,action)
                cost=surrogate_cost(left,right,joined,detail,rate)
                if detail.get('rejected'):cost+=10.
                if clean:
                    # Genuine recorded transitions need no modification. Do
                    # not label their legitimate high-frequency detail as noise.
                    cost=0. if action==0 else cost+.20
                costs.append(cost)
            examples.append((*features,np.asarray(costs,np.float32),clean))
            query_ids.add(uid)
            origins.update(inv.sources[u.utterance]['origin'] for u in used)
            if len(examples)>=limit:break
        if len(examples)<100:raise ValueError('Not enough disjoint join examples')
        waves,metrics,ids,costs,clean=zip(*examples)
        np.savez_compressed(output/(split+'.npz'),waves=np.stack(waves),metrics=np.stack(metrics),
                            ids=np.stack(ids),costs=np.stack(costs),clean=np.asarray(clean,bool))
        manifest[split]=dict(examples=len(examples),clean_recorded_boundaries=clean_count,
                             query_origins=sorted(query_ids),audio_origins=sorted(origins))
        print(json.dumps(dict(split=split,examples=len(examples),clean=clean_count)),flush=True)
    if set(manifest['train']['audio_origins']) & set(manifest['validation']['audio_origins']):
        raise RuntimeError('Join source leakage between train and validation')
    report=dict(bank_id=inv.bank_id,vocabulary=vocabulary,seed=seed,actions=ACTION_NAMES,
                supervision='DSP continuity/distortion surrogate plus clean recorded negatives',
                audio_origins_disjoint=True,humans_listened=False,quality_passed=False,splits=manifest)
    (output/'dataset.json').write_text(json.dumps(report,indent=2))


def train(dataset,output,epochs=25,device='cuda'):
    if output.exists():raise FileExistsError('Join run already exists')
    output.mkdir(parents=True)
    torch.manual_seed(20261029);torch.set_num_threads(4)
    metadata=json.loads((dataset/'dataset.json').read_text())
    arrays={s:dict(np.load(dataset/(s+'.npz'))) for s in ('train','validation')}
    mean=arrays['train']['metrics'].mean(0);std=arrays['train']['metrics'].std(0).clip(.1)
    tensors={}
    for split,a in arrays.items():
        tensors[split]=(torch.tensor(a['waves'],device=device),
            torch.tensor((a['metrics']-mean)/std,device=device),torch.tensor(a['ids'],device=device),
            torch.tensor(a['costs'],device=device),torch.tensor(a['clean'],device=device))
    model=JoinNet(len(metadata['vocabulary'])).to(device)
    optimizer=torch.optim.AdamW(model.parameters(),lr=3e-4,weight_decay=1e-4)
    best=float('inf');history=[];start=time.monotonic()
    for epoch in range(1,epochs+1):
        metrics={}
        for split,values in tensors.items():
            model.train(split=='train');order=torch.randperm(len(values[0]),device=device)
            total=count=regret=clean_edits=clean_count=0
            with torch.set_grad_enabled(split=='train'):
                for indices in order.split(128):
                    wave,x,ids,cost,clean=[v[indices] for v in values]
                    logits=model(wave,x,ids)
                    target=torch.softmax(-cost/.025,dim=-1)
                    loss=-(target*torch.log_softmax(logits,dim=-1)).sum(-1).mean()
                    if not torch.isfinite(loss):raise RuntimeError('Nonfinite join-controller loss')
                    if split=='train':
                        optimizer.zero_grad(set_to_none=True);loss.backward()
                        nn.utils.clip_grad_norm_(model.parameters(),1.);optimizer.step()
                    chosen=logits.argmax(-1)
                    regret+=(cost.gather(1,chosen[:,None]).squeeze(1)-cost.min(-1).values).sum().item()
                    clean_edits+=((chosen!=0)&clean).sum().item();clean_count+=clean.sum().item()
                    total+=loss.item()*len(indices);count+=len(indices)
            metrics[split]=total/count
            metrics[split+'_surrogate_regret']=regret/count
            metrics[split+'_clean_edit_rate']=clean_edits/max(clean_count,1)
        row=dict(epoch=epoch,**metrics,elapsed_seconds=time.monotonic()-start)
        history.append(row);print(json.dumps(row),flush=True)
        if metrics['validation']<best:
            best=metrics['validation']
            torch.save(dict(state={k:v.cpu() for k,v in model.state_dict().items()},
                vocabulary=metadata['vocabulary'],mean=mean.tolist(),std=std.tolist(),
                bank_id=metadata['bank_id'],parameters=sum(p.numel() for p in model.parameters()),
                epoch=epoch,metrics=metrics,actions=ACTION_NAMES,quality_passed=False),output/'best.pt')
        (output/'history.json').write_text(json.dumps(history,indent=2))
    report=dict(parameters=sum(p.numel() for p in model.parameters()),bank_id=metadata['bank_id'],epochs=epochs,
                best_validation_loss=best,elapsed_seconds=time.monotonic()-start,
                supervision=metadata['supervision'],train_examples=len(arrays['train']['ids']),
                validation_examples=len(arrays['validation']['ids']),quality_passed=False,board_tested=False)
    (output/'report.json').write_text(json.dumps(report,indent=2))


class Controller:
    def __init__(self,checkpoint):
        torch.set_num_threads(4)
        data=torch.load(checkpoint,map_location='cpu',weights_only=True)
        self.bank_id=data['bank_id'];self.vocabulary=data['vocabulary'];self.parameters=data['parameters']
        self.mean=np.array(data['mean'],np.float32);self.std=np.array(data['std'],np.float32)
        self.model=JoinNet(len(self.vocabulary));self.model.load_state_dict(data['state']);self.model.eval()

    @torch.inference_mode()
    def join(self,left,right,rate,tokens,bank_id):
        if bank_id!=self.bank_id:raise ValueError('Join controller is bound to another bank')
        wave,metrics,ids=inputs(left,right,rate,self.vocabulary,tokens)
        probabilities=torch.softmax(self.model(torch.from_numpy(wave[None]),
            torch.from_numpy(((metrics-self.mean)/self.std)[None]),torch.from_numpy(ids[None])),dim=-1)[0].numpy()
        action=int(probabilities.argmax())
        result,detail=action_join(left,right,rate,action)
        detail.update(method='learned_bounded_splice',confidence=float(probabilities[action]))
        return result,detail


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='mode',required=True)
    a=sub.add_parser('prepare');a.add_argument('--original',type=Path,required=True);a.add_argument('--bank',type=Path,required=True)
    a.add_argument('--output',type=Path,required=True);a.add_argument('--maximum',type=int,default=12000)
    b=sub.add_parser('train');b.add_argument('--dataset',type=Path,required=True);b.add_argument('--output',type=Path,required=True)
    b.add_argument('--epochs',type=int,default=25)
    args=p.parse_args()
    if args.mode=='prepare':prepare(args.original,args.bank,args.output,args.maximum)
    else:train(args.dataset,args.output,args.epochs)
