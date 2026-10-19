#!/usr/bin/env python3
"""Collect scoped upstream comparisons; never infer authorship from an added file."""
import argparse
import concurrent.futures
import difflib
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.error
import urllib.request


def git(root, *args, required=True):
    run = subprocess.run(['git', '-C', str(root), *args], capture_output=True)
    if required and run.returncode:
        raise RuntimeError(run.stderr.decode(errors='replace'))
    return run.stdout if not run.returncode else None


def request(url):
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'openvela-technical-evidence-audit'})
        with urllib.request.urlopen(req, timeout=20) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, b''
    except (OSError, urllib.error.URLError) as error:
        return type(error).__name__, str(error).encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--nuttx', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--check-public', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    repo = args.source_root.resolve()
    nuttx = args.nuttx.resolve()
    reference = git(nuttx, 'rev-parse', 'HEAD').decode().strip()
    tree = git(nuttx, 'rev-parse', 'HEAD^{tree}').decode().strip()
    urls = [
        ('openvela_reference', 'open-vela/nuttx', reference),
        ('declared_nuttx_base', 'streetartist/esp32p4_nuttx', '2f1387d56eb04ad2599baca58a3fa2380cdaaedb'),
        ('declared_apps_base', 'streetartist/esp32p4_nuttx_apps', '88827afd368d4bbb4802b96ed44d9582f85b2f92'),
        ('legacy_team_nuttx', 'flash555588/esp32p4_nuttx', 'cd61ccdd11498e22c058c3b2540828f88e23172e'),
    ]
    receipts = []
    if args.check_public:
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            pending = [(name, owner, commit, pool.submit(request, f'https://api.github.com/repos/{owner}/commits/{commit}')) for name, owner, commit in urls]
            for name, owner, commit, future in pending:
                status, raw = future.result()
                row = dict(name=name, repository=owner, requested_commit=commit, http_status=status)
                if status == 200:
                    value = json.loads(raw)
                    row.update(commit=value['sha'], tree=value['commit']['tree']['sha'], parents=[p['sha'] for p in value['parents']], url=value['html_url'])
                else:
                    row['meaning'] = 'Anonymous lookup unavailable; not proof of private status or nonexistent commit.'
                receipts.append(row)
    official = next((r for r in receipts if r['name'] == 'openvela_reference'), {})
    public_tree_verified = official.get('http_status') == 200 and official.get('tree') == tree
    prefix = 'app/espdl-quickapp/overlay/nuttx/'
    groups = {
        'context': [
            (prefix+'arch/risc-v/src/common/riscv_exception_common.S', 'arch/risc-v/src/common/riscv_exception_common.S'),
            (prefix+'arch/risc-v/src/common/riscv_saveusercontext.S', 'arch/risc-v/src/common/riscv_saveusercontext.S'),
            (prefix+'arch/risc-v/src/esp32p4/riscv_extctx.S', 'arch/risc-v/src/esp32p4/riscv_extctx.S'),
            (prefix+'arch/risc-v/src/esp32p4/esp32p4_extctx.c', 'arch/risc-v/src/esp32p4/esp32p4_extctx.c'),
        ],
        'startup': [
            ('chip/esp32p4/common-espressif/esp_start.c', 'arch/risc-v/src/common/espressif/esp_start.c'),
            (prefix+'arch/risc-v/src/common/espressif/esp_spiflash.c', 'arch/risc-v/src/common/espressif/esp_spiflash.c'),
        ],
        'display': [('chip/esp32p4/common-espressif/esp_mipi_dsi.c', 'arch/risc-v/src/common/espressif/esp_mipi_dsi.c')],
        'ppa': [(prefix+'arch/risc-v/src/esp32p4/esp32p4_ppa.c', 'arch/risc-v/src/esp32p4/esp32p4_ppa.c')],
    }
    declared = next((r for r in receipts if r['name'] == 'declared_nuttx_base' and r.get('http_status') == 200), None)
    rows = []
    for group, files in groups.items():
        chunks = []
        declared_chunks = []
        for local, remote in files:
            source = repo/local
            if not source.is_file():
                rows.append(dict(group=group, source=local, status='source_missing'))
                continue
            after = source.read_bytes()
            before = git(nuttx, 'show', reference+':'+remote, required=False)
            existed = before is not None
            before = before or b''
            chunks.extend(difflib.unified_diff(before.decode('utf-8').replace('\r\n', '\n').splitlines(True), after.decode('utf-8').replace('\r\n', '\n').splitlines(True), fromfile='a/'+remote if existed else '/dev/null', tofile='b/'+remote))
            row = dict(group=group, source=local, reference_path=remote, reference_commit=reference, reference_file_exists=existed, source_sha256=hashlib.sha256(after).hexdigest(), reference_sha256=hashlib.sha256(before).hexdigest() if existed else None)
            if existed:
                target = out/'reference'/remote
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(before)
            if declared:
                url = f'https://raw.githubusercontent.com/{declared["repository"]}/{declared["commit"]}/{remote}'
                status, data = request(url)
                row['declared_base_http_status'] = status
                if status == 200:
                    row['declared_base_sha256'] = hashlib.sha256(data).hexdigest()
                    row['matches_declared_base_after_lf_normalization'] = data.replace(b'\r\n',b'\n') == after.replace(b'\r\n',b'\n')
                    target = out/'declared-base'/remote
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
                    declared_chunks.extend(difflib.unified_diff(data.decode('utf-8').replace('\r\n', '\n').splitlines(True), after.decode('utf-8').replace('\r\n', '\n').splitlines(True), fromfile='a/'+remote, tofile='b/'+remote))
                elif status == 404:
                    declared_chunks.extend(difflib.unified_diff([], after.decode('utf-8').replace('\r\n', '\n').splitlines(True), fromfile='/dev/null', tofile='b/'+remote))
            rows.append(row)
        (out/(group+'.patch')).write_text(''.join(chunks), encoding='utf-8')
        if declared:
            (out/(group+'-declared-base.patch')).write_text(''.join(declared_chunks), encoding='utf-8')
    for name in ['LICENSE', 'NOTICE']:
        data = git(nuttx, 'show', reference+':'+name, required=False)
        if data is not None:
            (out/name).write_bytes(data)
    metadata = dict(reference_repository='https://github.com/open-vela/nuttx', reference_commit=reference, reference_tree=tree, reference_tree_verified_against_github=public_tree_verified, reference_role='Public comparison point, not automatically the original import ancestor.', public_lookup=receipts, files=rows, attribution_limit='New relative to this reference does not mean independently authored. Exact historical import ancestry remains separate.', patch_line_endings='LF normalized for comparison; raw byte hashes retained.')
    (out/'upstream-comparison.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(reference_commit=reference, public_tree_verified=public_tree_verified, files=len(rows), public_lookup=[{k:r.get(k) for k in ['name','http_status']} for r in receipts])))


if __name__ == '__main__':
    main()
