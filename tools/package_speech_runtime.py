#!/usr/bin/env python3
"""Create and verify a local voice deployment bundle; never accesses a board."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['prepared','firmware','cache','output']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();repo=Path(__file__).resolve().parents[1]
    metadata=json.loads((a.firmware/'BUILD-METADATA.json').read_text())
    assert metadata['vision_models_sd_only']
    data_offset=int(metadata['data_offset'],0);data_size=int(metadata['data_size'],0)
    assert (data_offset,data_size) in [(0xc00000,0x400000),(0xe00000,0x200000)]
    assert int(metadata['program_limit'],0)==data_offset
    assert sha(a.firmware/'nuttx.bin')==metadata['sha256']['nuttx.bin']
    assert (a.firmware/'nuttx.bin').stat().st_size+0x2000<=data_offset
    assert all(sha(repo/name)==value for name,value in metadata['source_sha256'].items())
    lock=repo/'app/espdl-quickapp/speech/upstream.lock.json'
    assert sha(lock)==metadata['speech']['lock_sha256']
    assert sha(a.prepared/'sdcard/speech/manifest.json')==metadata['speech']['sd_manifest_sha256']
    tree=a.output/'deployment';tree.mkdir(parents=True,exist_ok=True)
    for name in ['nuttx.bin','resolved.config','BUILD-METADATA.json']:shutil.copy2(a.firmware/name,tree/name)
    manifest=json.loads((a.prepared/'sdcard/speech/manifest.json').read_text())
    for name,record in manifest['files'].items():
        source=a.prepared/'sdcard/speech'/name
        assert source.stat().st_size==record['bytes'] and sha(source)==record['sha256']
    shutil.copytree(a.prepared/'sdcard',tree/'sdcard',dirs_exist_ok=True)
    vision={
      'imagenet_cls_mobilenetv2_s8_v1.espdl':'04ecb03fa36b5dadf012e51aa2b8f5ac85ff8b48dc61a501152199d9de429b82',
      'human_face_detect_msr_s8_v1.espdl':'2bbaaf17f39ce9ee547abf5ffdc6d8fcd1844009a0d26878d7bfb33451f16211',
      'human_face_detect_mnp_s8_v1.espdl':'5b7fedcff59dbe69d1935d6d206bf371fc18b5dd9176320ef69c9f05ee83af55'}
    (tree/'sdcard/espdl').mkdir(exist_ok=True)
    for name,digest in vision.items():
        source=repo/'app/espdl-quickapp/models'/name;assert sha(source)==digest
        shutil.copy2(source,tree/'sdcard/espdl'/name)
    shutil.copy2(repo/'app/espdl-quickapp/models/ESP-DL-LICENSE',tree/'sdcard/espdl/ESP-DL-LICENSE')
    shutil.copy2(repo/'app/espdl-quickapp/speech/README.zh-CN.md',tree/'README.zh-CN.md')
    source_root=tree/'source';source_root.mkdir(exist_ok=True)
    shutil.copy2(lock,source_root/lock.name)
    desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
    for source in [*desktop.glob('glass_speech*'),desktop/'glass_voice_capture.inc',desktop/'glass_voice.c',desktop/'glass_music_pcm.c',desktop/'glass_music_pcm.h',desktop/'qpk_espdl_store.c']:
        if source.is_file():shutil.copy2(source,source_root/source.name)
    for name in ['qpk_volume.h','desktop_main.c','qpk_runtime.c','glass_portal_files.c','glass_qpk_builder.c','glass_qpk_builder.h','glass_qpk_remove.inc','glass_ui.inc','glass_chat_ui.inc','qpk_hardware.c','qpk_hardware.h','qpk_hardware_js.inc','qpk_hw_platform.c','qpk_hw_audio.c','espdl_assets.h']:
        shutil.copy2(desktop/name,source_root/name)
    shutil.copytree(desktop/'portal',source_root/'portal',dirs_exist_ok=True)
    shutil.copytree(desktop/'speech_port',source_root/'speech_port',dirs_exist_ok=True)
    for name in ['prepare_speech_runtime.py','build_speech_runtime.py','build_bilibili_v3.py','test_speech_host.py','test_speech_g2p.mjs','package_speech_runtime.py','backup_storage_export.py','test_install_volumes.py','test_storage_layout.py']:
        shutil.copy2(repo/'tools'/name,source_root/name)
    # Preserve every locked original upstream source and notice, including voice metadata.
    for entry in json.loads(lock.read_text())['sources']:
        source=a.cache/entry['cache']/entry['path'];assert sha(source)==entry['sha256']
        if source.suffix in ['.a','.bin','.xz']:continue
        target=source_root/'upstream'/entry['cache']/entry['path'];target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    records={f.relative_to(tree).as_posix():{'bytes':f.stat().st_size,'sha256':sha(f)}
             for f in sorted(tree.rglob('*')) if f.is_file() and f.name!='DEPLOYMENT-MANIFEST.json'}
    (tree/'DEPLOYMENT-MANIFEST.json').write_text(json.dumps({'board':'ESP32-P4 v3','board_tested':False,'flash_offset':'0x2000','userdata_offset':hex(data_offset),'userdata_size':hex(data_size),'layout':metadata['flash_layout'],'files':records},indent=2)+'\n')
    archive=a.output/f'p4-v3-{data_offset//0x100000}m-{data_size//0x100000}m-sdapps-zh.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for f in sorted(tree.rglob('*')):
            if f.is_file():z.write(f,f.relative_to(tree).as_posix())
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for name,r in records.items():assert hashlib.sha256(z.read(name)).hexdigest()==r['sha256']
    print('DEPLOYMENT_VERIFIED',len(records),archive.stat().st_size,sha(archive),archive)


if __name__=='__main__':main()
