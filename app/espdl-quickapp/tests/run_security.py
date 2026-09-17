"""Focused host regressions with real QuickJS and production binding bodies."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DESKTOP = ROOT / 'overlay/apps/system/desktop'


def function(source, name):
    pattern = r'^static [^\n]*\b' + re.escape(name) + r'\([^;]*?\)\s*\{.*?^\}'
    matches = list(re.finditer(pattern, source, re.M | re.S))
    if len(matches) != 1:
        raise ValueError('Missing or ambiguous function: ' + name)
    return matches[0].group(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--quickjs', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    args = parser.parse_args()
    qjs = args.quickjs.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='pet-fix-check-') as temporary:
        work = Path(temporary)
        names = ['qpk_show_error', 'qpk_widget_deleted', 'qpk_add_widget',
                 'qpk_widget_current', 'qpk_arg_int', 'js_ui_set_style',
                 'js_ui_set_pos', 'js_ui_set_size', 'js_ui_remove']
        common = ['cc', '-std=gnu11', '-O1', '-g', '-fwrapv', '-D_GNU_SOURCE',
                  '-DCONFIG_BIGNUM', '-DCONFIG_VERSION="' + (qjs/'VERSION').read_text().strip() + '"',
                  '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                  '-isystem', str(qjs), '-I'+str(DESKTOP), '-I'+str(work)]
        objects = []
        for name in ('quickjs.c','libregexp.c','libunicode.c','cutils.c','libbf.c'):
            obj = work / (name + '.o')
            subprocess.run(common + ['-c', str(qjs/name), '-o', str(obj)], check=True, timeout=120)
            objects.append(str(obj))
        for baseline in ([True, False] if args.baseline else [False]):
            source_dir = args.baseline/'overlay/apps/system/desktop' if baseline else DESKTOP
            source = (source_dir/'qpk_runtime.c').read_text(encoding='utf-8')
            selected = ['qpk_show_error'] if baseline else names
            (work/'runtime_subset.inc').write_text('\n\n'.join(function(source,name) for name in selected),encoding='utf-8')
            binary = work / ('baseline' if baseline else 'fixed')
            command = common + ['-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-sign-compare']
            if baseline: command += ['-DBASELINE_ONLY']
            command += [str(ROOT/'tests/runtime_security_host.c'),*objects,'-lm','-ldl','-pthread','-o',str(binary)]
            subprocess.run(command,check=True,timeout=120)
            subprocess.run([str(binary)],check=True,timeout=15)
        engine = work/'pet-engine'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all',str(ROOT/'tests/pet_engine_host.c'),
                        str(DESKTOP/'pet_engine.c'),'-o',str(engine)],check=True,timeout=60)
        subprocess.run([str(engine)],check=True,timeout=15)


if __name__ == '__main__': main()
