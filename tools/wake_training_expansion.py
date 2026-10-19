"""Train-only acoustic expansion and fixed-budget prototype fitting.

Uses observed augmented features as medoids, preserving the on-device DTW
representation and its 54/144 prototype budget. No validation/test audio is fit.
"""
import hashlib,json
from pathlib import Path
import numpy as np

def fit_expanded(rows,audio,base,anchor_ids,extract,multiplier,out,initial_templates,initial_metadata):
    variants=6*multiplier;seed=2026101910
    rng=np.random.default_rng(seed);features=[];labels=[];metadata=[]
    train=[i for i,row in enumerate(rows) if row['split']=='train']
    for number,index in enumerate(train):
        row=rows[index]
        for variant in range(variants):
            sound=audio[index].copy();rate=1.;gain=1.;echo=0.;snr=None;blur=1
            if variant:
                rate=float(rng.uniform(.78,1.22))
                sound=np.interp(np.arange(0,len(sound),rate),np.arange(len(sound)),sound)
                if variant%3:
                    delay=int(rng.uniform(.015,.12)*16000);echo=float(rng.uniform(.05,.45))
                    if len(sound)>delay:sound[delay:]+=echo*sound[:-delay].copy()
                if variant%4==0:
                    blur=int(rng.choice([3,5,7]));sound=np.convolve(sound,np.ones(blur)/blur,mode='same')
                gain=float(np.exp(rng.uniform(np.log(.04),np.log(1.4))));sound*=gain
                snr=float(rng.uniform(10,36));rms=float(np.sqrt(np.mean(sound**2)+1))
                sound+=rng.normal(0,rms*10**(-snr/20),len(sound))
            feature=base[index] if not variant else extract(sound)
            features.append(feature);labels.append(row['label'])
            metadata.append({'source_index':index,'source_pcm':row['pcm'],'speaker':row['speaker'],
                'label':row['label'],'variant':variant,'rate':rate,'gain':gain,'echo':echo,'snr_db':snr,'blur':blur})
        if number%20==0:print('WAKE_TRAIN_EXPAND',number+1,len(train),'examples',len(features),flush=True)
    features=np.ascontiguousarray(features,dtype=np.float32);labels=np.array(labels,dtype=np.int8)
    assert len(features)==len(train)*6*multiplier
    np.save(out/'training-features.npy',features);np.save(out/'training-labels.npy',labels)
    with (out/'training-examples.jsonl').open('w',encoding='utf-8') as file:
        for row in metadata:file.write(json.dumps(row,ensure_ascii=False)+'\n')
    # Match the runtime's first-eight coefficient emphasis for clustering.
    weights=np.tile(np.array([2,2,2,2,1,1,1,1,0,0,0,0,0],dtype=np.float32),32)
    scale=np.sqrt(weights);bank=[];bank_meta=[];fit_reports=[]
    for label,budget in [(1,54),(0,144)]:
        indices=np.flatnonzero(labels==label);data=features[indices];weighted=data*scale
        fixed=[i for i in anchor_ids if rows[i]['label']==label]
        centers=[base[i]*scale for i in fixed]
        norms=np.einsum('ij,ij->i',weighted,weighted)
        def distances(c):return np.maximum(norms[:,None]+np.sum(c*c,axis=1)[None,:]-2*weighted@c.T,0)
        minimum=np.min(distances(np.array(centers)),axis=1)
        for _ in range(budget-len(fixed)):
            total=float(minimum.sum());choice=int(rng.choice(len(data),p=minimum/total)) if total else 0
            centers.append(weighted[choice].copy());minimum=np.minimum(minimum,distances(np.array([centers[-1]]))[:,0])
        centers=np.array(centers,dtype=np.float32);losses=[]
        for epoch in range(12):
            dd=distances(centers);assignment=np.argmin(dd,axis=1)
            loss=float(dd[np.arange(len(data)),assignment].mean());losses.append(loss)
            for k in range(len(fixed),budget):
                selected=weighted[assignment==k]
                if len(selected):centers[k]=np.mean(selected,axis=0)
            print('WAKE_TRAIN_FIT','positive' if label else 'negative','epoch',epoch+1,'loss',round(loss,4),flush=True)
        bank.extend(base[i] for i in fixed)
        bank_meta.extend({**rows[i],'prototype_source':'clean training anchor'} for i in fixed)
        chosen=set()
        dd=distances(centers)
        for k in range(len(fixed),budget):
            ranked=np.argsort(dd[:,k]);choice=next(int(i) for i in ranked if int(i) not in chosen);chosen.add(choice)
            bank.append(data[choice]);bank_meta.append({**rows[metadata[int(indices[choice])]['source_index']],
                'prototype_source':'medoid fitted on expanded training audio',
                'augmentation':metadata[int(indices[choice])]})
        fit_reports.append({'label':label,'training_examples':len(data),'budget':budget,'fixed_clean_anchors':len(fixed),'epoch_loss':losses})
    medoids=np.ascontiguousarray(bank,dtype=np.float32)
    # A small trust region retains timing/room coverage of the accepted bank.
    # Fit all expanded examples, then regularize each original prototype toward
    # its nearest learned class medoid. Validation/test never choose medoids.
    result=initial_templates.copy();regularization=.05
    fitted_metadata=[]
    for i,original in enumerate(initial_templates):
        first,last=(0,54) if i<54 else (54,198)
        distances_to_fit=np.sum(((medoids[first:last]-original)*scale)**2,axis=1)
        match=first+int(np.argmin(distances_to_fit))
        # Keep the original unmodified clean examples as stable anchors.
        blend=0. if i%3==0 else regularization
        result[i]=(1-blend)*original+blend*medoids[match]
        fitted_metadata.append({**initial_metadata[i],'prototype_source':'expanded-data medoid fit with fixed clean anchors',
            'training_blend':blend,'fitted_medoid':bank_meta[match]})
    report={'multiplier':multiplier,'base_variants_per_training_clip':6,'variants_per_training_clip':variants,
        'base_training_examples':len(train)*6,'training_examples':len(features),'unique_training_clips':len(train),
        'synthetic_augmented':True,'new_human_recordings':0,'seed':seed,'fit_splits':['train'],
        'validation_test_used_for_fitting':False,'template_budget':{'positive':54,'negative':144},'fits':fit_reports,
        'prototype_trust_region':regularization,'trainer_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'sha256':{name:hashlib.sha256((out/name).read_bytes()).hexdigest() for name in ['training-features.npy','training-labels.npy','training-examples.jsonl']}}
    (out/'training-expansion.json').write_text(json.dumps(report,indent=2)+'\n')
    print('WAKE_TRAIN_EXPANSION_DONE',len(features),'multiplier',multiplier,flush=True)
    return result,fitted_metadata,report
