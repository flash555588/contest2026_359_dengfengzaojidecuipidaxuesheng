#!/usr/bin/env python3
"""Reconstruct this pinned Piper decoder and gate it against its ONNX oracle.

This project-owned bridge fills a missing upstream tool. Never emits an init
checkpoint unless all requested reference comparisons pass.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

import numpy as np
import onnx
from onnx import numpy_helper
import onnxruntime as ort
import torch
from train_roota_piper_decoder_student import DecoderStudent, initialize_piperlite_from_teacher


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--decoder',type=Path,required=True)
    p.add_argument('--pack-dir',type=Path,required=True)
    p.add_argument('--out-dir',type=Path,required=True)
    p.add_argument('--rows',type=int,default=32)
    p.add_argument('--export-checkpoint',action='store_true')
    a=p.parse_args();a.out_dir.mkdir(parents=True,exist_ok=True)
    graph=onnx.load(str(a.decoder));state={}
    for tensor in graph.graph.initializer:
        key=tensor.name
        if key.startswith('dec.'):key=key[4:]
        m=re.fullmatch(r'resblocks\.(\d+)\.convs\.(\d+)\.(weight|bias)',key)
        if m:
            branch,conv=int(m[1]),int(m[2])
            key=f'stages.{branch//3}.branches.{branch%3}.convs.{conv}.{m[3]}'
        if key.startswith(('conv_pre.','conv_post.','ups.','stages.')):
            state[key]=torch.from_numpy(numpy_helper.to_array(tensor).copy())
    config={'architecture':'project.piper_onnx_reference','channels':[256,128,64,32],
            'onnx_sha256':hashlib.sha256(a.decoder.read_bytes()).hexdigest()}
    temporary=a.out_dir/'teacher-unverified.pt'
    torch.save({'state_dict':state,'config':config},temporary)
    model=DecoderStudent(in_channels=192,channels=(256,128,64,32),res_layers=1,variant='piperlite')
    initialize_piperlite_from_teacher(model,temporary,(256,128,64,32),'first');model.eval()
    options=ort.SessionOptions();options.intra_op_num_threads=4
    session=ort.InferenceSession(str(a.decoder),sess_options=options,providers=['CPUExecutionProvider'])
    rows=json.loads((a.pack_dir/'rows.json').read_text());checks=[]
    with torch.inference_mode():
        for row in rows[:a.rows]:
            for chunk in row['chunks']:
                with np.load(chunk['tensor_npz']) as pack:
                    latent=np.asarray(pack['generator_input'],dtype=np.float32)
                # Exercise boundary padding and multiple latent lengths.
                for frames in [1,7,min(48,latent.shape[-1])]:
                    x=latent[...,:frames].copy()
                    ref=session.run(None,{session.get_inputs()[0].name:x})[0].reshape(-1)
                    got=model(torch.from_numpy(x)).numpy().reshape(-1)
                    if got.shape!=ref.shape:raise RuntimeError('Teacher reconstruction shape mismatch')
                    diff=got.astype(np.float64)-ref.astype(np.float64)
                    maximum=float(np.max(np.abs(diff)));rmse=float(np.sqrt(np.mean(diff*diff)))
                    checks.append({'row_id':row['row_id'],'frames':frames,'max_abs_error':maximum,'rmse':rmse})
                    if not np.isfinite(diff).all() or maximum>1e-4 or rmse>2e-5:
                        raise RuntimeError('Reconstructed teacher does not match ONNX: '+json.dumps(checks[-1]))
    if not checks:raise RuntimeError('No teacher parity cases')
    report={'passed':True,'source':config,'checks':checks,'max_abs_error':max(x['max_abs_error'] for x in checks),
            'rmse_max':max(x['rmse'] for x in checks),'project_owned_bridge':True}
    (a.out_dir/'parity-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if a.export_checkpoint:
        target=a.out_dir/'piper-decoder-teacher.pt'
        torch.save({'state_dict':state,'config':config,'parity':report},target)
    print('PIPER_TEACHER_PARITY_OK',len(checks),report['max_abs_error'],flush=True)


if __name__=='__main__':main()
