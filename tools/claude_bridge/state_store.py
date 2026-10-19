"""Atomic private files and a single-instance lock; no task execution policy."""
import json
import os
from pathlib import Path
import tempfile


def atomic(path, value):
    path = Path(path)
    fd, name = tempfile.mkstemp(prefix=path.name+'.', suffix='.tmp', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            json.dump(value, stream, ensure_ascii=False)
            stream.flush(); os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


class InstanceLock:
    def __init__(self, state):
        state.mkdir(parents=True, exist_ok=True)
        self.file = (state / 'bridge.lock').open('a+b')
        try:
            if self.file.seek(0,2)==0: self.file.write(b'\0'); self.file.flush()
            self.file.seek(0)
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.file.close(); raise RuntimeError('该状态目录已有桥接服务运行') from None

    def close(self): self.file.close()
