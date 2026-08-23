#!/usr/bin/env python3
import subprocess
from pathlib import Path
import time
import shutil

HAL = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty")
MB = HAL / "components/mbedtls/mbedtls"
sha = subprocess.check_output(
    ["git", "-C", str(HAL), "ls-tree", "HEAD", "components/mbedtls/mbedtls"],
    text=True,
).strip()
print("ls-tree", sha)
commit = sha.split()[2] if sha else ""
print("commit", commit)
urls = [
    "https://github.com/espressif/mbedtls.git",
    "https://gitclone.com/github.com/espressif/mbedtls.git",
]
if MB.exists() and not list(MB.iterdir()):
    MB.rmdir()
if (MB / "include").exists():
    print("mbedtls already populated")
else:
    last = None
    for url in urls:
        for attempt in range(1, 6):
            try:
                if MB.exists():
                    shutil.rmtree(MB)
                cmd = ["git", "clone", "--depth", "1"]
                if commit:
                    cmd += ["--branch", commit]  # may fail if not a branch
                # fetch specific commit instead
                MB.parent.mkdir(parents=True, exist_ok=True)
                subprocess.check_call(["git", "init", str(MB)])
                subprocess.check_call(["git", "-C", str(MB), "remote", "add", "origin", url])
                subprocess.check_call(
                    ["git", "-C", str(MB), "-c", "http.version=HTTP/1.1", "fetch", "--depth", "1", "origin", commit]
                )
                subprocess.check_call(["git", "-C", str(MB), "checkout", "--force", "FETCH_HEAD"])
                print("MBEDTLS_OK", url, attempt)
                last = None
                break
            except subprocess.CalledProcessError as e:
                last = e
                print("fail", url, attempt, e)
                time.sleep(2 * attempt)
        else:
            continue
        break
    if last:
        raise SystemExit("mbedtls clone failed")

# apply nuttx patches if present
patchdir = HAL / "nuttx/patches/components/mbedtls/mbedtls"
if patchdir.exists():
    patches = sorted(patchdir.glob("*.patch"))
    print("patches", [p.name for p in patches])
    for p in patches:
        r = subprocess.call(["git", "-C", str(MB), "apply", str(p)])
        print("apply", p.name, r)
print("MBEDTLS_READY")
