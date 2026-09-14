"""Package only verified final-image evidence for BLE device-name display."""
from pathlib import Path
import difflib
import hashlib
import json
import re
import shutil
import tarfile

root = Path(__file__).resolve().parent.parent
delivery = root / '04-v3-20260913/ble-device-names'
evidence = delivery / 'evidence'
base = delivery.parent / 'ble-startup-fix'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def read(path):
    return json.loads(path.read_text(encoding='utf-8'))

def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

digest = sha(delivery / 'nuttx.bin')
assert digest == '0ac1e9ff601f28db20a6878baf77cef7b48662c4837c427d76b7f34766dc6b09'
validation = read(evidence / 'validation.json')
assert validation['firmware_sha256'] == digest
assert 'Hash of data verified.' in (evidence / 'flash-sorted.log').read_text()
assert 'NuttShell (NSH)' in (evidence / 'boot-sorted.log').read_text()
assert (delivery / 'resolved.config').read_bytes() == (base / 'resolved.config').read_bytes()
suites = {'scan-sorted.json': 'ble-names-suite.json',
          'repeat-sorted.json': 'ble-names-repeat-suite.json',
          'final-console.json': 'ble-final-suite.json'}
runs = {}
for file, suite in suites.items():
    result = read(evidence / file)
    assert [c['name'] for c in result] == ['console'] + [c['name'] for c in read(root / 'diagnostics' / suite)]
    assert all(c['completed'] and all(c['checks'].values()) for c in result), file
    runs[file] = {'suite': 'build-scripts/' + suite, 'all_checks_passed': True,
                  'sha256': sha(evidence / file), 'case_count': len(result)}
final = {c['name']: c for c in read(evidence / 'final-console.json')}
assert '.storage-check-' not in final['storage_cleanup']['output']
parser = read(evidence / 'name-parser-test.json')
assert parser['returncode'] == 0 and not parser['stderr']
assert parser['source_sha256'] == sha(delivery / 'overlay/apps/wireless/bluetooth/nimble/glass_ble_name.h')
pages = []
names = set()
for directory in ['review-light-final', 'review-dark-final']:
    folder = evidence / directory
    assert read(folder / 'capture-metadata.json')['firmware_sha256'] == digest
    reports = read(folder / 'layout-audit.json')
    assert len(reports) == 1
    for report in reports:
        assert not report['overlaps'] and not report['overflow'], report
        page = report['page']
        objects = read(folder / (page + '.json'))
        parents = {obj['id']: obj for obj in objects}
        for obj in objects:
            parent = parents.get(obj['parent'], {})
            if obj['type'] == 'label' and parent.get('type') == 'button' and parent.get('h') == 68:
                if obj['text'] and obj['text'] != '未命名设备' and ' dBm' not in obj['text']:
                    names.add(obj['text'])
        assert (folder / (page + '.png')).exists()
        assert (evidence / 'final-console.json').stat().st_mtime > (folder / (page + '.png')).stat().st_mtime
        pages.append({'page': page, 'path': 'evidence/' + directory + '/' + page + '.png',
                      'overlaps': 0, 'overflow': 0})
assert names, 'Require a real named device visible on the final-image screenshot.'
count = int(re.search(r'就绪 · (\d+) 个设备', final['ready_state']['output']).group(1))
hardware = {'firmware_sha256': digest, 'flashed_and_verified': True, 'port': 'COM23',
            'named_devices_visually_verified': sorted(names), 'last_scan_count': count,
            'active_scan': True, 'repeat_scan_and_cancel': True, 'storage_selftest': 'PASS',
            'serial_resumed_after_jtag': True, 'data_partition_erased': False,
            'wifi_dhcp_this_release': 'Not attempted: user confirmed previous hotspot unavailable after relocation.',
            'bluetooth_audio': 'Not implemented; current C6 does not support classic BR/EDR or A2DP.',
            'pairing_tested': False, 'gatt_connection_tested': False, 'visual_review': pages}
write(evidence / 'hardware-validation.json', hardware)
write(evidence / 'evidence-index.json', {
    'firmware_sha256': digest, 'serial_runs': runs, 'visual_runs': pages,
    'final_image_evidence': ['flash-sorted.log', 'boot-sorted.log', 'build.log', 'validation.json',
                              'image-info.txt', 'name-parser-test.json'],
    'previous_image': read(evidence / 'first-image-provenance.json'),
    'temporary_windows_advertiser': 'Start rejected by Windows with invalid argument. No passing result claimed; name evidence is a real nearby device.',
    'other_files': 'Auxiliary references or previous experiments; not final-image passing evidence.'})

targets = {p.relative_to(delivery / 'overlay').as_posix(): p for p in (delivery / 'overlay').rglob('*')
           if p.is_file() and '__pycache__' not in p.parts}
patch = []
archive_path = delivery.parent / 'current-build-tree-source.tar.gz'
with tarfile.open(archive_path) as archive:
    for member in archive:
        name = member.name.removeprefix('./')
        if name in targets:
            before = archive.extractfile(member).read().decode('utf-8').splitlines(True)
            after = targets.pop(name).read_text(encoding='utf-8').splitlines(True)
            patch.extend(difflib.unified_diff(before, after, 'a/' + name, 'b/' + name))
for name, path in sorted(targets.items()):
    patch.extend(difflib.unified_diff([], path.read_text(encoding='utf-8').splitlines(True), '/dev/null', 'b/' + name))
(delivery / 'full-fix.patch').write_text(''.join(patch), encoding='utf-8', newline='\n')
write(delivery / 'build-metadata.json', {
    'firmware': validation, 'hardware_validation': hardware, 'base_firmware_sha256': sha(base / 'nuttx.bin'),
    'source_archive_sha256': sha(archive_path), 'config_sha256': sha(delivery / 'resolved.config'),
    'build_tree': '/tmp/v3-desktop-ble-names-20260914', 'config_changes': [],
    'toolchain': 'Espressif GCC 14.2; xPack RV32IMAC/ILP32 soft-float libgcc'})
scripts = delivery / 'build-scripts'
scripts.mkdir(exist_ok=True)
for name in ['prepare_ble_names.py', 'build_ble_names.py', 'build_linux.py', 'prepare_tls.py',
             'validate_ble_fix.py', 'flash_ble_fix.py', 'test_ble_names.py', 'test_ble_names.c',
             'capture_ui.py', 'audit_ui_layout.py', 'capture_serial.py', 'software_probe.py',
             'package_ble_names.py', *suites.values()]:
    shutil.copyfile(root / 'diagnostics' / name, scripts / name)
shutil.copytree(root / 'diagnostics/ble-name-source', scripts / 'ble-name-source', dirs_exist_ok=True)
readme = f'''# 蓝牙设备名称显示版

已编译、烧录到当前 ESP32-P4 v3.2 + ESP32-C6 实板。真实广播中的 **{', '.join(sorted(names))}** 已在最终界面显示；最后一次扫描发现 {count} 个设备。

## 本次完成

- 主动扫描，合并广播和扫描响应中的完整／缩略设备名，完整名称优先；不带名称的后续数据不会覆盖已有名称。
- 名称和地址／RSSI 分成两行，中文使用桌面中文字体，长名称自动省略；未提供名称时显示“未命名设备”。
- 扫描结束后有名称的设备优先显示，同组内按信号强度排序。扫描期间更新原有行，保留滚动位置。
- 列表容量从 12 增至 32；每个名称最多保存 63 字节，截断保留完整 UTF-8 字符，处理畸形和控制字符。
- 保留此前蓝牙启动、10 秒扫描结束、取消扫描、桌面排版、存储与启动修复；配置未变，相机未修改。

## 验证

实际名称接收、深浅色界面、重复扫描、退出取消和存储自测通过。解析器使用 ASan／UBSan 验证完整名称优先、扫描响应合并、中文、截断以及 25,600 组异常数据，无错误。

截图：[浅色](evidence/review-light-final/light-bluetooth.png)、[深色](evidence/review-dark-final/dark-bluetooth.png)。可见控件坐标审计未发现非预期重叠与越界。物理触摸精度、真实外设连接和配对未实测。

用户已更换工作地点，原 Wi-Fi 热点消失，本版未再尝试 DHCP。最终串口检查确认蓝牙就绪、存储挂载、系统继续运行。`evidence/evidence-index.json` 区分最终固件与初次未排序版本的记录。

## 耳机／音箱放音尚未完成

板载 C6 只有 BLE，不支持经典蓝牙 BR/EDR，不能直接实现普通耳机／音箱的 A2DP 放音。显示出耳机的 BLE 名称并不代表能够传输音频。

需要外接支持 **A2DP Source** 的音频发射模块，或原版 ESP32 作为音频协处理器。配对、绑定、重连和播放器音频路由需根据模块型号、协议与接线实现；目前尚未收到这些硬件信息。[具体接入要求](AUDIO-INTEGRATION.md)。

## 固件与源码

- `nuttx.bin` SHA-256：`{digest}`，大小 {validation['bytes']:,} 字节，偏移 `0x2000`。
- 擦除范围止于 `0x3c2000`，未触及从 `0x400000` 开始的 `/data`，测试临时目录已清理。
- `nuttx.elf`、`nuttx.map`、`resolved.config`、`SHA256SUMS` 保存对应调试结果、配置和校验。
- `overlay/` 为累计覆盖层；`bluetooth-names.patch` 为相对上一 BLE 修复版的 5 个文件增量；`full-fix.patch` 相对上级 `current-build-tree-source.tar.gz`。

在完整源码工作区运行 `python diagnostics/prepare_ble_names.py`，然后从 WSL 运行 `python3 diagnostics/build_ble_names.py`。构建依赖已验证的 `/tmp/v3-desktop-ble-startup-20260914`，在 `/tmp/v3-desktop-ble-names-20260914` 隔离目录进行；使用 Espressif GCC 14.2、xPack 软浮点 libgcc 和原 TLS 缓存。`build-scripts/` 是追溯副本，需放回完整工作区的诊断脚本位置执行。

如需重烧，在源码根目录运行 `python diagnostics/flash_ble_fix.py --delivery 04-v3-20260913/ble-device-names --log flash-next.log`，日志须采用新文件名；完成后复位启动。
'''
(delivery / 'README.md').write_text(readme, encoding='utf-8')
paths = sorted(p for p in delivery.rglob('*') if p.is_file() and p.name != 'SHA256SUMS' and '__pycache__' not in p.parts)
(delivery / 'SHA256SUMS').write_text(''.join(sha(p) + '  ' + p.relative_to(delivery).as_posix() + '\n' for p in paths), encoding='utf-8')
print(json.dumps({'files': len(paths), 'firmware_sha256': digest, 'real_names': sorted(names), 'scan_count': count}))
