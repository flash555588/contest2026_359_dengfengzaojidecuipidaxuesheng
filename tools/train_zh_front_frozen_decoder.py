#!/usr/bin/env python3
"""Train only the Chinese acoustic front through a frozen official decoder.

The project-owned adaptation adds latent context around each supervised audio
crop; it leaves pinned upstream sources and inference checkpoint formats intact.
"""
import argparse
import hashlib
import importlib
import json
import random
from pathlib import Path
import sys
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared',type=Path,required=True)
    parser.add_argument('--trained',type=Path,required=True)
    parser.add_argument('--decoder',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--steps',type=int,default=3000)
    parser.add_argument('--context',type=int,default=16)
    parser.add_argument('--acoustic',type=Path,help='Optional completed acoustic warm-start')
    parser.add_argument('--balanced-initials',action='store_true',
                        help='Focus s/sh/z/zh/r/g/m/b crop centers and include real utterance boundaries')
    args=parser.parse_args()
    if args.steps<1 or not 1<=args.context<=32:raise ValueError('Invalid steps/context')
    import torch
    torch.set_num_threads(4)
    if not torch.cuda.is_available():raise RuntimeError('CUDA is required for this experiment')
    prepared=args.prepared.resolve();trained=args.trained.resolve();out=args.output.resolve()
    manifest=json.loads((prepared/'prepared.json').read_text())
    binding=json.loads((trained/'input-binding.json').read_text())
    fingerprint=hashlib.sha256(json.dumps(manifest,sort_keys=True).encode()).hexdigest()
    if binding['sha256']!=fingerprint:raise ValueError('Prepared/trained binding differs')
    tools=prepared/'upstream/tools'
    for row in manifest['upstream']['sources']:
        if sha(prepared/'upstream'/row['path'])!=row['sha256']:raise ValueError('Changed upstream source')
    if out.exists():raise ValueError('Choose a fresh output directory')
    out.mkdir(parents=True)
    acoustic=args.acoustic or trained/'voice/acoustic/latent-student.pt'
    cut=trained/'voice/decoder-cut/zh_CN-xiao_ya-medium-decoder-from-generator-input.onnx'
    decoder_before=torch.load(args.decoder,map_location='cpu',weights_only=False)['model_state_dict']
    inputs=[Path(__file__),acoustic,args.decoder,cut,trained/'voice/train512-decoder/rows.json']
    report={'status':'running','quality_passed':False,'decoder_frozen':True,
        'context_latent_frames_per_side':args.context,'supervised_crop_frames':64,
        'steps':args.steps,'prepared_binding':binding,
        'balanced_initials':args.balanced_initials,
        'input_sha256':{str(p):sha(p) for p in inputs},
        'scope':'Only acoustic parameters train; fixed full official teacher decoder and existing durations; no deployment'}
    receipt=out/'frozen-receipt.json'
    def save():receipt.write_text(json.dumps(report,indent=2)+'\n')
    save();started=time.monotonic()
    sys.path.insert(0,str(tools))
    module=importlib.import_module('train_roota_joint_z_finetune')
    load_original=module.load_decoder_checkpoint
    batch_original=module.joint_z_crop_batch
    context=args.context;hop=256
    active={'left':context,'right':context,'mode':'middle'}
    selection_counts={'middle':0,'start':0,'end':0,'focused':{},'uniform':0}
    original_select=module.joint_common.select_crops
    focus_index={}
    config=json.loads((prepared/'teacher/zh_CN-xiao_ya-medium.onnx.json').read_text())
    focus_ids={int(config['phoneme_id_map'][symbol][0]):symbol for symbol in ['s','sh','z','zh','r','g','m','b']}
    def balanced_select(samples,batch_size,crop_frames):
        eligible=[sample for sample in samples if sample.latent.shape[-1]>=crop_frames]
        if not eligible:raise RuntimeError('No eligible context crops')
        if not focus_index:
            for sample in samples:
                offset=0
                for ident,duration in zip(sample.phoneme_ids,sample.durations):
                    duration=int(duration);ident=int(ident)
                    if ident in focus_ids and duration>0:
                        focus_index.setdefault(ident,[]).append((sample,offset+duration//2))
                    offset+=duration
        crops=[]
        for _ in range(batch_size):
            sample=random.choice(eligible);frames=sample.latent.shape[-1]
            if active['mode']=='start':start=0
            elif active['mode']=='end':start=frames-crop_frames
            elif random.random()<.75:
                ident=random.choice(sorted(focus_index))
                choices=[item for item in focus_index[ident] if item[0].latent.shape[-1]>=crop_frames]
                if not choices:raise RuntimeError('Focused initial has no eligible context')
                sample,center=random.choice(choices);frames=sample.latent.shape[-1]
                start=max(0,min(frames-crop_frames,center-active['left']-32+random.randint(-12,12)))
                symbol=focus_ids[ident]
                selection_counts['focused'][symbol]=selection_counts['focused'].get(symbol,0)+1
            else:
                start=random.randint(0,frames-crop_frames);selection_counts['uniform']+=1
            selection_counts[active['mode']]+=1
            crops.append(module.joint_common.Crop(sample=sample,start=start,end=start+crop_frames))
        return crops
    if args.balanced_initials:module.joint_common.select_crops=balanced_select
    def load_frozen(path,device):
        model,config=load_original(path,device)
        model.requires_grad_(False)
        # Train() subsequently calls .train(); this architecture contains only
        # convolutions and deterministic activations, with no batchnorm/dropout.
        def center_output(_model,_inputs,value):
            if not isinstance(value,torch.Tensor):raise RuntimeError('Unexpected decoder feature return')
            end=-active['right']*hop if active['right'] else None
            return value[...,active['left']*hop:end]
        model.register_forward_hook(center_output)
        return model,config
    def contextual_batch(samples,**kwargs):
        mode=random.random() if args.balanced_initials else .5
        active.update(left=0 if mode<.15 else context,right=0 if mode>.85 else context,
                      mode='start' if mode<.15 else ('end' if mode>.85 else 'middle'))
        kwargs['crop_frames']+=active['left']+active['right']
        predicted,teacher,target=batch_original(samples,**kwargs)
        end=-active['right']*hop if active['right'] else None
        return predicted,teacher,target[...,active['left']*hop:end]
    module.load_decoder_checkpoint=load_frozen
    module.joint_z_crop_batch=contextual_batch
    command=['frozen-front','--pack-dir',str(trained/'voice/train512-decoder'),
        '--teacher-decoder',str(cut),'--acoustic-checkpoint',str(acoustic),
        '--decoder-checkpoint',str(args.decoder),'--out-dir',str(out),
        '--steps',str(args.steps),'--crop-frames','64','--batch-size','4',
        '--acoustic-lr','0.00002','--decoder-lr','0.00001',
        '--spectral-weight','1','--waveform-l1-weight','0.1','--z-anchor-weight','0.5',
        '--adv-weight','0','--adv-feature-weight','0','--adv-delta-weight','0',
        '--adv-delta-feature-weight','0','--duration-params','34436','--device','cuda']
    report['upstream_arguments']=command;save()
    old_argv=sys.argv;sys.argv=command
    try:
        train_args=module.parse_args()
        result=module.train(train_args)
        if result['parameter_accounting']['decoder_trainable_parameters']!=0:
            raise RuntimeError('Decoder unexpectedly trained')
        if [x['name'] for x in result['optimizer_groups']]!=['acoustic']:
            raise RuntimeError('Unexpected optimizer groups')
        after=torch.load(out/'decoder-student.pt',map_location='cpu',weights_only=False)['model_state_dict']
        if set(after)!=set(decoder_before) or any(not torch.equal(after[k],v) for k,v in decoder_before.items()):
            raise RuntimeError('Frozen decoder weights changed')
        report.update(status='completed',decoder_weights_bit_exact=True,
            crop_selection_counts=selection_counts,
            acoustic_trainable_parameters=result['parameter_accounting']['acoustic_trainable_parameters'],
            output_sha256={name:sha(out/name) for name in ['latent-student.pt','decoder-student.pt','train-report.json']})
    except BaseException as exc:
        report.update(status='failed',error=repr(exc));raise
    finally:
        sys.argv=old_argv;report['elapsed_seconds']=time.monotonic()-started;save()


if __name__=='__main__':main()
