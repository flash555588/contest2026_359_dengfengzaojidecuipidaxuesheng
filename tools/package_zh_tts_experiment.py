#!/usr/bin/env python3
"""Save a reviewable Chinese TTS research bundle; no publication or board writes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['prepared','trained','fixture','output']:p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--board-report',type=Path)
    a=p.parse_args();out=a.output;out.mkdir(parents=True,exist_ok=True)
    prepared=json.loads((a.prepared/'prepared.json').read_text())
    quant=json.loads((a.trained/'quantize-status.json').read_text())
    if quant['status']!='experiment_completed' or not quant.get('front_quantization_gate_passed') or not quant.get('decoder_quantization_gate_passed'):
        raise ValueError('Both quantization gates must be completed')
    fingerprint=hashlib.sha256(json.dumps(prepared,sort_keys=True).encode()).hexdigest()
    if quant['inputs_sha256']!=fingerprint:raise ValueError('Prepared inputs do not match the model')
    fixture=json.loads((a.fixture/'manifest.json').read_text())
    for row in fixture['sources']:
        if sha(a.fixture/row['path'])!=row['sha256']:raise ValueError('Changed fixture: '+row['path'])
    if fixture['inputs_sha256']!=fingerprint:raise ValueError('Fixture belongs to different inputs')
    prior=out/'manifest.json'
    if prior.is_file() and json.loads(prior.read_text())['inputs_sha256']!=fingerprint:
        raise ValueError('Choose a fresh output directory for a changed model')
    records=[]
    def copy(source,relative):
        target=out/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
        records.append({'path':relative,'bytes':target.stat().st_size,'sha256':sha(target)})
    for row in quant['blobs']:
        source=Path(row['path'])
        if sha(source)!=row['sha256']:raise ValueError('Changed quantized blob')
        name=source.name
        if sha(a.fixture/'model'/name)!=row['sha256']:raise ValueError('Fixture weights differ')
        copy(source,'model/'+name)
    for source,relative in [(a.prepared/'prepared.json','provenance/prepared.json'),
        (a.trained/'quantize-status.json','checks/quantization.json'),
        (a.trained/'asr-check.json','checks/asr-floating-student.json'),
        (a.prepared/'corpora/full.jsonl','provenance/corpus.jsonl'),
        (a.prepared/'corpora/acceptance.jsonl','provenance/acceptance.jsonl'),
        (a.fixture/'fixture.json','provenance/p4-fixture.json'),
        (a.trained/'front_heldout_c_gates.log','checks/front-c.log'),
        (a.trained/'decoder_heldout_c_gates.log','checks/decoder-c.log')]:copy(source,relative)
    training=a.trained/'coverage-status.json'
    if not training.is_file():training=a.trained/'full-status.json'
    if training.is_file():copy(training,'provenance/training.json')
    host=a.fixture.with_name(a.fixture.name+'-host')
    host_verification=json.loads((host/'verification.json').read_text())
    copy(host/'verification.json','checks/host-reference-parity.json')
    for source in (a.fixture/'notices').iterdir():copy(source,'notices/'+source.name)
    for name in ['snt_front_q8.c','snt_piperlite_q8.c']:copy(a.fixture/'generated'/name,'runtime/'+name)
    for name in ['snt_front_q8.h','snt_piperlite_q8.h']:copy(a.fixture/'vendor'/name,'runtime/'+name)
    desktop=Path(__file__).resolve().parents[1]/'app/espdl-quickapp/overlay/apps/system/desktop'
    for name,digest in host_verification['project_source_sha256'].items():
        if sha(desktop/name)!=digest:raise ValueError('Project runtime changed after host verification: '+name)
    for name in ['glass_zh_q8.c','glass_zh_q8.h','glass_zh_q8_pie.S','glass_speech_compute.c','glass_speech_compute.h']:
        copy(desktop/name,'runtime/'+name)
    for lane in ['teacher','student']:
        for index in [2,18,19,23]:copy(a.trained/'audition'/lane/(str(index).zfill(5)+'.wav'),'audio/'+lane+'-'+str(index).zfill(2)+'.wav')
    import numpy as np
    import soundfile as sf
    pcm=np.fromfile(host/'zh-q8-host.f32',dtype='<f4')
    wave=out/'audio/c-q8-nihao.wav';sf.write(wave,np.clip(pcm,-1,1),22050,subtype='PCM_16')
    records.append({'path':'audio/c-q8-nihao.wav','bytes':wave.stat().st_size,'sha256':sha(wave),
                    'source_float_sha256':sha(host/'zh-q8-host.f32')})
    if a.board_report:
        board=json.loads(a.board_report.read_text())
        if not board.get('restored'):raise ValueError('Board experiment has not restored its original firmware')
        expected={Path(row['path']).name:row['sha256'] for row in quant['blobs']}
        if board.get('model_sha256')!=expected:raise ValueError('Board report belongs to different model weights')
        copy(a.board_report,'checks/board.json')
    readme=out/'README.txt'
    readme.write_text('''Custom Chinese distilled research model; NOT an official quantized voice release.
Teacher: official zh_CN-xiao_ya-medium. Retain notices/TEACHER_MODEL_CARD and its non-commercial source-data restriction.
Weights: per-channel INT8. Activations: mainly 12-bit values in INT16, final stage 15-bit; accumulation INT32, some operations FP32.
Sample rate 22050 Hz. Four blobs in model/ are the inputs to the portable C adapter.
API: glass_zh_q8_synthesize(blobs, ids, count, backend, pcm, cancel, user, stats).
backend: 0 scalar, 1 PIE, 2 dual-core PIE. P4 uses the supplied int16 assembly; host builds use the scalar reference dot.
ids must use the teacher phoneme table. This bundle does not install a complete arbitrary-text frontend.
Maximum 128 IDs / 512 latent frames per chunk; working-buffer budget 8 MiB. Larger PIE workspaces fall back to scalar.
checks/asr-floating-student.json evaluates the floating student with Whisper base, not a human listening test or the INT8 waveform.
Quantized C/Python numerical gates and source/adapted PCM parity are recorded separately.
audio/c-q8-nihao.wav comes from the complete quantized C adapter, converted from its float PCM to PCM16 for preview.
All artifacts remain experimental; production voice quality has not been approved.
''',encoding='utf-8')
    records.append({'path':'README.txt','bytes':readme.stat().st_size,'sha256':sha(readme)})
    manifest={'inputs_sha256':fingerprint,'official_quantized_model':False,'quality_passed':False,
        'parameters':quant['inference_state_parameters'],'model_bytes':quant['total_quantized_bytes'],
        'files':records}
    prior.write_text(json.dumps(manifest,indent=2)+'\n')
    archive=out.with_suffix('.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(out.rglob('*')):
            if path.is_file():z.write(path,str(path.relative_to(out)))
    print('ZH_TTS_RESEARCH_BUNDLE',archive,sha(archive))
if __name__=='__main__':main()
