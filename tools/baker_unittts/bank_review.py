"""Audit bank context/continuity; repair legacy metadata in a fresh immutable bank.

Repair changes no selected unit, PCM sample, feature value or clipping boundary.
It does not improve bank coverage, pronunciation labels or subjective quality.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import sqlite3

import numpy as np

from .bank import digest_file
from .synthesize import Inventory


def verify_files(directory):
    manifest = json.loads((directory/'bank.json').read_text())
    for name, record in manifest['files'].items():
        if name not in ('audio.pcm', 'bank.sqlite'):
            raise ValueError('Unexpected bank manifest file')
        path = directory/name
        if path.stat().st_size != record['bytes'] or digest_file(path) != record['sha256']:
            raise ValueError('Frozen bank file changed: '+name)
    if set(manifest['files']) != {'audio.pcm', 'bank.sqlite'}:
        raise ValueError('Incomplete frozen bank manifest')
    return manifest


def matched_units(original, inventory):
    lookup = {u.uid: u for seq in original.units.values() for u in seq}
    for sequence in inventory.units.values():
        for unit in sequence:
            reference = lookup[unit.uid]
            source = inventory.sources[unit.utterance]
            if (source['origin'] != reference.utterance or unit.token != reference.token or
                    unit.start+source['origin_start'] != reference.start or
                    unit.end+source['origin_start'] != reference.end):
                raise ValueError('Packed unit differs from original source')
            # The bank's float32 feature serialization has intentional rounding.
            np.testing.assert_allclose(inventory.target_vector(unit), original.target_vector(reference),
                                       rtol=1e-5, atol=1e-5)
            yield unit, reference


def audit(original, inventory):
    retained = set()
    mismatches = Counter()
    for unit, reference in matched_units(original, inventory):
        retained.add(unit.uid)
        actual, expected = inventory.source_context(unit), original.source_context(reference)
        for name in ('ordinal', 'count', 'previous', 'following'):
            mismatches[name] += getattr(actual, name) != getattr(expected, name)
    original_pairs = retained_pairs = 0
    for sequence in original.units.values():
        for left, right in zip(sequence, sequence[1:]):
            if right.f['gap_before'] <= .10:
                original_pairs += 1
                retained_pairs += left.uid in retained and right.uid in retained
    labels = {t: len(units) for t, units in inventory.by_token.items()}
    run_lengths = [len(seq) for seq in inventory.units.values()]
    return dict(selected_units=len(retained), train_units=sum(map(len, original.units.values())),
                labels=len(labels), labels_with_one_example=sum(n == 1 for n in labels.values()),
                labels_with_at_most_three=sum(n <= 3 for n in labels.values()),
                clips=len(run_lengths), singleton_clips=sum(n == 1 for n in run_lengths),
                longest_clip_units=max(run_lengths, default=0),
                natural_adjacent_pairs_retained=retained_pairs,
                natural_adjacent_pairs_in_train=original_pairs,
                context_mismatches=dict(mismatches), source_context_complete=inventory.context_complete,
                quality_passed=False, tone_labels_audited=False,
                note='Coverage and continuity diagnostics; these counts are not perceptual quality scores.')


def repair(original_path: Path, bank: Path, output: Path):
    if output.exists():
        raise FileExistsError('Use a fresh destination; old bank and models remain immutable')
    old_manifest = verify_files(bank)
    if old_manifest['schema'] != 1:
        raise ValueError('Only legacy schema-1 banks need this repair')
    original, legacy = Inventory(original_path), Inventory(bank/'bank.sqlite')
    if original.bank_id:
        raise ValueError('Original input must be the full source corpus index')
    before = audit(original, legacy)
    records = []
    for unit, reference in matched_units(original, legacy):
        context = original.source_context(reference)
        records.append((context.ordinal, context.count, context.previous, context.following, unit.uid))
    identity = dict(schema=2, parent_bank_id=legacy.bank_id,
                    parent_database_sha256=old_manifest['files']['bank.sqlite']['sha256'],
                    audio_sha256=old_manifest['files']['audio.pcm']['sha256'],
                    source_index_sha256=digest_file(original_path),
                    operation='restore original context; retain all units and PCM')
    bank_id = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    output.mkdir(parents=True)
    shutil.copyfile(bank/'audio.pcm', output/'audio.pcm')
    source = sqlite3.connect(f'file:{(bank/"bank.sqlite").resolve().as_posix()}?mode=ro', uri=True)
    destination = sqlite3.connect(output/'bank.sqlite')
    with source, destination:
        source.backup(destination)
        for name, kind in [('source_ordinal','INTEGER'), ('source_count','INTEGER'),
                           ('previous_token','TEXT'), ('following_token','TEXT')]:
            destination.execute(f'ALTER TABLE units ADD COLUMN {name} {kind}')
        destination.executemany('UPDATE units SET source_ordinal=?,source_count=?,previous_token=?,following_token=? WHERE id=?', records)
        manifest = {k: v for k, v in old_manifest.items() if k not in ('files','total_bytes')}
        manifest.update(schema=2, bank_id=bank_id, parent_bank_id=legacy.bank_id,
                        source_context_complete=True, quality_passed=False,
                        repair_identity=identity)
        destination.execute("UPDATE metadata SET value=? WHERE key='manifest'", (json.dumps(manifest),))
    source.close()
    destination.close()
    manifest['files'] = {name: dict(bytes=(output/name).stat().st_size, sha256=digest_file(output/name))
                         for name in ('audio.pcm','bank.sqlite')}
    manifest['total_bytes'] = sum(r['bytes'] for r in manifest['files'].values())
    if manifest['files']['audio.pcm'] != old_manifest['files']['audio.pcm']:
        raise ValueError('PCM changed during metadata repair')
    if manifest['total_bytes']+len(json.dumps(manifest).encode()) > manifest['budget_bytes']:
        raise ValueError('Repaired metadata exceeds the storage budget')
    (output/'bank.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    fixed = Inventory(output/'bank.sqlite')
    after = audit(original, fixed)
    if any(after['context_mismatches'].values()) or before['selected_units'] != after['selected_units']:
        raise ValueError('Context repair verification failed')
    receipt = dict(before=before, after=after, identity=identity, bank_id=bank_id,
                   pcm_bit_identical=True, models_require_retraining=True, quality_passed=False)
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--bank', type=Path, required=True, help='Frozen bank directory')
    parser.add_argument('--repair-to', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists():
        raise FileExistsError('Refusing to replace an audit report')
    if args.repair_to:
        report = repair(args.original, args.bank, args.repair_to)
    else:
        verify_files(args.bank)
        report = audit(Inventory(args.original), Inventory(args.bank/'bank.sqlite'))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)
