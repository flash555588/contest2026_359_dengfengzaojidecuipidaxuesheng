"""Build a lossless, utterance-disjoint syllable inventory from Baker and MFA.

Native PCM stays in the original WAV files. The index references offsets; it
does not resynthesize, denoise, normalize, pitch shift, or compress audio.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sqlite3

import numpy as np
from scipy import fft, signal
import soundfile as sf

SCHEMA = 1
SILENCE = {'', 'sil', 'sp', 'spn', '<eps>'}


def encode_features(features: dict) -> bytes:
    values = [features['duration'], *features['f0'], features['voiced'], features['log_rms'],
              features['gap_before'], features['gap_after'], *features['left'], *features['right']]
    return np.asarray(values, dtype='<f4').tobytes()


def decode_features(encoded) -> dict:
    if isinstance(encoded, str):
        return json.loads(encoded)
    values = np.frombuffer(encoded, dtype='<f4')
    if len(values) != 56 or not np.isfinite(values).all():
        raise ValueError('Invalid compact unit features')
    return dict(duration=float(values[0]), f0=values[1:4].tolist(), voiced=float(values[4]),
                log_rms=float(values[5]), gap_before=float(values[6]), gap_after=float(values[7]),
                left=values[8:32].tolist(), right=values[32:56].tolist())


@dataclass(frozen=True)
class Interval:
    start: float
    end: float
    label: str


def read_textgrid(path: Path) -> dict[str, list[Interval]]:
    text = path.read_text(encoding='utf-8-sig')
    tiers = {}
    for block in re.split(r'(?m)^\s*item\s*\[\d+\]:\s*$', text)[1:]:
        name = re.search(r'\bname\s*=\s*"([^"]*)"', block)
        if not name:
            continue
        intervals = []
        previous = -1e-9
        for m in re.finditer(
            r'intervals\s*\[\d+\]:\s*xmin\s*=\s*([\d.eE+\-]+)\s*'
            r'xmax\s*=\s*([\d.eE+\-]+)\s*text\s*=\s*"((?:[^"\n]|"")*)"', block):
            start, end = float(m[1]), float(m[2])
            if not math.isfinite(start + end) or start < 0 or end <= start or start < previous - 1e-6:
                raise ValueError('Invalid/overlapping interval in ' + str(path))
            intervals.append(Interval(start, end, m[3].replace('""', '"')))
            previous = end
        if name[1] in tiers:
            raise ValueError('Duplicate TextGrid tier')
        tiers[name[1]] = intervals
    if not tiers.get('words'):
        raise ValueError('No words tier in ' + str(path))
    return tiers


def normalize_token(token: str) -> str:
    token = token.lower().strip().replace('u:', 'v').replace('ü', 'v')
    if not re.fullmatch(r'[a-zv]+[0-5]', token):
        raise ValueError('Unsupported syllable label: ' + repr(token))
    return token[:-1] + ('5' if token[-1] == '0' else token[-1])


def split_for(utterance: str) -> str:
    value = int(hashlib.sha256(('baker-unittts-v1:' + utterance).encode()).hexdigest()[:8], 16) % 100
    return 'test' if value < 5 else 'validation' if value < 10 else 'train'


def pitch_track(audio: np.ndarray, rate: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Conservative autocorrelation pitch; low-confidence frames are masked."""
    g = math.gcd(rate, 12000)
    y = signal.resample_poly(audio.astype(np.float64), 12000 // g, rate // g)
    length, hop = 600, 120
    if len(y) < length:
        return np.zeros(0), np.zeros(0), np.zeros(0)
    frames = np.lib.stride_tricks.sliding_window_view(y, length)[::hop].copy()
    frames -= frames.mean(axis=1, keepdims=True)
    energy = np.mean(frames * frames, axis=1)
    frames *= np.hanning(length)
    spec = fft.rfft(frames, n=2048, axis=1)
    ac = fft.irfft(spec * spec.conj(), n=2048, axis=1)[:, :length]
    window_ac = signal.correlate(np.hanning(length), np.hanning(length), mode='full')[length-1:]
    ac /= np.maximum(window_ac[None, :], 1e-6)
    ac /= np.maximum(ac[:, :1], 1e-20)
    low, high = 12000 // 500, 12000 // 75
    peaks = ac[:, low:high]
    lag = peaks.argmax(axis=1) + low
    confidence = ac[np.arange(len(lag)), lag]
    # Prefer the earliest near-best local maximum to reduce octave-down errors.
    local = (peaks[:, 1:-1] > peaks[:, :-2]) & (peaks[:, 1:-1] >= peaks[:, 2:])
    good = local & (peaks[:, 1:-1] >= confidence[:, None] * .92)
    has = good.any(axis=1)
    lag[has] = good[has].argmax(axis=1) + low + 1
    confidence = ac[np.arange(len(lag)), lag]
    hz = 12000 / lag.astype(np.float64)
    confidence[(confidence < .65) | (energy < 1e-7)] = 0
    hz[confidence == 0] = 0
    times = (np.arange(len(hz)) * hop + length / 2) / 12000
    return times, hz, np.clip(confidence, 0, 1)


def edge_descriptor(audio: np.ndarray, rate: int, point: int, side: str) -> list[float]:
    width = max(32, round(rate * .025))
    a, b = (point, point + width) if side == 'left' else (point - width, point)
    frame = audio[max(0, a):min(len(audio), b)].astype(np.float64)
    if len(frame) < width:
        frame = np.pad(frame, (0, width-len(frame)))
    power = np.abs(fft.rfft(frame * np.hanning(width), n=2048)) ** 2
    hz = fft.rfftfreq(2048, 1 / rate)
    bounds = np.geomspace(70, min(11000, rate/2), 25)
    bands = np.array([power[(hz >= a) & (hz < b)].mean() if np.any((hz >= a) & (hz < b)) else 0
                      for a, b in zip(bounds[:-1], bounds[1:])])
    bands = np.log(np.maximum(bands, 1e-12))
    bands -= bands.mean()
    return bands.astype(np.float32).tolist()


def extract_utterance(job: tuple[str, str]) -> dict:
    wav, grid = map(Path, job)
    tiers = read_textgrid(grid)
    if any(x.label == '<unk>' for x in tiers['words']):
        return dict(id=wav.stem, quarantined='unknown_alignment_label')
    audio, rate = sf.read(wav, dtype='float64', always_2d=True)
    if audio.shape[1] != 1:
        raise ValueError('Only mono source WAV is accepted: ' + str(wav))
    audio = audio[:, 0]
    intervals = [x for x in tiers['words'] if x.label not in SILENCE]
    if not intervals or intervals[-1].end > len(audio) / rate + .002:
        raise ValueError('Alignment does not match waveform duration: ' + str(wav))
    times, pitches, confidence = pitch_track(audio, rate)
    units = []
    for ordinal, interval in enumerate(intervals):
        start, end = round(interval.start * rate), round(interval.end * rate)
        if end <= start or end > len(audio):
            raise ValueError('Out-of-range source offset')
        block = audio[start:end]
        mask = (times >= interval.start) & (times < interval.end) & (pitches > 0)
        f0 = pitches[mask]
        if len(f0) >= 3:
            f0s = [float(np.median(x)) for x in np.array_split(f0, 3)]
            voiced = float(np.mean(confidence[mask]))
        else:
            f0s, voiced = [0., 0., 0.], 0.
        before = interval.start - intervals[ordinal-1].end if ordinal else interval.start
        after = intervals[ordinal+1].start - interval.end if ordinal+1 < len(intervals) else len(audio)/rate-interval.end
        units.append(dict(token=normalize_token(interval.label), start=start, end=end,
                          duration=(end-start)/rate, f0=f0s, voiced=voiced,
                          log_rms=float(np.log(max(np.sqrt(np.mean(block**2)), 1e-7))),
                          gap_before=max(0., before), gap_after=max(0., after),
                          left=edge_descriptor(audio, rate, start, 'left'),
                          right=edge_descriptor(audio, rate, end, 'right')))
    return dict(id=wav.stem, path=str(wav.resolve()), sha256=hashlib.sha256(wav.read_bytes()).hexdigest(), rate=rate, frames=len(audio),
                subtype=sf.info(wav).subtype, split=split_for(wav.stem), units=units)


def build(wave_root: Path, alignment_root: Path, output: Path, workers: int = 8, limit: int = 0) -> dict:
    if output.exists():
        raise FileExistsError('Refusing to replace an existing corpus index: ' + str(output))
    waves = sorted(wave_root.rglob('*.wav'))
    grids = {p.stem: p for p in alignment_root.rglob('*.TextGrid')}
    if limit:
        waves = waves[:limit]
    if not waves:
        raise ValueError('No WAV files found')
    missing = [p.stem for p in waves if p.stem not in grids]
    if missing:
        raise ValueError('Missing alignments: ' + repr(missing[:20]))
    output.parent.mkdir(parents=True, exist_ok=True)
    partial = output.with_suffix('.building.sqlite')
    if partial.exists():
        raise FileExistsError('Previous partial build exists; choose another output')
    connection = sqlite3.connect(partial)
    connection.executescript('''
        PRAGMA journal_mode=WAL;
        CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE utterances (id TEXT PRIMARY KEY, path TEXT NOT NULL, sha256 TEXT NOT NULL, rate INTEGER NOT NULL,
            frames INTEGER NOT NULL, subtype TEXT NOT NULL, split TEXT NOT NULL);
        CREATE TABLE units (id INTEGER PRIMARY KEY, utterance TEXT NOT NULL, ordinal INTEGER NOT NULL,
            token TEXT NOT NULL, start INTEGER NOT NULL, end INTEGER NOT NULL,
            features TEXT NOT NULL, UNIQUE(utterance, ordinal));
        CREATE INDEX units_token ON units(token);
        CREATE INDEX units_source ON units(utterance, ordinal);
    ''')
    counts = {s: dict(utterances=0, units=0) for s in ('train', 'validation', 'test')}
    quarantined = []
    tokens = set()
    jobs = [(str(w), str(grids[w.stem])) for w in waves]
    try:
        with ProcessPoolExecutor(max_workers=workers) as pool:
            for done, entry in enumerate(pool.map(extract_utterance, jobs, chunksize=8), 1):
                if 'quarantined' in entry:
                    quarantined.append(entry)
                    continue
                connection.execute('INSERT INTO utterances VALUES (?,?,?,?,?,?,?)',
                                   tuple(entry[k] for k in ('id', 'path', 'sha256', 'rate', 'frames', 'subtype', 'split')))
                for ordinal, unit in enumerate(entry['units']):
                    connection.execute('INSERT INTO units(utterance,ordinal,token,start,end,features) VALUES (?,?,?,?,?,?)',
                        (entry['id'], ordinal, unit['token'], unit['start'], unit['end'], json.dumps(unit, separators=(',', ':'))))
                    tokens.add(unit['token'])
                counts[entry['split']]['utterances'] += 1
                counts[entry['split']]['units'] += len(entry['units'])
                if done % 100 == 0:
                    connection.commit()
                    print(json.dumps(dict(utterances=done, total=len(jobs))), flush=True)
        report = dict(schema=SCHEMA, counts=counts, tokens=len(tokens), quarantined=quarantined,
                      wave_root=str(wave_root.resolve()), alignment_root=str(alignment_root.resolve()),
                      waveform_transform='none; source PCM referenced by offsets',
                      library_split='train', quality_passed=False)
        connection.execute('INSERT INTO metadata VALUES (?,?)', ('manifest', json.dumps(report)))
        connection.commit()
        connection.execute('PRAGMA wal_checkpoint(TRUNCATE)')
        connection.close()
        os.replace(partial, output)
        output.with_suffix('.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        return report
    except BaseException:
        connection.close()
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--waves', type=Path, required=True)
    parser.add_argument('--alignments', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=8)
    parser.add_argument('--limit', type=int, default=0)
    args = parser.parse_args()
    print(json.dumps(build(args.waves, args.alignments, args.output, args.workers, args.limit), indent=2))
