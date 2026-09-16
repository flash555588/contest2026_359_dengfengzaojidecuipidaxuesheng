from pathlib import Path
import subprocess
ws=Path(__file__).resolve().parent.parent
src=ws/'04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop'
out=ws/'04-v3-20260913/espdl-quickapp/evidence'
cjson=Path('/tmp/v3-desktop-espdl-20260915/apps/netutils/cjson/cJSON')
subprocess.run(['cc','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',
  '-I'+str(src),'-I'+str(cjson),str(ws/'diagnostics/music-tests/test_music.c'),
  str(src/'glass_music_parse.c'),str(cjson/'cJSON.c'),'-lm','-o','/tmp/test-music'],check=True)
result=subprocess.check_output(['/tmp/test-music',str(out/'music-sample.mp3'),'/tmp/music-decoded.pcm'],text=True)
(out/'music-host-tests.log').write_text(result)
print(result)
