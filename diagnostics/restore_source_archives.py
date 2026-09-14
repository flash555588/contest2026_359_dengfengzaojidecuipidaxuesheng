"""Restore source archives from ordinary Git parts, checking every SHA256.

Requires Python 3.9 or newer. Run from a clone after downloading all Git files.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile


def checked_path(root, name):
    relative = Path(name)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("Invalid archive path: " + name)
    result = (root / relative).resolve()
    if not result.is_relative_to(root) or result == root:
        raise ValueError("Archive path escapes its directory: " + name)
    return result


def file_digest(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def restore(root, output_root, verify_only=False):
    root = root.resolve()
    output_root = output_root.resolve()
    manifest = json.loads((root / "ARCHIVE-PARTS.json").read_text(encoding="utf-8"))
    if manifest["version"] != 1:
        raise ValueError("Unsupported archive manifest version")
    for archive in manifest["archives"]:
        target = checked_path(output_root, archive["path"])
        if not verify_only and target.exists():
            if target.stat().st_size != archive["bytes"] or file_digest(target) != archive["sha256"]:
                raise ValueError("Refusing to overwrite a different file: " + str(target))
            print("Already verified: " + archive["path"])
            continue
        temporary = None
        output = None
        try:
            if not verify_only:
                target.parent.mkdir(parents=True, exist_ok=True)
                output = tempfile.NamedTemporaryFile(
                    mode="wb", dir=target.parent, prefix=target.name + ".", suffix=".partial", delete=False)
                temporary = Path(output.name)
            digest = hashlib.sha256()
            size = 0
            for part in archive["parts"]:
                path = checked_path(root, part["path"])
                part_digest = hashlib.sha256()
                part_size = 0
                with path.open("rb") as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        part_digest.update(block)
                        digest.update(block)
                        part_size += len(block)
                        if output is not None:
                            output.write(block)
                if part_size != part["bytes"] or part_digest.hexdigest() != part["sha256"]:
                    raise ValueError("Part checksum mismatch: " + part["path"])
                size += part_size
            if size != archive["bytes"] or digest.hexdigest() != archive["sha256"]:
                raise ValueError("Archive checksum mismatch: " + archive["path"])
            if output is not None:
                output.close()
                output = None
                # Publish without replacing any file created in the meantime.
                os.link(temporary, target)
                temporary.unlink()
                temporary = None
            print(("Verified: " if verify_only else "Restored: ") + archive["path"])
        finally:
            if output is not None:
                output.close()
            if temporary is not None and temporary.exists():
                temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--output", type=Path, help="Restore into another directory instead of the repository")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    restore(args.root, args.output or args.root, args.verify_only)


if __name__ == "__main__":
    main()
