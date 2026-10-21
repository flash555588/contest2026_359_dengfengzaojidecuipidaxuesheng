"""The deployer must reject files or metadata differing from the compiled table."""
from pathlib import Path
import json
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from deploy_vision_models import verify

class ModelInputs(unittest.TestCase):
    def test_locked_models_and_corruption(self):
        source=Path(__file__).resolve().parents[2]/'app/espdl-quickapp/models'
        self.assertEqual(len(verify(source)['files']),4)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            names=['manifest.json','ESP-DL-LICENSE']
            manifest=json.loads((source/'manifest.json').read_text())
            names += [row['file'] for row in manifest['records']]
            for name in names:shutil.copyfile(source/name,root/name)
            name=manifest['records'][0]['file']
            with (root/name).open('r+b') as f:
                first=f.read(1);f.seek(0);f.write(bytes([first[0]^1]))
            with self.assertRaisesRegex(ValueError,'hash mismatch'):verify(root)
            shutil.copyfile(source/name,root/name)
            manifest['records'][0]['sha256']='0'*64
            (root/'manifest.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'compiled table'):verify(root)

if __name__=='__main__':unittest.main()
