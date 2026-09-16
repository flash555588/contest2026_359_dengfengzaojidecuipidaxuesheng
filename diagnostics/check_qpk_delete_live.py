"""Create an exclusive disposable app fixture; never modify existing apps."""
from pathlib import Path
import json, time, serial

ws = Path(__file__).resolve().parent.parent
output = ws / '04-v3-20260913/espdl-quickapp/evidence/qpk-delete-fixture-1.json'
assert not output.exists()
results = []
p = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=3)
p.dtr = p.rts = False
p.port = 'COM23'

def command(text):
    assert len(text.encode()) <= 78
    p.reset_input_buffer()
    for start in range(0, len(text.encode()) + 1, 8):
        p.write((text + '\r').encode()[start:start+8]); time.sleep(.03)
    data = bytearray(); began = time.monotonic(); last = began
    while time.monotonic() - began < 10:
        chunk = p.read(max(1, p.in_waiting))
        if chunk: data.extend(chunk); last = time.monotonic()
        if b'nsh> ' in data and time.monotonic() - last > .2: break
    reply = data.decode('utf-8', errors='replace').replace('\r', '')
    results.append(dict(command=text, output=reply))
    output.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
    return reply

with p:
    for path in ('/data/qpk/dtest', '/data/qpk/.data/dtest'):
        assert 'No such file' in command('ls ' + path), 'Fixture must not exist'
    for path in ('/data/qpk/dtest', '/data/qpk/.data/dtest'):
        assert 'failed:' not in command('mkdir ' + path)
    command("echo '{\"name\":\"DeleteTest\",' > /data/qpk/dtest/manifest.json")
    command("echo '\"package\":\"dtest\"}' >> /data/qpk/dtest/manifest.json")
    command("echo 'ui.text(\"DeleteTest\",20,20,280);' > /data/qpk/dtest/app.js")
    command('echo 42 > /data/qpk/.data/dtest/score.txt')
    reply = command('cat /data/qpk/dtest/manifest.json')
    assert json.loads(reply[reply.index('{'):reply.rindex('}')+1])['package'] == 'dtest'
    command('desktop ui apps')
    command('desktop ui click:下一页')
    command('desktop ui audit')
print(output)
