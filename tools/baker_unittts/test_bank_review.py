"""Regression tests for linguistic context surviving PCM-bank packing."""
import hashlib
import contextlib
import io
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest

import numpy as np

from .bank import digest_file,compile_bank
from .bank_review import repair
from .selector import candidate_inputs
from .synthesize import Inventory
from . import test_unittts as fixtures


class BankContextTests(unittest.TestCase):
    def make_legacy(self, root):
        fixture, wave = fixtures.SelectionTests().make_inventory(root)
        original = root/'corpus.sqlite'
        connection = sqlite3.connect(original)
        connection.executescript('''
          CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT);
          CREATE TABLE utterances(id TEXT PRIMARY KEY,path TEXT,sha256 TEXT,rate INTEGER,
                                  frames INTEGER,subtype TEXT,split TEXT);
          CREATE TABLE units(id INTEGER PRIMARY KEY,utterance TEXT,ordinal INTEGER,
                             token TEXT,start INTEGER,end INTEGER,features TEXT);
        ''')
        source = fixture.sources['train-a']
        connection.execute('INSERT INTO utterances VALUES(?,?,?,?,?,?,?)',
                           ('train-a', source['path'], source['sha256'], 48000, len(wave), 'PCM_16', 'train'))
        for u in fixture.units['train-a']:
            connection.execute('INSERT INTO units VALUES(?,?,?,?,?,?,?)',
                               (u.uid, u.utterance, u.ordinal, u.token, u.start, u.end, json.dumps(u.f)))
        connection.commit(); connection.close()
        bank = root/'legacy'; bank.mkdir()
        block = wave[2192:4208].astype('<i2').tobytes()
        (bank/'audio.pcm').write_bytes(block)
        connection = sqlite3.connect(bank/'bank.sqlite')
        connection.executescript('''
          CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT);
          CREATE TABLE utterances(id TEXT PRIMARY KEY,path TEXT,sha256 TEXT,rate INTEGER,
            frames INTEGER,subtype TEXT,split TEXT,origin TEXT,origin_start INTEGER,raw_offset INTEGER);
          CREATE TABLE units(id INTEGER PRIMARY KEY,utterance TEXT,ordinal INTEGER,
                             token TEXT,start INTEGER,end INTEGER,features TEXT);
        ''')
        manifest = dict(schema=1,bank_id='legacy-bank',budget_bytes=10_000_000,
                        selected_units=1,quality_passed=False)
        connection.execute('INSERT INTO metadata VALUES(?,?)', ('manifest',json.dumps(manifest)))
        connection.execute('INSERT INTO utterances VALUES(?,?,?,?,?,?,?,?,?,?)',
                           ('clip','audio.pcm',hashlib.sha256(block).hexdigest(),48000,len(block)//2,
                            'PCM_16','train','train-a',2192,0))
        u = fixture.units['train-a'][1]
        connection.execute('INSERT INTO units VALUES(?,?,?,?,?,?,?)',
                           (u.uid,'clip',0,u.token,u.start-2192,u.end-2192,json.dumps(u.f)))
        connection.commit(); connection.close()
        manifest['files'] = {name:dict(bytes=(bank/name).stat().st_size,sha256=digest_file(bank/name))
                             for name in ('audio.pcm','bank.sqlite')}
        (bank/'bank.json').write_text(json.dumps(manifest))
        return original, bank

    def test_repair_preserves_pcm_and_original_context_scores_and_model_features(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            original, bank = self.make_legacy(root)
            hashes = {name: digest_file(bank/name) for name in ('bank.sqlite','audio.pcm','bank.json')}
            report = repair(original, bank, root/'fixed')
            self.assertGreater(report['before']['context_mismatches']['ordinal'], 0)
            self.assertFalse(report['before']['source_context_complete'])
            self.assertFalse(any(report['after']['context_mismatches'].values()))
            self.assertTrue(report['after']['source_context_complete'])
            self.assertNotEqual(report['bank_id'], 'legacy-bank')
            for name, expected in hashes.items():
                self.assertEqual(digest_file(bank/name), expected)
            full, fixed = Inventory(original), Inventory(root/'fixed/bank.sqlite')
            u, p = full.by_token['hao3'][0], fixed.by_token['hao3'][0]
            np.testing.assert_array_equal(full.wave(u.utterance)[0][u.start:u.end],
                                          fixed.wave(p.utterance)[0][p.start:p.end])
            for tokens, position in [(['hao3'],0), (['ni3','hao3'],1), (['ma1','hao3'],1)]:
                desired = full.default_targets(['hao3'])[0]
                targets = np.tile(desired, (len(tokens),1))
                self.assertAlmostEqual(full.candidates(tokens,position,targets)[0].cost,
                                       fixed.candidates(tokens,position,targets)[0].cost)
                vocab = {'hao3':1,'ni3':2,'ma1':3,'<boundary>':0}
                a = candidate_inputs(full,[u],tokens,position,desired,vocab)
                b = candidate_inputs(fixed,[p],tokens,position,desired,vocab)
                for x, y in zip(a,b):
                    np.testing.assert_array_equal(x,y)

    def test_changed_bank_pcm_is_rejected_before_repair(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            original, bank = self.make_legacy(root)
            data = bytearray((bank/'audio.pcm').read_bytes()); data[100] ^= 1
            (bank/'audio.pcm').write_bytes(data)
            with self.assertRaisesRegex(ValueError,'Frozen bank file changed'):
                repair(original,bank,root/'fixed')
            self.assertFalse((root/'fixed').exists())

    def test_compiler_keeps_full_unit_features_and_native_pcm_with_context(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            original,_=self.make_legacy(root)
            inventory=Inventory(original)
            controls=root/'controls';controls.mkdir()
            sequence=inventory.units['train-a']
            feature_path=controls/'train-a.npz'
            np.savez_compressed(feature_path,tokens=np.array([u.token for u in sequence]),
                source_pcm_sha256=np.array(inventory.sources['train-a']['sha256']),
                frame_times=np.arange(25,dtype=np.float32)*.005,
                acoustic_features=np.ones((25,24),np.float16),
                frame_f0=np.full(25,200.,np.float32),frame_voiced=np.ones(25,np.float32),
                log_f0=np.full((2,24),np.log(200),np.float32),voiced=np.ones((2,24),np.float32),
                phones=np.array([['n','i','','','',''],['h','ao','','','','']]),
                durations=np.array([[.01,.03,0,0,0,0],[.01,.03,0,0,0,0]],np.float32),
                characters=np.array(['你','好']))
            (controls/'dataset.json').write_text(json.dumps(dict(source_index_sha256=digest_file(original),
                records=[dict(id='train-a',split='train',path=feature_path.name,sha256=digest_file(feature_path))])))
            with contextlib.redirect_stdout(io.StringIO()):
                compile_bank(original,root/'new-bank',10_000_000,control_data=controls)
            compiled=Inventory(root/'new-bank/bank.sqlite')
            self.assertTrue(compiled.context_complete)
            for token in ('ni3','hao3'):
                source=inventory.by_token[token][0];packed=compiled.by_token[token][0]
                data=compiled.acoustic_unit(packed)
                self.assertEqual(data['features'].shape[1],24)
                self.assertEqual(data['log_f0'].shape,(24,))
                self.assertGreater(len(data['frame_times']),1)
                self.assertEqual(inventory.source_context(source),compiled.source_context(packed))
                np.testing.assert_array_equal(inventory.wave(source.utterance)[0][source.start:source.end],
                                              compiled.wave(packed.utterance)[0][packed.start:packed.end])


if __name__ == '__main__':
    unittest.main()
