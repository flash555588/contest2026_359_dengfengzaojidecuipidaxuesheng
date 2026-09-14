from pathlib import Path
import urllib.request
import json
import tarfile
import io

url = 'https://codeload.github.com/espressif/esp-hosted-mcu/tar.gz/refs/heads/main'
data = urllib.request.urlopen(url, timeout=30).read()
with tarfile.open(fileobj=io.BytesIO(data)) as archive:
    for member in archive:
        path = member.name.split('/', 1)[-1]
        if member.isfile() and path.endswith(('.c', '.h')) and any(s in path for s in ['sdio', 'hci', 'transport_drv']):
            dest = Path(__file__).parent / 'hosted-reference' / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(archive.extractfile(member).read())
            print(path, member.size, flush=True)
