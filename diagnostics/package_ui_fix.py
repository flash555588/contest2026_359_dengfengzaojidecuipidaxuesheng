"""Package only verified screenshots from the final UI image."""
from pathlib import Path
import hashlib
import json
import re
import shutil

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/ui-layout-fix'
evidence = delivery / 'evidence'
digest = hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest()
validation = json.loads((evidence / 'validation.json').read_text())
assert validation['firmware_sha256'] == digest
assert 'Hash of data verified.' in (evidence / 'flash.log').read_text(encoding='utf-8')
assert 'NuttShell (NSH)' in (evidence / 'boot1.log').read_text(encoding='utf-8')
regression = json.loads((evidence / 'regression.json').read_text(encoding='utf-8'))
assert all(case['completed'] and all(case['checks'].values()) for case in regression)
checks = {case['name']: case for case in regression}
assert '.storage-check-' not in checks['storage_cleanup']['output']
address = re.search(r'inet addr:(\S+) DRaddr:(\S+) Mask:(\S+)', checks['assigned_address']['output'])
assert address
pages = []
for directory in [evidence / 'review-light', evidence / 'review-dark']:
    capture = json.loads((directory / 'capture-metadata.json').read_text())
    assert capture['firmware_sha256'] == digest
    reports = json.loads((directory / 'layout-audit.json').read_text(encoding='utf-8'))
    assert len(reports) == len(capture['requested_pages'])
    for report in reports:
        assert not report['overlaps'] and not report['overflow'], report
        assert (directory / (report['page'] + '.png')).exists()
        objects = json.loads((directory / (report['page'] + '.json')).read_text(encoding='utf-8'))
        actual_text = '\n'.join(obj['text'] for obj in objects)
        key = report['page'].split('-', 1)[1]
        expected = {'settings': ['控制面板', '当前屏幕使用固定亮度'],
                    'wifi': ['密码'], 'hello': ['累计启动次数'],
                    'ha': ['Home Assistant', '暂无设备'],
                    'ha-settings': ['地址', '令牌', '高级服务'],
                    'ha-address': ['Home Assistant 地址'],
                    'ha-advanced': ['服务名', 'JSON', '执行'],
                    'toast': ['操作完成，中文提示清晰可读']}.get(key, [])
        assert all(label in actual_text for label in expected), report['page']
        pages.append({'page': report['page'], 'objects': report['objects'],
                      'screenshot': (directory / (report['page'] + '.png')).relative_to(delivery).as_posix()})
# These two historical logs were copied from the storage image. Their source
# is recorded explicitly; never infer current-image final state from them.
provenance = json.loads((evidence / 'provenance-correction.json').read_text(encoding='utf-8'))
assert provenance['target_firmware_sha256'] == digest
assert provenance['usable_as_target_final_state'] is False
hardware = {
    'firmware_sha256': digest, 'flashed_and_verified': True, 'port': 'COM23',
    'device_serial': 'E8:F6:0A:E3:A9:5F', 'chip_revision': '3.2',
    'geometry': '1024x600 RGB565', 'framebuffer_visual_review': True,
    'layout_overlap_count': 0, 'layout_overflow_count': 0, 'tested_pages_and_states': pages,
    'storage_selftest': 'PASS', 'dhcp_result': 0, 'ipv4': address.group(1),
    'gateway': address.group(2), 'netmask': address.group(3),
    'data_partition_erased': False, 'display_running': True,
    'serial_resumed_after_jtag': None, 'physical_touch_accuracy_tested': False,
    'final_state_provenance': 'evidence/provenance-correction.json',
    'backlight': 'fixed; current configuration has no PWM control',
    'camera_state_reviewed': 'startup failure UI; /dev/video0 is absent; live preview not tested',
    'limits': ['Inherited running-state/final-console logs are not final-state evidence for this image.',
               'Page states listed here only; arbitrary external QPKs are not covered.',
               'HA has no configured server/entities; no remote device control was tested.',
               'BLE page layout is covered; pairing is not validated.',
               'Camera error-state controls only; /dev/video0 is absent.',
               'Framebuffer inspection does not measure physical panel color or touch accuracy.']}
(evidence / 'hardware-validation.json').write_text(json.dumps(hardware, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
metadata = {
    'date': '2026-09-14', 'status': 'built-flashed-and-ui-reviewed', 'firmware': validation,
    'config_sha256': hashlib.sha256((delivery / 'resolved.config').read_bytes()).hexdigest(),
    'base_firmware_sha256': '2159a0adca9a7e76680233064d907aa3676b6d10da12b1e35dae3901f7b65932',
    'config_changes': [], 'build_tree': '/tmp/v3-desktop-ui-layout-20260914',
    'original_tree_preserved': '/home/streetartist/nuttxspace',
    'compiler': 'Espressif GCC 14.2; xPack RV32IMAC/ILP32 soft-float libgcc',
    'hardware_validation': hardware}
(delivery / 'build-metadata.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
gallery = ['# 实板界面检查\n',
           '最终固件 SHA-256：`' + digest + '`\n',
           '本轮检查 ' + str(len(pages)) + ' 个页面/主题状态；对象坐标审计未发现非预期重叠或越界。'
           '所有图片均为实板帧缓冲截图。功能验证和范围限制见 README 与硬件验证 JSON。\n',
           '| 页面/状态 | 对象数 | 截图 |\n| --- | ---: | --- |\n']
gallery += ['| ' + item['page'] + ' | ' + str(item['objects']) + ' | [查看](' +
            item['screenshot'] + ') |\n' for item in pages]
(delivery / 'UI-REVIEW.md').write_text('\n'.join(gallery), encoding='utf-8')
scripts = delivery / 'build-scripts'
scripts.mkdir(exist_ok=True)
for name in ['build_ui_fix.py', 'build_linux.py', 'prepare_tls.py', 'update_ui_layout.py',
             'prepare_ui_tools.py', 'validate_ui_fix.py', 'flash_ui_fix.py', 'package_ui_fix.py',
             'capture_ui.py', 'audit_ui_layout.py', 'capture_serial.py', 'software_probe.py',
             'inspect_running.py', 'ui-regression-suite.json', 'ui-final-suite.json']:
    shutil.copyfile(workspace / 'diagnostics' / name, scripts / name)
shutil.copyfile(workspace / 'diagnostics/homeassistant-updated.js', delivery / 'homeassistant-app.js')
paths = sorted(path for path in delivery.rglob('*') if path.is_file() and
               path.name != 'SHA256SUMS' and '__pycache__' not in path.parts)
(delivery / 'SHA256SUMS').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest() +
    '  ' + path.relative_to(delivery).as_posix() + '\n' for path in paths), encoding='utf-8')
print('Packaged', len(paths), 'files;', len(pages), 'reviewed page/theme states;', digest)
