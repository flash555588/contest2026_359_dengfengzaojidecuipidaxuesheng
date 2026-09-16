"""Real QuickJS compile-only checks, pinned examples and failure-safe persistence."""
from pathlib import Path
import errno
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from prepare_qpk_guide import prepare_qpk_guide
from qpk_test_build import qpk_test_inputs

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/espdl-quickapp'
overlay = delivery / 'overlay/apps/system'
build = Path('/tmp/v3-desktop-espdl-20260915')
cjson = build / 'apps/netutils/cjson/cJSON'
catalog = prepare_qpk_guide(overlay)
flags, support, library = qpk_test_inputs(build, overlay)
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
checks = []


def node(*paths):
    executable = shutil.which('node') or shutil.which('node.exe')
    if not executable:
        raise RuntimeError('The example behavior checks require an installed Node.js runtime')
    args = list(map(str, paths))
    if executable.endswith('.exe') and os.name != 'nt':
        args = [subprocess.check_output(['wslpath', '-w', p], text=True).strip() for p in args]
    subprocess.run([executable, *args], check=True)

with tempfile.TemporaryDirectory(prefix='qpk-authoring-') as directory:
    temp = Path(directory)
    qpk = temp / 'qpk'
    binary = temp / 'runner'
    # The tool bridge is exercised by test_chat_core; this binary owns storage.
    sources = [s for s in support if s.name not in ('espclaw_qpk.c', 'claw_stream.c')]
    subprocess.run(['cc', '-std=gnu11', '-g', '-O1', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-pthread', *flags,
                    '-I' + str(overlay / 'desktop'), '-I' + str(cjson),
                    f'-DQPK_BUILDER_ROOT="{qpk}"', str(ws / 'diagnostics/qpk-tests/runner.c'),
                    *map(str, sources), str(overlay / 'desktop/glass_chat_store.c'),
                    str(cjson / 'cJSON.c'), str(library), '-lm', '-o', str(binary)], check=True)

    def run(*args, raw=False):
        result = subprocess.run([str(binary), *map(str, args)], env=env, text=True,
                                capture_output=True, timeout=15)
        if result.returncode:
            print(result.stdout); print(result.stderr); result.check_returncode()
        return result.stdout if raw else json.loads(result.stdout)

    def write(source, revision=0, slug='counter', name='随手计数', cancel=False, **extra):
        request = temp / 'request.json'
        request.write_text(json.dumps(dict(name=name, slug=slug, source=source,
                                          base_revision=revision, **extra), ensure_ascii=False), encoding='utf-8')
        return run('write', request, *(['cancel'] if cancel else []))

    def fresh():
        if qpk.exists():
            shutil.rmtree(qpk)

    listed = run('list')
    assert listed['ok'] and len(listed['examples']) == 3 and listed['file_check_error'] == 0
    embedded_guide = run('guide', raw=True)
    hardware_guide = (overlay / 'espclaw/guide/HARDWARE_API.md').read_text(encoding='utf-8')
    assert len(embedded_guide.encode()) > 20000 and hardware_guide in embedded_guide
    for snippet in re.findall(r'```javascript\n(.*?)\n```', hardware_guide, re.S):
        fresh()
        compiled = write(snippet)
        assert compiled['ok'], (compiled, snippet)
    fresh()
    checks.append('Complete hardware API reference is returned to AI; every hardware JavaScript snippet compiles with production QuickJS')
    run('list')
    for example in catalog:
        for filename in example['files']:
            chunks, line = [], 1
            while line is not None:
                part = run('read', example['id'], filename, line, 40)
                assert part['ok'] and part['verified_source']
                assert len(part['content'].encode()) <= 6144
                chunks.append(part['content']); line = part['next_line']
            actual = ''.join(chunks).encode()
            expected = (overlay / 'espclaw/qpk' / example['id'] / filename).read_bytes()
            assert actual == expected, (example['id'], filename)
        assert hashlib.sha256((qpk / '.examples' / example['id'] / 'app.js').read_bytes()).hexdigest() == example['source_sha256']
    assert run('read', '../.data', 'value', 1, 20)['error'] == 'not_an_example'
    assert not run('read', 'hello', '../../config/ai.json', 1, 20)['ok']
    assert not run('read', 'hello', 'app.js', 0, 20)['ok']
    assert not run('read', 'hello', 'app.js', 1, 161)['ok']
    checks.append('All three pinned projects read from actual qpk files, UTF-8 pagination and source SHA-256; paths and bounds restricted')

    target = qpk / '.examples/hello/app.js'
    target.write_text('changed source', encoding='utf-8')
    assert run('read', 'hello', 'app.js', 1, 20)['error'] == 'example_changed'
    assert target.read_text() == 'changed source'
    target.unlink()
    private = temp / 'private.txt'; private.write_text('private-test-sentinel')
    target.symlink_to(private)
    refused = run('read', 'hello', 'app.js', 1, 20)
    assert not refused['ok'] and 'private-test-sentinel' not in json.dumps(refused)
    assert private.read_text() == 'private-test-sentinel'
    checks.append('Changed examples and symlinks refused without overwriting files or returning private data')

    for example in catalog:
        fresh()
        source = (overlay / 'espclaw/qpk' / example['id'] / 'app.js').read_text(encoding='utf-8')
        assert write(source, slug=example['id'])['ok'], example['id']
    fresh()
    # Successful validation must not evaluate the program or require UI globals.
    assert write("throw new Error('validation must not execute this');")['ok']
    checks.append('Pinned scripts compile with the firmware QuickJS; compile-only never executes app code')

    guide = (overlay / 'espclaw/guide/QUICKAPP_GUIDE.md').read_text(encoding='utf-8')
    # The guide has to teach the slicing API, not only the timer primitives:
    # a long computation is sliced with await system.yield() instead of being
    # killed by the callback budget.
    assert 'setTimeout' in guide and 'clearTimeout' in guide
    slicing = [block for block in re.findall(r'```javascript\n(.*?)\n```', guide, re.S)
               if 'system.yield' in block]
    assert len(slicing) == 1, 'guide needs exactly one system.yield example'
    fresh()
    assert write(slicing[0], name='切片示例')['ok'], 'the guide slicing example must compile'
    checks.append('Guide documents setTimeout/clearTimeout and a compiling system.yield example')

    fresh()
    counter = re.findall(r'```javascript\n(.*?)\n```', guide, re.S)[-1]
    counter_path = temp / 'guide-counter.js'; counter_path.write_text(counter, encoding='utf-8')
    created = write(counter, name='计数 "A"')
    assert created['ok'] and created['revision'] == 1 and not created['previewed']
    state = run('state'); assert state['revision'] == 1 and state['source'] == counter
    before = {p.name: p.read_bytes() for p in (qpk / '.draft').iterdir()}
    assert write('ui.button(', 1)['error'] == 'syntax_error'
    assert write(counter, 0)['error'] == 'stale_revision'
    assert write(counter, 1, cancel=True)['error'] == 'cancelled'
    for invalid in ('../bad', 'com.openvela.ha', '', 'UPPER', 'x' * 21):
        assert not write(counter, 1, slug=invalid)['ok']
    assert not write(counter, 1, name='中' * 16)['ok']
    assert not write(counter + '\x00', 1)['ok']
    assert not write(counter, 1, unexpected=True)['ok']
    assert before == {p.name: p.read_bytes() for p in (qpk / '.draft').iterdir()}
    checks.append('Complete guide counter compiles; syntax errors, stale revisions, cancellation, NUL, size/name/slug/schema bounds preserve draft')

    second = counter + '\n// revision 2\n'
    assert write(second, 1, name='计数 "A"')['revision'] == 2
    assert run('state')['source'] == second
    slots = sorted((qpk / '.draft').glob('*.json'))
    newest = max(slots, key=lambda p: json.loads(p.read_text())['revision'])
    older = next(p for p in slots if p != newest)
    protected = older.read_bytes()
    newest.write_bytes(newest.read_bytes()[:70])
    assert run('state')['revision'] == 1
    assert write(second, 1, name='计数 "A"')['ok']
    assert older.read_bytes() == protected
    assert run('state')['revision'] == 2
    # A failed destination cannot destroy the current valid source snapshot.
    old_backup = temp / 'old-slot.json'
    older.rename(old_backup); older.mkdir()
    newest_bytes = newest.read_bytes()
    assert not write(counter, 2)['ok']
    assert newest.read_bytes() == newest_bytes and run('state')['revision'] == 2
    older.rmdir(); old_backup.rename(older)
    checks.append('Process restart, partial newer snapshot recovery, SHA-256 and repair preserve last valid snapshot')

    assert run('save', 2)['code'] == -errno.EAGAIN
    assert run('save', 1, 'preview')['code'] == -errno.ESTALE
    assert run('save', 2, 'preview', 'runtime error')['code'] == -errno.EAGAIN
    saved = run('save', 2, 'preview'); assert saved['ok'] and saved['saved_revision'] == 2
    package = qpk / 'ai.counter'
    assert (package / 'app.js').read_text(encoding='utf-8') == second
    manifest = json.loads((package / 'manifest.json').read_text(encoding='utf-8'))
    assert manifest['name'] == '计数 "A"' and manifest['entry'] == 'app.js' and manifest['package'] == 'ai.counter'
    state = run('state')
    assert state['saved_revision'] == 2 and state['previewed_revision'] == 0
    assert run('save', 2, 'preview')['ok']
    installed_bytes = {p.name: p.read_bytes() for p in package.iterdir()}
    assert write(counter, 2)['revision'] == 3
    assert run('save', 3, 'preview')['code'] == -errno.EEXIST
    assert installed_bytes == {p.name: p.read_bytes() for p in package.iterdir()}
    checks.append('Save requires current successful preview, creates a valid independent package, survives reboot, is idempotent and never overwrites a different installed app')

    # Discard resets the visible revision; an independent durable sequence
    # orders snapshots. Cancelled requests cannot revive the abandoned idea.
    unrelated = qpk / '.user-sentinel'; unrelated.write_text('keep')
    assert run('discard', 'async')['ok']
    state = run('state')
    assert not state['has_draft'] and state['source'] == '' and state['name'] == ''
    assert state['revision'] == 0 and state['saved_revision'] == 0
    assert installed_bytes == {p.name: p.read_bytes() for p in package.iterdir()}
    assert unrelated.read_text() == 'keep'
    assert write(counter, 3)['error'] == 'stale_revision'
    assert write(counter, 0, cancel=True)['error'] == 'cancelled'
    assert write(counter, 0)['revision'] == 1
    # A failed tombstone write keeps the current draft usable across restart.
    slots = sorted((qpk / '.draft').glob('*.json'))
    older = min(slots, key=lambda p: json.loads(p.read_text())['sequence'])
    old_bytes = older.read_bytes(); older.unlink(); older.mkdir()
    assert not run('discard')['ok']
    assert run('state')['source'] == counter and run('state')['revision'] == 1
    older.rmdir(); older.write_bytes(old_bytes)
    assert run('discard')['ok'] and not run('state')['has_draft']
    checks.append('Async discard survives restart, rejects stale/cancelled generation, allows a new idea, preserves installed apps/user files and retains the draft on storage failure')

    # Uninstall removes current/legacy data and nested assets, never follows
    # links, and must preserve other packages that share a storage prefix.
    data = qpk / '.data'
    assert run('storage-write', data, 'ai.counter')['ok']
    legacy = data / 'ai.counter'; legacy.mkdir(); (legacy / 'old.txt').write_text('old')
    (data / ('ai.counter'.encode().hex() + '_old.txt')).write_text('old')
    assets = package / 'assets'; assets.mkdir(); (assets / 'test.bin').write_bytes(b'asset')
    private.write_text('keep-private'); (assets / 'link').symlink_to(private)
    for invalid in ('.draft', '.examples', '../qpk', 'music', 'com.openvela.camera.preview'):
        assert run('remove', invalid, invalid)['code'] == -errno.EPERM
    assert run('remove', 'ai.counter', 'wrong.package')['code'] == -errno.ESTALE
    assert package.exists()
    assert run('remove', 'ai.counter', 'ai.counter')['ok']
    assert not package.exists() and not legacy.exists()
    assert list(data.iterdir()) == [] and private.read_text() == 'keep-private'
    assert run('state')['revision'] == 0 and unrelated.read_text() == 'keep'
    short = 'a' * 28
    long = short + 'b'
    for pkg in (short, long):
        app = qpk / pkg; app.mkdir()
        (app / 'manifest.json').write_text(json.dumps(dict(name=pkg, package=pkg)))
        assert run('storage-write', data, pkg)['ok']
    assert run('remove', short, short)['ok']
    assert (data / ('@2p' + short) / 'pb/kscore/value').read_text() == '42'
    assert run('remove', long, long)['ok'] and list(data.iterdir()) == []
    checks.append('Async uninstall removes nested app assets and all three storage layouts, protects built-ins/private paths/symlinks and preserves overlapping package storage prefixes')

    for p in (qpk / '.draft').glob('*.json'): p.write_bytes(b'{broken')
    broken = {p.name: p.read_bytes() for p in (qpk / '.draft').iterdir()}
    assert not run('state')['ok']
    assert not write(counter)['ok']
    assert broken == {p.name: p.read_bytes() for p in (qpk / '.draft').iterdir()}
    checks.append('Two corrupt draft copies are reported and preserved, never silently reset')

    # Read previous firmware records without rewriting their checksums, then
    # continue with the new sequence independent of the visible revision.
    fresh()
    (qpk / '.draft').mkdir(parents=True)
    legacy_record = dict(format=2, revision=9, name='', slug='', source='')
    canonical = json.dumps(legacy_record, separators=(',', ':')).encode()
    legacy_record['sha256'] = hashlib.sha256(canonical).hexdigest()
    (qpk / '.draft/draft0.json').write_text(json.dumps(legacy_record))
    assert run('state')['revision'] == 0 and not run('state')['has_draft']
    assert write(counter)['revision'] == 1
    assert run('state')['source'] == counter
    assert run('discard')['ok'] and run('state')['revision'] == 0
    assert write(counter)['revision'] == 1
    checks.append('Previous firmware tombstone migrates to visible revision zero; subsequent writes/discards remain durable and restart at version one')

    fresh()
    assert write(counter)['ok']
    for i in range(80):
        d = qpk / f'existing{i}'; d.mkdir(); (d / 'manifest.json').write_text('{"name":"existing"}')
    assert run('save', 1, 'preview')['ok']
    assert (qpk / 'ai.counter/app.js').read_text() == counter
    checks.append('Installation succeeds with 80 existing applications; no fixed application-count limit')

    fresh()
    large = counter + '\n/*' + 'x' * (2 * 1024 * 1024 - len(counter.encode()) - 5) + '*/'
    assert len(large.encode()) == 2 * 1024 * 1024
    assert write(large)['ok']
    assert run('state')['source'] == large
    assert run('save', 1, 'preview')['ok']
    assert (qpk / 'ai.counter/app.js').read_text() == large
    checks.append('2 MiB UTF-8 script compiles, survives restart and installs without truncation')

    node(delivery / 'tests/test_qpk_examples.js', overlay / 'espclaw/qpk', counter_path)
    node(delivery / 'tests/test_ouo_app.js', overlay / 'espclaw/qpk/ouo/app.js')
    checks.append('Hello, 2048, OuO and the guide counter pass behavior tests with explicit UI fixtures')

report = dict(asan=True, ubsan=True, quickjs='production firmware sources',
              external_ai_tested=False, hardware_tested=False,
              guide_sha256=hashlib.sha256((overlay / 'espclaw/guide/QUICKAPP_GUIDE.md').read_bytes()).hexdigest(),
              hardware_guide_sha256=hashlib.sha256(hardware_guide.encode()).hexdigest(),
              embedded_guide_sha256=hashlib.sha256(embedded_guide.encode()).hexdigest(),
              examples={e['id']: e['source_sha256'] for e in catalog}, checks=checks)
(delivery / 'evidence/qpk-authoring-validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, ensure_ascii=False, indent=2))
