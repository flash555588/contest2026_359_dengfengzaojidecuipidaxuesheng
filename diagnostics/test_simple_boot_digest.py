"""Verify ROM digest and hidden Flash mappings using real exported images."""
from pathlib import Path
import hashlib
import importlib.util
import struct
import unittest
import sys
sys.dont_write_bytecode = True

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/black-screen-fix'
spec = importlib.util.spec_from_file_location('simple_boot_digest',
    delivery / 'overlay/nuttx/tools/espressif/simple_boot_digest.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def ram_end(data):
    offset = 24
    for _ in range(data[1]):
        _, size = struct.unpack_from('<II', data, offset)
        offset += 8 + size
    return (offset + 16) & ~15


def mapped_segments(data, offset):
    result = []
    while offset < len(data):
        address, size = struct.unpack_from('<II', data, offset)
        if size > len(data) - offset - 8:
            raise ValueError('Segment outside image')
        if address:
            result.append((offset, address, data[offset + 8:offset + 8 + size]))
        offset += 8 + size
    return result


class SimpleBootDigestTest(unittest.TestCase):
    def test_real_image_preserves_flash_segments_and_matches_rom(self):
        data = (workspace / 'diagnostics/original-nuttx.bin').read_bytes()
        end = ram_end(data)
        fixed = module.add_digest(data)
        self.assertEqual(len(fixed), len(data))
        self.assertEqual(fixed[23], 1)
        self.assertEqual(fixed[end:end + 32], hashlib.sha256(fixed[:end]).digest())
        self.assertEqual(mapped_segments(data, end), mapped_segments(fixed, end + 32))
        self.assertEqual(module.add_digest(fixed), fixed)

    def test_corrupt_checksum_is_rejected(self):
        data = bytearray((workspace / 'diagnostics/original-nuttx.bin').read_bytes())
        data[32] ^= 1
        with self.assertRaisesRegex(ValueError, 'checksum'):
            module.add_digest(data)

    def test_insufficient_padding_is_rejected(self):
        data = bytearray((workspace / 'diagnostics/original-nuttx.bin').read_bytes())
        struct.pack_into('<I', data, ram_end(data) + 4, 16)
        with self.assertRaisesRegex(ValueError, 'padding'):
            module.add_digest(data)

    def test_current_build(self):
        data = (delivery / 'nuttx.bin').read_bytes()
        fixed = module.add_digest(data)
        end = ram_end(fixed)
        self.assertEqual(fixed[end:end + 32], hashlib.sha256(fixed[:end]).digest())
        self.assertEqual(module.add_digest(fixed), fixed)
        self.assertLessEqual(0x2000 + ((len(fixed) + 4095) & ~4095), 0x400000)


if __name__ == '__main__':
    unittest.main()
