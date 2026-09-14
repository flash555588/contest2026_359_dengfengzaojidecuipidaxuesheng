"""Host validation of real voice packet code with sanitizers and independent parsers."""
from pathlib import Path
import email.policy
from email.parser import BytesParser
import os
import subprocess
import wave

ws = Path(__file__).resolve().parent.parent
root = Path('/tmp/v3-desktop-voice-20260915')
source = ws / '04-v3-20260913/espclaw-voice/overlay/apps/system/espclaw'
out = Path('/tmp/espclaw-voice-tests')
out.mkdir(exist_ok=True)
cjson = root / 'apps/netutils/cjson/cJSON'
command = ['cc', '-D_GNU_SOURCE', '-std=c11', '-g', '-fsanitize=address,undefined',
  '-fno-omit-frame-pointer', '-I'+str(source/'include'), '-I'+str(cjson),
  str(ws/'diagnostics/voice-tests/test_voice_format.c'), str(source/'port/voice_format.c'),
  str(cjson/'cJSON.c'), '-lm', '-o', str(out/'test-format')]
subprocess.run(command, check=True)
result = subprocess.check_output([str(out/'test-format')],cwd=out,text=True)
with wave.open(str(out/'voice-test.wav'),'rb') as wav:
    assert (wav.getnchannels(),wav.getsampwidth(),wav.getframerate(),wav.getnframes())==(1,2,16000,240000)
boundary='espclaw-voice-89bcd571a2e64aa79de2442537a156cd'
message=BytesParser(policy=email.policy.default).parsebytes(
    f'Content-Type: multipart/form-data; boundary={boundary}\r\nMIME-Version: 1.0\r\n\r\n'.encode()+
    (out/'voice-test.multipart').read_bytes())
parts={part.get_param('name',header='content-disposition'):part for part in message.iter_parts()}
assert parts['file'].get_payload(decode=True)==(out/'voice-test.wav').read_bytes()
assert parts['model'].get_payload(decode=True)==b'test-transcriber'
assert parts['response_format'].get_payload(decode=True)==b'json'
result+='PASS: Python WAV reader and MIME parser verified actual binary upload\n'
stub = ws/'diagnostics/voice-tests/stubs'
subprocess.run(['cc','-D_GNU_SOURCE','-std=c11','-g','-fsanitize=address,undefined',
  '-fno-omit-frame-pointer','-I'+str(stub),'-I'+str(source/'include'),
  '-I'+str(root/'apps/system/espclaw/include'),'-I'+str(cjson),
  '-DCONFIG_FILE="voice-test.json"','-DRECORD_FILE="voice-test-record.wav"',
  str(ws/'diagnostics/voice-tests/test_voice_service.c'),str(source/'port/voice_service.c'),
  str(source/'port/voice_format.c'),str(cjson/'cJSON.c'),'-pthread','-lm','-o',str(out/'test-service')],check=True)
result+=subprocess.check_output([str(out/'test-service')],cwd=out,text=True)
(ws/'04-v3-20260913/espclaw-voice/evidence/host-test.log').write_text(result)
print(result)
