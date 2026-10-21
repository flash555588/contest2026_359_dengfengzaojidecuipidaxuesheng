"""Verify the five app asset ZIPs and optionally assemble an empty SD staging tree."""
import argparse
import hashlib
import json
import zipfile
from pathlib import Path, PurePosixPath


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def safe_path(root, name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
        raise ValueError(f'Unsafe asset path: {name}')
    result = root.joinpath(*path.parts).resolve()
    result.relative_to(root)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--output', type=Path, help='New or empty staging directory; existing data is not overwritten.')
    args = parser.parse_args()
    if not args.verify and args.output is None:
        parser.error('Pass --verify, --output, or both.')
    root = Path(__file__).resolve().parent
    manifest = json.loads((root/'manifest.json').read_text(encoding='utf-8'))
    output = args.output.resolve() if args.output else None
    if output:
        if output.exists() and (not output.is_dir() or next(output.iterdir(), None) is not None):
            parser.error('Output must be a new or empty directory.')
        output.mkdir(parents=True, exist_ok=True)
    count, total = 0, 0
    for package in manifest['packages']:
        path = safe_path(root, package['archive'])
        if path.stat().st_size != package['bytes'] or digest(path) != package['sha256']:
            raise ValueError(f"Archive checksum mismatch: {package['archive']}")
        with zipfile.ZipFile(path) as archive:
            rows = {row['path']: row for row in package['files']}
            if len(archive.infolist()) != len(rows) or set(archive.namelist()) != set(rows):
                raise ValueError(f"Archive entries differ from manifest: {package['archive']}")
            for name, expected in rows.items():
                target = safe_path(output, name) if output else None
                if archive.getinfo(name).file_size != expected['bytes']:
                    raise ValueError(f'Unexpected asset length: {name}')
                h, size = hashlib.sha256(), 0
                dest = None
                try:
                    if target:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        dest = target.open('xb')
                    with archive.open(name) as stream:
                        for block in iter(lambda: stream.read(1024*1024), b''):
                            size += len(block)
                            h.update(block)
                            if dest:
                                dest.write(block)
                finally:
                    if dest:
                        dest.close()
                if size != expected['bytes'] or h.hexdigest() != expected['sha256']:
                    raise ValueError(f'Asset checksum mismatch: {name}')
                count += 1
                total += size
        print('APP_ASSETS_VERIFIED', package['name'], len(rows), flush=True)
    print(f'APP_SD_READY {count} files, {total} bytes, extracted={bool(output)}')


if __name__ == '__main__':
    main()
