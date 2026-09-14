"""Fetch a pinned upstream USB host/UVC implementation for the P4 port."""
from pathlib import Path, PurePosixPath
import hashlib
import io
import json
import tarfile
import urllib.request

root = Path(__file__).resolve().parent
folder = root / 'cherryusb-reference'
folder.mkdir(exist_ok=True)
def get(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'VelaDesk-local-build'})
    with urllib.request.urlopen(request, timeout=45) as response:
        return response.read()
manifest_path = folder / 'manifest.json'
if manifest_path.exists():
    manifest = json.loads(manifest_path.read_text())
else:
    # GitHub API is rate limited here. Git archives carry the commit in PAX
    # metadata; the local archive SHA256 is always the dependency lock.
    data = get('https://codeload.github.com/cherry-embedded/CherryUSB/tar.gz/refs/heads/master')
    with tarfile.open(fileobj=io.BytesIO(data)) as source:
        commit = source.pax_headers.get('comment')
    (folder / 'source.tar.gz').write_bytes(data)
    manifest = {'repository': 'https://github.com/cherry-embedded/CherryUSB', 'commit': commit,
                'sha256': hashlib.sha256(data).hexdigest()}
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
archive = folder / 'source.tar.gz'
assert hashlib.sha256(archive.read_bytes()).hexdigest() == manifest['sha256']
with tarfile.open(archive) as source:
    for member in source:
        parts = PurePosixPath(member.name).parts[1:]
        if not member.isfile() or not parts:
            continue
        assert '..' not in parts and not PurePosixPath(member.name).is_absolute()
        target = folder / 'source' / Path(*parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.extractfile(member).read())
print(json.dumps(manifest, indent=2))
