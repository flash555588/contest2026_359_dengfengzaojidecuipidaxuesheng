# Baker unit TTS research

This is a host-side research implementation for the single-speaker Baker / BZNSYP
corpus. It does not replace the board's active TTS backend. Baker's original
non-commercial restriction applies to the corpus and must remain with derived
assets. Downloaded PaddleSpeech checkpoints retain their original notices.

The budget is **at most 25 million active inference parameters**, not 30 MB of
all assets. The initial planner is deliberately smaller. Original PCM and
indexes have a separate storage budget. A small parameter count does not prove
real-time operation on ESP32-P4.

## Quality strategy

1. Validate the fixed corpus archive SHA-256 and record teacher archive hashes.
2. Keep native-rate PCM16 WAV files. Index exact tone-bearing syllables and
   continuous runs without duplicating or regenerating waveform interiors.
3. Partition complete source utterances deterministically into train,
   validation and test before inventory selection or planner fitting. Only
   train utterances enter the synthesis library.
4. Train a small contextual duration/F0/energy planner on original recordings.
   This is supervised prosody training, **not distillation from FastSpeech2**.
5. Search complete paths using exact labels, target compatibility, source
   continuity and spectral/pitch/energy join costs.
6. Retain both hard-join and bounded-join audio. Bounded joins only overlap
   correlated voiced boundaries or apply a <=0.25 ms endpoint fade. No pitch
   shift, time stretch, denoising or neural waveform repair is enabled.

The official MFA word tier provides syllable labels; they are not automatically
assumed to be surface-tone annotations. The initial pypinyin frontend does not
constitute verified contextual G2P or tone-sandhi handling. Use explicit pinyin
for controlled comparisons, then separately qualify the text frontend.

## Linux commands

Use a dedicated environment with NumPy, SciPy, SoundFile and pypinyin. Planner
training additionally needs a CUDA-compatible PyTorch build. Run from `tools/`
or set `PYTHONPATH` to its absolute path.

```bash
python -m baker_unittts.prepare --source /path/to/downloads --output /path/to/prepared
python -m unittest baker_unittts.test_unittts -v
python -m baker_unittts.corpus --waves /path/to/BZNSYP/Wave \
  --alignments /path/to/baker_alignment_tone --output /path/to/corpus.sqlite --workers 8
python -m baker_unittts.prosody --database /path/to/corpus.sqlite \
  --output /path/to/planner-run --epochs 20 --device cuda
python -m baker_unittts.synthesize --database /path/to/corpus.sqlite \
  --planner /path/to/planner-run/best.pt --text '你好，欢迎使用语音助手。' \
  --output /path/to/audition
```

Existing corpus indexes, planner runs and auditions are never overwritten.
Extraction uses a fresh destination, rejects traversal and non-regular entries,
and preserves original downloads. Failed builds keep partial artifacts for
diagnosis; choose a new output when retrying rather than silently replacing one.

## Evidence limits and remaining work

- Bit-exact contiguous-source reconstruction is an implementation check, not a
  quality score for novel sentences.
- Boundary edits are logged; preserved interiors do not guarantee natural
  prosody, correct alignment, correct G2P or seamless concatenation.
- The Python inventory and search implementation loads substantial host-side
  metadata. It is **not** the bounded-memory C implementation for P4.
- The waveform corpus is externally stored and not included in the parameter
  count. Storage and memory must be reported independently.
- Missing exact syllables/tones fail rather than selecting similar-sounding
  substitutes. Automatic long-tail coverage is not complete.
- Official teacher inference, blind listening, unseen-text testing and native
  board playback are separate evidence categories.
- All reports retain `quality_passed: false` pending actual listening acceptance.

No firmware was built, flashed or hardware-qualified by these host tools.

## Frozen-bank and learned-control experiments

`bank.py` compiles train-only PCM clips under an explicit **storage** budget.
It keeps at least one example per train label, frequent continuous runs, and
diverse remaining contexts. It merges adjacent retained units and packs native
PCM with 8 ms outer context into a raw file. Compact features and source hashes
are stored in SQLite. This selection stage is still an explicit heuristic;
it is not an end-to-end learned bank compiler.

`selector.py` trains a small candidate ranker after the bank is frozen. Its
soft reference scores use the source recording's duration, F0, energy and edge
spectra. Query and validation recordings are disjoint. A query's own original
recording is excluded from its candidate set. These are acoustic-oracle labels,
not human preference scores or a demonstrated perceptual optimum.

`join_control.py` trains a small convolutional action predictor bound to the
same bank. Its finite action set includes no change, short endpoint fades,
smooth local offset corrections and guarded <=0.75 ms low-energy cuts. Offset
windows are at most 4 ms on each side. Training includes real clean waveform
boundaries and cross-recording seams; DSP continuity/distortion scores provide
soft targets. Candidate audio origins are disjoint across its train/validation
split. The model never emits waveform samples. This is **surrogate-supervised
splice control**, not audio-quality-qualified end-to-end waveform training.

```bash
python -m baker_unittts.bank --database /path/to/corpus.sqlite \
  --output /path/to/bank-200m-v1 --budget-bytes 200000000
python -m baker_unittts.selector prepare --original /path/to/corpus.sqlite \
  --bank /path/to/bank-200m-v1/bank.sqlite --planner /path/to/planner/best.pt \
  --output /path/to/selector-dataset
python -m baker_unittts.selector train --dataset /path/to/selector-dataset \
  --output /path/to/selector-run --epochs 35
python -m baker_unittts.join_control prepare --original /path/to/corpus.sqlite \
  --bank /path/to/bank-200m-v1/bank.sqlite --output /path/to/join-dataset
python -m baker_unittts.join_control train --dataset /path/to/join-dataset \
  --output /path/to/join-run --epochs 25
```

The cumulative audition generated by `ablation.py` keeps the bank and all three
checkpoints fixed. Its final stage must use exactly the same selected source
spans as the preceding stage. All four stages retain the same pinyin input;
official teacher speech is an additional reference. There is no loudness
normalization, pitch shift or global waveform filtering. Host inference speed
does not establish P4 speed, and a lower surrogate loss does not establish that
join noise or pronunciation problems are solved.

## Context regression and next control experiment

The first reduced bank (schema 1) discarded original unit positions and
neighbors when packing clips. A packed clip boundary was incorrectly treated
as a recording boundary by candidate costs and ranker inputs. Schema 2 stores
source ordinal, source unit count, and both original neighboring labels
separately from clip-local indexing. Old banks can still be read to reproduce
the failed comparison; synthesis receipts flag their incomplete context.
`bank_review.py` restores context from the source index into a fresh bank,
verifies source offsets and PCM hashes, and gives it a new identity. Old
bank-bound checkpoints cannot be silently reused with the repaired bank.
This fixes metadata only; it does not restore discarded recordings or certify
tone labels, joins or audio quality.

```bash
python -m baker_unittts.bank_review --original /path/to/corpus.sqlite \
  --bank /path/to/bank-200m-v1 --repair-to /path/to/bank-context-v2 \
  --report /path/to/context-audit.json
python -m unittest baker_unittts.test_bank_review -v
```

The control experiment uses real corpus audio for duration and F0 references.
The official teacher remains a separate listening reference. `controls_data.py`
retains complete utterance frame features, 24 time-indexed F0 targets per syllable,
phone labels/durations and source hashes. `controls.py` trains the global
context planner. `dsp_controls.py` is a bounded host PSOLA reference with a
bit-exact identity bypass; numerical tests do not certify speech quality.

`bank_controls.py` freezes a bank identity before preparing its training data.
A shared acoustic encoder reads every stored frame of the center unit and both
selected neighbors, original source position/neighbor labels, and the global
planner's sentence context. It learns bounded phone-duration and F0 corrections
against actual recordings, with phone-aligned pitch correspondence. Normal
cross-recording examples exclude their target recording from the candidate
bank pool; additional train-only identity examples are counted separately.
The checkpoint checks both bank file hashes and global planner identity.
`bank_audition.py` holds new-bank source paths fixed for the four control modes
and puts the accepted old full-bank rule audio in a separate reference column.

The planned shared splice head and final joint optimization are **not trained
by these modules**. The remaining design and acceptance requirements are:

- Correct pronunciations and surface tones before acoustic control. Audit
  alignment and voiced/unvoiced decisions; three F0 medians are insufficient
  as the only pitch-trajectory supervision.
- Keep full selected unit PCM plus analysis handles outside each cut, original
  context, phone intervals, F0/voicing tracks, and source provenance. Treat the
  handles as analysis context, not extra speech to emit. Size handles according
  to reliable pitch periods and transient boundaries. Read adjacent units from
  storage together rather than loading the whole bank into RAM.
- Current retrieval tokens come from the alignment's tone-bearing syllable
  tier, not its finer phone tier. Preserve and use both: syllable-level tone
  planning and phone-level duration/voicing/transient boundaries. A sentence
  encoder supplies phrase position, stress/punctuation context and neighboring
  targets; a unit encoder supplies intrinsic acoustic features; an edge encoder
  supplies paired boundary evidence. Shared unit representations must be reused
  on both neighboring edges so one unit cannot receive contradictory duration
  or pitch instructions from separate pair decisions. Retaining raw PCM and
  provenance makes information recoverable; compressed model features do not
  imply lossless use of every detail.
- A shared contextual encoder sees the full variable-length left/right unit
  features and detailed boundary features. Separate heads predict monotonic
  duration maps, voiced F0 correction curves, and bounded cut/alignment/overlap
  controls. It outputs controls, not waveform samples. Apply duration and pitch
  maps together in a single rendering pass, with splice controls in the same
  time coordinates. Naturally continuous source spans bypass unnecessary edits.
- Validate known correct target contours through DSP first. Train duration/F0
  heads, then train splice controls with the earlier heads fixed. Only after
  these work, optimize and validate the combined chain. Discrete cut selection
  and a non-differentiable PSOLA renderer do not permit ordinary end-to-end
  waveform-loss backpropagation; use explicit control search/supervision or
  implement a separately verified differentiable renderer.
- Include intact natural boundaries as identity examples, controlled
  timing/pitch/cut perturbations paired with their original audio, and actual
  cross-recording paths from the frozen bank. Cross-recording samples have no
  unique sample-aligned waveform target: do not use raw waveform L1 against an
  unrelated recording as if it were ground truth. Distinguish same-recording
  reconstruction from novel-text listening and tone correctness.
- Review bank variants for natural-run coverage, contextual/tonal diversity,
  retained recording quality and required adjustment magnitude. Never accept
  label coverage alone as a quality gate. Report storage separately from the
  active model-parameter limit and preserve the previously heard baseline.

## Program selection and direct global control

`program_tts.py` implements the current experiment: the global planner supplies
phone durations and 24-point F0/voicing trajectories, deterministic search picks
source spans, and PSOLA directly follows these targets. It does not load the
material-conditioned neural adjuster or learned candidate ranker. The current
planner has 3,856,294 parameters. This changes runtime control, not its training
data or weights.

Candidates must match the complete phone topology and declared duration/pitch
ranges. Search uses full F0 contour differences, phone duration ratios, original
neighbors and the existing span/join costs. Out-of-range sources are rejected;
targets are not silently clipped to the earlier adjuster's +/-2-semitone range.
The direct renderer allows up to +/-12 semitones and 0.5--2.0 duration factors,
which are feasibility limits, not perceptual quality guarantees. Optional
measurement feedback always re-renders the original PCM rather than editing an
already processed waveform. Final output is measured again after joining.

```bash
PYTHONPATH=src OMP_NUM_THREADS=2 venv/bin/python -m baker_unittts.program_audition \
  --bank bank-600m-context-v2/bank.sqlite --full corpus.sqlite \
  --planner controls-planner-v1/best-plain.pt --output program-audition-v1
python -m unittest baker_unittts.test_program_tts -v
```

The audition preserves the accepted full-bank rules reference and compares
reduced-bank rules, program selection without adjustment, and the same program
source path with direct adjustment. Every case retains explicit pinyin inputs.
This is not a validated general text frontend. Reports include actual F0
deviation, reliable voiced coverage, and duration deviation per syllable. A
numeric tracking pass does not validate the planner's target, lexical tones or
perceived naturalness. The existing splice algorithm remains in use; its seams
have not been resolved by this experiment. Host output does not establish P4
runtime, memory feasibility or real-device audio quality.

## Sentence-context seam spectral experiment

`seam_spectral.py` adds a separate controller after the fixed program-direct
baseline. A convolutional front end and two bidirectional GRU layers read the
entire phrase and emit 24 bounded spectral gains near cross-recording joins.
Native-rate STFT processing retains phase and sample count. It does not predict
new audio samples, F0 or durations. A separate 0.8 ms cubic boundary bridge is
compared independently, rather than crediting its effect to the neural model.

`prepare` uses the frozen bank and original corpus features to construct
phone-aligned cross-recording envelope targets, excluding the target recording
from the source candidates. These feature-warped pairs are only a warm-up.
`seam_pairs.py` renders actual PSOLA-plus-splice source waveforms using reference
controls and measures their spectra, then maps the original recording's
spectral envelopes to output unit times. This supplies real renderer defects
without claiming unrelated recordings have a sample-aligned waveform target.
Unrenderable examples are counted with reasons in the manifest. Original
utterance train/validation splits remain separate. Online clean identity and
known EQ corruption examples discourage editing natural speech unnecessarily.

Reference spectral targets are not clipped. Rendering gains are limited to
+/-4 dB, and corrections taper to zero beyond 140 ms from a join. Tests check
bit-exact zero control, physical gain limits, unchanged sample count/frequency,
no silent normalization, local repair scope, and padded-sequence invariance.
The loss is a spectral-envelope proxy. Even success on actual renderer pairs
does not establish perceived continuity or reconstruct missing coarticulation.
The model remains a host experiment pending listening and P4 evaluation.
