"""Exact-label retrieval TTS with bounded, logged joins and unmodified interiors."""
from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass
import hashlib
import io
import json
import math
from pathlib import Path
import re
import sqlite3
import time

import numpy as np
import soundfile as sf

from .corpus import normalize_token, decode_features


@dataclass
class SourceContext:
    ordinal: int
    count: int
    previous: str | None
    following: str | None


@dataclass
class Unit:
    uid: int
    utterance: str
    ordinal: int
    token: str
    start: int
    end: int
    f: dict
    context: SourceContext | None = None


@dataclass(frozen=True)
class Span:
    utterance: str
    first: int
    last: int
    target_start: int
    cost: float = 0.

    @property
    def length(self):
        return self.last-self.first+1


def merge_spans(spans: list[Span]) -> list[Span]:
    merged = []
    for span in spans:
        if merged and merged[-1].utterance == span.utterance and merged[-1].last+1 == span.first:
            previous = merged[-1]
            merged[-1] = Span(previous.utterance, previous.first, span.last,
                              previous.target_start, previous.cost+span.cost)
        else:
            merged.append(span)
    return merged


def text_phrases(text: str) -> list[tuple[list[str], float]]:
    """Exploratory pypinyin frontend; explicit pinyin bypasses its ambiguity."""
    from pypinyin import lazy_pinyin, Style
    phrases = []
    for part in re.findall(r'[^，。！？；、,.!?;\n]+[，。！？；、,.!?;\n]?', text):
        punctuation = part[-1] if re.match(r'[，。！？；、,.!?;\n]', part[-1]) else ''
        body = part[:-1] if punctuation else part
        body = re.sub(r'\s+', '', body)
        if not body:
            continue
        if re.search(r'[0-9A-Za-z]', body):
            raise ValueError('Spell numbers in Chinese and supply explicit pinyin for Latin words; no guessed pronunciation')
        tokens = lazy_pinyin(body, style=Style.TONE3, neutral_tone_with_five=True,
                             v_to_u=False, errors=lambda s: [s])
        tokens = [normalize_token(t) for t in tokens]
        pause = .25 if punctuation in '。！？.!?\n' and punctuation else .13 if punctuation else 0.
        phrases.append((tokens, pause))
    if not phrases:
        raise ValueError('No supported text to speak')
    return phrases


def pinyin_phrases(text: str):
    chunks = text.split('|')
    if any(not chunk.strip() for chunk in chunks):
        raise ValueError('Empty explicit-pinyin phrase')
    return [([normalize_token(t) for t in chunk.split()], .13 if i+1 < len(chunks) else 0.)
            for i, chunk in enumerate(chunks)]


class Inventory:
    def __init__(self, database: Path, exclude: set[str] | None = None):
        self.path = database
        self.exclude = exclude or set()
        connection = sqlite3.connect(f'file:{database.resolve().as_posix()}?mode=ro', uri=True)
        connection.row_factory = sqlite3.Row
        self.bank_id = None
        self.context_complete = True
        metadata = connection.execute("SELECT value FROM metadata WHERE key='manifest'").fetchone()
        if metadata:
            self.bank_id = json.loads(metadata[0]).get('bank_id')
        self.sources = {r['id']: dict(r) for r in connection.execute("SELECT * FROM utterances WHERE split='train'")
                        if r['id'] not in self.exclude and dict(r).get('origin') not in self.exclude}
        self.units = defaultdict(list)
        self.by_token = defaultdict(list)
        for r in connection.execute("SELECT units.* FROM units JOIN utterances ON units.utterance=utterances.id WHERE split='train' ORDER BY units.utterance,ordinal"):
            if r['utterance'] not in self.sources:
                continue
            unit = Unit(r['id'], r['utterance'], r['ordinal'], r['token'], r['start'], r['end'], decode_features(r['features']))
            if 'source_ordinal' in r.keys():
                unit.context = SourceContext(r['source_ordinal'], r['source_count'],
                                             r['previous_token'], r['following_token'])
            if unit.ordinal != len(self.units[unit.utterance]):
                raise ValueError('Noncontiguous source unit ordinals')
            self.units[unit.utterance].append(unit)
            self.by_token[unit.token].append(unit)
        connection.close()
        # Packed adjacency is needed to read PCM; linguistic context must use
        # the original recording. Schema-1 banks did not preserve that context.
        self.context_complete = not self.bank_id or all(
            u.context is not None for seq in self.units.values() for u in seq)
        self._wave_cache = {}
        self._acoustic_cache = {}
        self.parameters = 0
        self.selector = None

    def source_context(self, unit: Unit) -> SourceContext:
        if unit.context is not None:
            return unit.context
        sequence = self.units[unit.utterance]
        return SourceContext(unit.ordinal, len(sequence),
                             sequence[unit.ordinal-1].token if unit.ordinal else None,
                             sequence[unit.ordinal+1].token if unit.ordinal+1 < len(sequence) else None)

    @staticmethod
    def target_vector(unit: Unit) -> np.ndarray:
        return np.array([math.log(unit.f['duration']),
                         *[math.log(max(f, 75)) for f in unit.f['f0']], unit.f['log_rms']])

    def default_targets(self, tokens: list[str]) -> np.ndarray:
        targets = []
        for token in tokens:
            candidates = self.by_token.get(token)
            if not candidates:
                raise ValueError('Missing exact syllable/tone in train library: ' + token)
            values = np.array([self.target_vector(u) for u in candidates])
            voiced = np.array([u.f['voiced'] > 0 for u in candidates])
            med = np.median(values, axis=0)
            if voiced.any():
                med[1:4] = np.median(values[voiced, 1:4], axis=0)
            targets.append(med)
        return np.array(targets)

    @staticmethod
    def target_cost(unit: Unit, target: np.ndarray) -> float:
        difference = Inventory.target_vector(unit)-target
        weights = np.array([1.2, .30, .25, .30, .04])
        if unit.f['voiced'] == 0:
            weights[1:4] = 0
        return float((np.minimum(difference**2, 4.)*weights).sum())

    def candidates(self, tokens: list[str], position: int, targets: np.ndarray, maximum: int = 8, learned_costs=None):
        by_length = defaultdict(list)
        for initial in self.by_token.get(tokens[position], []):
            sequence = self.units[initial.utterance]
            cumulative = 0.
            for offset in range(min(maximum, len(tokens)-position, len(sequence)-initial.ordinal)):
                unit = sequence[initial.ordinal+offset]
                if unit.token != tokens[position+offset]:
                    break
                # Do not smuggle an unrelated source pause into a target word.
                if offset and unit.f['gap_before'] > .10:
                    break
                cumulative += (learned_costs[position+offset][unit.uid] if learned_costs is not None
                               else self.target_cost(unit, targets[position+offset]))
                if offset == 0:
                    context = self.source_context(initial)
                    if position == 0 and context.ordinal != 0:
                        cumulative += .10
                    elif position > 0 and context.ordinal > 0 and context.previous != tokens[position-1]:
                        cumulative += .06
                final_position = position+offset == len(tokens)-1
                context = self.source_context(unit)
                terminal_cost = .12 if final_position and context.ordinal != context.count-1 else 0.
                span = Span(initial.utterance, initial.ordinal, unit.ordinal, position, cumulative+terminal_cost)
                by_length[offset+1].append(span)
        if not by_length:
            raise ValueError('No exact candidate for token ' + tokens[position])
        result = []
        for length, spans in by_length.items():
            result.extend(sorted(spans, key=lambda s: (s.cost, s.utterance, s.first))[:24])
        return result

    def join_cost(self, previous: Span, current: Span) -> float:
        if previous.utterance == current.utterance and previous.last+1 == current.first:
            return 0.
        a = self.units[previous.utterance][previous.last].f
        b = self.units[current.utterance][current.first].f
        spectral = np.mean((np.array(a['right'])-b['left'])**2)
        energy = abs(a['log_rms']-b['log_rms'])
        pitch = 0.
        if a['f0'][-1] > 0 and b['f0'][0] > 0:
            pitch = abs(math.log(a['f0'][-1]/b['f0'][0]))
        return .75 + .045*min(spectral, 25.) + .12*min(energy, 3.) + .8*min(pitch, 1.5)

    def select(self, tokens: list[str], targets: np.ndarray | None = None, beam: int = 24):
        for token in tokens:
            if token not in self.by_token:
                raise ValueError('Missing exact syllable/tone: ' + token)
        targets = self.default_targets(tokens) if targets is None else targets
        if targets.shape != (len(tokens), 5) or not np.isfinite(targets).all():
            raise ValueError('Invalid prosody target shape or values')
        states = [[] for _ in range(len(tokens)+1)]
        states[0] = [(0., [])]
        learned_costs = None
        if getattr(self, 'selector', None) is not None:
            learned_costs = [self.selector.costs(self, tokens, i, targets) for i in range(len(tokens))]

        def prune(entries):
            # The future only depends on the last source endpoint. Retaining
            # multiple worse paths to that same endpoint wastes the beam.
            best = {}
            for entry in entries:
                endpoint = (entry[1][-1].utterance, entry[1][-1].last) if entry[1] else None
                if endpoint not in best or entry[0] < best[endpoint][0]:
                    best[endpoint] = entry
            return sorted(best.values(), key=lambda x: x[0])[:beam]

        for position in range(len(tokens)):
            current_states = prune(states[position])
            if not current_states:
                continue
            candidates = self.candidates(tokens, position, targets, learned_costs=learned_costs)
            for cost, path in current_states:
                for span in candidates:
                    join = self.join_cost(path[-1], span) if path else 0.
                    states[position+span.length].append((cost+span.cost+join, path+[span]))
            # Bound work and retained paths, rather than retaining every search expansion.
            for future in range(position+1, min(len(tokens)+1, position+9)):
                if len(states[future]) > beam*4:
                    states[future] = prune(states[future])
        if not states[-1]:
            raise RuntimeError('No complete exact-label synthesis path')
        cost, spans = min(states[-1], key=lambda x: x[0])
        return merge_spans(spans), cost

    def wave(self, utterance: str):
        if utterance not in self.sources:
            raise ValueError('Non-train or excluded waveform requested')
        if utterance not in self._wave_cache:
            source = self.sources[utterance]
            if 'raw_offset' in source:
                raw_path = self.path.parent / source['path']
                with raw_path.open('rb') as stream:
                    stream.seek(source['raw_offset'])
                    data = stream.read(source['frames']*2)
                if len(data) != source['frames']*2 or hashlib.sha256(data).hexdigest() != source['sha256']:
                    raise ValueError('Packed source waveform checksum changed')
                audio = np.frombuffer(data, dtype='<i2').copy()
                if len(self._wave_cache) >= 32:
                    self._wave_cache.pop(next(iter(self._wave_cache)))
                self._wave_cache[utterance] = (audio, source['rate'])
                return self._wave_cache[utterance]
            info = sf.info(source['path'])
            if info.samplerate != source['rate'] or info.frames != source['frames'] or info.subtype != 'PCM_16':
                raise ValueError('Source waveform changed or unsupported PCM format')
            if hashlib.sha256(Path(source['path']).read_bytes()).hexdigest() != source['sha256']:
                raise ValueError('Source waveform checksum changed')
            audio, rate = sf.read(source['path'], dtype='int16', always_2d=True)
            if audio.shape[1] != 1:
                raise ValueError('Non-mono source')
            if len(self._wave_cache) >= 32:
                self._wave_cache.pop(next(iter(self._wave_cache)))
            self._wave_cache[utterance] = (audio[:, 0], rate)
        return self._wave_cache[utterance]

    def materialize(self, span: Span):
        units = self.units[span.utterance]
        first, last = units[span.first], units[span.last]
        wave, rate = self.wave(span.utterance)
        return wave[first.start:last.end].copy(), rate

    def acoustic_unit(self, unit: Unit):
        """Read the full unit feature sequence plus its analysis handles on demand."""
        if unit.utterance not in self.sources:
            raise ValueError('Excluded acoustic source requested')
        if unit.uid not in self._acoustic_cache:
            with sqlite3.connect(f'file:{self.path.resolve().as_posix()}?mode=ro',uri=True) as connection:
                row = connection.execute('SELECT payload FROM unit_acoustics WHERE unit_id=?',(unit.uid,)).fetchone()
            if row is None:
                raise ValueError('This bank has no full acoustic context for the selected unit')
            with np.load(io.BytesIO(row[0]),allow_pickle=False) as payload:
                data = dict(payload)
            if (data['features'].ndim != 2 or data['features'].shape[1] != 24 or
                    not np.isfinite(data['features']).all()):
                raise ValueError('Invalid packed acoustic features')
            if len(self._acoustic_cache) >= 128:
                self._acoustic_cache.pop(next(iter(self._acoustic_cache)))
            self._acoustic_cache[unit.uid] = data
        return self._acoustic_cache[unit.uid]


def bounded_join(left: np.ndarray, right: np.ndarray, rate: int, left_f0: float, right_f0: float):
    """Preserve interiors. Overlap only correlated, pitch-compatible voiced edges.

    Rejected voiced matches and unvoiced/transient boundaries get a <=0.25 ms
    endpoint fade instead. No generated samples, gain normalization or time warp.
    """
    if left.dtype != np.int16 or right.dtype != np.int16 or not len(left) or not len(right):
        raise ValueError('Join requires nonempty PCM16 arrays')
    a, b = left.astype(np.float64), right.astype(np.float64)
    best, best_length = -1., 0
    if left_f0 > 0 and right_f0 > 0 and abs(math.log(left_f0/right_f0)) < .14:
        period = rate / ((left_f0+right_f0)/2)
        low = max(16, round(period*.75))
        high = min(round(period*1.25), round(rate*.012), len(a)//5, len(b)//5)
        for length in range(low, high+1, max(1, round(rate*.000125))):
            x, y = a[-length:], b[:length]
            x, y = x-x.mean(), y-y.mean()
            correlation = float(np.dot(x, y)/max(np.linalg.norm(x)*np.linalg.norm(y), 1e-8))
            if correlation > best:
                best, best_length = correlation, length
    if best >= .65:
        ramp = np.linspace(0., 1., best_length)
        bridge = a[-best_length:]*(1-ramp)+b[:best_length]*ramp
        result = np.concatenate((a[:-best_length], bridge, b[best_length:]))
        detail = dict(method='correlated_voiced_overlap', samples=best_length, correlation=best,
                      modified_input_samples=2*best_length, removed_samples=best_length)
    else:
        length = min(max(1, round(rate*.00025)), len(a)//8, len(b)//8)
        if length:
            a[-length:] *= np.linspace(1., 0., length)
            b[:length] *= np.linspace(0., 1., length)
        result = np.concatenate((a, b))
        detail = dict(method='microfade', samples=length, correlation=best,
                      modified_input_samples=2*length, removed_samples=0)
    return np.clip(np.rint(result), -32768, 32767).astype(np.int16), detail


def render(inventory: Inventory, spans: list[Span], joins: bool = True, controller=None):
    if not spans:
        raise ValueError('Empty synthesis path')
    spans = merge_spans(spans)
    result, rate = inventory.materialize(spans[0])
    records = []
    for previous, current in zip(spans, spans[1:]):
        block, new_rate = inventory.materialize(current)
        if rate != new_rate:
            raise ValueError('Mixed source sample rates require an explicit conversion experiment')
        if joins:
            if controller is not None:
                tokens=(inventory.units[previous.utterance][previous.last].token,
                        inventory.units[current.utterance][current.first].token)
                result,detail=controller.join(result,block,rate,tokens,inventory.bank_id)
            else:
                a = inventory.units[previous.utterance][previous.last].f['f0'][-1]
                b = inventory.units[current.utterance][current.first].f['f0'][0]
                result, detail = bounded_join(result, block, rate, a, b)
        else:
            result = np.concatenate((result, block))
            detail = dict(method='hard_join', samples=0, modified_input_samples=0, removed_samples=0)
        records.append(dict(left=previous.utterance, right=current.utterance, **detail))
    return result, rate, records


def synthesize(database: Path, output: Path, text: str | None = None, pinyin: str | None = None,
               planner: Path | None = None, exclude: list[str] | None = None,
               inventory: Inventory | None = None, predictor=None,
               selector: Path | None = None, join_controller: Path | None = None, controller=None):
    if output.exists():
        raise FileExistsError('Refusing to replace an existing audition')
    if bool(text) == bool(pinyin):
        raise ValueError('Provide exactly one of text or pinyin')
    start = time.monotonic()
    inventory = inventory if inventory is not None else Inventory(database, set(exclude or []))
    if inventory.exclude != set(exclude or []):
        raise ValueError('Cached inventory has a different exclusion set')
    if planner and predictor is None:
        from .prosody import Predictor
        predictor = Predictor(planner)
    if selector:
        from .selector import Selector
        inventory.selector=Selector(selector)
    if join_controller and controller is None:
        from .join_control import Controller
        controller=Controller(join_controller)
    parameters=(predictor.parameters if predictor else 0)+(inventory.selector.parameters if inventory.selector else 0)+(controller.parameters if controller else 0)
    if parameters>25_000_000:
        raise ValueError('Combined active-model parameter budget exceeded')
    phrases = pinyin_phrases(pinyin) if pinyin else text_phrases(text)
    audio_parts, raw_parts, all_segments, all_joins = [], [], [], []
    rate = None
    selection_seconds = 0.
    for tokens, pause in phrases:
        before = time.monotonic()
        targets = predictor(tokens) if predictor else None
        spans, cost = inventory.select(tokens, targets)
        selection_seconds += time.monotonic()-before
        rendered, new_rate, joins = render(inventory, spans, True, controller)
        raw, _, _ = render(inventory, spans, False)
        if rate is not None and rate != new_rate:
            raise ValueError('Mixed phrase sample rates')
        rate = new_rate
        silence = np.zeros(round(rate*pause), dtype=np.int16)
        audio_parts.extend((rendered, silence))
        raw_parts.extend((raw, silence))
        all_segments.append(dict(tokens=tokens, cost=cost, spans=[dict(
            utterance=s.utterance, first=s.first, last=s.last,
            start=inventory.units[s.utterance][s.first].start,
            end=inventory.units[s.utterance][s.last].end,
            tokens=[u.token for u in inventory.units[s.utterance][s.first:s.last+1]]) for s in spans]))
        all_joins.extend(joins)
    output.mkdir(parents=True)
    audio, raw = np.concatenate(audio_parts), np.concatenate(raw_parts)
    sf.write(output/'bounded_joins.wav', audio, rate, subtype='PCM_16')
    sf.write(output/'hard_joins.wav', raw, rate, subtype='PCM_16')
    report = dict(schema=1, text=text, explicit_pinyin=pinyin, sample_rate=rate,
                  seconds=len(audio)/rate, elapsed_seconds=time.monotonic()-start,
                  selection_seconds=selection_seconds, active_parameters=parameters,
                  planner_sha256=predictor.sha256 if predictor else None,
                  frontend='explicit pinyin' if pinyin else 'experimental pypinyin; not pronunciation-qualified',
                  exact_label_matching=True, library_split='train', excluded=exclude or [],
                  bank_id=inventory.bank_id,learned_selection=inventory.selector is not None,
                  source_context_complete=inventory.context_complete,
                  legacy_packed_context=not inventory.context_complete,
                  learned_splice_controls=controller is not None,
                  pitch_shift=False, time_stretch=False, neural_waveform_repair=False,
                  source_interiors='unaltered PCM16 except logged boundary windows',
                  peak_pcm=int(np.abs(audio.astype(np.int32)).max()),
                  clipped_samples=int(np.sum((audio == -32768) | (audio == 32767))),
                  joins=all_joins, phrases=all_segments, quality_passed=False,
                  board_tested=False)
    (output/'selection.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--text')
    source.add_argument('--pinyin')
    parser.add_argument('--planner', type=Path)
    parser.add_argument('--selector', type=Path)
    parser.add_argument('--join-controller', type=Path)
    parser.add_argument('--exclude', action='append', default=[])
    args = parser.parse_args()
    result = synthesize(args.database, args.output, args.text, args.pinyin, args.planner, args.exclude,
                        selector=args.selector,join_controller=args.join_controller)
    print(json.dumps({k:v for k,v in result.items() if k not in ('phrases', 'joins')}, ensure_ascii=False, indent=2))
