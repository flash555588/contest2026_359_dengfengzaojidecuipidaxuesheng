"""Summarize the completed flash/boot/DHCP checks from preserved evidence."""
from pathlib import Path
import hashlib
import json
import re

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/wifi-dhcp-fix'
evidence = delivery / 'evidence'
digest = hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest()
assert digest == '559e8f13dc4f1627048c6a5250383762d96262f3b892018382a1a68c60bca953'
flash = (evidence / 'flash.log').read_text(errors='replace')
boot = (evidence / 'boot1.log').read_text(errors='replace')
running = (evidence / 'running-state.txt').read_text(errors='replace')
post = {case['name']: case for case in json.loads((evidence / 'postflash.json').read_text())}
final = {case['name']: case for case in json.loads((evidence / 'final-console.json').read_text())}
assert 'Hash of data verified.' in flash
assert 'Wrote 3925060 bytes' in flash
assert 'NuttShell (NSH)' in boot and 'SHA-256 comparison failed' not in boot
assert 'no mem for pair' not in boot and 'Not enough memory' not in boot
assert all(case['completed'] and all(case['checks'].values()) for case in [*post.values(), *final.values()])
assert 'eth0' not in post['interfaces_before_c6']['output']
assert 'EVENT StaConnected' in post['association_observation']['output']
assert 'DHCP ret=0' in post['dhcp']['output']
address = final['address_after_jtag']['output']
assert address.count('Link encap:') == 1
assert '10:bd:a3:89:58:74' in address
ipv4, gateway, mask = re.search(r'inet addr:(\S+) DRaddr:(\S+) Mask:(\S+)', address).groups()
assert ipv4 != '0.0.0.0'
assert 'video_running = true' in running and 'OSINIT_IDLELOOP' in running
assert 'dpi_h_res = 1024, dpi_v_res = 600' in running
assert 'emac_rx' not in final['final_tasks']['output']
report = {'date': '2026-09-14', 'status': 'flashed-boot-display-driver-and-dhcp-verified',
          'firmware_sha256': digest, 'flashed': True, 'flash_content_verified': True,
          'port': 'COM23', 'chip_revision': '3.2', 'boot_nsh': True,
          'desktop_task_alive': True, 'display_video_running': True,
          'display_geometry': '1024x600 RGB565', 'physical_screen_visual_confirmation': 'not-observed',
          'wifi_associated': True, 'wifi_credentials_source': 'C6 stored configuration',
          'dhcp_result': 0, 'ipv4': ipv4, 'gateway': gateway, 'netmask': mask,
          'dns': re.findall(r'c6probe: DNS ([0-9.]+)', final['dns_from_dhcp']['output']),
          'interface_count': 1, 'interface': 'eth0', 'interface_mac': '10:bd:a3:89:58:74',
          'data_partition_erased': False, 'data_qpk_directory_present': True,
          'internet_session_tested': False, 'console_resumed_after_jtag': True,
          'serial_port_released': True}
(evidence / 'hardware-validation.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
