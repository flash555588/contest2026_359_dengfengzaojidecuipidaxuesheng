"""Offline checks for SD manifest validation; never opens a serial port."""
from pathlib import Path
import hashlib,json,sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from prepare_jizura_sd import verify

class ManifestTests(unittest.TestCase):
    def setUp(self):
        base=Path(__file__).resolve().parents[3]/'output/jizura-sd-tests';base.mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(dir=base);self.root=Path(self.tmp.name)
        (self.root/'styles.json').write_bytes(b'{"version":1}')
        self.record={'bytes':13,'sha256':hashlib.sha256(b'{"version":1}').hexdigest()}
    def tearDown(self):self.tmp.cleanup()
    def manifest(self,files):
        (self.root/'manifest.json').write_text(json.dumps({'files':files}),encoding='utf-8')
    def test_valid(self):
        self.manifest({'styles.json':self.record});self.assertEqual(len(verify(self.root)['files']),1)
    def test_tamper(self):
        self.manifest({'styles.json':self.record});(self.root/'styles.json').write_bytes(b'{"version":2}')
        with self.assertRaises(ValueError):verify(self.root)
    def test_escape(self):
        for name in ['../styles.json','/styles.json','a\\styles.json']:
            with self.subTest(name=name):
                self.manifest({name:self.record})
                with self.assertRaises(ValueError):verify(self.root)
    def test_missing(self):
        self.manifest({'gone.ttf':self.record})
        with self.assertRaises(ValueError):verify(self.root)
if __name__=='__main__':unittest.main()
