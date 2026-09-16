"""Embed ESP-DL's public 320x240 example image for deterministic board tests."""
from pathlib import Path
import hashlib
ws=Path(__file__).resolve().parent.parent
source=ws/'diagnostics/espdl-reference/esp-dl/examples/human_face_detect/main/human_face.jpg'
data=source.read_bytes()
target=ws/'04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop/espdl_port/face_fixture.h'
target.write_text('/* ESP-DL v3.2.0 human_face_detect example JPEG; SHA256 '+hashlib.sha256(data).hexdigest()+' */\n'
                  'static const unsigned char face_fixture[] = {\n'+
                  '\n'.join(','.join(str(v) for v in data[i:i+24])+',' for i in range(0,len(data),24))+'\n};\n')
print('Fixture bytes:',len(data))
