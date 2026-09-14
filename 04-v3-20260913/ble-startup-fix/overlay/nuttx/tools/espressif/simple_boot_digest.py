#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Add ESP32-P4 Simple Boot's RAM digest without moving mapped segments.

elf2image --ram-only-header puts the first padding segment immediately after
the RAM checksum. P4 eco7 ROM still checks a digest there when hash_appended
is zero. Reserve 32 bytes from that padding and mark the digest present.
The Simple Boot parser must skip the digest when hash_appended is set.
"""

import hashlib
from pathlib import Path
import struct
import sys


def add_digest(image):
    data = bytearray(image)
    if len(data) < 24 or data[0] != 0xE9 or data[12:14] != b'\x12\x00':
        raise ValueError('Expected an ESP32-P4 image')
    if not 1 <= data[1] <= 16:
        raise ValueError('Invalid RAM segment count')
    offset = 24
    checksum = 0xEF
    for _ in range(data[1]):
        address, size = struct.unpack_from('<II', data, offset)
        if not (0x30000000 <= address < 0x30200000 or
                0x4FF00000 <= address < 0x50000000):
            raise ValueError('Expected only RAM segments in visible header')
        end = offset + 8 + size
        if end > len(data):
            raise ValueError('Truncated RAM segment')
        for byte in data[offset + 8:end]:
            checksum ^= byte
        offset = end
    digest_offset = (offset + 16) & ~15
    if digest_offset > len(data) or data[digest_offset - 1] != checksum:
        raise ValueError('RAM checksum mismatch')
    # Accept a previously processed image without consuming padding twice.
    if data[23] == 1 and data[digest_offset:digest_offset + 32] == hashlib.sha256(data[:digest_offset]).digest():
        return bytes(data)
    address, size = struct.unpack_from('<II', data, digest_offset)
    padding_end = digest_offset + 8 + size
    if address != 0 or size < 32 or padding_end + 8 > len(data):
        raise ValueError('Missing sufficient padding before mapped segments')
    if any(data[digest_offset + 8:padding_end]):
        raise ValueError('Nonzero Simple Boot padding')
    data[23] = 1
    digest = hashlib.sha256(data[:digest_offset]).digest()
    data[digest_offset:digest_offset + 32] = digest
    struct.pack_into('<II', data, digest_offset + 32, 0, size - 32)
    assert len(data) == len(image)
    assert data[padding_end:] == image[padding_end:]
    return bytes(data)


if __name__ == '__main__':
    path = Path(sys.argv[1])
    path.write_bytes(add_digest(path.read_bytes()))
    print('Simple Boot RAM SHA256 added; mapped segment offsets unchanged')
