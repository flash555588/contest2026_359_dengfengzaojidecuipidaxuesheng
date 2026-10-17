#!/usr/bin/env python3
"""Regenerate the built-in camera script resource and quickapp mirror."""
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
desktop = root / 'app/espdl-quickapp/overlay/apps/system/desktop'
source = (desktop / 'camera/app.js').read_text(encoding='utf-8')
(root / 'quickapp/camera/app.js').write_text(source, encoding='utf-8', newline='\n')
resource = '/* Generated from camera/app.js. SPDX-License-Identifier: Apache-2.0 */\n'
resource += 'static const char g_camera_app_js[] =\n'
resource += '\n'.join(json.dumps(line, ensure_ascii=False) for line in source.splitlines(keepends=True))
resource += ';\nconst char *camera_get_app_js(unsigned int *len) { *len=sizeof(g_camera_app_js)-1; return g_camera_app_js; }\n'
(desktop / 'camera_resource.c').write_text(resource, encoding='utf-8', newline='\n')
print('CAMERA_RESOURCE_OK', len(source.encode('utf-8')), 'bytes')
