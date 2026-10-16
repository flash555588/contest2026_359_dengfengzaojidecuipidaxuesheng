"""Prepared data partitions must not move as a side effect of a UI build."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('builder',Path(__file__).resolve().parents[1]/'build_bilibili_v3.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)

def config(offset,size):
    return f'CONFIG_ESPRESSIF_STORAGE_MTD_OFFSET={offset}\nCONFIG_ESPRESSIF_STORAGE_MTD_SIZE={size}\n'

class PreparedStorageLayout(unittest.TestCase):
    def test_preserves_existing_board_layout(self):
        self.assertEqual(builder.storage_layout(config('0xc00000','0x400000')),(0xc00000,0x400000))
    def test_preserves_explicitly_prepared_new_layout(self):
        self.assertEqual(builder.storage_layout(config('0xe00000','0x200000')),(0xe00000,0x200000))
    def test_missing_or_inconsistent_layout_cannot_silently_pick_a_new_offset(self):
        for value in ['',config('0xc00000','0x200000'),config('0x800000','0x800000')]:
            with self.subTest(config=value),self.assertRaises(ValueError):builder.storage_layout(value)
    def test_only_explicit_choice_can_override_layout(self):
        self.assertEqual(builder.storage_layout(config('0xe00000','0x200000'),'12m-4m'),(0xc00000,0x400000))
        self.assertEqual(builder.storage_layout(config('0xc00000','0x400000'),'14m-2m'),(0xe00000,0x200000))
        with self.assertRaises(ValueError):builder.storage_layout('', 'unknown')

if __name__=='__main__':unittest.main()
