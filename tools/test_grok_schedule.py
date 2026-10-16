#!/usr/bin/env python3
"""Compare actual before/after LVGL frame pixels and scheduling under sanitizers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('apps', 'cache', 'assets', 'before', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    desktop = repo / 'app/espdl-quickapp/overlay/apps/system/desktop'
    args.output.mkdir(parents=True, exist_ok=True)
    objects = sorted(args.cache.glob('lvgl-*.o'))
    assert objects, 'Prepared LVGL host cache is required'
    flags = ['-std=gnu11', '-O1', '-g', '-pthread', '-fsanitize=address,undefined',
             '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
             '-DLV_CONF_INCLUDE_SIMPLE', '-DGROK_ASSET_ROOT="' + str(args.assets) + '"']
    results = {}
    for name, source in [('before', args.before), ('after', desktop)]:
        binary = args.output / name
        cmd = ['gcc', *flags, '-I' + str(source), '-I' + str(desktop),
               '-I' + str(args.apps / 'graphics/lvgl'), '-I' + str(args.cache),
               str(repo / 'app/espdl-quickapp/tests/grok_schedule_host.c'),
               str(desktop / 'glass_grok_state.c'), *map(str, objects), '-lm', '-o', str(binary)]
        with (args.output / (name + '-compile.log')).open('w') as log:
            subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
        result = subprocess.run([str(binary), str(args.output / (name + '.trace'))],
                                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1',
                                     'UBSAN_OPTIONS': 'halt_on_error=1'}, capture_output=True, text=True)
        (args.output / (name + '.log')).write_text(result.stdout + result.stderr)
        print(name, result.stdout + result.stderr, flush=True)
        result.check_returncode()
        results[name] = json.loads(re.search(r'^METRIC (.*)$', result.stdout, re.M)[1])
    before = (args.output / 'before.trace').read_bytes()
    after = (args.output / 'after.trace').read_bytes()
    assert before == after, 'Frame order, source pixels or rendered framebuffer changed'
    assert results['before']['frames'] == results['after']['frames'] == 100
    assert results['after']['callbacks'] < results['before']['callbacks']
    assert results['after']['seeks'] < results['before']['seeks']
    report = {'method': 'deterministic host scheduling with real LVGL; not board FPS/CPU',
              'identical_frame_hashes': len(before.splitlines()), 'metrics': results,
              'source_sha256': {name: hashlib.sha256((folder / 'glass_grok_player.inc').read_bytes()).hexdigest()
                                for name, folder in [('before', args.before), ('after', desktop)]},
              'trace_sha256': hashlib.sha256(after).hexdigest()}
    for key in ('callbacks', 'seeks'):
        report[key + '_reduction_percent'] = 100 * (1 - results['after'][key] / results['before'][key])
    (args.output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
