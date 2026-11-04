"""Run official PaddleSpeech ONNX exports as a separate desktop teacher baseline."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import time

import numpy as np
import onnxruntime as ort
import soundfile as sf

from .corpus import SILENCE, normalize_token, read_textgrid


def phones_from_pinyin(tokens: list[str]) -> list[str]:
    """Explicit syllables to Paddle's initial/final symbols. No text G2P is implied."""
    initials = ('zh', 'ch', 'sh', 'b', 'p', 'm', 'f', 'd', 't', 'n', 'l', 'g', 'k', 'h', 'j', 'q', 'x', 'r', 'z', 'c', 's')
    y_finals = dict(yi='i', ya='ia', yao='iao', ye='ie', you='iou', yan='ian', yin='in',
                    yang='iang', ying='ing', yong='iong', yu='v', yue='ve', yuan='van', yun='vn')
    w_finals = dict(wu='u', wa='ua', wo='uo', wai='uai', wei='uei', wan='uan', wen='uen', wang='uang', weng='ueng')
    result = []
    for raw in tokens:
        token = normalize_token(raw)
        base, tone = token[:-1], token[-1]
        initial = ''
        if base in y_finals:
            final = y_finals[base]
        elif base in w_finals:
            final = w_finals[base]
        else:
            initial = next((x for x in initials if base.startswith(x)), '')
            final = base[len(initial):]
            final = {'iu':'iou', 'ui':'uei', 'un':'uen'}.get(final, final)
            if initial in ('j', 'q', 'x'):
                final = {'u':'v', 'ue':'ve', 'uan':'van', 'uen':'vn'}.get(final, final)
            if initial in ('n', 'l') and final == 'ue':
                final = 've'
            if final == 'i' and initial in ('z', 'c', 's'):
                final = 'ii'
            if final == 'i' and initial in ('zh', 'ch', 'sh', 'r'):
                final = 'iii'
        if initial:
            result.append(initial)
        result.append(final+tone)
    return result


class Teacher:
    def __init__(self, acoustic: Path, vocoder: Path, phone_map: Path):
        self.phone_map = dict(line.split() for line in phone_map.read_text().splitlines() if line.strip())
        self.phone_map = {k:int(v) for k,v in self.phone_map.items()}
        options = ort.SessionOptions()
        options.intra_op_num_threads = 4
        options.inter_op_num_threads = 1
        self.acoustic = ort.InferenceSession(str(acoustic), options, providers=['CPUExecutionProvider'])
        self.vocoder = ort.InferenceSession(str(vocoder), options, providers=['CPUExecutionProvider'])
        self.hashes = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (acoustic,vocoder,phone_map)}

    def synthesize(self, phones: list[str], output: Path, source: Path | None = None):
        if output.exists():
            raise FileExistsError('Refusing to overwrite teacher audition')
        unknown = [p for p in phones if p not in self.phone_map or p == '<unk>']
        if unknown:
            raise ValueError('Unsupported exact teacher phones: ' + repr(unknown))
        ids = np.array([self.phone_map[p] for p in phones], np.int64)
        start = time.monotonic()
        mel = self.acoustic.run(None, {self.acoustic.get_inputs()[0].name:ids})[0]
        wave = self.vocoder.run(None, {self.vocoder.get_inputs()[0].name:mel})[0].reshape(-1)
        if not len(wave) or not np.isfinite(wave).all():
            raise RuntimeError('Teacher returned invalid audio')
        output.mkdir(parents=True)
        sf.write(output/'teacher.wav', wave, 24000, subtype='PCM_16')
        if source:
            original, rate = sf.read(source, dtype='int16')
            sf.write(output/'original.wav', original, rate, subtype='PCM_16')
        report = dict(backend='official PaddleSpeech FastSpeech2 + HiFi-GAN ONNX exports',
                      hashes=self.hashes, phones=phones, sample_rate=24000,
                      seconds=len(wave)/24000, elapsed_seconds=time.monotonic()-start,
                      peak_float=float(np.abs(wave).max()), samples_outside_pcm_range=int((np.abs(wave)>=1).sum()),
                      training_checkpoint_parity_verified=False, quality_passed=False,
                      source=str(source) if source else None)
        (output/'teacher.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--acoustic', type=Path, required=True)
    parser.add_argument('--vocoder', type=Path, required=True)
    parser.add_argument('--phone-map', type=Path, required=True)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--textgrid', type=Path)
    inputs.add_argument('--pinyin')
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.textgrid:
        phones = [x.label for x in read_textgrid(args.textgrid)['phones'] if x.label not in ('', 'spn')]
    else:
        phones = phones_from_pinyin(args.pinyin.split())+['sp']
    teacher = Teacher(args.acoustic, args.vocoder, args.phone_map)
    print(json.dumps(teacher.synthesize(phones, args.output, args.source), indent=2))
