"""Compile a byte-budgeted, immutable PCM bank from train utterances only."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import io
import json
from pathlib import Path
import sqlite3

import numpy as np

from .corpus import encode_features
from .synthesize import Inventory


def digest_file(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(4*1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()


def compile_bank(database: Path, output: Path, budget: int, context_radius: int = 1,
                 padding_seconds: float = .04, control_data: Path | None = None):
    if output.exists():
        raise FileExistsError('Choose a fresh bank directory; frozen banks are never replaced')
    if budget < 10_000_000:
        raise ValueError('Bank budget too small for this prototype')
    if not 0 <= context_radius <= 3 or not .008 <= padding_seconds <= .10:
        raise ValueError('Invalid continuity radius or analysis padding')
    inv = Inventory(database)
    if inv.bank_id:
        raise ValueError('Compile from the full source corpus, not an already reduced bank')
    control_manifest = None
    control_records = {}
    if control_data:
        control_manifest = json.loads((control_data/'dataset.json').read_text())
        if control_manifest['source_index_sha256'] != digest_file(database):
            raise ValueError('Acoustic controls came from a different source index')
        control_records = {r['id']: r for r in control_manifest['records']
                           if r.get('split') == 'train' and 'skipped' not in r}
    selected = set()
    units_by_id = {u.uid:u for seq in inv.units.values() for u in seq}
    padding = {uid:round(s['rate']*padding_seconds) for uid,s in inv.sources.items()}
    costs = {u.uid:2*(u.end-u.start + round(u.f['gap_before']*inv.sources[u.utterance]['rate'])
                      +2*padding[u.utterance])+1024+(16384 if control_data else 0)
             for u in units_by_id.values()}
    available = int(budget*.93)-1_000_000
    charged = 0

    def add(items):
        nonlocal charged
        # Retain actual adjacent speech, rather than collecting mostly isolated
        # syllables. Analysis padding alone cannot restore coarticulatory units.
        ids = set()
        for unit in items:
            sequence = inv.units[unit.utterance]
            ids.add(unit.uid)
            for direction in (-1,1):
                index = unit.ordinal
                for _ in range(context_radius):
                    adjacent = index+direction
                    if not 0 <= adjacent < len(sequence):break
                    if sequence[max(index,adjacent)].f['gap_before'] > .10:break
                    ids.add(sequence[adjacent].uid)
                    index = adjacent
        ids -= selected
        cost = sum(costs[i] for i in ids)
        if charged+cost > available:
            return False
        selected.update(ids)
        charged += cost
        return True

    representatives = {}
    for token, candidates in sorted(inv.by_token.items()):
        vectors = np.array([inv.target_vector(u) for u in candidates])
        center = np.median(vectors, axis=0)
        scale = np.array([.35,.25,.25,.25,1.])
        scores = np.mean(((vectors-center)/scale)**2, axis=1)
        order = np.argsort(scores, kind='stable')
        representatives[token] = [candidates[i] for i in order]
        if not add([candidates[order[0]]]):
            raise ValueError('Budget cannot retain one exact example of every train label')

    # Preserve frequent continuous runs in their original recordings. This
    # reduces joins without copying a complete utterance for every phrase.
    occurrences = defaultdict(list)
    for sequence in inv.units.values():
        for start in range(len(sequence)):
            for length in (2,3,4):
                run = sequence[start:start+length]
                if len(run) != length or any(u.f['gap_before'] > .10 for u in run[1:]):
                    continue
                key = tuple(u.token for u in run)
                occurrences[key].append(run)
    phrases = sorted(((key,runs) for key,runs in occurrences.items() if len(runs)>=4),
                     key=lambda x: (-len(x[1])*(len(x[0])-1), x[0]))
    phrase_ceiling = min(available*.48, charged+available*.25)
    phrase_count = 0
    for key,runs in phrases:
        if charged >= phrase_ceiling:
            break
        run = min(runs, key=lambda r:sum(costs[u.uid] for u in r if u.uid not in selected))
        if add(run):
            phrase_count += 1

    # Add diverse contexts/positions/prosodies, rather than taking the first N
    # occurrences. Every choice and the exact source offsets are recorded.
    for round_index in range(12):
        added = 0
        tokens = sorted(representatives, key=lambda t:(-min(len(inv.by_token[t]),1000),t))
        for token in tokens:
            candidates = representatives[token]
            present = [u for u in candidates if u.uid in selected]
            if len(present) >= 2+round_index:
                continue
            covered_left = {inv.units[u.utterance][u.ordinal-1].token if u.ordinal else '<start>' for u in present}
            covered_right = {inv.units[u.utterance][u.ordinal+1].token if u.ordinal+1<len(inv.units[u.utterance]) else '<end>' for u in present}
            def value(u):
                seq = inv.units[u.utterance]
                left = seq[u.ordinal-1].token if u.ordinal else '<start>'
                right = seq[u.ordinal+1].token if u.ordinal+1<len(seq) else '<end>'
                distance = min(float(np.mean((inv.target_vector(u)-inv.target_vector(p))**2)) for p in present)
                novelty = 2*(left not in covered_left)+2*(right not in covered_right)+min(distance,2)
                return novelty/max(costs[u.uid],1)**.6
            remaining = [u for u in candidates if u.uid not in selected and charged+costs[u.uid]<=available]
            if remaining and add([max(remaining, key=value)]):
                added += 1
        if not added:
            break

    identity = dict(source_index_sha256=digest_file(database), selected_unit_ids=sorted(selected),
                    budget_bytes=budget, padding_seconds=padding_seconds,
                    context_radius=context_radius, schema=2,
                    control_dataset_sha256=digest_file(control_data/'dataset.json') if control_data else None)
    bank_id = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    output.mkdir(parents=True)
    connection = sqlite3.connect(output/'bank.sqlite')
    connection.executescript('''
      CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
      CREATE TABLE utterances(id TEXT PRIMARY KEY,path TEXT NOT NULL,sha256 TEXT NOT NULL,
        rate INTEGER NOT NULL,frames INTEGER NOT NULL,subtype TEXT NOT NULL,split TEXT NOT NULL,
        origin TEXT NOT NULL,origin_start INTEGER NOT NULL,raw_offset INTEGER NOT NULL);
      CREATE TABLE units(id INTEGER PRIMARY KEY,utterance TEXT NOT NULL,ordinal INTEGER NOT NULL,
        token TEXT NOT NULL,start INTEGER NOT NULL,end INTEGER NOT NULL,features BLOB NOT NULL,
        source_ordinal INTEGER NOT NULL,source_count INTEGER NOT NULL,
        previous_token TEXT,following_token TEXT,
        UNIQUE(utterance,ordinal));
      CREATE INDEX units_token ON units(token);
      CREATE INDEX units_source ON units(utterance,ordinal);
      CREATE TABLE unit_acoustics(unit_id INTEGER PRIMARY KEY,payload BLOB NOT NULL);
    ''')
    clip_count = unit_count = 0
    with (output/'audio.pcm').open('wb') as packed:
        for uid, sequence in sorted(inv.units.items()):
            ordinals = [i for i,u in enumerate(sequence) if u.uid in selected]
            groups = []
            for ordinal in ordinals:
                if groups and groups[-1][-1]+1 == ordinal and sequence[ordinal].f['gap_before'] <= .10:
                    groups[-1].append(ordinal)
                else:
                    groups.append([ordinal])
            if not groups:
                continue
            wave,rate = inv.wave(uid)
            acoustic = None
            if control_data:
                record = control_records[uid]
                feature_path = control_data/record['path']
                if digest_file(feature_path) != record['sha256']:
                    raise ValueError('Source control features changed')
                with np.load(feature_path,allow_pickle=False) as feature_file:
                    acoustic = dict(feature_file)
                if (acoustic['tokens'].tolist() != [u.token for u in sequence] or
                        str(acoustic['source_pcm_sha256']) != inv.sources[uid]['sha256']):
                    raise ValueError('Acoustic units do not match original PCM/index')
            for group in groups:
                a = max(0,sequence[group[0]].start-padding[uid])
                b = min(len(wave),sequence[group[-1]].end+padding[uid])
                data = wave[a:b].astype('<i2', copy=False).tobytes()
                offset = packed.tell()
                packed.write(data)
                name = f'{uid}-c{group[0]:03d}'
                connection.execute('INSERT INTO utterances VALUES(?,?,?,?,?,?,?,?,?,?)',
                   (name,'audio.pcm',hashlib.sha256(data).hexdigest(),rate,b-a,'PCM_16','train',uid,a,offset))
                for ordinal,source_ordinal in enumerate(group):
                    u = sequence[source_ordinal]
                    context = inv.source_context(u)
                    connection.execute('INSERT INTO units VALUES(?,?,?,?,?,?,?,?,?,?,?)',
                       (u.uid,name,ordinal,u.token,u.start-a,u.end-a,encode_features(u.f),
                        context.ordinal,context.count,context.previous,context.following))
                    if acoustic is not None:
                        mask = ((acoustic['frame_times'] >= u.start/rate-padding_seconds) &
                                (acoustic['frame_times'] <= u.end/rate+padding_seconds))
                        payload = io.BytesIO()
                        np.savez_compressed(payload,
                            frame_times=acoustic['frame_times'][mask]-u.start/rate,
                            features=acoustic['acoustic_features'][mask],
                            frame_f0=acoustic['frame_f0'][mask],frame_voiced=acoustic['frame_voiced'][mask],
                            log_f0=acoustic['log_f0'][source_ordinal],voiced=acoustic['voiced'][source_ordinal],
                            phones=acoustic['phones'][source_ordinal],durations=acoustic['durations'][source_ordinal],
                            character=acoustic['characters'][source_ordinal])
                        connection.execute('INSERT INTO unit_acoustics VALUES(?,?)',(u.uid,payload.getvalue()))
                    unit_count += 1
                clip_count += 1
    report = dict(schema=2, bank_id=bank_id, budget_bytes=budget, selected_units=unit_count,
                  clips=clip_count, exact_labels=len(inv.by_token), frequent_runs=phrase_count,
                  sample_rates=sorted({s['rate'] for s in inv.sources.values()}),
                  waveform='native PCM16, no resampling or compression', source_split='train',
                  source_index_sha256=identity['source_index_sha256'], source_context_complete=True,
                  context_radius=context_radius, padding_seconds=padding_seconds,
                  control_dataset_sha256=identity['control_dataset_sha256'],
                  full_unit_acoustic_context=control_data is not None,
                  quality_passed=False)
    connection.execute('INSERT INTO metadata VALUES(?,?)',('manifest',json.dumps(report)))
    connection.commit()
    connection.close()
    report['files'] = {p.name:dict(bytes=p.stat().st_size,sha256=digest_file(p)) for p in output.iterdir() if p.is_file()}
    report['total_bytes'] = sum(x['bytes'] for x in report['files'].values())
    encoded = json.dumps(report,indent=2)
    if report['total_bytes']+len(encoded.encode()) > budget:
        raise ValueError('Actual bank exceeds budget; this directory is not a valid frozen bank')
    (output/'bank.json').write_text(encoded,encoding='utf-8')
    print(json.dumps(report,indent=2),flush=True)
    return report


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--budget-bytes',type=int,default=200_000_000)
    parser.add_argument('--context-radius',type=int,default=1)
    parser.add_argument('--padding-seconds',type=float,default=.04)
    parser.add_argument('--control-data',type=Path)
    args=parser.parse_args()
    compile_bank(args.database,args.output,args.budget_bytes,args.context_radius,args.padding_seconds,args.control_data)
