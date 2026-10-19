#!/usr/bin/env python3
"""Run the offline ESP-TTS regression suite and optionally verify a linked image."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from esp_tts_inputs import upstream_lock, verify_prepared,voice_variants


def verify_firmware(folder, toolchain):
    folder = folder.resolve()
    metadata = json.loads((folder/'BUILD-METADATA.json').read_text())
    compile_record = json.loads((folder/'speech/compile.json').read_text())
    if metadata.get('tts_backend') != 'esp-tts' or compile_record.get('tts_backend') != 'esp-tts':
        raise ValueError('Firmware was not built with the official ESP-TTS backend')
    if compile_record.get('esp_tts_inputs') != upstream_lock():
        raise ValueError('Firmware uses a different ESP-TTS input lock')
    if metadata['speech']['compile'] != compile_record:
        raise ValueError('Firmware speech build records differ')
    for name in ['nuttx.bin', 'nuttx.elf', 'resolved.config']:
        if hashlib.sha256((folder/name).read_bytes()).hexdigest() != metadata['sha256'][name]:
            raise ValueError('Firmware artifact hash differs: ' + name)
    if compile_record['config_sha256'] != metadata['sha256']['resolved.config']:
        raise ValueError('Speech was compiled with a different board configuration')
    archive = folder/'speech/libspeech_nuttx.a'
    if hashlib.sha256(archive.read_bytes()).hexdigest() != compile_record['archive_sha256']:
        raise ValueError('Speech archive hash differs')
    for name, digest in compile_record['esp_tts_library_sha256'].items():
        if hashlib.sha256((folder/'speech/lib'/name).read_bytes()).hexdigest() != digest:
            raise ValueError('Adapted official library hash differs: ' + name)
    nm = subprocess.check_output([str(toolchain/'riscv32-esp-elf-nm'), '-C', str(folder/'nuttx.elf')], text=True)
    for symbol in ['glass_esp_synthesize', 'esp_tts_create', 'esp_tts_stream_play', 'glass_esp_tts_voice_load']:
        if not re.search(r'\b'+symbol+r'$', nm, re.M):
            raise ValueError('Missing linked ESP-TTS symbol: ' + symbol)
    if re.search(r'\bglass_sano_synthesize$', nm, re.M):
        raise ValueError('Legacy TTS backend found in ESP-TTS image')
    if compile_record.get('esp_tts_voice_storage')=='sd':
        for name,target,row in voice_variants():
            if hashlib.sha256((folder/'speech/sdcard/speech/esp-tts'/target).read_bytes()).hexdigest()!=row['sha256']:
                raise ValueError('Packaged SD voice differs from the official input: '+name)
        if re.search(r'\bglass_esp_voice_data$',nm,re.M):raise ValueError('SD voice build still embeds duplicate voice bytes')
    ebss = int(re.search(r'^([0-9a-f]+) \w _ebss$', nm, re.M).group(1), 16)
    if ebss > 0x4ffaefc0 or ebss != int(metadata['ebss'], 0):
        raise ValueError('Unexpected P4 early-boot BSS boundary')
    if 0x2000 + (folder/'nuttx.bin').stat().st_size > int(metadata['data_offset'], 0):
        raise ValueError('Firmware overlaps the data partition')
    return {'sha256': metadata['sha256']['nuttx.bin'], 'bytes': (folder/'nuttx.bin').stat().st_size,
            'ebss': hex(ebss), 'tts_backend': 'esp-tts'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--toolchain', type=Path)
    args = parser.parse_args()
    if args.firmware and not args.toolchain:
        parser.error('--firmware requires --toolchain')
    repo = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'board_tested': False, 'vendor_engine_executed': False,
              'audio_quality_passed': None, 'stages': []}
    try:
        lock = verify_prepared(args.prepared)
        report['upstream_revision'] = lock['revision']
        report['stages'].append({'name': 'official-inputs', 'passed': True})
        stages = [
            ('input-recovery', 'tests/test_esp_tts_inputs.py', ['--prepared', args.prepared.resolve()]),
            ('adapter', 'test_esp_tts_host.py', ['--prepared', args.prepared.resolve(), '--output', out/'adapter']),
            ('sd-voice-integrity', 'test_esp_tts_voice.py', ['--prepared', args.prepared.resolve(), '--output', out/'voice']),
            ('prebuffer', 'test_speech_buffer.py', ['--output', out/'prebuffer']),
            ('phrase-loudness', 'test_speech_gain.py', ['--output', out/'gain']),
            ('wake-audio-handoff', 'test_wake_handoff.py', ['--output', out/'wake-handoff']),
            ('pcm-framing', 'tests/test_music_pcm_framing.py', []),
            ('codec-errors', 'test_es8311_return.py', []),
        ]
        for name, script, options in stages:
            print('ESP_TTS_CHECK', name, flush=True)
            command = list(map(str, [sys.executable, repo/'tools'/script, *options]))
            with (out/(name+'.log')).open('w') as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=180)
            report['stages'].append({'name': name, 'passed': result.returncode == 0, 'log': name+'.log'})
            if result.returncode:
                raise RuntimeError(name+' failed; see '+str(out/(name+'.log')))
        if args.firmware:
            report['firmware'] = verify_firmware(args.firmware, args.toolchain.resolve())
            report['stages'].append({'name': 'linked-image', 'passed': True})
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (out/'verification.json').write_text(json.dumps(report, indent=2)+'\n')
    print('ESP_TTS_VERIFIED', out/'verification.json')


if __name__ == '__main__':
    main()
