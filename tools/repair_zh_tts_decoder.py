#!/usr/bin/env python3
"""Isolated teacher-latent decoder repair after human reports of electronic tails.

Training losses are diagnostic hypotheses, not automatic perceptual approval.
Acoustic/duration checkpoints remain fixed. Optional teacher initialization
permits a wider research decoder; neither mode deploys a model.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, required=True)
    parser.add_argument('--trained', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--steps', type=int, default=2000)
    parser.add_argument('--python', type=Path, required=True)
    parser.add_argument('--teacher-init-checkpoint', type=Path,
                        help='Rebuild from verified official teacher weights instead of the rejected student')
    parser.add_argument('--channels', default='96,48,24,12')
    parser.add_argument('--perceptual-losses', action='store_true',
                        help='Add waveform multi-period adversarial/feature matching and log-mel losses')
    parser.add_argument('--lr', type=float, default=5e-5)
    args = parser.parse_args()
    if args.steps < 1:
        raise ValueError('Steps must be positive')
    channels = tuple(int(x) for x in args.channels.split(','))
    if len(channels) != 4 or any(x <= 0 or x > maximum for x,maximum in zip(channels,(256,128,64,32))):
        raise ValueError('Channels must be four positive widths no larger than the teacher')
    if not 0 < args.lr <= 1e-3:
        raise ValueError('Invalid learning rate')
    if not args.teacher_init_checkpoint and channels != (96,48,24,12):
        raise ValueError('Changed channel widths require teacher initialization')
    prepared, trained, output = args.prepared.resolve(), args.trained.resolve(), args.output.resolve()
    binding = json.loads((trained/'input-binding.json').read_text())
    if Path(binding['prepared']).resolve() != prepared:
        raise ValueError('Prepared corpus differs from the trained model binding')
    script = prepared/'upstream/tools/train_roota_piper_decoder_student.py'
    initial = args.teacher_init_checkpoint or trained/'voice/joint/decoder-student.pt'
    pack = trained/'voice/train512-decoder'
    cut = trained/'voice/decoder-cut/zh_CN-xiao_ya-medium-decoder-from-generator-input.onnx'
    inputs = [script, initial, pack/'rows.json', cut, prepared/'prepared.json',
              trained/'voice/joint/latent-student.pt', trained/'voice/duration/duration-student.pt']
    inputs_sha = {str(p): sha(p) for p in inputs}
    receipt = output/'repair-receipt.json'
    if output.exists():
        raise ValueError('Use a fresh output directory to preserve the previous experiment')
    output.mkdir(parents=True)
    init_args = ['--teacher-init-checkpoint', str(initial), '--teacher-init-method', 'importance'] if args.teacher_init_checkpoint else ['--init-decoder-checkpoint', str(initial)]
    command = [str(args.python), str(script), '--pack-dir', str(pack),
        '--eval-pack-dir', str(trained/'voice/eval128'), '--teacher-decoder', str(cut),
        *init_args, '--out-dir', str(output),
        '--variant', 'piperlite', '--channels', args.channels,
        '--steps', str(args.steps), '--lr', str(args.lr), '--batch-size', '4', '--crop-frames', '64',
        '--spectral-weight', '0.5', '--frame-comb-weight', '0.05',
        '--quiet-ceiling-weight', '0.02', '--quiet-ceiling-margin-db', '3',
        '--adv-weight', '0.1' if args.perceptual_losses else '0',
        '--adv-feature-weight', '1' if args.perceptual_losses else '0',
        '--mel-loss-weight', '5' if args.perceptual_losses else '0',
        '--adv-start-step', '500',
        '--adv-delta-weight', '0', '--adv-delta-feature-weight', '0',
        '--render-rows', '3', '--skip-chunk-eval', '--log-interval', '100', '--device', 'cuda']
    report = {'status': 'running', 'command': command, 'input_sha256': inputs_sha,
        'prepared_binding': binding, 'quality_passed': False,
        'human_feedback': {'teacher_latent_small_decoder': 'severe electronic artifacts',
                           'small_acoustic_teacher_decoder': 'mild electronic artifacts'},
        'scope': 'Teacher latent to waveform only; no acoustic or duration updates; no deployment',
        'training_hypothesis': 'Teacher-initialized wider decoder and perceptual waveform training' if args.teacher_init_checkpoint else 'Reduce frame-hop comb excess and excess energy in teacher-quiet frames',
        'channels': channels, 'perceptual_losses': args.perceptual_losses,
        'driver_sha256': sha(Path(__file__))}
    def save():
        receipt.write_text(json.dumps(report, indent=2)+'\n')
    save()
    start = time.monotonic()
    try:
        with (output/'training.log').open('w') as log:
            env = os.environ.copy()
            env.update(OMP_NUM_THREADS='4',MKL_NUM_THREADS='4',OPENBLAS_NUM_THREADS='4',PYTHONUNBUFFERED='1')
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env)
        report.update(exit_code=result.returncode, elapsed_seconds=time.monotonic()-start,
                      status='completed' if result.returncode == 0 else 'failed')
        if result.returncode == 0:
            report['output_sha256'] = {name: sha(output/name) for name in
                                      ['decoder-student.pt', 'train-report.json']}
        if any(sha(Path(path)) != digest for path,digest in inputs_sha.items()):
            raise RuntimeError('An input changed during isolated repair')
    except BaseException as exc:
        report.update(status='failed', error=repr(exc))
        raise
    finally:
        save()
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
