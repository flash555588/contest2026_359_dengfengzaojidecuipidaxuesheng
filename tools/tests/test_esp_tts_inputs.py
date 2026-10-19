#!/usr/bin/env python3
"""Exercise official input verification, cache recovery, and build preflight."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import esp_tts_inputs as inputs
from prepare_esp_tts import prepare
from build_speech_runtime import build


class OfficialInputsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='esp-tts-inputs-')
        self.root = Path(self.temporary.name)
        self.prepared = self.root/'prepared'
        shutil.copytree(PREPARED, self.prepared)
        self.row = next(row for row in inputs.upstream_lock()['sources'] if row['path'].endswith('esp_tts.h'))
        self.header = self.prepared/'upstream'/self.row['path']

    def tearDown(self):
        self.temporary.cleanup()

    def test_offline_reuse_does_not_download_or_rewrite_inputs(self):
        before = self.header.stat().st_mtime_ns
        with patch('urllib.request.urlopen', side_effect=AssertionError('Network used')):
            self.assertEqual(prepare(self.prepared, offline=True)['files'], len(inputs.upstream_lock()['sources']))
        self.assertEqual(self.header.stat().st_mtime_ns, before)
        inputs.verify_prepared(self.prepared)

    def test_forged_lock_and_matching_corrupted_file_are_rejected(self):
        self.header.write_bytes(b'forged')
        lock = inputs.upstream_lock()
        row = next(row for row in lock['sources'] if row['path'] == self.row['path'])
        row.update(bytes=6, sha256=hashlib.sha256(b'forged').hexdigest(),
                   git_blob_sha1=hashlib.sha1(b'blob 6\0forged').hexdigest())
        (self.prepared/'upstream.lock.json').write_text(json.dumps(lock))
        with self.assertRaisesRegex(ValueError, 'repository lock'):
            inputs.verify_prepared(self.prepared)

    def test_corrupt_cache_fails_offline(self):
        self.header.write_bytes(b'corrupt')
        with patch('urllib.request.urlopen', side_effect=AssertionError('Network used')):
            with self.assertRaisesRegex(ValueError, 'offline ESP-TTS'):
                prepare(self.prepared, offline=True)

    def test_online_recovery_downloads_only_the_missing_input(self):
        original = self.header.read_bytes()
        self.header.unlink()
        from io import BytesIO
        with patch('urllib.request.urlopen', return_value=BytesIO(original)) as request:
            prepare(self.prepared)
        request.assert_called_once_with(self.row['url'], timeout=60)
        inputs.verify_prepared(self.prepared)

    def test_failed_download_does_not_publish_partial_file(self):
        from io import BytesIO
        self.header.write_bytes(b'previous')
        with patch('urllib.request.urlopen', side_effect=lambda *a, **k: BytesIO(b'partial')), patch('prepare_esp_tts.time.sleep'):
            with self.assertRaises(ValueError):
                prepare(self.prepared)
        self.assertEqual(self.header.read_bytes(), b'previous')
        self.assertFalse(list(self.header.parent.glob('*.tmp')))

    def test_license_and_git_blob_are_checked(self):
        (self.prepared/'upstream/LICENSE').unlink()
        with self.assertRaises(FileNotFoundError):
            inputs.verify_prepared(self.prepared)
        row = dict(self.row, git_blob_sha1='0'*40)
        with self.assertRaisesRegex(ValueError, 'Git blob'):
            inputs.verify_bytes(self.header.read_bytes(), row)

    def test_paths_cannot_escape_the_prepared_tree(self):
        for path in ['../outside', '/tmp/outside', 'C:/outside', 'dir\\outside']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                inputs.source_path(self.root, {'path': path})
        outside = self.root/'outside'
        outside.mkdir()
        (self.prepared/'upstream/escape').symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            inputs.source_path(self.prepared/'upstream', {'path': 'escape/file'})

    def test_build_rejects_forged_inputs_before_compilation(self):
        (self.prepared/'upstream.lock.json').write_text('{}')
        with patch('subprocess.run', side_effect=AssertionError('Compiler ran')):
            with self.assertRaisesRegex(ValueError, 'repository lock'):
                build(self.root/'apps', self.root/'vendor', self.root/'build', self.root/'bin', esp_tts_prepared=self.prepared)

    def test_build_rejects_wrong_chip_before_compilation(self):
        (self.root/'nuttx').mkdir()
        (self.root/'nuttx/.config').write_text('CONFIG_ARCH_CHIP_ESP32S3=y\n')
        with patch('subprocess.run', side_effect=AssertionError('Compiler ran')):
            with self.assertRaisesRegex(ValueError, 'ESP32-P4'):
                build(self.root/'apps', self.root/'vendor', self.root/'build', self.root/'bin', esp_tts_prepared=self.prepared)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    PREPARED = args.prepared.resolve()
    inputs.verify_prepared(PREPARED)
    unittest.main(argv=[sys.argv[0], *remaining])
