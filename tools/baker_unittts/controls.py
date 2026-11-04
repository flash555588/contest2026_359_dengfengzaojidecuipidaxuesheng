"""Phone duration and syllable F0 control planning from real Baker recordings.

This first training stage is a context encoder with independent control heads.
It emits no waveform. Full-unit acoustic/edge encoders are subsequent stages,
not implicitly included in the parameter counts or quality claims here.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import random
import time

import numpy as np
import torch
from torch import nn
from torch.nn.utils.rnn import pad_sequence,pack_padded_sequence,pad_packed_sequence
from torch.utils.data import DataLoader

from .controls_data import KNOTS,MAX_PHONES,text_alignment

F0_SCALE = 2*np.log(2)/12  # two semitones
DURATION_SCALE = .35


class ControlPlanner(nn.Module):
    def __init__(self,tokens,characters,phones,hidden=256,layers=3):
        super().__init__()
        self.tokens=nn.Embedding(tokens,96,padding_idx=0)
        self.characters=nn.Embedding(characters,64,padding_idx=0)
        self.phones=nn.Embedding(phones,16,padding_idx=0)
        self.tones=nn.Embedding(6,16,padding_idx=0)
        self.encoder=nn.GRU(96+64+16*MAX_PHONES+16+4,hidden,num_layers=layers,
                            batch_first=True,bidirectional=True,dropout=.15)
        self.duration=nn.Sequential(nn.Linear(2*hidden,192),nn.SiLU(),nn.Linear(192,MAX_PHONES))
        self.pitch=nn.Sequential(nn.Linear(2*hidden,192),nn.SiLU(),nn.Linear(192,KNOTS))
        self.voicing=nn.Sequential(nn.Linear(2*hidden,96),nn.SiLU(),nn.Linear(96,KNOTS))
        # Initially reproduce train-only acoustic priors, then learn context.
        for head in (self.duration,self.pitch):
            nn.init.zeros_(head[-1].weight);nn.init.zeros_(head[-1].bias)

    def forward(self,tokens,characters,phones,lengths):
        encoded=self.encode(tokens,characters,phones,lengths)
        return self.duration(encoded),self.pitch(encoded),self.voicing(encoded)

    def encode(self,tokens,characters,phones,lengths):
        position=torch.arange(tokens.shape[1],device=tokens.device)[None,:].expand_as(tokens)
        n=lengths.to(tokens.device)[:,None]
        relative=position/n.clamp_min(1)
        context=torch.stack((relative,1-relative,(position==0).float(),(position==n-1).float()),-1)
        tones=tokens.new_zeros(tokens.shape)  # supplied separately via token-tone buffer
        tones=self.token_tones[tokens]
        values=torch.cat((self.tokens(tokens),self.characters(characters),
                          self.phones(phones).flatten(-2),self.tones(tones),context),-1)
        packed=pack_padded_sequence(values,lengths.cpu(),batch_first=True,enforce_sorted=False)
        encoded,_=self.encoder(packed)
        encoded,_=pad_packed_sequence(encoded,batch_first=True,total_length=tokens.shape[1])
        return encoded

    def set_tones(self,values):
        self.register_buffer('token_tones',torch.tensor(values,dtype=torch.long))


def load_dataset(directory):
    manifest=json.loads((directory/'dataset.json').read_text())
    if manifest['teacher_supervised'] or manifest['knots']!=KNOTS:
        raise ValueError('Unexpected control dataset supervision/schema')
    examples={'train':[],'validation':[],'test':[]}
    vocab={name:{'<pad>':0,'<unk>':1} for name in ('tokens','characters','phones')}
    raw=[]
    for record in manifest['records']:
        if 'skipped' in record:continue
        path=directory/record['path']
        if hashlib.sha256(path.read_bytes()).hexdigest()!=record['sha256']:
            raise ValueError('Control dataset feature file changed')
        data=dict(np.load(path,allow_pickle=False))
        raw.append((record,data))
        if record['split']=='train':
            for name in vocab:
                for label in data[name].flatten().tolist():
                    if label and label not in vocab[name]:vocab[name][label]=len(vocab[name])
    duration_values=defaultdict(list);pitch_values=defaultdict(list);tone_values=defaultdict(list)
    prototypes=defaultdict(Counter)
    for record,data in raw:
        if record['split']!='train':continue
        for token,phones,durations,f0,mask in zip(data['tokens'],data['phones'],data['durations'],data['log_f0'],data['voiced']):
            prototypes[str(token)][tuple(p for p in phones if p)]+=1
            for p,d in zip(phones,durations):
                if p and d>0:duration_values[str(p)].append(np.log(d))
            pitch_values[str(token)].append(np.where(mask>0,f0,np.nan))
            tone_values[int(token[-1])].append(np.where(mask>0,f0,np.nan))
    phone_base=np.full(len(vocab['phones']),np.log(.10),np.float32)
    for label,values in duration_values.items():phone_base[vocab['phones'][label]]=np.median(values)
    # Masks and sparse labels fall back to train tone/voice statistics only.
    all_voiced=np.concatenate([x[np.isfinite(x)] for values in pitch_values.values() for x in values])
    global_f0=np.median(all_voiced)
    tone_base={}
    for tone,values in tone_values.items():
        array=np.array(values);count=np.isfinite(array).sum(0)
        tone_base[tone]=np.divide(np.nansum(array,axis=0),count,
                                  out=np.full(KNOTS,global_f0),where=count>0)
    token_base=np.full((len(vocab['tokens']),KNOTS),global_f0,np.float32)
    for token,values in pitch_values.items():
        array=np.array(values);count=np.isfinite(array).sum(0)
        fallback=tone_base[int(token[-1])]
        # Smooth sparse-token means towards its tone template, without using validation.
        token_base[vocab['tokens'][token]]=(np.nansum(array,axis=0)+3*fallback)/(count+3)
    for record,data in raw:
        ids={name:np.array([[vocab[name].get(str(x),1) if x else 0 for x in row] for row in data[name]],np.int64)
             if name=='phones' else np.array([vocab[name].get(str(x),1) if x else 0 for x in data[name]],np.int64)
             for name in vocab}
        dmask=(data['durations']>0).astype(np.float32)
        dbase=phone_base[ids['phones']]
        fbase=token_base[ids['tokens']].copy()
        for i,token in enumerate(data['tokens']):
            if ids['tokens'][i]==1:fbase[i]=tone_base[int(token[-1])]
        examples[record['split']].append(dict(id=record['id'],**ids,
            duration_target=(np.log(np.maximum(data['durations'],.001))-dbase)/DURATION_SCALE,
            pitch_target=(data['log_f0']-fbase)/F0_SCALE,voiced=data['voiced'],
            dmask=dmask,dbase=dbase,fbase=fbase))
    metadata=dict(vocabulary=vocab,phone_base=phone_base.tolist(),token_base=token_base.tolist(),
                  tone_base={str(k):v.tolist() for k,v in tone_base.items()},
                  prototypes={k:[str(p) for p in v.most_common(1)[0][0]] for k,v in prototypes.items()},
                  dataset_sha256=hashlib.sha256((directory/'dataset.json').read_bytes()).hexdigest(),
                  training_utterances=[x['id'] for x in examples['train']],
                  validation_utterances=[x['id'] for x in examples['validation']],
                  token_tones=[int(t[-1]) if t[-1:].isdigit() else 0 for t in vocab['tokens']])
    return examples,json.loads(json.dumps(metadata))


def collate(examples):
    keys=('tokens','characters','phones','duration_target','pitch_target','voiced','dmask','dbase','fbase')
    batch={k:pad_sequence([torch.from_numpy(x[k]) for x in examples],batch_first=True) for k in keys}
    batch['lengths']=torch.tensor([len(x['tokens']) for x in examples])
    return batch


def objective(predictions,batch):
    duration,pitch,voicing=predictions
    dmask=batch['dmask'];valid=(torch.arange(pitch.shape[1],device=pitch.device)[None,:]<batch['lengths'].to(pitch.device)[:,None]).float()
    pmask=batch['voiced']*valid[:,:,None]
    dl=(torch.nn.functional.smooth_l1_loss(duration,batch['duration_target'],reduction='none')*dmask).sum()/dmask.sum().clamp_min(1)
    pl=(torch.nn.functional.smooth_l1_loss(pitch,batch['pitch_target'],reduction='none')*pmask).sum()/pmask.sum().clamp_min(1)
    pair=pmask[:,:,1:]*pmask[:,:,:-1]
    slope=(torch.nn.functional.smooth_l1_loss(torch.diff(pitch,dim=-1),torch.diff(batch['pitch_target'],dim=-1),reduction='none')*pair).sum()/pair.sum().clamp_min(1)
    vl=(torch.nn.functional.binary_cross_entropy_with_logits(voicing,batch['voiced'],reduction='none')*valid[:,:,None]).sum()/(valid.sum()*KNOTS).clamp_min(1)
    predicted_d=torch.exp(batch['dbase']+duration*DURATION_SCALE)*dmask
    actual_d=torch.exp(batch['dbase']+batch['duration_target']*DURATION_SCALE)*dmask
    syllable=(abs(predicted_d.sum(-1)-actual_d.sum(-1))*valid).sum()/valid.sum().clamp_min(1)
    loss=dl+pl+.25*slope+.15*vl+2*syllable
    metrics=dict(duration_ms=(abs(predicted_d-actual_d)*1000).sum().detach()/dmask.sum().clamp_min(1),
                 syllable_ms=syllable.detach()*1000,
                 f0_semitones=(abs(pitch-batch['pitch_target'])*2*pmask).sum().detach()/pmask.sum().clamp_min(1))
    return loss,metrics


def train(directory,output,epochs=80,device='cuda',patience=12):
    if output.exists():raise FileExistsError('Choose a fresh control training run')
    output.mkdir(parents=True)
    torch.manual_seed(20261029);np.random.seed(20261029);random.seed(20261029);torch.set_num_threads(4)
    examples,metadata=load_dataset(directory)
    for split in ('train','validation'):
        if not examples[split]:raise ValueError('Missing '+split+' examples')
    vocab=metadata['vocabulary']
    model=ControlPlanner(len(vocab['tokens']),len(vocab['characters']),len(vocab['phones']))
    model.set_tones(metadata['token_tones']);model.to(device)
    parameters=sum(p.numel() for p in model.parameters())
    if parameters>20_000_000:raise ValueError('Planner leaves insufficient room for joint controls under 25M total')
    optimizer=torch.optim.AdamW(model.parameters(),lr=3e-4,weight_decay=1e-4)
    scheduler=torch.optim.lr_scheduler.ReduceLROnPlateau(optimizer,patience=4,factor=.5,min_lr=1e-5)
    loaders={s:DataLoader(examples[s],batch_size=64,shuffle=s=='train',collate_fn=collate) for s in ('train','validation')}
    best=float('inf');history=[];started=time.monotonic();best_epoch=0
    # Baseline uses exactly the train-only priors that initialize both heads.
    with torch.inference_mode():
        values=[]
        for batch in loaders['validation']:
            batch={k:v.to(device) if k!='lengths' else v for k,v in batch.items()}
            shape=batch['pitch_target'].shape
            loss,m=objective((torch.zeros_like(batch['duration_target']),torch.zeros_like(batch['pitch_target']),
                              torch.zeros(shape,device=device)),batch)
            values.append((len(batch['lengths']),float(loss),{k:float(v) for k,v in m.items()}))
        baseline={k:sum(n*m[k] for n,_,m in values)/sum(n for n,_,_ in values) for k in values[0][2]}
    print(json.dumps(dict(parameters=parameters,validation_prior_baseline=baseline,teacher_supervised=False)),flush=True)
    for epoch in range(1,epochs+1):
        row=dict(epoch=epoch)
        for split,loader in loaders.items():
            model.train(split=='train');count=total=0;totals=Counter()
            with torch.set_grad_enabled(split=='train'):
                for batch in loader:
                    batch={k:v.to(device) if k!='lengths' else v for k,v in batch.items()}
                    prediction=model(batch['tokens'],batch['characters'],batch['phones'],batch['lengths'])
                    loss,metrics=objective(prediction,batch)
                    if not torch.isfinite(loss):raise RuntimeError('Nonfinite control loss')
                    if split=='train':
                        optimizer.zero_grad(set_to_none=True);loss.backward()
                        nn.utils.clip_grad_norm_(model.parameters(),1.);optimizer.step()
                    n=len(batch['lengths']);count+=n;total+=float(loss.detach())*n
                    for k,v in metrics.items():totals[k]+=float(v)*n
            row[split]=dict(loss=total/count,**{k:v/count for k,v in totals.items()})
        scheduler.step(row['validation']['loss'])
        row['seconds']=time.monotonic()-started;row['lr']=optimizer.param_groups[0]['lr']
        history.append(row);print(json.dumps(row),flush=True)
        if row['validation']['loss']<best:
            best=row['validation']['loss'];best_epoch=epoch
            torch.save(dict(state={k:v.cpu() for k,v in model.state_dict().items()},metadata=metadata,
                            parameters=parameters,epoch=epoch,metrics=row['validation'],
                            validation_prior_baseline=baseline,teacher_supervised=False,
                            waveform_generator=False,quality_passed=False),output/'best.pt')
        (output/'history.json').write_text(json.dumps(history,indent=2))
        if epoch-best_epoch>=patience:break
    report=dict(parameters=parameters,best_epoch=best_epoch,epochs=len(history),
                best_validation=history[best_epoch-1]['validation'],validation_prior_baseline=baseline,
                splits={s:len(x) for s,x in examples.items()},seconds=time.monotonic()-started,
                teacher_supervised=False,waveform_generator=False,quality_passed=False,
                renderer_trained=False,splice_trained=False)
    (output/'report.json').write_text(json.dumps(report,indent=2));return report


class Predictor:
    def __init__(self,path):
        torch.set_num_threads(4)
        data=torch.load(path,map_location='cpu',weights_only=True)
        self.metadata=data['metadata'];v=self.metadata['vocabulary'];self.parameters=data['parameters']
        self.model=ControlPlanner(len(v['tokens']),len(v['characters']),len(v['phones']))
        self.model.set_tones(self.metadata['token_tones']);self.model.load_state_dict(data['state']);self.model.eval()
        self.sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest()

    @torch.inference_mode()
    def __call__(self,tokens,text=''):
        m=self.metadata;v=m['vocabulary'];characters,_,aligned=text_alignment(text,len(tokens))
        labels=[m['prototypes'].get(t) for t in tokens]
        if any(p is None for p in labels):raise ValueError('No train phone topology for requested exact syllable')
        ids=torch.tensor([[v['tokens'].get(t,1) for t in tokens]])
        chars=torch.tensor([[v['characters'].get(c,1) if c else 0 for c in characters]])
        phones=torch.tensor([[[v['phones'].get(p,1) for p in row]+[0]*(MAX_PHONES-len(row)) for row in labels]])
        d,p,voiced=self.model(ids,chars,phones,torch.tensor([len(tokens)]))
        durations=np.exp(np.array(m['phone_base'])[phones.numpy()[0]]+d[0].numpy()*DURATION_SCALE)*(phones.numpy()[0]>0)
        base=np.array(m['token_base'])[ids.numpy()[0]]
        f0=np.exp(base+p[0].numpy()*F0_SCALE)
        return dict(tokens=tokens,phones=labels,phone_durations=durations.tolist(),
                    f0=f0.tolist(),voiced=torch.sigmoid(voiced[0]).numpy().tolist(),
                    characters_aligned=aligned,parameters=self.parameters)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dataset',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--epochs',type=int,default=80);p.add_argument('--patience',type=int,default=12)
    a=p.parse_args();print(json.dumps(train(a.dataset,a.output,a.epochs,patience=a.patience),indent=2))
