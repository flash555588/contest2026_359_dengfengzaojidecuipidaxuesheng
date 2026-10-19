#!/usr/bin/env python3
"""Package allowlisted technical metrics and excerpts, excluding private raw data."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_bytes((json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))


def verify(out):
    manifest = json.loads((out/'manifest.json').read_text(encoding='utf-8'))
    for row in manifest['files']:
        path = (out/row['path']).resolve()
        path.relative_to(out.resolve())
        assert path.is_file() and digest(path) == row['sha256'], row['path']
    print('TECHNICAL_EVIDENCE_OK', len(manifest['files']), 'files')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    if args.verify:
        verify(out)
        return
    assert args.run is not None
    run = args.run.resolve()
    repo = Path(__file__).resolve().parents[1]
    workspace = repo.parent
    out.mkdir(parents=True, exist_ok=False)
    index = json.loads((repo/'docs/evidence/technical-contributions-20261019.json').read_text(encoding='utf-8'))
    metrics = index['metrics']
    selected = {
        'T03': metrics['T03'],
        'T04': {k:metrics['T04'][k] for k in ['firmware_sha256','render_reduction_percent','summed_stage_reduction_percent']},
        'T05': {k:metrics['T05'][k] for k in ['firmware_sha256','playback_480_decode_reduction_percent']},
        'T06': {'host_fixture_devices':512,'card_pool':12,'host_widgets':325,'board_startup_samples':metrics['T06']['startup_samples']},
        'T07': metrics['T07'], 'T08':metrics['T08'], 'T09':metrics['T09'],
        'T10': {'format':metrics['T10']['format'],'recordings':metrics['T10']['board']['recordings'],'fault_detected':metrics['T10']['board']['fault_detected']},
    }
    write_json(out/'historical-metrics.json',dict(scope='Historical snapshots only; not new device measurements.',metrics=selected,original_records=index['evidence_inventory']))
    allowed = re.compile(r'^(?:\[local-video\] profile samples=|\[media-cpu\] |\[nano\] (?:backend=|kernel_cases=|pcm_sha256=)|\[qpk\] started .*with QuickJS compile_ms=)')
    sources = ['media-v3-ppa-irq/paired848.log','media-v3-decode-affinity-scoped/cpu-status.log','media-v3-decode-affinity-scoped/play848.log','mijia-waterfall/board-verification/script-optimized.log','official-tts-q8-20261017/board-short-tx.log']
    excerpts = []
    for rel in sources:
        path = workspace/'output'/rel
        lines = []
        for number,line in enumerate(path.read_text(encoding='utf-8',errors='replace').splitlines(),1):
            clean = re.sub(r'\x1b\[[0-9;]*[A-Za-z]','',line).strip()
            if allowed.match(clean):
                lines.append(dict(line=number,text=clean))
        excerpts.append(dict(original=rel,original_sha256=digest(path),selected_lines=lines))
    write_json(out/'historical-excerpts.json',dict(selection='Only benchmark counters and named built-in application compilation timings; no raw sessions, addresses, configuration or media.',records=excerpts))
    build = json.loads((run/'build-status-final.json').read_text())
    assert build['linked'] and build['build_passed_limits'] and not build['board_written']
    public_build = {k:build[k] for k in ['linked','exit_code','completed_at_utc','sha256','firmware_bytes','ebss','startup_ram_within_limit','program_limit','build_passed_limits','board_written','source_snapshot']}
    public_build['compiler'] = json.loads((run/'firmware/BUILD-METADATA.json').read_text())['compiler']
    public_build['reused_libraries'] = [{'name':Path(k).name,'sha256':v} for k,v in build['reused_libraries'].items()]
    write_json(out/'current-build.json',public_build)
    snapshot = run/'snapshot-final'
    shutil.copyfile(snapshot/'source-manifest.json',out/'source-manifest.json')
    tests = [
        ('javascript','js-tests.log',r'# pass 179\b'),
        ('python_unittest_and_import_time_c_harnesses','python-tests.log',r'(?m)^OK$'),
        ('agent_mcp_home','agent-tests.log',r'BOARD_HOME_OK:'),
        ('stream_http_memory_and_deadlines','chat-memory-tests.log',r'CHAT_HTTP_MEMORY_OK:'),
        ('quickjs_security','runtime-security-final.log',r'PASS: real QuickJS exception hooks blocked'),
        ('quickjs_compile_cache','compile-cache-tests.log',r'COMPILE_CACHE_OK'),
        ('camera_video','camera-video-tests.log',r'CAMERA_AV_SYNC_OK'),
        ('wake_capture_handoff','wake-handoff-tests.log',r'VOICE_CONNECT_CANCEL_OK'),
    ]
    rows = []
    for name,relative,pattern in tests:
        path = run/relative
        text = path.read_text(encoding='utf-8',errors='replace')
        passed = bool(re.search(pattern,text)) and not re.search(r'Traceback|ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:',text)
        assert passed, name
        lines = [line for line in text.splitlines() if re.match(r'^(?:[A-Z_]+_OK[: ]|PASS: real QuickJS|CAMERA_AV_SYNC_OK|# (?:tests|pass|fail|skipped) |Ran \d+ tests|OK$)',line)]
        rows.append(dict(name=name,passed=True,raw_log=relative,raw_log_sha256=digest(path),summary_lines=lines))
    write_json(out/'current-host-tests.json',dict(scope='Host and synthetic adapters only, not real device or human acceptance.',tests=rows,fixture_fixes=['Temporary desktop/c6probe include layout restored','Source/package LF bytes preserved by attributes','Static assertion checks authorization assignments; readonly page diagnostics allowed','QuickJS security harness updated for current inert presentation adapters; original adversarial cases retained']))
    shutil.copytree(run/'upstream',out/'upstream')
    # Keep only hashes for private full logs, not their contents or Flash dumps.
    write_json(out/'retained-attempts.json',dict(failures=[dict(file=name,sha256=digest(run/name),reason='Stale native-presentation test adapters; corrected before final pass.') for name in ['runtime-security-tests.log','runtime-security-fixed.log']]))
    hashes = []
    for path in sorted(out.rglob('*')):
        if path.is_file():
            hashes.append(dict(path=path.relative_to(out).as_posix(),bytes=path.stat().st_size,sha256=digest(path)))
    write_json(out/'manifest.json',dict(schema_version=1,files=hashes,authenticity_limit='Checksums verify byte integrity only; summaries do not substitute for raw measurements or certify authorship.'))
    verify(out)


if __name__ == '__main__':
    main()
