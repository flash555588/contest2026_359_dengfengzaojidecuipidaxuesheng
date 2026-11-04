#!/usr/bin/env python3
"""Build the NetSurf browser as an openvela native-module app.

Runs on Linux (WSL). Upstream NetSurf sources are fetched at pinned
revisions into a separate work directory; this repository only holds the
openvela frontend, small port patches and this script.

Output (in --output):
  com.netsurf.browser/manifest.json
  com.netsurf.browser/app.js
  com.netsurf.browser/native/module.elf
  com.netsurf.browser/native/host.abi
  NetSurfBrowser-<version>.zip, BUILD.json

Nothing is flashed or installed. The module requires a firmware whose
native loader exports every symbol it imports (checked here against
desktop/native/exports.txt, and optionally against a firmware ELF).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
NATIVE = REPO / 'app/espdl-quickapp/overlay/apps/system/desktop/native'
DESKTOP = NATIVE.parent
LODEPNG = REPO / 'app/bilibili_live/local_vendor/lodepng'

GITHUB = 'https://github.com/netsurf-browser/{}.git'
PINS = json.loads((HERE / 'pins.json').read_text())
LIBS = ['libwapcaplet', 'libparserutils', 'libhubbub', 'libdom', 'libcss',
        'libnsutils', 'libnsbmp', 'libnsgif']
TRIPLE = 'riscv32-esp-elf'

# Loader-supported relocation types (NuttX libs/libc/machine/risc-v/arch_elf.c
# patches these). BRANCH/JAL/RVC_* are only accepted pre-resolved, and
# PCREL pairs depend on a small lookup table, so neither may remain.
ALLOWED_RELOCS = {'R_RISCV_32', 'R_RISCV_CALL', 'R_RISCV_CALL_PLT',
                  'R_RISCV_HI20', 'R_RISCV_LO12_I', 'R_RISCV_LO12_S'}


def log(msg):
    print(f'[netsurf] {msg}', flush=True)


def run(cmd, cwd=None, env=None, capture=False, quiet=False):
    if not quiet:
        log(' '.join(str(c) for c in cmd)[:400])
    r = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, text=True,
                       stdout=subprocess.PIPE if capture else None,
                       stderr=subprocess.STDOUT if capture else None)
    if r.returncode != 0:
        if capture:
            print(r.stdout[-6000:])
        raise SystemExit(f'command failed ({r.returncode}): {cmd[0]}')
    return r.stdout if capture else ''


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# --------------------------------------------------------------- sources

def fetch_sources(src, offline):
    src.mkdir(parents=True, exist_ok=True)
    for name, rev in PINS['netsurf'].items():
        d = src / name
        if not (d / '.git').exists():
            if offline:
                raise SystemExit(f'missing source {d} (offline)')
            run(['git', 'clone', '-q', '--filter=blob:none', GITHUB.format(name), d])
        head = run(['git', '-C', d, 'rev-parse', 'HEAD'], capture=True, quiet=True).strip()
        if head != rev:
            if offline:
                raise SystemExit(f'{name} is at {head}, pin is {rev}')
            run(['git', '-C', d, 'fetch', '-q', 'origin', rev])
            run(['git', '-C', d, 'checkout', '-q', '--force', rev])


def apply_patches(src):
    for repo_dir in sorted((HERE / 'port/patches').iterdir()):
        target = src / repo_dir.name
        for patch in sorted(repo_dir.glob('*.patch')):
            check = subprocess.run(['git', '-C', target, 'apply', '--reverse', '--check', patch],
                                   capture_output=True)
            if check.returncode == 0:
                continue  # already applied
            run(['git', '-C', target, 'apply', patch])
    # libcss keeps generated selection code in git; regenerate after patching
    run([sys.executable, 'src/select/select_generator.py'], cwd=src / 'libcss', capture=True)


def fetch_zlib(work, offline):
    z = PINS['zlib']
    archive = work / 'downloads' / z['archive']
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        if offline:
            raise SystemExit(f'missing {archive} (offline)')
        run(['curl', '-sSL', '-o', archive, z['url']])
    if sha256(archive) != z['sha256']:
        raise SystemExit(f'{archive} sha256 mismatch')
    out = work / 'src' / 'zlib'
    if not (out / 'zlib.h').exists():
        tmp = work / 'src' / '.zlib-unpack'
        shutil.rmtree(tmp, ignore_errors=True)
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(tmp)
        shutil.rmtree(out, ignore_errors=True)
        (tmp / z['dir']).rename(out)
        shutil.rmtree(tmp)
    return out


# ------------------------------------------------------------- toolchain

def firmware_flags(workspace, tc, env):
    """NuttX application CFLAGS exactly as the firmware's desktop app uses."""
    out = run(['make', '-s', '-C', workspace / 'apps/system/desktop',
               f'APPDIR={workspace / "apps"}', f'TOPDIR={workspace / "nuttx"}',
               f'CROSSDEV={tc}/{TRIPLE}-',
               '--eval=ovflags:;$(info OV_CFLAGS=$(CFLAGS))', 'ovflags'],
              env=env, capture=True, quiet=True)
    line = next(l for l in out.splitlines() if l.startswith('OV_CFLAGS='))
    flags = line.split('=', 1)[1].split()
    # drop debug info and warnings-as-errors; keep includes, defines, ABI
    drop = {'-g', '-g3', '"-g3"'}
    return [f for f in flags if f not in drop and not f.startswith('-Werror')]


def module_codegen():
    # medlow on RV32 reaches the whole address space with absolute HI20/LO12
    # pairs; no relaxation and no hot/cold splitting keep branches local.
    # -msmall-data-limit=0 keeps everything out of .sdata/.srodata/.sbss,
    # which NuttX's gnu-elf.ld does not merge, and avoids gp-relative code.
    return ['-mcmodel=medlow', '-mno-relax', '-msmall-data-limit=0',
            '-fno-common', '-fvisibility=hidden',
            '-fno-lto', '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
            '-fno-reorder-blocks-and-partition', '-ffunction-sections',
            '-fdata-sections', '-Os']


def base_cflags(workspace):
    return ['-march=rv32imafc', '-mabi=ilp32f', '-nostdlib',
            '-isystem', str(workspace / 'nuttx/include'), '-D__NuttX__', '-DNDEBUG',
            '-fno-strict-aliasing', '-Wno-error=incompatible-pointer-types',
            '-Wno-error=int-conversion', '-Wno-error=implicit-function-declaration']


# ------------------------------------------------------------- libraries

def build_host_buildsystem(src, host_prefix, env):
    run(['make', '-s', '-C', src / 'buildsystem', 'install', f'PREFIX={host_prefix}'],
        env=env, capture=True)


def build_libs(src, prefix, host_prefix, cflags, env):
    compat = HERE / 'port/compat/nuttx_compat.h'
    for lib in LIBS:
        lib_flags = list(cflags) + ['-include', str(compat)]
        extra = []
        if lib == 'libhubbub':
            lib_flags.insert(0, '-DOV_COMPAT_RENAME_DIR')
        if lib == 'libdom':
            extra = ['WITH_EXPAT_BINDING=no', 'WITH_LIBXML_BINDING=no',
                     'WITH_HUBBUB_BINDING=yes']
        e = dict(env, CFLAGS=' '.join(lib_flags),
                 PKG_CONFIG_PATH=str(prefix / 'lib/pkgconfig'),
                 PKG_CONFIG_LIBDIR=str(prefix / 'lib/pkgconfig'))
        log(f'building {lib}')
        run(['make', '-s', '-C', src / lib, *extra, f'HOST={TRIPLE}', f'PREFIX={prefix}',
             f'NSSHARED={host_prefix}/share/netsurf-buildsystem',
             'COMPONENT_TYPE=lib-static', 'WARNFLAGS=-Wall -Wno-unused', 'install'],
            env=e, capture=True, quiet=True)


def build_zlib(zsrc, prefix, cflags, env):
    names = ['adler32', 'compress', 'crc32', 'deflate', 'gzclose', 'gzlib', 'gzread',
             'gzwrite', 'infback', 'inffast', 'inflate', 'inftrees', 'trees', 'uncompr',
             'zutil']
    objdir = zsrc / 'build-openvela'
    objdir.mkdir(exist_ok=True)
    cc = f'{TRIPLE}-gcc'
    objs = []
    for n in names:
        o = objdir / f'{n}.o'
        # same flags as openvela's apps/system/zlib
        run([cc, *cflags, '-Dunix', '-DZ_HAVE_UNISTD_H', '-Werror=implicit-function-declaration',
             '-Wno-shadow', '-Wno-strict-prototypes', '-Wno-undef',
             '-I', zsrc, '-c', zsrc / f'{n}.c', '-o', o], env=env, quiet=True)
        objs.append(o)
    (prefix / 'lib').mkdir(parents=True, exist_ok=True)
    (prefix / 'include').mkdir(parents=True, exist_ok=True)
    lib = prefix / 'lib/libz.a'
    lib.unlink(missing_ok=True)
    run([f'{TRIPLE}-ar', 'rcs', lib, *objs], env=env, quiet=True)
    for h in ('zlib.h', 'zconf.h'):
        shutil.copy(zsrc / h, prefix / 'include' / h)


# -------------------------------------------------------------- frontend

def c_array(name, data):
    lines = [f'static const uint8_t {name}[] = {{']
    for i in range(0, len(data), 16):
        lines.append('  ' + ','.join(str(b) for b in data[i:i + 16]) + ',')
    lines.append('  0};')
    return '\n'.join(lines)


def install_frontend(src, host_build, env):
    ns = src / 'netsurf'
    fe = ns / 'frontends/openvela'
    shutil.rmtree(fe, ignore_errors=True)
    shutil.copytree(HERE / 'frontend/openvela', fe)

    # Messages: Chinese with English fallback, upstream split-messages tool
    tool = host_build / 'split-messages'
    tool.parent.mkdir(parents=True, exist_ok=True)
    run(['gcc', '-O1', '-I', ns, '-o', tool, ns / 'tools/split-messages.c', '-lz'],
        env=env, quiet=True)
    messages = host_build / 'Messages'
    run([tool, '-l', 'zh_CN', '-d', 'en', '-p', 'any', '-f', 'messages', '-o', messages,
         ns / 'resources/FatMessages'], env=env, quiet=True)

    resources = {
        'Messages': messages,
        'default.css': ns / 'resources/default.css',
        'quirks.css': ns / 'resources/quirks.css',
        'internal.css': ns / 'resources/internal.css',
        'adblock.css': ns / 'resources/adblock.css',
        'welcome.html': HERE / 'frontend/res/welcome.html',
    }
    parts = ['/* Generated by app/netsurf_browser/build.py; do not edit. */',
             '#include <stddef.h>', '#include <stdint.h>',
             'struct ov_resource { const char *name; const uint8_t *data; size_t len; };']
    table = []
    for i, (name, path) in enumerate(resources.items()):
        data = Path(path).read_bytes()
        parts.append(c_array(f'res_{i}', data))
        table.append(f'  {{"{name}", res_{i}, {len(data)}}},')
    parts.append('const struct ov_resource ov_resources[] = {')
    parts.extend(table)
    parts.append('};')
    parts.append(f'const size_t ov_resource_count = {len(resources)};')
    (fe / 'res_data.c').write_text('\n'.join(parts) + '\n')


def build_netsurf(src, prefix, workspace, fw_flags, cflags, jobs, env):
    ns = src / 'netsurf'
    gnu_ld = workspace / 'nuttx/libs/libc/elf/gnu-elf.ld'
    fw = list(fw_flags) + module_codegen() + [
        # no nuttx_compat.h here: NuttX headers need CODE (webclient, ...)
        '-I', str(DESKTOP), '-I', str(LODEPNG), '-I', str(prefix / 'include'),
        '-Wno-shadow', '-Wno-undef',
        '-Wno-missing-prototypes', '-Wno-missing-declarations',
        '-Wno-strict-prototypes', '-Wno-redundant-decls']
    ldflags = ['-nostdlib', '-nostartfiles', '-r', f'-Wl,-T,{gnu_ld}',
               f'-L{prefix / "lib"}']
    e = dict(env, CFLAGS=' '.join(cflags + module_codegen()),
             PKG_CONFIG_PATH=str(prefix / 'lib/pkgconfig'),
             PKG_CONFIG_LIBDIR=str(prefix / 'lib/pkgconfig'))
    run(['make', '-C', ns, f'-j{jobs}', 'TARGET=openvela', f'HOST={TRIPLE}',
         f'CC={TRIPLE}-gcc', 'BUILD_CC=gcc', 'PKG_CONFIG=pkg-config', 'Q=@', 'V=0',
         'OV_FIRMWARE_CFLAGS=' + ' '.join(fw),
         'OV_MODULE_LDFLAGS=' + ' '.join(ldflags)],
        env=e, capture=True)
    return ns / 'module.elf'


# ---------------------------------------------------------- verification

def readelf_relocs(elf, env):
    out = run([f'{TRIPLE}-readelf', '-rW', elf], env=env, capture=True, quiet=True)
    types = {}
    for line in out.splitlines():
        m = re.match(r'^[0-9a-f]+\s+[0-9a-f]+\s+(R_RISCV_\w+)', line)
        if m:
            types[m.group(1)] = types.get(m.group(1), 0) + 1
    return types


def undefined_symbols(elf, env):
    out = run([f'{TRIPLE}-nm', '-u', elf], env=env, capture=True, quiet=True)
    return sorted({l.split()[-1] for l in out.splitlines() if l.strip()})


def exported_symbols():
    names = set()
    for line in (NATIVE / 'exports.txt').read_text().splitlines():
        line = line.strip()
        if line and not line.startswith('#'):
            names.add(line)
    return names


def firmware_defined(elf, env):
    out = run([f'{TRIPLE}-nm', '--defined-only', elf], env=env, capture=True, quiet=True)
    return {l.split()[-1] for l in out.splitlines() if len(l.split()) >= 3}


def module_exports(elf, env):
    out = run([f'{TRIPLE}-readelf', '-sW', elf], env=env, capture=True, quiet=True)
    names = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[4] == 'GLOBAL' and parts[5] == 'DEFAULT' and \
                parts[6] != 'UND' and parts[3] in ('FUNC', 'OBJECT'):
            names.append(parts[7])
    return names


R_RISCV_CALL, R_RISCV_CALL_PLT, R_RISCV_LO12_S = 18, 19, 28


def normalize_add_relocs(path):
    """Zero immediate fields that the NuttX relocator adds to.

    arch_elf.c applies R_RISCV_CALL[_PLT] and R_RISCV_LO12_S with _add_val(),
    i.e. it adds the computed immediate to whatever the field holds. With
    RELA the field content is ignored by the psABI (the addend is in
    r_addend), so zeroing it preserves semantics and makes the loader's
    addition exact. Instruction shapes are verified on the way.
    """
    import struct
    data = bytearray(Path(path).read_bytes())
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        raise SystemExit('module is not ELF32 little endian')
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x2e)
    sections = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize)
                for i in range(shnum)]
    changed = checked = 0
    for sh in sections:
        if sh[1] != 4:  # SHT_RELA
            continue
        target = sections[sh[7]]  # sh_info
        if target[1] == 8:  # SHT_NOBITS
            continue
        base = target[4]
        for off in range(sh[4], sh[4] + sh[5], 12):
            r_offset, r_info, _ = struct.unpack_from('<IIi', data, off)
            rtype = r_info & 0xff
            at = base + r_offset
            if rtype in (R_RISCV_CALL, R_RISCV_CALL_PLT):
                auipc, jalr = struct.unpack_from('<II', data, at)
                if auipc & 0x7f != 0x17 or jalr & 0x707f != 0x67:
                    raise SystemExit(f'CALL relocation at {at:#x} is not auipc+jalr')
                new = (auipc & 0xfff, jalr & 0xfffff)
                if new != (auipc, jalr):
                    struct.pack_into('<II', data, at, *new)
                    changed += 1
                checked += 1
            elif rtype == R_RISCV_LO12_S:
                insn, = struct.unpack_from('<I', data, at)
                if insn & 0x7f not in (0x23, 0x27):  # STORE, STORE-FP
                    raise SystemExit(f'LO12_S relocation at {at:#x} is not a store')
                new = insn & 0x01fff07f
                if new != insn:
                    struct.pack_into('<I', data, at, new)
                    changed += 1
                checked += 1
    Path(path).write_bytes(bytes(data))
    return {'checked': checked, 'normalized': changed}


def host_abi():
    text = (NATIVE / 'native_host_abi.h').read_text()
    return re.search(r'OV_NATIVE_HOST_ABI "([0-9a-f]{64})"', text).group(1)


# ---------------------------------------------------------------- main

def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--workspace', type=Path, required=True,
                   help='prepared openvela workspace containing nuttx/ and apps/')
    p.add_argument('--toolchain', type=Path,
                   help=f'directory containing {TRIPLE}-gcc (default: PATH)')
    p.add_argument('--work', type=Path, default=Path.home() / 'netsurf-openvela')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--firmware-elf', type=Path,
                   help='firmware ELF to confirm every import is defined')
    p.add_argument('--offline', action='store_true')
    p.add_argument('--reuse-libs', action='store_true',
                   help='keep previously cross-built libraries (frontend iteration)')
    p.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    a = p.parse_args()

    env = dict(os.environ)
    if a.toolchain:
        env['PATH'] = f'{a.toolchain}:{env["PATH"]}'
    gcc = shutil.which(f'{TRIPLE}-gcc', path=env['PATH'])
    if not gcc:
        raise SystemExit(f'{TRIPLE}-gcc not found')
    tc = Path(gcc).parent
    ws = a.workspace.resolve()
    if not (ws / 'nuttx/include/nuttx/config.h').exists():
        raise SystemExit('workspace is not configured (nuttx/include/nuttx/config.h missing)')

    work = a.work.resolve()
    src = work / 'src'
    prefix = work / 'cross-prefix'
    host_prefix = work / 'host-prefix'
    host_build = work / 'host-build'

    run([sys.executable, REPO / 'tools/generate_native_exports.py', '--check'])
    fetch_sources(src, a.offline)
    apply_patches(src)
    zsrc = fetch_zlib(work, a.offline)

    cflags = base_cflags(ws) + module_codegen()
    fw_flags = firmware_flags(ws, tc, env)
    build_host_buildsystem(src, host_prefix, env)
    if a.reuse_libs and (prefix / 'lib/libcss.a').exists() and (prefix / 'lib/libz.a').exists():
        log('reusing libraries in ' + str(prefix))
    else:
        shutil.rmtree(prefix, ignore_errors=True)
        build_libs(src, prefix, host_prefix, cflags, env)
        build_zlib(zsrc, prefix, cflags, env)
    install_frontend(src, host_build, env)
    elf = build_netsurf(src, prefix, ws, fw_flags, base_cflags(ws), a.jobs, env)

    out = a.output.resolve()
    app = out / 'com.netsurf.browser'
    shutil.rmtree(app, ignore_errors=True)
    (app / 'native').mkdir(parents=True)
    module = app / 'native/module.elf'
    run([f'{TRIPLE}-strip', '--strip-debug', '-R', '.comment', '-o', module, elf], env=env)

    add_fields = normalize_add_relocs(module)
    if normalize_add_relocs(module)['normalized'] != 0:
        raise SystemExit('relocation field normalization is not idempotent')
    relocs = readelf_relocs(module, env)
    bad = {k: v for k, v in relocs.items() if k not in ALLOWED_RELOCS}
    imports = undefined_symbols(module, env)
    missing = sorted(set(imports) - exported_symbols())
    exports = module_exports(module, env)
    report = {
        'module_bytes': module.stat().st_size,
        'relocations': relocs,
        'add_relocation_fields': add_fields,
        'unsupported_relocations': bad,
        'imports': len(imports),
        'imports_missing_from_exports_txt': missing,
        'module_exports': exports,
    }
    if a.firmware_elf:
        defined = firmware_defined(a.firmware_elf, env)
        report['exports_missing_from_firmware'] = sorted(
            s for s in imports if s in exported_symbols() and s not in defined)

    abi = host_abi()
    (app / 'native/host.abi').write_text(abi + '\n')
    shutil.copy(HERE / 'package/manifest.json', app / 'manifest.json')
    shutil.copy(HERE / 'package/app.js', app / 'app.js')
    version = json.loads((HERE / 'package/manifest.json').read_text())['versionName']

    ok = not bad and not missing and exports == ['ov_native_module_query'] and \
        not report.get('exports_missing_from_firmware')
    build = {
        'deployable': False,  # needs board acceptance; see README
        'checks_passed': ok,
        'host_abi': abi,
        'version': version,
        'pins': PINS,
        'files': {str(f.relative_to(out)): sha256(f)
                  for f in sorted(app.rglob('*')) if f.is_file()},
        'report': report,
    }
    zip_path = out / f'NetSurfBrowser-{version}.zip'
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(app.rglob('*')):
            if f.is_file():
                zf.write(f, f.relative_to(out))
    build['zip_sha256'] = sha256(zip_path)
    (out / 'BUILD.json').write_text(json.dumps(build, indent=2, ensure_ascii=False) + '\n')

    log(f'module {report["module_bytes"]} bytes, {len(imports)} imports, relocs {relocs}, '
        f'add-fields {add_fields}')
    if missing:
        log('imports missing from exports.txt: ' + ' '.join(missing))
    if bad:
        log(f'unsupported relocations: {bad}')
    if exports != ['ov_native_module_query']:
        log(f'unexpected module exports: {exports}')
    if report.get('exports_missing_from_firmware'):
        log('not defined by firmware: ' + ' '.join(report['exports_missing_from_firmware']))
    log(('NETSURF_MODULE_OK ' if ok else 'NETSURF_MODULE_CHECKS_FAILED ') + str(zip_path))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
