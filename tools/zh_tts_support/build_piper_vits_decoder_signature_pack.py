#!/usr/bin/env python3
"""Project-owned bridge for the missing upstream ONNX signature exporter."""
import argparse
import json
from pathlib import Path

import numpy as np
import onnx
from onnx import helper,TensorProto
import onnxruntime as ort
import torch
from train_roota_piper_decoder_student import pooled_activation_signature

NODE_LABELS={
    '/Mul_7_output_0':'latent',
    '/dec/conv_pre/Conv_output_0':'conv_pre',
    '/dec/ups.0/ConvTranspose_output_0':'up0_raw',
    '/dec/Div_output_0':'stage0_mix',
    '/dec/Div_1_output_0':'stage1_mix',
    '/dec/Div_2_output_0':'stage2_mix',
    '/dec/conv_post/Conv_output_0':'pre_tanh',
    '/dec/Tanh_output_0':'audio',
}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--model',type=Path,required=True)
    p.add_argument('--pack-dir',type=Path,required=True)
    p.add_argument('--out-dir',type=Path,required=True)
    p.add_argument('--feed-latent-from-pack',action='store_true',required=True)
    p.add_argument('--dtype',choices=['float16','float32'],default='float16')
    p.add_argument('--node',action='append',required=True)
    a=p.parse_args();a.out_dir.mkdir(parents=True,exist_ok=True)
    model=onnx.load(str(a.model));latent=model.graph.input[0].name
    present={x.name for x in model.graph.output};requested=[]
    for name in a.node:
        if name not in NODE_LABELS:raise ValueError('Unmapped signature node '+name)
        if name==latent:continue
        if name not in present:model.graph.output.append(helper.make_tensor_value_info(name,TensorProto.FLOAT,None))
        requested.append(name)
    labels={NODE_LABELS[x] for x in requested}
    if not {'stage0_mix','stage1_mix','stage2_mix','pre_tanh','audio'}<=labels:
        raise ValueError('Missing decoder signature labels')
    options=ort.SessionOptions();options.intra_op_num_threads=4
    session=ort.InferenceSession(model.SerializeToString(),sess_options=options,providers=['CPUExecutionProvider'])
    rows=json.loads((a.pack_dir/'rows.json').read_text());records=[]
    dtype=np.float16 if a.dtype=='float16' else np.float32
    with torch.inference_mode():
        for row in rows:
            for index,chunk in enumerate(row['chunks']):
                source=Path(chunk['tensor_npz']).resolve()
                with np.load(source) as pack:x=np.asarray(pack['generator_input'],dtype=np.float32)
                frames=x.shape[-1];outputs=session.run(requested,{latent:x})
                payload={'latent_frames':np.array([frames],dtype=np.int64)}
                for name,value in zip(requested,outputs):
                    value=np.asarray(value,dtype=np.float32)
                    if value.ndim==2:value=value[:,None,:]
                    if value.ndim!=3 or value.shape[0]!=1 or not np.isfinite(value).all():
                        raise ValueError('Invalid ONNX signature '+name)
                    mean,logrms=pooled_activation_signature(torch.from_numpy(value),frames)
                    for suffix,tensor in [('mean',mean),('logrms',logrms)]:
                        payload[NODE_LABELS[name]+'_'+suffix]=tensor[0].numpy().astype(dtype)
                target=a.out_dir/f'{row["row_id"]}-chunk{index:02d}.npz'
                np.savez_compressed(target,**payload)
                records.append({'source_tensor_npz':str(source),'signature_npz':str(target.resolve())})
                print('SIGNATURE',len(records),row['row_id'],flush=True)
    if not records:raise RuntimeError('No signature rows')
    (a.out_dir/'decoder-signature-manifest.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in records),encoding='utf-8')
    (a.out_dir/'summary.json').write_text(json.dumps({'passed':True,'chunks':len(records),'dtype':a.dtype,
      'nodes':{n:NODE_LABELS[n] for n in requested},'project_owned_bridge':True},indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
