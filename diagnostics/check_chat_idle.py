"""Check for a pending request or unsent editor contents before firmware reset."""
from pathlib import Path
import json, re, subprocess, sys
ws = Path(__file__).resolve().parent.parent
path = ws / 'diagnostics/private/chat-stream-preflight.json'
path.parent.mkdir(exist_ok=True)
subprocess.run([sys.executable, str(ws/'diagnostics/espdl_console.py'), '--commands',
    'desktop ui chat-status', 'desktop ui audit', '--seconds', '3', '--output', str(path)],
    check=True, capture_output=True)
entries = json.loads(path.read_text(encoding='utf-8'))
status, audit = [e['output'] for e in entries]
assert 'phase=1 ' in status, 'Chat is active; do not reset'
match = re.search(r'UI_LAYOUT_BEGIN\s*(.*?)\s*UI_LAYOUT_END', audit, re.S)
assert match, 'Missing UI audit'
nodes = json.loads(match.group(1))
texts = [n.get('text', '') for n in nodes]
if 'editor=1' in status:
    assert '0 / 512' in texts, 'Unsent editor text; preserve before reset'
else:
    assert '聊点什么…' in texts, 'Unsent draft or different page; inspect before reset'
print('PASS: chat idle and input empty; private audit saved')
