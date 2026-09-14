"""Prepare an isolated Linux build from the latest source export."""

import argparse
import os
from pathlib import Path, PurePosixPath
import tarfile


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("archive", type=Path)
parser.add_argument("destination", type=Path)
args = parser.parse_args()
root = args.destination.resolve()
root.mkdir(parents=True, exist_ok=False)
old_root = "/home/flash/glass-ble-v3-20260913/"
links = []
count = 0
with tarfile.open(args.archive) as archive:
    for member in archive:
        name = PurePosixPath(member.name)
        if name.is_absolute() or ".." in name.parts:
            raise ValueError(f"Unsafe archive name: {member.name}")
        target = root.joinpath(*name.parts)
        if member.isdir():
            target.mkdir(parents=True, exist_ok=True)
        elif member.isfile():
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source, target.open("xb") as output:
                while data := source.read(1024 * 1024):
                    output.write(data)
            target.chmod(member.mode & 0o777)
            count += 1
        elif member.issym():
            links.append((target, member.linkname))
        else:
            raise ValueError(f"Unsupported archive member: {member.name}")

for target, link in links:
    if link.startswith(old_root):
        link = os.path.relpath(root / link[len(old_root):], target.parent)
    elif link.startswith("/"):
        print(f"External dependency link omitted: {target.relative_to(root)} -> {link}")
        continue
    resolved = (target.parent / link).resolve()
    if not resolved.is_relative_to(root):
        raise ValueError(f"Link escapes build tree: {target} -> {link}")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.symlink_to(link)

print(f"Prepared {count} files in {root}")
for base in [Path('/home/streetartist/toolchains'),
             Path('/home/streetartist/opt'),
             Path('/home/streetartist/.espressif/tools'),
             Path('/home/streetartist/bin')]:
    if base.is_dir():
        for compiler in base.rglob('*riscv*gcc'):
            print(f"Available compiler: {compiler}")
