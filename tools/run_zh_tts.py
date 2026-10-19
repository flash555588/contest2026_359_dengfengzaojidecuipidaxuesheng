#!/usr/bin/env python3
"""Run a Chinese distillation probe or the pinned upstream tiny recipe.

Run with the dedicated Linux CUDA Python environment. Probe is a plumbing
experiment with 32 training + 4 held-out texts, NOT an intelligibility gate.
No board access and no publication or paid compute are performed.
"""
import argparse
import atexit
import hashlib
import json
import os
from pathlib import Path
import shutil
import random
import subprocess
import sys
import time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--phase',choices=['packs','probe','smoke-packs','smoke','full','quantize','refine','coverage'],default='probe')
    p.add_argument('--base-trained',type=Path,help='Completed training directory for acoustic-capacity refinement')
    p.add_argument('--warm-start',type=Path,help='Acoustic checkpoint used after training-coverage expansion')
    p.add_argument('--duration-steps',type=int,default=4000)
    p.add_argument('--acoustic-steps',type=int)
    p.add_argument('--joint-steps',type=int,default=4000)
    p.add_argument('--teacher-python',type=Path,help='Optional separate CPU data-preparation interpreter')
    p.add_argument('--device',choices=['cuda','cpu'],default='cuda')
    p.add_argument('--dry-run',action='store_true')
    a=p.parse_args();prepared=a.prepared.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    source=prepared/'upstream';teacher=prepared/'teacher/zh_CN-xiao_ya-medium.onnx'
    config=teacher.with_suffix('.onnx.json')
    manifest=json.loads((prepared/'prepared.json').read_text(encoding='utf-8'))
    for row in manifest.get('support_sources',[]):
        if hashlib.sha256((source/row['path']).read_bytes()).hexdigest()!=row['sha256']:
            raise ValueError('Changed project-owned training bridge: '+row['path'])
    fingerprint=hashlib.sha256(json.dumps(manifest,sort_keys=True).encode()).hexdigest()
    binding=out/'input-binding.json'
    if binding.is_file() and json.loads(binding.read_text())['sha256']!=fingerprint:
        raise ValueError('Training inputs changed; choose a fresh output directory to avoid stale teacher packs/checkpoints')
    binding.write_text(json.dumps({'sha256':fingerprint,'prepared':str(prepared)},indent=2)+'\n',encoding='utf-8')
    for name,row in manifest['corpora'].items():
        if hashlib.sha256((prepared/'corpora'/(name+'.jsonl')).read_bytes()).hexdigest()!=row['sha256']:
            raise ValueError('Changed train/eval corpus: '+name)
    for label,base in [('upstream',source),('teacher',prepared/'teacher')]:
        for row in manifest[label]['sources']:
            path=base/row['path'] if label=='upstream' else base/Path(row['path']).name
            if hashlib.sha256(path.read_bytes()).hexdigest()!=row['sha256']:
                raise ValueError('Changed training input: '+str(path))
    env=os.environ.copy();env.update(PYTHONUNBUFFERED='1',OMP_NUM_THREADS='4',MKL_NUM_THREADS='4',OPENBLAS_NUM_THREADS='4')
    records=[];report={'phase':a.phase,'device':a.device,'status':'running','stages':records,
                       'quality_passed':False,'board_written':False,'official_quantized_model':False,
                       'inputs_sha256':fingerprint,'driver_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    def save():
        (out/'status.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        (out/(a.phase+'-status.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    save()
    def unfinished_exit():
        if report['status']=='running':
            report['status']='failed'
            report['error']='Process exited before all requested stages completed; inspect the last stage log.'
            save()
    atexit.register(unfinished_exit)
    def run(name,args,marker=None,native=False):
        # Resolving a venv's python symlink would invoke the system interpreter
        # and lose its installed packages. Keep the venv entry-point path.
        interpreter=os.path.abspath(a.teacher_python) if name.startswith('teacher_') and a.teacher_python else sys.executable
        command=[*([] if native else [interpreter]),*map(str,args)]
        input_hashes={str(path):hashlib.sha256(path.read_bytes()).hexdigest()
                      for arg in args if (path:=Path(str(arg))).is_file() and path!=marker}
        print('ZH_STAGE',name,flush=True)
        # Skip only a stage carrying our successful command-and-output receipt.
        receipt=out/(name+'.receipt.json')
        if marker and marker.is_file() and receipt.is_file():
            old=json.loads(receipt.read_text())
            if old['command']==command and old.get('input_hashes')==input_hashes and old['marker_sha256']==hashlib.sha256(marker.read_bytes()).hexdigest():
                records.append(old);save();return
        if a.dry_run:
            print(json.dumps(command,ensure_ascii=False),flush=True);return
        started=time.monotonic()
        with (out/(name+'.log')).open('w',encoding='utf-8') as log:
            result=subprocess.run(command,cwd=source,env=env,stdout=log,stderr=subprocess.STDOUT)
        row={'name':name,'command':command,'input_hashes':input_hashes,'exit_code':result.returncode,'elapsed_seconds':time.monotonic()-started}
        records.append(row);save()
        if result.returncode:
            report['status']='failed';save();raise RuntimeError('Chinese stage failed: '+str(out/(name+'.log')))
        if marker:
            if not marker.is_file():raise RuntimeError('Missing successful output: '+str(marker))
            row['marker_sha256']=hashlib.sha256(marker.read_bytes()).hexdigest()
            receipt.write_text(json.dumps(row,indent=2)+'\n',encoding='utf-8')
    if not a.dry_run and a.phase not in ['packs','smoke-packs']:
        import torch
        torch.set_num_threads(4)
        if a.device=='cuda' and not torch.cuda.is_available():raise RuntimeError('CUDA is unavailable; do not silently train on CPU')
        x=torch.randn(128,128,device=a.device,requires_grad=True);loss=(x@x.T).square().mean();loss.backward()
        conv_x=torch.randn(2,16,64,device=a.device,requires_grad=True)
        conv_w=torch.randn(16,16,5,device=a.device,requires_grad=True)
        torch.nn.functional.conv1d(conv_x,conv_w,padding=2).square().mean().backward()
        if a.device=='cuda':torch.cuda.synchronize()
        report['torch']=torch.__version__
        report['gpu']=torch.cuda.get_device_name(0) if a.device=='cuda' else None
        report['backprop_finite']=bool(torch.isfinite(x.grad).all() and torch.isfinite(conv_x.grad).all() and torch.isfinite(conv_w.grad).all())
        report['cuda_backprop_finite']=report['backprop_finite'] if a.device=='cuda' else None
        report['cuda_preflight_peak_allocated_bytes']=torch.cuda.max_memory_allocated() if a.device=='cuda' else None
        save()
        if not report['backprop_finite']:raise RuntimeError('Nonfinite backpropagation')
    tools=source/'tools';corpus=prepared/'corpora/smoke.jsonl'
    vocab=max(i for ids in json.loads(config.read_text())['phoneme_id_map'].values() for i in ids)+1
    if a.phase in ['probe','packs']:
        common=['--model',teacher,'--config',config,'--source-jsonl',corpus,
                '--allow-text-only-source','--noise-scale','0','--length-scale','1','--noise-w','0','--tensor-mode','acoustic']
        train=out/'train32';evaluate=out/'eval4'
        run('teacher_train32',[tools/'build_piper_vits_roota_probe_pack.py',*common,'--max-rows','32','--skip-rows','140','--out-dir',train],train/'rows.json')
        run('teacher_eval4',[tools/'build_piper_vits_roota_probe_pack.py',*common,'--max-rows','4','--skip-rows','12','--out-dir',evaluate],evaluate/'rows.json')
        if a.phase=='packs':
            report['scope']='CPU teacher output extraction only; no student training'
            report['status']='dry-run' if a.dry_run else 'experiment_completed';save()
            print('ZH_TEACHER_PACKS_FINISHED',out/'status.json',flush=True);return
        duration=out/'duration/duration-student.pt';acoustic=out/'acoustic/latent-student.pt'
        run('duration200',[tools/'train_roota_piper_duration_student.py','--pack-dir',train,'--eval-pack-dir',evaluate,
                          '--out-dir',duration.parent,'--hidden','32','--depth','3','--kernel-size','5',
                          '--duration-vocab-size',vocab,'--duration-oov-id','0','--batch-size','8','--steps','200','--device',a.device],duration)
        run('acoustic200',[tools/'train_roota_piper_latent_student.py','--pack-dir',train,'--eval-pack-dir',evaluate,
                          '--out-dir',acoustic.parent,'--architecture','token_context','--hidden','48','--token-depth','3',
                          '--depth','3','--kernel-size','5','--vocab-size',vocab,'--steps','200','--skip-render','--device',a.device],acoustic)
        golden=out/'front-q8'
        run('front_float_golden',[tools/'export_front_golden.py',duration,acoustic,'--out',golden,'--pack',evaluate],golden/'latent.bin')
        run('front_quantize',[tools/'export_front_q8.py',duration,acoustic,'--out',golden,'--pack',train,'--calib-n','16'],golden/'front_calib_q8.json')
        gate=out/'front-q8-gate'
        run('front_c_build',['gcc','-O2','-g','-std=c99','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                             '-I'+str(source/'mcu/include'),source/'mcu/src/snt_front_q8.c',
                             source/'mcu/test/front_q8_golden_test.c','-lm','-o',gate],gate,native=True)
        run('front_c_gate',[gate,golden],native=True)
        report['front_quantization_gate_passed']=not a.dry_run
        report['scope']='duration/acoustic training + front quantization only; decoder not trained'
    elif a.phase=='quantize':
        voice=out/'voice'
        duration=voice/'duration/duration-student.pt'
        acoustic=voice/'joint/latent-student.pt';decoder=voice/'joint/decoder-student.pt'
        if not a.dry_run:
            for checkpoint in [duration,acoustic,decoder]:
                if not checkpoint.is_file():raise RuntimeError('Missing trained checkpoint: '+str(checkpoint))
        front=out/'front-q8';dec=out/'decoder-q8'
        calibration={}
        for label,training in [('front',voice/'train-acoustic'),('decoder',voice/'train512-decoder')]:
            selected=out/('calibration-'+label)
            calibration[label]=selected
            if not a.dry_run:
                rows=json.loads((training/'rows.json').read_text())
                row_ids=[{i for chunk in row['chunks'] for i in chunk['phoneme_ids']} for row in rows]
                uncovered=set().union(*row_ids);indices=[]
                while uncovered:
                    index=max(range(len(rows)),key=lambda i:len(row_ids[i]&uncovered))
                    if not row_ids[index]&uncovered:raise ValueError('Cannot cover training phonemes for calibration')
                    indices.append(index);uncovered-=row_ids[index]
                count=min(32,len(rows))
                if len(indices)>count:raise ValueError('Calibration budget is insufficient to cover the training inventory')
                remainder=[i for i in range(len(rows)) if i not in indices];random.Random(4242).shuffle(remainder)
                indices+=remainder[:count-len(indices)]
                selected.mkdir(exist_ok=True)
                (selected/'rows.json').write_text(json.dumps([rows[i] for i in indices],ensure_ascii=False)+'\n',encoding='utf-8')
                report.setdefault('calibration',{})[label]={'training_rows_only':True,'rows':len(indices),
                    'indices':indices,'phoneme_ids':sorted(set().union(*(row_ids[i] for i in indices))),
                    'rows_sha256':hashlib.sha256((selected/'rows.json').read_bytes()).hexdigest()}
        for label,exporter,checkpoints,calibration,golden in [
            ('front','front',[duration,acoustic],calibration['front'],front),
            ('decoder','piperlite',[decoder],calibration['decoder'],dec)]:
            run(label+'_float_golden',[tools/('export_'+exporter+'_golden.py'),*checkpoints,
                                      '--pack',voice/'eval128','--out',golden],golden/'golden.json')
            run(label+'_quantize',[tools/('export_'+exporter+'_q8.py'),*checkpoints,
                                  '--pack',calibration,'--calib-n','32','--out',golden],
                golden/('front_calib_q8.json' if label=='front' else 'calib_q8.json'))
            gate=out/(label+'-q8-gate')
            run(label+'_c_build',['gcc','-O2','-g','-std=c99','-fsanitize=address,undefined',
                                 '-fno-omit-frame-pointer','-I'+str(source/'mcu/include'),
                                 source/('mcu/src/snt_'+exporter+'_q8.c'),
                                 source/('mcu/test/'+exporter+'_q8_golden_test.c'),'-lm','-o',gate],gate,native=True)
            run(label+'_c_gate',[gate,golden],native=True)
            checks=[golden]
            for index in [17,63,127]:
                heldout=out/(label+'-q8-row'+str(index))
                run(label+'_golden_row'+str(index),[tools/('export_'+exporter+'_golden.py'),*checkpoints,
                    '--pack',voice/'eval128','--chunk-row',index,'--out',heldout],heldout/'golden.json')
                if not a.dry_run:
                    names=['front_weights_q8.bin','front_meta_q8.bin'] if label=='front' else ['weights_q8.bin','meta_q8.bin']
                    for name in names:shutil.copyfile(golden/name,heldout/name)
                checks.append(heldout)
            run(label+'_heldout_c_gates',[gate,*checks],native=True)
        if not a.dry_run:
            import torch
            report['checkpoint_state_parameters']={name:sum(v.numel() for v in torch.load(path,map_location='cpu',weights_only=False)['model_state_dict'].values())
                for name,path in [('duration',duration),('acoustic',acoustic),('decoder',decoder)]}
            report['inference_state_parameters']=sum(report['checkpoint_state_parameters'].values())
            report['checkpoint_sha256']={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in [duration,acoustic,decoder]}
            blobs=[front/'front_weights_q8.bin',front/'front_meta_q8.bin',dec/'weights_q8.bin',dec/'meta_q8.bin']
            report['blobs']=[{'path':str(f),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in blobs]
            report['total_quantized_bytes']=sum(f.stat().st_size for f in blobs)
        report['front_quantization_gate_passed']=not a.dry_run
        report['decoder_quantization_gate_passed']=not a.dry_run
        report['scope']='Joint checkpoints quantized; held-out C/PyTorch numerical gates only; intelligibility and P4 pending'
    elif a.phase in ['refine','coverage']:
        if not a.base_trained:raise ValueError('--base-trained is required for refinement')
        base=a.base_trained.resolve()
        base_binding=json.loads((base/'input-binding.json').read_text())
        if a.phase=='refine' and base_binding['sha256']!=fingerprint:
            raise ValueError('Base model belongs to different prepared inputs')
        original_prepared=Path(base_binding['prepared'])
        if a.phase=='coverage':
            original_manifest=json.loads((original_prepared/'prepared.json').read_text())
            for label in ['upstream','teacher','support_sources']:
                if original_manifest[label]!=manifest[label]:raise ValueError('Coverage expansion changed teacher or training source: '+label)
            old_rows=[json.loads(x) for x in (original_prepared/'corpora/full.jsonl').read_text(encoding='utf-8').splitlines()]
            new_rows=[json.loads(x) for x in (prepared/'corpora/full.jsonl').read_text(encoding='utf-8').splitlines()]
            if new_rows[:len(old_rows)]!=old_rows:raise ValueError('Coverage phase must preserve the original split and append training only')
            if not a.warm_start or not a.warm_start.is_file():raise ValueError('A completed acoustic --warm-start checkpoint is required')
        voice=out/'voice';voice.mkdir(exist_ok=True)
        for name in (['train-acoustic','train512-decoder','eval128','decoder-cut'] if a.phase=='refine' else ['eval128','decoder-cut']):
            target=voice/name;original=base/'voice'/name
            if not original.is_dir():raise ValueError('Missing base pack: '+str(original))
            if target.exists():
                if target.resolve()!=original.resolve():raise ValueError('Refinement pack differs: '+str(target))
            else:target.symlink_to(original,target_is_directory=True)
        duration=voice/'duration/duration-student.pt';duration.parent.mkdir(exist_ok=True)
        if not duration.exists():shutil.copyfile(base/'voice/duration/duration-student.pt',duration)
        acoustic=voice/'acoustic/latent-student.pt';decoder=base/'voice/decoder/decoder-student.pt'
        if not decoder.is_file():decoder=base/'voice/joint/decoder-student.pt'
        cut=voice/'decoder-cut/zh_CN-xiao_ya-medium-decoder-from-generator-input.onnx'
        report['base_checkpoints']={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in [base/'voice/duration/duration-student.pt',decoder]}
        if a.phase=='coverage':
            extra=voice/'extra-teacher'
            run('teacher_coverage',[tools/'build_piper_vits_roota_probe_pack.py',
                '--model',teacher,'--config',config,'--source-jsonl',prepared/'corpora/full.jsonl',
                '--out-dir',extra,'--max-rows',len(new_rows)-len(old_rows),'--skip-rows',len(old_rows),
                '--tensor-mode','acoustic','--allow-text-only-source','--noise-scale','0','--length-scale','1','--noise-w','0'],extra/'rows.json')
            base_rows=json.loads((base/'voice/train-acoustic/rows.json').read_text())
            extra_rows=json.loads((extra/'rows.json').read_text());combined=base_rows+extra_rows
            allowed={r['text'] for r in new_rows[140:]};heldout={r['text'] for r in new_rows[:140]}
            if len(combined)!=len(allowed) or {r['text'] for r in combined}!=allowed or allowed&heldout:
                raise ValueError('Combined teacher pack does not match the expanded training split')
            ids=set();phonemes=set()
            for row in combined:
                for chunk in row['chunks']:
                    if not Path(chunk['tensor_npz']).is_file():raise ValueError('Missing reused teacher tensor')
                    ids.update(chunk['phoneme_ids']);phonemes.update(chunk['phonemes'])
            if not {'n','c'}.issubset(phonemes):raise ValueError('Expanded training still lacks common consonants')
            pack=voice/'train-acoustic';pack.mkdir(exist_ok=True)
            (pack/'rows.json').write_text(json.dumps(combined,ensure_ascii=False)+'\n',encoding='utf-8')
            target=voice/'train512-decoder'
            if not target.exists():target.symlink_to(pack,target_is_directory=True)
            if target.resolve()!=pack.resolve():raise ValueError('Decoder pack is not the verified combined pack')
            report['coverage']={'training_rows':len(combined),'phoneme_ids':sorted(ids),'phonemes':sorted(phonemes),
                'heldout_overlap':False,'base_rows_sha256':hashlib.sha256((base/'voice/train-acoustic/rows.json').read_bytes()).hexdigest(),
                'extra_rows_sha256':hashlib.sha256((extra/'rows.json').read_bytes()).hexdigest()}
            run('duration_coverage'+str(a.duration_steps),[tools/'train_roota_piper_duration_student.py',
                '--pack-dir',pack,'--eval-pack-dir',voice/'eval128','--out-dir',duration.parent,
                '--initial-checkpoint',base/'voice/duration/duration-student.pt','--hidden','32','--depth','3',
                '--kernel-size','5','--duration-vocab-size',vocab,'--duration-oov-id','0',
                '--steps',a.duration_steps,'--device',a.device],duration)
        warm_args=['--load-checkpoint',a.warm_start] if a.phase=='coverage' else []
        acoustic_steps=a.acoustic_steps or (6000 if a.phase=='coverage' else 8000)
        run(('acoustic_coverage' if a.phase=='coverage' else 'acoustic_h96_')+str(acoustic_steps),[tools/'train_roota_piper_latent_student.py',
            '--pack-dir',voice/'train-acoustic','--eval-pack-dir',voice/'eval128','--out-dir',acoustic.parent,
            '--architecture','token_context','--hidden','96','--token-depth','3','--depth','3',
            '--kernel-size','5','--vocab-size',vocab,'--norm-l1-weight','0.25','--delta-l1-weight','0.10',
            '--channel-stat-weight','0.05','--latent-adv-weight','0.1','--latent-adv-start-step','1500',
            '--decoder',cut,'--steps',acoustic_steps,'--skip-render','--device',a.device,*warm_args],acoustic)
        import torch
        duration_params=sum(v.numel() for v in torch.load(duration,map_location='cpu',weights_only=False)['model_state_dict'].values())
        run('refine_joint'+str(a.joint_steps),[tools/'train_roota_joint_z_finetune.py','--pack-dir',voice/'train512-decoder',
            '--teacher-decoder',cut,'--acoustic-checkpoint',acoustic,'--decoder-checkpoint',decoder,
            '--out-dir',voice/'joint','--z-anchor-weight','0.5','--duration-params',duration_params,
            '--steps',a.joint_steps,'--device',a.device],voice/'joint/decoder-student.pt')
        report['scope']='Expanded training coverage with held-out split preserved; acoustic warm-start and joint finetune, quality pending' if a.phase=='coverage' else 'Increase acoustic hidden size only; retain tiny decoder; fresh acoustic training and joint finetune, quality gates pending'
    else:
        corpus=prepared/'corpora'/('full.jsonl' if a.phase=='full' else 'smoke.jsonl')
        data_only=a.phase=='smoke-packs'
        run('upstream_packs' if data_only else 'upstream_tiny',[tools/'train_voice_from_piper.py','--voice','zh_CN-xiao_ya-medium',
                           '--teacher-onnx',teacher,'--teacher-config',config,'--text',corpus,
                           '--out-dir',out/'voice','--package-name','xiaoya-zh-tiny-p4-'+a.phase,
                           '--size','tiny','--profile','shipped' if a.phase=='full' else 'smoke',
                           '--device',a.device,'--python',sys.executable,'--stop-after','s1' if data_only else 's9'],
            out/'voice/train512-decoder/rows.json' if data_only else out/'voice/joint/decoder-student.pt')
        report['scope']='CPU teacher packs only' if data_only else 'upstream three-student recipe through joint finetune; quality and quantization gates pending'
    report['status']='dry-run' if a.dry_run else 'experiment_completed';save()
    print('ZH_EXPERIMENT_FINISHED',out/'status.json',flush=True)


if __name__=='__main__':main()
