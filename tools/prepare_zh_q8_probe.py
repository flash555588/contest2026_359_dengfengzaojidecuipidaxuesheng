#!/usr/bin/env python3
"""Prepare a provenance-checked, diagnostic-only P4 Chinese Q8 fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared',type=Path,required=True);p.add_argument('--trained',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--text',default='你好。',help='Chinese diagnostic utterance (at most 128 phoneme IDs)')
    a=p.parse_args()
    source=a.prepared/'upstream';out=a.output;out.mkdir(parents=True,exist_ok=True)
    status=json.loads((a.trained/'quantize-status.json').read_text())
    if not status.get('front_quantization_gate_passed') or not status.get('decoder_quantization_gate_passed'):
        raise ValueError('Both C quantization gates must pass before fixture preparation')
    prepared=json.loads((a.prepared/'prepared.json').read_text())
    fingerprint=hashlib.sha256(json.dumps(prepared,sort_keys=True).encode()).hexdigest()
    if status['inputs_sha256']!=fingerprint:raise ValueError('Quantized model belongs to different training inputs')
    records=[]
    def copy(src,target,expected=None):
        if expected and digest(src)!=expected:raise ValueError('Changed fixture input: '+str(src))
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,target)
        records.append({'path':str(target.relative_to(out)),'bytes':target.stat().st_size,'sha256':digest(target)})
    for row in prepared['upstream']['sources']:
        if Path(row['path']).name in ['LICENSE','LICENSE.MIT']:
            copy(source/row['path'],out/'notices'/Path(row['path']).name,row['sha256'])
    teacher_card=a.prepared/'teacher/MODEL_CARD'
    if teacher_card.is_file():copy(teacher_card,out/'notices/TEACHER_MODEL_CARD')
    names=['front_weights_q8.bin','front_meta_q8.bin','weights_q8.bin','meta_q8.bin']
    assembly=['.section .rodata.zh_q8,"a",@progbits']
    for i,row in enumerate(status['blobs']):
        src=Path(row['path']);target=out/'model'/names[i];copy(src,target,row['sha256'])
        assembly.extend(['.balign 16',f'.global zh_q8_blob_{i}',f'zh_q8_blob_{i}:',f'.incbin "{target.resolve().as_posix()}"',f'.global zh_q8_blob_{i}_end',f'zh_q8_blob_{i}_end:'])
    assembly.append('.section .note.GNU-stack,"",@progbits')
    (out/'blobs.S').write_text('\n'.join(assembly)+'\n')
    for row in prepared['upstream']['sources']:
        if row['path'] in ['mcu/src/snt_front_q8.c','mcu/src/snt_piperlite_q8.c','mcu/include/snt_front_q8.h','mcu/include/snt_piperlite_q8.h']:
            copy(source/row['path'],out/'vendor'/Path(row['path']).name,row['sha256'])
    if len(list((out/'vendor').glob('*')))!=4:raise ValueError('Incomplete portable C runtime')
    # Allocation-free reference loops gain cancellation checkpoints only;
    # model arithmetic is unmodified. Keep both original and generated hashes.
    for name,signature in [('snt_front_q8.c','static void fq_conv_row('),('snt_piperlite_q8.c','static void q16_conv')]:
        src=out/'vendor'/name;text=src.read_text()
        text='#include "glass_speech_compute.h"\n'+text
        if name=='snt_front_q8.c':
            start=text.index(signature);opening=text.index('{',start)
            text=text[:opening+1]+'\n    glass_speech_compute_checkpoint();'+text[opening+1:]
        else:
            text='#include "glass_zh_q8.h"\n'+text
            # Wrap complete output-channel loops, so a 64-frame tile is
            # packed once and reused across every output row while in cache.
            for anchor,call in [
                ('float mm = s_z *',
                 'pl[1],NULL,pl[0],m->wq[0],m->wscale[0],b,m->in_ch,m->c0,T,7,1,s_z,s_pre,NULL,0,0'),
                ('float mm = s_t1 *',
                 'pl[p_y],NULL,pl[p_t],m->wq[slot],m->wscale[slot],m->f32[slot+1],out_c[s],out_c[s],L,bk[br],bd1[br],s_t1,s_y1,pl[p_up],s_up/s_y1,0'),
                ('float mm = s_t2 *',
                 'NULL,sumf,pl[p_t],m->wq[slot+2],m->wscale[slot+2],m->f32[slot+3],out_c[s],out_c[s],L,bk[br],bd2[br],s_t2,1.0f,pl[p_y],s_y1,1')]:
                if text.count(anchor)!=1:raise ValueError('Changed decoder stage: '+anchor)
                start=text.rfind('for (oc =',0,text.index(anchor));opening=text.index('{',start)
                depth=1;end=opening+1
                while depth:
                    depth+=(text[end]=='{')-(text[end]=='}');end+=1
                text=text[:start]+'if (!glass_zh_q8_conv1d_stage('+call+')) '+text[start:end]+text[end:]
            anchor='float mm = s_a *'
            if text.count(anchor)!=1:raise ValueError('Changed decoder transpose stage')
            start=text.rfind('for (oc =',0,text.index(anchor));opening=text.index('{',start);depth=1;end=opening+1
            while depth:
                depth+=(text[end]=='{')-(text[end]=='}');end+=1
            call='pl[p_up],pl[p_a],m->wq[wbase],m->wscale[wbase],m->f32[wbase+1],in_c[s],out_c[s],T,up_k[s],up_s[s],up_p[s],L,s_a,s_up'
            text=text[:start]+'if (!glass_zh_q8_convtr_stage('+call+')) '+text[start:end]+text[end:]
            start=text.index('q16_conv1d_row(rowbuf, pl[p_t], m->wq[44]')
            end=text.index('            if (m->stage_cb)',start)
            call='NULL,audio_out,pl[p_t],m->wq[44],m->wscale[44],m->f32[45],m->c3,1,T,7,1,s_ap,1.0f,NULL,0,2'
            text=text[:start]+'if (!glass_zh_q8_conv1d_stage('+call+')) {\n'+text[start:end]+'            }\n'+text[end:]
            for signature in ['static void q16_conv1d_row(','static void q16_convtr1d_row(']:
                start=text.index(signature);opening=text.index('{',start)
                text=text[:opening+1]+'\n    glass_speech_compute_checkpoint();'+text[opening+1:]
        target=out/'generated'/name;target.parent.mkdir(exist_ok=True);target.write_text(text)
        records.append({'path':str(target.relative_to(out)),'bytes':target.stat().st_size,'sha256':digest(target)})
    sys.path.insert(0,str(source/'pypkg'))
    from sanotts.frontend import load_phoneme_table
    from sanotts.zh_g2p import text_to_phoneme_ids
    table=load_phoneme_table(a.prepared/'teacher/zh_CN-xiao_ya-medium.onnx.json')
    text=a.text.strip();ids,unmapped=text_to_phoneme_ids(text,table)
    if unmapped:raise ValueError(unmapped)
    if not text or not 0 < len(ids) <= 128:raise ValueError('Diagnostic text must produce between 1 and 128 phoneme IDs')
    (out/'fixture.json').write_text(json.dumps({'text':text,'ids':ids.tolist(),'sample_rate':22050,'purpose':'diagnostic, not quality acceptance'},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (out/'fixture.h').write_text('#include <stdint.h>\nstatic const int32_t zh_q8_ids[]={'+','.join(map(str,ids.tolist()))+'};\n')
    for name in ['blobs.S','fixture.json','fixture.h']:
        records.append({'path':name,'bytes':(out/name).stat().st_size,'sha256':digest(out/name)})
    (out/'manifest.json').write_text(json.dumps({'diagnostic_only':True,'quality_passed':False,
        'official_quantized_model':False,'inputs_sha256':fingerprint,'model_bytes':status['total_quantized_bytes'],
        'sources':records},indent=2)+'\n')
    print('ZH_Q8_FIXTURE_READY',out/'manifest.json')
if __name__=='__main__':main()
