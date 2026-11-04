"""Real-recording control targets with phone boundaries and time-indexed F0.

Waveforms remain native and immutable. Analysis resampling is explicitly for
features, never for the output bank. No teacher model/audio is used here.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ProcessPoolExecutor
import hashlib
import json
import math
from pathlib import Path
import re
import sqlite3
import time

import numpy as np
import parselmouth
from scipy import signal
import soundfile as sf

from .corpus import read_textgrid, normalize_token, SILENCE
from .bank import digest_file

KNOTS = 24
MAX_PHONES = 6


def labels_file(path):
    result = {}
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        match = re.match(r'^(\d{6})\s+(.+)', line)
        if match:
            result[match[1]] = match[2]
    return result


def text_alignment(text, count):
    characters = re.findall(r'[\u3400-\u9fff]', text)
    if len(characters) != count:
        return ['']*count, np.zeros(count, np.int64), False
    boundary = np.zeros(count, np.int64)
    position = -1
    for match in re.finditer(r'[\u3400-\u9fff]|#[1-4]', text):
        if match[0].startswith('#'):
            if position >= 0:
                boundary[position] = int(match[0][1])
        else:
            position += 1
    return characters, boundary, True


def sample_pitch(times, hz, strength, query):
    """Sample actual time positions; never compact away unvoiced intervals."""
    index = np.searchsorted(times, query).clip(0, len(times)-1)
    previous = np.maximum(index-1, 0)
    index = np.where(abs(times[previous]-query) < abs(times[index]-query), previous, index)
    valid = (abs(times[index]-query) <= .008) & (hz[index] > 0) & (strength[index] >= .6)
    return np.where(valid, hz[index], 0), valid.astype(np.float32)


def analyse(job):
    uid, split, wav, expected_sha256, grid, text, output = job
    if digest_file(Path(wav)) != expected_sha256:
        raise ValueError('Original source recording changed')
    tiers = read_textgrid(Path(grid))
    words = [x for x in tiers['words'] if x.label not in SILENCE]
    phones = tiers.get('phones', [])
    if not phones:
        return dict(id=uid, skipped='missing_phone_alignment')
    audio, rate = sf.read(wav, dtype='float64')
    if audio.ndim != 1:
        raise ValueError('Non-mono source')
    divisor = math.gcd(rate, 16000)
    analysis = signal.resample_poly(audio, 16000//divisor, rate//divisor)
    sound = parselmouth.Sound(analysis, 16000)
    pitch = sound.to_pitch_ac(time_step=.005, pitch_floor=75, pitch_ceiling=600,
                              voicing_threshold=.5, octave_jump_cost=.35)
    times = pitch.xs()
    hz = pitch.selected_array['frequency']
    strength = pitch.selected_array['strength']
    if not len(times):
        return dict(id=uid, skipped='no_pitch_frames')
    tokens, starts, ends = [], [], []
    phone_labels = []
    phone_durations = []
    f0, voiced = [], []
    phone_knots = []
    for word in words:
        token = normalize_token(word.label)
        parts = [(p.label, max(p.start, word.start), min(p.end, word.end)) for p in phones
                 if p.start < word.end-1e-6 and p.end > word.start+1e-6]
        if (not parts or len(parts) > MAX_PHONES or any(b-a <= 0 for _,a,b in parts) or
                abs(sum(b-a for _,a,b in parts)-(word.end-word.start)) > .002):
            return dict(id=uid, skipped='phone_alignment_incomplete_or_too_many_parts')
        if any(label in SILENCE for label,_,_ in parts):
            return dict(id=uid, skipped='silence_inside_syllable')
        query = word.start+(np.arange(KNOTS)+.5)/KNOTS*(word.end-word.start)
        values, mask = sample_pitch(times, hz, strength, query)
        f0.append(np.log(np.maximum(values, 75)))
        voiced.append(mask)
        phone_knots.append(np.searchsorted([b for _,_,b in parts], query).clip(0,len(parts)-1))
        phone_labels.append([p[0] for p in parts]+['']*(MAX_PHONES-len(parts)))
        phone_durations.append([b-a for _,a,b in parts]+[0.]*(MAX_PHONES-len(parts)))
        tokens.append(token); starts.append(word.start); ends.append(word.end)
    characters, boundaries, aligned = text_alignment(text, len(tokens))
    # Full-utterance feature sequence remains available for subsequent unit/edge
    # encoders; the 24-knot supervision is not the only stored acoustic evidence.
    width, hop = 400, 80
    frames = np.lib.stride_tricks.sliding_window_view(analysis, width)[::hop]
    spectrum = abs(np.fft.rfft(frames*np.hanning(width), n=1024))**2
    frequency = np.fft.rfftfreq(1024, 1/16000)
    bands = np.geomspace(70, 7900, 25)
    features = np.stack([np.log(np.maximum(spectrum[:, (frequency >= a) & (frequency < b)].mean(1), 1e-12))
                         for a,b in zip(bands,bands[1:])], 1)
    if not np.isfinite(features).all():
        raise ValueError('Invalid full-utterance acoustic features')
    frame_times = (np.arange(len(frames))*hop+width/2)/16000
    frame_hz, frame_voiced = sample_pitch(times, hz, strength, frame_times)
    path = Path(output)/f'{uid}.npz'
    np.savez_compressed(path, tokens=np.array(tokens), starts=np.array(starts), ends=np.array(ends),
                        phones=np.array(phone_labels), durations=np.array(phone_durations,np.float32),
                        log_f0=np.array(f0,np.float32), voiced=np.array(voiced,np.float32),
                        phone_knots=np.array(phone_knots,np.int64), characters=np.array(characters),
                        boundaries=boundaries, frame_times=frame_times.astype(np.float32),
                        acoustic_features=features.astype(np.float16),
                        frame_f0=frame_hz.astype(np.float32), frame_voiced=frame_voiced.astype(np.float32),
                        source_rate=np.array(rate), source_pcm_sha256=np.array(hashlib.sha256(Path(wav).read_bytes()).hexdigest()))
    return dict(id=uid, split=split, units=len(tokens), phones=sum(x > 0 for row in phone_durations for x in row),
                aligned_characters=aligned, path=path.name, sha256=digest_file(path),
                mean_voiced_fraction=float(np.mean(voiced)), source=wav, text=text,
                alignment_sha256=digest_file(Path(grid)))


def prepare(database, alignments, labels, output, workers=8, maximum=None):
    if output.exists():
        raise FileExistsError('Choose a fresh control dataset directory')
    output.mkdir(parents=True)
    texts = labels_file(labels)
    db = sqlite3.connect(f'file:{database.resolve().as_posix()}?mode=ro', uri=True)
    sources = list(db.execute('SELECT id,split,path,sha256 FROM utterances ORDER BY id'))
    db.close()
    if maximum:
        sources = sources[:maximum]
    jobs = [(uid, split, wav, sha256, str(alignments/f'{uid}.TextGrid'), texts.get(uid,''), str(output))
            for uid,split,wav,sha256 in sources]
    receipts = []; started = time.monotonic()
    with ProcessPoolExecutor(max_workers=workers) as pool:
        for row in pool.map(analyse, jobs, chunksize=4):
            receipts.append(row)
            if len(receipts)%100 == 0:
                print(json.dumps(dict(processed=len(receipts),total=len(jobs),seconds=time.monotonic()-started)),flush=True)
    manifest = dict(schema=1, knots=KNOTS, max_phones=MAX_PHONES,
                    source_index_sha256=digest_file(database), source_labels_sha256=digest_file(labels),
                    pitch_analysis='Praat autocorrelation 75-600Hz; 5ms; voiced strength >=0.6',
                    parselmouth_version=parselmouth.__version__, analysis_rate=16000,
                    teacher_supervised=False, waveform_generator=False, quality_passed=False,
                    elapsed_seconds=time.monotonic()-started, records=receipts)
    (output/'dataset.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(complete=True,records=len(receipts),skipped=sum('skipped' in x for x in receipts),
                         seconds=manifest['elapsed_seconds'])),flush=True)
    return manifest


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--database',type=Path,required=True);p.add_argument('--alignments',type=Path,required=True)
    p.add_argument('--labels',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--workers',type=int,default=8);p.add_argument('--maximum',type=int)
    a=p.parse_args();prepare(a.database,a.alignments,a.labels,a.output,a.workers,a.maximum)
