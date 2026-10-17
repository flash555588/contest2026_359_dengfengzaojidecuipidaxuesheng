"""TTS deployment integrity checks, entirely offline with no serial access."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('speech_deploy',Path(__file__).resolve().parents[1]/'deploy_speech_models.py')
deploy=importlib.util.module_from_spec(spec);spec.loader.exec_module(deploy)


class TTSDeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name);self.prepared=self.root/'prepared'
        self.source=self.prepared/'sdcard/speech';self.source.mkdir(parents=True)
        (self.prepared/'vendor').mkdir()
        self.records={};entries=[]
        for name in ['front.bin','dec.bin','g2p.js','lexicon.bin','phonemes.json','licenses/sanoTTS-MIT.txt','licenses/sanoTTS-GPL3.txt']:
            relative=name if name.startswith('licenses/') else 'sanotts-zh/'+name
            data=('fixture '+name).encode();path=self.source/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            digest=hashlib.sha256(data).hexdigest();self.records[relative]={'bytes':len(data),'sha256':digest}
            if relative.startswith('sanotts-zh/'):
                entries.append('{"'+relative+'",'+str(len(data))+',{'+','.join('0x'+digest[i:i+2] for i in range(0,64,2))+'}}')
        self.manifest=self.source/'manifest.json';self.write_manifest()
        (self.prepared/'vendor/speech_assets.h').write_text(',\n'.join(entries))

    def tearDown(self):
        self.temp.cleanup()

    def write_manifest(self):
        self.manifest.write_text(json.dumps({'files':self.records}))

    def test_runtime_and_licenses_only_and_repeat_is_identical(self):
        self.records['sanotts-zh/unused.tsv']={'bytes':99,'sha256':'0'*64};self.write_manifest()
        target=self.root/'transfer'
        root,manifest,names=deploy.prepare_tts_transfer(self.prepared,target)
        self.assertEqual(len(names),7)
        self.assertNotIn('unused.tsv',names)
        self.assertTrue((root/'licenses/sanoTTS-GPL3.txt').is_file())
        self.assertEqual(deploy.prepare_tts_transfer(self.prepared,target)[1],manifest)

    def test_same_size_corrupt_file_is_rejected_before_copy(self):
        path=self.source/'sanotts-zh/front.bin';data=bytearray(path.read_bytes());data[-1]^=1;path.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'Corrupt TTS asset'):
            deploy.prepare_tts_transfer(self.prepared,self.root/'transfer')
        self.assertFalse((self.root/'transfer').exists())

    def test_manifest_cannot_reauthorize_modified_script(self):
        path=self.source/'sanotts-zh/g2p.js';path.write_bytes(b'changed script')
        self.records['sanotts-zh/g2p.js']={'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()};self.write_manifest()
        with self.assertRaisesRegex(ValueError,'compiled asset table'):
            deploy.prepare_tts_transfer(self.prepared,self.root/'transfer')

    def test_missing_required_resource_is_rejected(self):
        del self.records['sanotts-zh/lexicon.bin'];self.write_manifest()
        with self.assertRaisesRegex(ValueError,'Missing or escaping'):
            deploy.prepare_tts_transfer(self.prepared,self.root/'transfer')

    def test_conflicting_destination_is_preserved(self):
        target=self.root/'transfer';target.mkdir();(target/'front.bin').write_bytes(b'preserve me')
        with self.assertRaisesRegex(ValueError,'Existing transfer file differs'):
            deploy.prepare_tts_transfer(self.prepared,target)
        self.assertEqual((target/'front.bin').read_bytes(),b'preserve me')


if __name__=='__main__':unittest.main()
