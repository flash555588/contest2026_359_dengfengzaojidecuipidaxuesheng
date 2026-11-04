"""Host tests for PCM preservation, exact pronunciation, split isolation and bounds."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest

import numpy as np
import soundfile as sf

from .corpus import Interval, normalize_token, read_textgrid, split_for
from .prepare import safe_name
from .synthesize import Inventory, Span, Unit, bounded_join, merge_spans, render


class BoundaryTests(unittest.TestCase):
    def test_transient_is_not_swallowed_by_a_long_overlap(self):
        left = np.zeros(1000, np.int16)
        right = np.zeros(1000, np.int16)
        right[20] = 25000
        joined, detail = bounded_join(left, right, 48000, 0., 0.)
        self.assertEqual(detail['method'], 'microfade')
        self.assertEqual(len(joined), 2000)
        self.assertEqual(joined[1020], 25000)
        np.testing.assert_array_equal(joined[1012:], right[12:])

    def test_voiced_overlap_preserves_both_interiors_and_never_clips(self):
        t = np.arange(2400)/48000
        left = np.rint(12000*np.sin(2*np.pi*240*t)).astype(np.int16)
        right = left.copy()
        joined, detail = bounded_join(left, right, 48000, 240., 240.)
        self.assertEqual(detail['method'], 'correlated_voiced_overlap')
        n = detail['samples']
        np.testing.assert_array_equal(joined[:len(left)-n], left[:-n])
        np.testing.assert_array_equal(joined[len(left):], right[n:])
        self.assertLessEqual(np.abs(joined.astype(np.int32)).max(), 12000)

    def test_empty_or_wrong_pcm_is_rejected(self):
        with self.assertRaises(ValueError):
            bounded_join(np.array([], np.int16), np.ones(30, np.int16), 48000, 0, 0)
        with self.assertRaises(ValueError):
            bounded_join(np.zeros(30), np.ones(30), 48000, 0, 0)


class CorpusTests(unittest.TestCase):
    def test_tones_are_not_substituted(self):
        self.assertEqual(normalize_token('lv0'), 'lv5')
        self.assertNotEqual(normalize_token('ma1'), normalize_token('ma3'))
        with self.assertRaises(ValueError):
            normalize_token('hello')

    def test_archive_traversal_rejected(self):
        for path in ('../bad', '/absolute', 'A:/bad', 'good/../../bad', 'a\\..\\bad'):
            with self.assertRaises(ValueError):
                safe_name(path)
        safe_name('BZNSYP/Wave/000001.wav')

    def test_textgrid_words_and_phones_are_separate(self):
        grid = '''File type = "ooTextFile"
item [1]:
 name = "words"
 intervals [1]:
  xmin = 0.0
  xmax = 0.2
  text = "ni3"
item [2]:
 name = "phones"
 intervals [1]:
  xmin = 0.0
  xmax = 0.1
  text = "n"
 intervals [2]:
  xmin = 0.1
  xmax = 0.2
  text = "i3"
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'x.TextGrid'
            path.write_text(grid, encoding='utf-8')
            parsed = read_textgrid(path)
            self.assertEqual(parsed['words'], [Interval(0., .2, 'ni3')])
            self.assertEqual(len(parsed['phones']), 2)
            path.write_text(grid.replace('xmax = 0.2', 'xmax = -0.2'), encoding='utf-8')
            with self.assertRaises(ValueError):
                read_textgrid(path)

    def test_split_is_repeatable_and_has_all_three_partitions(self):
        outcomes = [split_for(f'{x:06d}') for x in range(1000)]
        self.assertEqual(set(outcomes), {'train', 'validation', 'test'})
        self.assertEqual(outcomes, [split_for(f'{x:06d}') for x in range(1000)])


class SelectionTests(unittest.TestCase):
    def make_inventory(self, root):
        waveform = np.arange(-3000, 3000, dtype=np.int16)
        wav = root/'source.wav'
        sf.write(wav, waveform, 48000, subtype='PCM_16')
        inv = object.__new__(Inventory)
        inv.sources = {'train-a': dict(path=str(wav), rate=48000, frames=len(waveform),
                                      sha256=hashlib.sha256(wav.read_bytes()).hexdigest())}
        inv._wave_cache = {}
        f = dict(duration=.04, voiced=0., f0=[0.,0.,0.], log_rms=-3., gap_before=0., gap_after=0., left=[0.]*24, right=[0.]*24)
        a = Unit(1, 'train-a', 0, 'ni3', 100, 2100, dict(f))
        b = Unit(2, 'train-a', 1, 'hao3', 2200, 4200, dict(f))
        inv.units = {'train-a': [a,b]}
        inv.by_token = {'ni3':[a], 'hao3':[b]}
        return inv, waveform

    def test_contiguous_source_is_reproduced_bit_exact_including_gap(self):
        with tempfile.TemporaryDirectory() as directory:
            inv, source = self.make_inventory(Path(directory))
            spans, _ = inv.select(['ni3','hao3'])
            self.assertEqual(len(spans), 1)
            pcm, rate, joins = render(inv, spans)
            np.testing.assert_array_equal(pcm, source[100:4200])
            self.assertEqual(rate, 48000)
            self.assertEqual(joins, [])

    def test_missing_tone_is_rejected_instead_of_approximated(self):
        with tempfile.TemporaryDirectory() as directory:
            inv, _ = self.make_inventory(Path(directory))
            with self.assertRaisesRegex(ValueError, 'Missing exact'):
                inv.select(['ni2','hao3'])

    def test_changed_waveform_is_rejected_even_at_the_same_length(self):
        with tempfile.TemporaryDirectory() as directory:
            inv, source = self.make_inventory(Path(directory))
            source[1000] += 3
            sf.write(Path(directory)/'source.wav', source, 48000, subtype='PCM_16')
            with self.assertRaisesRegex(ValueError, 'checksum'):
                inv.wave('train-a')

    def test_non_train_waveform_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            inv, _ = self.make_inventory(Path(directory))
            with self.assertRaisesRegex(ValueError, 'excluded'):
                inv.wave('test-a')

    def test_coalescing_does_not_merge_different_sources(self):
        path = [Span('a', 0, 0, 0), Span('b', 1, 1, 1)]
        self.assertEqual(merge_spans(path), path)


if __name__ == '__main__':
    unittest.main()
