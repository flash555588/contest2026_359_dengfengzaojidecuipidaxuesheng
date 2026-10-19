#!/usr/bin/env python3
"""Check exact production phrase buffering with inert synthesis/playback."""
import argparse,re,subprocess
from pathlib import Path
from render_all_ui import function
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
a.output.mkdir(parents=True,exist_ok=True)
text=(desktop/'glass_speech.c').read_text()
frames=re.search(r'^#define MUSIC_PCM_FRAMES \d+$',(desktop/'glass_music_pcm.h').read_text(),re.M)[0]
(a.output/'speech_buffer_config.inc').write_text(frames+'\n')
struct=text[text.index('struct speech_buffer {'):text.index('static int speech_buffer_write')]
(a.output/'speech_buffer_production.inc').write_text(struct+function(text,'speech_buffer_write')+'\n'+function(text,'speech_esp_play')+'\n'+function(text,'speech_sano_play'))
executable=a.output/'speech-buffer-host'
subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
    '-I'+str(a.output),str(repo/'app/espdl-quickapp/tests/speech_buffer_host.c'),'-o',str(executable)],check=True)
subprocess.run([str(executable)],check=True,timeout=30)
