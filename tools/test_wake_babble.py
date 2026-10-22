#!/usr/bin/env python3
"""Replay overlapping synthetic speech through the production wake detector.

This measures development fixtures, never real television or room false alarms.
No microphone, network, model fitting, or device writes are used.
"""
import argparse
import ctypes
import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np
from test_wake_scenarios import Detector
from wake_streaming import stage_sources

RATE = 16000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--seconds', type=int, default=60)
    parser.add_argument('--streams', type=int, default=4)
    args = parser.parse_args()
    if not 10 <= args.seconds <= 600 or not 1 <= args.streams <= 32:
        parser.error('seconds must be 10..600 and streams 1..32')
    repo = Path(__file__).resolve().parents[1]
    desktop = repo / 'app/espdl-quickapp/overlay/apps/system/desktop'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sources = stage_sources(args.source or desktop, output / 'source')
    library = output / 'babble.so'
    subprocess.run(['gcc', '-shared', '-fPIC', '-O3', '-I'+str(output / 'source'), '-I'+str(desktop),
                    *map(str, sources), '-lm', '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.glass_custom_wake_create.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.glass_custom_wake_feed.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t]
    lib.glass_custom_wake_destroy.argtypes = [ctypes.c_void_p]
    lib.glass_custom_wake_evaluations.argtypes = [ctypes.c_void_p]
    model = repo / 'app/espdl-quickapp/models/nihao_openvela.gkw'
    runtime = output / 'runtime/custom'
    runtime.mkdir(parents=True, exist_ok=True)
    (runtime / model.name).write_bytes(model.read_bytes())
    manifest = args.corpus / 'corpus.json'
    rows = json.loads(manifest.read_text(encoding='utf-8'))['records']
    rows = [r for r in rows if r['split'] in ['validation', 'test']]
    audio = {}
    for row in rows:
        data = (args.corpus / row['pcm']).read_bytes()
        if hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Corrupt fixture ' + row['pcm'])
        raw = np.frombuffer(data, dtype='<i2').astype(float)
        # Retain word-internal pauses; remove only outer quiet margins.
        voiced = np.flatnonzero(np.abs(raw) > 300)
        if len(voiced):
            raw = raw[max(0, voiced[0]-320):min(len(raw), voiced[-1]+321)]
        audio[row['pcm']] = raw
    negatives = [r for r in rows if not r['label']]
    positives = [r for r in rows if r['label']]
    if not positives or len({r['speaker'] for r in negatives}) < 2:
        raise ValueError('Need positive fixtures and at least two negative speakers')
    rng = np.random.default_rng(20261021)

    def speech_track(speaker_rows, size):
        parts, count = [], 0
        while count < size:
            row = speaker_rows[int(rng.integers(len(speaker_rows)))]
            raw = audio[row['pcm']]
            raw = raw * (1500 / max(1, np.sqrt(np.mean(raw**2))))
            gap = np.zeros(int(rng.uniform(.06, .45)*RATE))
            parts.extend([raw, gap])
            count += len(raw)+len(gap)
        return np.concatenate(parts)[:size]

    def measure(sound):
        detector = Detector(lib, runtime.parent)
        try:
            events = detector.feed(np.clip(sound, -32768, 32767).astype('<i2'), (320,))
            return {'event_seconds': [round(t/RATE, 3) for t in events],
                    'evaluations': int(lib.glass_custom_wake_evaluations(detector.state))}
        finally:
            detector.close()

    report = {'synthetic_only': True, 'human_room_tested': False,
              'human_false_alarms_per_hour': None, 'seed': 20261021,
              'model_sha256': hashlib.sha256(model.read_bytes()).hexdigest(),
              'corpus_sha256': hashlib.sha256(manifest.read_bytes()).hexdigest(),
              'source_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
              'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'negative_streams': [], 'positive_over_babble': []}
    speakers = sorted({r['speaker'] for r in negatives})
    for index in range(args.streams):
        size = args.seconds*RATE
        tracks = [speech_track([r for r in negatives if r['speaker'] == speaker], size)
                  for speaker in speakers[:2]]
        for name, raw in [('single_voice', tracks[0]), ('two_voices', tracks[0]+.7*tracks[1])]:
            sound = np.pad(raw, (RATE, RATE*2))
            result = measure(sound)
            report['negative_streams'].append({'kind': name, 'stream': index,
                                               'seconds': len(sound)/RATE, **result})
            print('BABBLE_NEGATIVE', name, index, len(result['event_seconds']), flush=True)
    for ratio in [0, .25, .5]:
        detected = 0
        for row in positives:
            raw = audio[row['pcm']]
            raw = raw*(1500/max(1, np.sqrt(np.mean(raw**2))))
            # Keep the babble running across the wake phrase and its endpoint.
            size = len(raw)+RATE*3
            bed = speech_track(negatives, size)
            sound = ratio*bed
            sound[RATE:RATE+len(raw)] += raw
            result = measure(np.pad(sound, (RATE, RATE)))
            phrase_start, phrase_end = 2, 2+len(raw)/RATE
            # Late detections from a different background phrase do not count.
            detected += any(phrase_start <= t <= phrase_end+1.2 for t in result['event_seconds'])
        report['positive_over_babble'].append({'background_gain': ratio,
                                               'positive': len(positives), 'detected': detected})
        print('BABBLE_POSITIVE', ratio, detected, len(positives), flush=True)
    (output / 'babble.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('BABBLE_REPLAY_COMPLETE: synthetic development evidence; real-room acceptance pending', flush=True)


if __name__ == '__main__':
    main()
