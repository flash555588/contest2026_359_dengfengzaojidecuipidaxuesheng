#!/usr/bin/env python3
"""Verify/apply optional C6 patches; never configures or flashes hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def check(tree, patch, reverse=False):
    args = ["git", "-C", str(tree), "apply", "--check"]
    if reverse:
        args.append("--reverse")
    return subprocess.run(args + [str(patch)], capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path, help="Directory containing nuttx and apps")
    parser.add_argument("--apply", action="store_true", help="Default is check-only")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent / "patches/c6"
    manifest = json.loads((root / "manifest.json").read_text())
    pending = []
    for name in ("nuttx", "apps"):
        spec = manifest[name]
        patch = (root / spec["patch"]).resolve()
        tree = (args.workspace / name).resolve()
        if patch.parent != root.resolve() or not tree.is_dir():
            raise SystemExit("Invalid patch or workspace path")
        if hashlib.sha256(patch.read_bytes()).hexdigest() != spec["sha256"]:
            raise SystemExit(f"Patch hash mismatch: {name}")
        result = check(tree, patch)
        if result.returncode == 0:
            pending.append((tree, patch))
            print(f"{name}: ready")
        elif check(tree, patch, reverse=True).returncode == 0:
            print(f"{name}: already applied (reverse check passed)")
        else:
            raise SystemExit(f"{name}: conflicts; no patches applied\n{result.stderr}")
    if args.apply:
        # Both trees are preflighted before the first write. Do not run concurrent edits.
        for tree, patch in pending:
            subprocess.run(["git", "-C", str(tree), "apply", str(patch)], check=True)
        print("Applied. Configuration, build and device verification still required.")
    else:
        print("Check only. Use --apply to change the workspace.")


if __name__ == "__main__":
    main()
