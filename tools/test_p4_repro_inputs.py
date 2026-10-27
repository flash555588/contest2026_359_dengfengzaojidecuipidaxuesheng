#!/usr/bin/env python3
"""Offline archive/manifest/path safety and relocation tests; no board access."""
import argparse, contextlib, hashlib, io, json, tarfile, tempfile, unittest
from export_p4_repro_inputs import export
from pathlib import Path
from prepare_p4_repro_inputs import install, obtain, verify, read_lock

p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
class Inputs(unittest.TestCase):
    def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.n='baseline'
    def tearDown(self):self.tmp.cleanup()
    def archive(self,extra=None,link=None):
        archive=self.root/'inputs.tar.gz';payload=b'source "/author/baseline/other/Kconfig"\n';files={'nuttx/Kconfig':payload,'nuttx/.config':b'CONFIG_ARCH_CHIP_ESP32P4=y\n','LICENSE':b'fixture license\n'}
        entries=[{'path':n,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()} for n,b in files.items()]
        if link:entries.append({'path':'nuttx/link','link':link})
        manifest=json.dumps({'format':1,'name':self.n,'original_root':'/author/baseline','entries':entries}).encode()
        with tarfile.open(archive,'w:gz') as t:
            for n,b in files.items():
                info=tarfile.TarInfo(self.n+'/'+n);info.size=len(b);t.addfile(info,io.BytesIO(b))
            if link:
                info=tarfile.TarInfo(self.n+'/nuttx/link');info.type=tarfile.SYMTYPE;info.linkname=link;t.addfile(info)
            if extra:
                info=tarfile.TarInfo(extra);info.size=1;t.addfile(info,io.BytesIO(b'x'))
            info=tarfile.TarInfo(self.n+'/BUILD-INPUT-MANIFEST.json');info.size=len(manifest);t.addfile(info,io.BytesIO(manifest))
        row={'name':self.n,'archive':archive.name,'bytes':archive.stat().st_size,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'url':'https://github.com/fixture/releases/download/test/inputs.tar.gz'}
        return row,archive
    def test_export_cache_exclusion_and_library_retention(self):
        base=self.root/'source';(base/'nuttx').mkdir(parents=True);(base/'espdl-build').mkdir()
        (base/'nuttx/LICENSE').write_text('fixture license')
        (base/'nuttx/source.c').write_text('int fixture;')
        (base/'nuttx/source.o').write_bytes(b'generated')
        (base/'nuttx/libarch.a').write_bytes(b'generated')
        (base/'espdl-build/libqpk_espdl.a').write_bytes(b'required input')
        with contextlib.redirect_stdout(io.StringIO()):row=export(base,['nuttx','espdl-build'],'bundle',self.root/'cache',True)
        with tarfile.open(self.root/'cache'/row['archive']) as t:names=t.getnames()
        self.assertIn('bundle/espdl-build/libqpk_espdl.a',names)
        self.assertNotIn('bundle/nuttx/source.o',names);self.assertNotIn('bundle/nuttx/libarch.a',names)
    def test_export_credential_rejection_removes_partial_archive(self):
        base=self.root/'source';(base/'nuttx').mkdir(parents=True)
        (base/'nuttx/LICENSE').write_text('fixture license')
        (base/'nuttx/config.h').write_text('sk-'+'TEST_FIXTURE_ONLY_0'*3)
        with self.assertRaises(ValueError):export(base,['nuttx'],'bundle',self.root/'cache',True)
        self.assertFalse((self.root/'cache/bundle.tar.gz').exists())
    def test_export_preserves_existing_file(self):
        out=self.root/'cache';out.mkdir();p=out/'bundle.tar.gz';p.write_bytes(b'existing')
        with self.assertRaises(ValueError):export(self.root,[],'bundle',out)
        self.assertEqual(p.read_bytes(),b'existing')
    def test_missing_role_rejected(self):
        path=self.root/'lock.json';path.write_text(json.dumps({'format':1,'inputs':[]}))
        with self.assertRaises(ValueError):read_lock(path)
    def test_noncanonical_member_rejected(self):
        row,a=self.archive(extra='baseline//unexpected')
        with self.assertRaises(ValueError):install(row,a,self.root/'out')
    def test_roundtrip_relocation_reuse(self):
        row,a=self.archive(link='.config');dest=self.root/'installed';target=install(row,a,dest)
        self.assertIn(str(target),(target/'nuttx/Kconfig').read_text());self.assertEqual(install(row,a,dest),target);verify(target)
    def test_modified_input_rejected(self):
        row,a=self.archive();target=install(row,a,self.root/'out');(target/'nuttx/.config').write_text('changed')
        with self.assertRaises(ValueError):verify(target)
    def test_corrupt_archive_rejected(self):
        row,a=self.archive();a.write_bytes(a.read_bytes()+b'changed')
        with self.assertRaises(ValueError):install(row,a,self.root/'out')
    def test_offline_missing(self):
        row,a=self.archive()
        with self.assertRaises(FileNotFoundError):obtain(row,self.root/'empty',True)
    def test_cached_wrong_size(self):
        row,a=self.archive();row['bytes']+=1
        with self.assertRaises(ValueError):obtain(row,self.root,True)
    def test_http_not_used(self):
        row,a=self.archive();row['url']='http://github.com/fixture'
        with self.assertRaises(ValueError):obtain(row,self.root/'new-cache',False)
    def test_traversal_rejected(self):
        row,a=self.archive(extra='../escape')
        with self.assertRaises(ValueError):install(row,a,self.root/'out')
        self.assertFalse((self.root/'escape').exists())
    def test_external_symlink_rejected(self):
        row,a=self.archive(link='../../../escape')
        with self.assertRaises(ValueError):install(row,a,self.root/'out')
    def test_absolute_symlink_rejected(self):
        row,a=self.archive(link='/tmp/escape')
        with self.assertRaises(ValueError):install(row,a,self.root/'out')
    def test_wrong_root_rejected(self):
        row,a=self.archive(extra='other/file')
        with self.assertRaises(ValueError):install(row,a,self.root/'out')
    def test_duplicate_rejected(self):
        row,a=self.archive(extra='baseline/LICENSE')
        with self.assertRaises(ValueError):install(row,a,self.root/'out')

suite=unittest.defaultTestLoader.loadTestsFromTestCase(Inputs)
with (args.output/'tests.log').open('w') as log:result=unittest.TextTestRunner(stream=log,verbosity=2).run(suite)
report={'tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'passed':result.wasSuccessful(),'scope':'offline file fixtures; no hardware or public upload'}
(args.output/'RESULT.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
raise SystemExit(0 if result.wasSuccessful() else 1)
