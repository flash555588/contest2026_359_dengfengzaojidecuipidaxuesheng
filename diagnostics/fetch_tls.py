"""Fetch the exact upstream TLS inputs recorded in espclaw's tls.lock.json."""

from pathlib import Path
import hashlib
import json
import urllib.request

root = Path(__file__).resolve().parent / 'downloads'
root.mkdir(exist_ok=True)
pins = {
    'mbedtls': ('Mbed-TLS/mbedtls', 'ec4044008d2d069da38288bc76b0fee34ec78646'),
    'tf-psa-crypto': ('Mbed-TLS/TF-PSA-Crypto', '76920edddcad00ac41b248e12d937b845df7bedb'),
    'framework': ('Mbed-TLS/mbedtls-framework', '457996474728cb8e968ed21953b72f74d2f536b2'),
}
manifest = {}
for name, (repository, commit) in pins.items():
    url = f'https://codeload.github.com/{repository}/tar.gz/{commit}'
    target = root / f'{name}-{commit}.tar.gz'
    if not target.exists():
        temporary = target.with_suffix('.part')
        print(f'Downloading {repository} at {commit}', flush=True)
        with urllib.request.urlopen(url, timeout=60) as response, temporary.open('wb') as output:
            while data := response.read(1024 * 1024):
                output.write(data)
        temporary.rename(target)
    manifest[name] = {'commit': commit, 'url': url, 'file': target.name,
                      'sha256': hashlib.sha256(target.read_bytes()).hexdigest()}
    print(f'{name}: {target.stat().st_size} bytes', flush=True)
(root / 'tls-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
