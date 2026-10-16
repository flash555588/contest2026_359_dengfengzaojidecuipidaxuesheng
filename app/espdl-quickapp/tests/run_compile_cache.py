"""Exercise production compile caching with real QuickJS and ASan/UBSan."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--quickjs', required=True, type=Path)
    parser.add_argument('--objects', type=Path,
                        help='Existing ASan/UBSan objects from render_all_ui.py')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    desktop = root / 'overlay/apps/system/desktop'
    qjs = args.quickjs.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='qpk-compile-cache-') as temporary:
        work = Path(temporary)
        flags = ['-std=gnu11', '-O1', '-g', '-fwrapv', '-D_GNU_SOURCE',
                 '-DCONFIG_BIGNUM', '-DCONFIG_VERSION="' + (qjs/'VERSION').read_text().strip() + '"',
                 '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                 '-isystem', str(qjs), '-I'+str(desktop)]
        objects = []
        for name in ('quickjs.c', 'libbf.c', 'cutils.c', 'libunicode.c', 'libregexp.c'):
            obj = (args.objects or work) / (name + '.o')
            if not args.objects:
                subprocess.run(['cc', *flags, '-c', str(qjs/name), '-o', str(obj)], check=True)
            assert obj.is_file(), obj
            objects.append(str(obj))
        binary = work / 'compile-cache-host'
        subprocess.run(['cc', *flags, '-Wall', '-Wextra', '-Werror',
                        str(root/'tests/compile_cache_host.c'), *objects,
                        '-lm', '-ldl', '-pthread', '-o', str(binary)], check=True)
        subprocess.run([str(binary), str(desktop/'homeassistant/app.js')], check=True)


if __name__ == '__main__':
    main()
