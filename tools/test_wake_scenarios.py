#!/usr/bin/env python3
"""Exercise the production wake detector with continuous synthetic audio.

This is a development stress test, not human evaluation or a measured room
false-alarm rate. Profiles and gates are fixed before inspecting their scores.
No device access, microphone recording, online TTS or model fitting occurs.
"""
import argparse
import ctypes
import hashlib
import json
import subprocess
import time
from pathlib import Path

import numpy as np
from wake_streaming import stage_sources
from wake_augmentation import RATE, PROFILES, audio_profile


class Detector:
    def __init__(self, lib, root):
        self.lib = lib
        self.state = ctypes.c_void_p()
        ret = lib.glass_custom_wake_create(str(root).encode(), ctypes.byref(self.state))
        if ret:
            raise RuntimeError('Wake model loader failed: ' + str(ret))

    def close(self):
        if self.state:
            self.lib.glass_custom_wake_destroy(self.state)
            self.state = ctypes.c_void_p()

    def feed(self, pcm, chunks=(1023,), offset=0):
        events = []
        at = 0
        step = 0
        sp = ctypes.POINTER(ctypes.c_int16)
        while at < len(pcm):
            count = min(chunks[step % len(chunks)], len(pcm) - at)
            block = pcm[at:at + count]
            ret = self.lib.glass_custom_wake_feed(self.state, block.ctypes.data_as(sp), count)
            if ret < 0:
                raise RuntimeError('Streaming inference failed: ' + str(ret))
            if ret:
                events.append(offset + at + count)
            at += count
            step += 1
        return events


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['corpus', 'model', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--continuous-minutes', type=float, default=15)
    parser.add_argument('--asan', action='store_true', help='Replay long streams in a native ASan/UBSan runner')
    args = parser.parse_args()
    if not 1 <= args.continuous_minutes <= 60:
        parser.error('Continuous duration must be 1..60 minutes')
    args.output.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[1]
    desktop = repo / 'app/espdl-quickapp/overlay/apps/system/desktop'
    model = args.model / 'nihao_openvela.gkw'
    trained = json.loads((args.model / 'training.json').read_text(encoding='utf-8'))
    if not trained['synthetic_gate_passed'] or hashlib.sha256(model.read_bytes()).hexdigest() != trained['model_sha256']:
        raise ValueError('Model does not match the accepted training record')
    import shutil
    root = args.output / 'runtime'
    (root / 'custom').mkdir(parents=True, exist_ok=True)
    shutil.copyfile(model, root / 'custom/nihao_openvela.gkw')
    sources = stage_sources(desktop, args.output / 'sources', args.model / 'glass_wake_model.h')
    library = args.output / 'scenarios.so'
    subprocess.run(['gcc', '-shared', '-fPIC', '-O3', '-I' + str(desktop),
                    *map(str, sources), '-lm', '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.glass_custom_wake_create.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.glass_custom_wake_feed.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t]
    lib.glass_custom_wake_destroy.argtypes = [ctypes.c_void_p]
    manifest = json.loads((args.corpus / 'corpus.json').read_text(encoding='utf-8'))
    rows = [row for row in manifest['records'] if row['split'] == 'test']
    if not rows or not any(row['label'] for row in rows) or not any(not row['label'] for row in rows):
        raise ValueError('Both test classes are required')
    audio = {}
    for row in rows:
        data = (args.corpus / row['pcm']).read_bytes()
        if hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Corrupt input ' + row['pcm'])
        audio[row['pcm']] = np.frombuffer(data, dtype='<i2').copy()
    seed = 20261017
    rng = np.random.default_rng(seed)
    report = {'synthetic_only': True, 'human_wake_tested': False,
              'test_is_development_evidence': True, 'human_false_alarms_per_hour': None,
              'model_sha256': trained['model_sha256'], 'seed': seed,
              'source_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
              'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'augmentation_sha256': hashlib.sha256(Path(__file__).with_name('wake_augmentation.py').read_bytes()).hexdigest(),
              'corpus_sha256': hashlib.sha256((args.corpus / 'corpus.json').read_bytes()).hexdigest(),
              'profiles': {}, 'chunk_invariance': {}, 'continuous_negative': {}}
    started = time.monotonic()
    clean_events = {}
    for profile in PROFILES:
        positive, negative, detected, false = 0, 0, 0, 0
        failures = []
        for row in rows:
            pcm = audio_profile(audio[row['pcm']], profile, rng)
            detector = Detector(lib, root)
            try:
                events = detector.feed(pcm)
            finally:
                detector.close()
            if profile == 'clean':
                clean_events[row['pcm']] = len(events)
            if row['label']:
                positive += 1
                detected += len(events) == 1
            else:
                negative += 1
                false += len(events) > 0
            if len(events) != row['label']:
                failures.append({'pcm': row['pcm'], 'text': row['text'], 'label': row['label'], 'events': len(events)})
        report['profiles'][profile] = {'positive': positive, 'detected': detected, 'negative': negative,
                                       'false_positive_clips': false, 'failures': failures,
                                       'recall': detected / positive, 'false_positive_fraction': false / negative}
        (args.output / 'scenarios.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print('WAKE_SCENARIO', profile, detected, positive, false, negative, flush=True)
    # Use a real irregular producer pattern, including a one-sample fragment,
    # and maximum accepted blocks. Event endpoints are rounded to feed blocks;
    # event counts, rather than these rounded timestamps, must be invariant.
    for name, chunks in [('irregular', (1, 7, 319, 320, 511, 1023)), ('max_block', (4096,))]:
        mismatches = []
        for row in rows:
            detector = Detector(lib, root)
            try:
                pcm = audio_profile(audio[row['pcm']], 'clean', rng)
                events = detector.feed(pcm, chunks)
            finally:
                detector.close()
            if len(events) != clean_events[row['pcm']]:
                mismatches.append({'pcm': row['pcm'], 'events': len(events), 'reference': clean_events[row['pcm']]})
        report['chunk_invariance'][name] = {'cases': len(rows), 'mismatches': mismatches}
        print('WAKE_CHUNKS', name, len(rows), len(mismatches), flush=True)
    # Explicitly cover speech arriving at the first captured sample, rather
    # than relying on the quiet prefix provided by TTS and room profiles.
    startup_detected, startup_positive = 0, 0
    startup_failures = []
    for row in [r for r in rows if r['label']]:
        sound = audio[row['pcm']]
        first = 0
        while first + 320 < len(sound) and np.sqrt(np.mean(sound[first:first + 320].astype(float) ** 2)) < 150:
            first += 320
        pcm = np.concatenate([sound[first:], np.zeros(int(1.2 * RATE), dtype=np.int16)])
        detector = Detector(lib, root)
        try:
            count = len(detector.feed(pcm, (496,)))
        finally:
            detector.close()
        startup_positive += 1
        startup_detected += count == 1
        if count != 1:
            startup_failures.append({'pcm': row['pcm'], 'events': count})
    report['startup_no_quiet_margin'] = {'positive': startup_positive, 'detected': startup_detected, 'failures': startup_failures}
    print('WAKE_STARTUP', startup_detected, startup_positive, flush=True)
    negative_rows = [row for row in rows if not row['label']]
    duration = int(args.continuous_minutes * 60 * RATE)
    for profile in ['clean', 'noise_20db']:
        detector = Detector(lib, root)
        cursor, segments = 0, []
        stream_file = (args.output / (profile + '-continuous.pcm')).open('wb') if args.asan else None
        try:
            while cursor < duration:
                row = negative_rows[int(rng.integers(len(negative_rows)))]
                pcm = audio_profile(audio[row['pcm']], profile, rng)
                # Short gaps prevent resetting segmentation implicitly between
                # phrases; the detector state persists for the whole stream.
                pcm = pcm[int(.4 * RATE):-int(.9 * RATE)]
                pcm = pcm[:duration - cursor]
                if stream_file:
                    stream_file.write(pcm.tobytes())
                events = detector.feed(pcm, (496, 512, 1023), cursor)
                segments.append({'pcm': row['pcm'], 'start_sample': cursor, 'samples': len(pcm), 'event_samples': events})
                cursor += len(pcm)
        finally:
            detector.close()
            if stream_file:
                stream_file.close()
        count = sum(len(segment['event_samples']) for segment in segments)
        report['continuous_negative'][profile] = {'seconds': cursor / RATE, 'segments': len(segments),
                                                  'events': count, 'sequence': segments}
        print('WAKE_CONTINUOUS', profile, cursor / RATE, count, flush=True)
    report['long_stream_asan_ubsan'] = None
    if args.asan:
        binary = args.output / 'long-stream-host'
        subprocess.run(['gcc', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I' + str(args.output / 'sources'), '-I' + str(desktop),
                        str(repo / 'app/espdl-quickapp/tests/custom_wake_stream_host.c'),
                        *map(str, sources), '-lm', '-o', str(binary)], check=True)
        run = subprocess.run([str(binary), str(root), str(args.output / 'clean-continuous.pcm'),
                              str(args.output / 'noise_20db-continuous.pcm')], check=True, capture_output=True, text=True)
        (args.output / 'long-stream-asan.log').write_text(run.stdout + run.stderr)
        report['long_stream_asan_ubsan'] = {'passed': True, 'output': run.stdout}
        print(run.stdout, flush=True)
    report['elapsed_host_seconds'] = round(time.monotonic() - started, 3)
    report['stress_gate_passed'] = (
        all(value['recall'] >= .85 and value['false_positive_fraction'] <= .01 for value in report['profiles'].values())
        and all(not value['mismatches'] for value in report['chunk_invariance'].values())
        and startup_detected / startup_positive >= .85
        and all(not value['events'] for value in report['continuous_negative'].values()))
    (args.output / 'scenarios.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('WAKE_STRESS_COMPLETE', report['stress_gate_passed'], flush=True)
    if not report['stress_gate_passed']:
        raise SystemExit('Stress gate failed; retain the installed model until investigated')


if __name__ == '__main__':
    main()
