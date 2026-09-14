from pathlib import Path
import hashlib
root = Path(__file__).resolve().parent
digest = hashlib.sha256((root.parent / '04-v3-20260913/ui-layout-fix/nuttx.bin').read_bytes()).hexdigest()
s = (root / 'validate_storage_fix.py').read_text(encoding='utf-8')
s = s.replace('app-storage-fix', 'ui-layout-fix').replace('wifi-dhcp-fix', 'app-storage-fix')
s = s.replace("if path.is_file() and '__pycache__' not in path.parts:",
              "if path.is_file() and '__pycache__' not in path.parts and path.name not in ['desktop_main.c', 'qpk_runtime.c']:")
s = s.replace('config_identical_to_wifi_fix', 'config_identical_to_storage_fix')
s = s.replace('prior_fix_overlay_unchanged', 'prior_functional_fixes_preserved')
s = s.replace("workspace / 'diagnostics/storage-comparison' / ('current-' + path.name)",
              "workspace / 'diagnostics/ui-baseline' / path.name")
s = s.replace("delivery / 'storage.patch'", "delivery / 'ui-layout.patch'")
s = s.replace("needed = [", "needed = ['g_ui_probe_frame', 'g_ui_probe_frame_bytes', ")
s = s.replace("spec = importlib.util.spec_from_file_location", '''# UI edits must preserve the previously verified storage JS bindings.
import re
old_runtime = (base / 'overlay/apps/system/desktop/qpk_runtime.c').read_text(encoding='utf-8')
new_runtime = (delivery / 'overlay/apps/system/desktop/qpk_runtime.c').read_text(encoding='utf-8')
for function in ['qpk_storage_key', 'js_storage_get', 'js_storage_set', 'js_storage_delete']:
    pattern = r'(?ms)^static [^\\n]*\\b' + function + r'\\(.*?(?=^static |\\Z)'
    before = re.search(pattern, old_runtime)
    after = re.search(pattern, new_runtime)
    assert before and after and before.group() == after.group(), function
spec = importlib.util.spec_from_file_location''')
(root / 'validate_ui_fix.py').write_text(s, encoding='utf-8')
s = (root / 'flash_storage_fix.py').read_text(encoding='utf-8')
s = s.replace('app-storage-fix', 'ui-layout-fix').replace('wifi-dhcp-fix', 'app-storage-fix')
s = s.replace('2159a0adca9a7e76680233064d907aa3676b6d10da12b1e35dae3901f7b65932',
              digest)
s = s.replace('559e8f13dc4f1627048c6a5250383762d96262f3b892018382a1a68c60bca953',
              '2159a0adca9a7e76680233064d907aa3676b6d10da12b1e35dae3901f7b65932')
(root / 'flash_ui_fix.py').write_text(s, encoding='utf-8')
