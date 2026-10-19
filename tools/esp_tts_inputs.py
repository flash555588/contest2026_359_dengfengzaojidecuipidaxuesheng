#!/usr/bin/env python3
"""Verify ESP-TTS inputs against the repository's authoritative upstream lock."""
import hashlib
import json
from pathlib import Path, PurePosixPath


LOCK_PATH = Path(__file__).resolve().parents[1] / 'app/espdl-quickapp/speech/esp-tts/upstream.lock.json'


def upstream_lock():
    return json.loads(LOCK_PATH.read_text(encoding='utf-8'))


def voice_variants(lock=None):
    lock = lock or upstream_lock()
    names = [('xiaoxin', 'voice.dat', 'esp_tts_voice_data_xiaoxin.dat'),
             ('xiaole', 'voice-xiaole.dat', 'esp_tts_voice_data_xiaole.dat'),
             ('small', 'voice-small.dat', 'esp_tts_voice_data_xiaoxin_small.dat')]
    return [(name, target, next(row for row in lock['sources'] if row['path'].endswith('/'+source)))
            for name, target, source in names]


def voice_config(directory):
    rows = []
    for name, target, row in voice_variants():
        digest = ','.join('0x'+row['sha256'][i:i+2] for i in range(0,64,2))
        rows.append('{'+json.dumps(name)+','+json.dumps(str(directory).rstrip('/')+'/'+target)+','+
                    str(row['bytes'])+',{'+digest+'}}')
    return ('struct glass_esp_voice_source { const char *name,*path; size_t bytes; unsigned char sha256[32]; };\n'
            'static const struct glass_esp_voice_source glass_esp_tts_voices[]={'+','.join(rows)+'};\n'
            '#define GLASS_ESP_TTS_VOICE_PATH '+json.dumps(str(directory).rstrip('/')+'/voice.dat')+'\n')


def source_path(root, row):
    relative = PurePosixPath(row['path'])
    if (relative.is_absolute() or '..' in relative.parts or
            '\\' in row['path'] or ':' in row['path'] or not relative.parts):
        raise ValueError('Unsafe ESP-TTS source path: ' + row['path'])
    root = Path(root).resolve()
    target = (root / Path(*relative.parts)).resolve()
    if not target.is_relative_to(root):
        raise ValueError('ESP-TTS source escapes prepared directory: ' + row['path'])
    return target


def verify_bytes(data, row):
    if len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
        raise ValueError('Official ESP-TTS input differs from repository lock: ' + row['path'])
    blob = b'blob ' + str(len(data)).encode('ascii') + b'\0' + data
    if hashlib.sha1(blob).hexdigest() != row['git_blob_sha1']:
        raise ValueError('ESP-TTS Git blob differs: ' + row['path'])


def verify_prepared(prepared):
    prepared = Path(prepared).resolve()
    lock = upstream_lock()
    supplied = json.loads((prepared / 'upstream.lock.json').read_text(encoding='utf-8'))
    if supplied != lock:
        raise ValueError('Prepared ESP-TTS lock differs from the repository lock; re-run prepare_esp_tts.py')
    for row in lock['sources']:
        verify_bytes(source_path(prepared / 'upstream', row).read_bytes(), row)
    return lock
