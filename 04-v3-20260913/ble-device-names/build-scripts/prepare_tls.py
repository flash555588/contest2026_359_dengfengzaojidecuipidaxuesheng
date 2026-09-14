"""Unpack the locked upstream TLS source into the isolated build directory."""

from pathlib import Path, PurePosixPath
import hashlib
import json
import tarfile


def prepare_tls(build_root, downloads):
    tls = build_root / 'standalone-tls'
    marker = tls / '.prepared'
    if marker.exists():
        return tls
    manifest = json.loads((downloads / 'tls-manifest.json').read_text())
    for name in ['mbedtls', 'tf-psa-crypto', 'framework']:
        entry = manifest[name]
        archive_path = downloads / entry['file']
        if hashlib.sha256(archive_path.read_bytes()).hexdigest() != entry['sha256']:
            raise ValueError(f'TLS archive checksum mismatch: {name}')
        destination = tls if name == 'mbedtls' else tls / name
        with tarfile.open(archive_path) as archive:
            for member in archive:
                parts = PurePosixPath(member.name).parts[1:]
                if '..' in parts or PurePosixPath(member.name).is_absolute():
                    raise ValueError(f'Unsafe archive path: {member.name}')
                target = destination.joinpath(*parts)
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                elif member.isfile():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.extractfile(member).read())
                    target.chmod(member.mode & 0o777)
                elif member.issym():
                    resolved = (target.parent / member.linkname).resolve()
                    if not resolved.is_relative_to(tls.resolve()):
                        raise ValueError(f'Unsafe TLS link: {member.name}')
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.symlink_to(member.linkname)
                else:
                    raise ValueError(f'Unsupported TLS archive member: {member.name}')
    framework = tls / 'tf-psa-crypto/framework'
    if framework.is_dir() and not any(framework.iterdir()):
        framework.rmdir()
    if not framework.exists():
        framework.symlink_to('../framework')
    marker.write_text(json.dumps(manifest, indent=2) + '\n')
    return tls
