"""Embed the public Mozilla trust bundle used by the ESPClaw HTTPS client."""
from pathlib import Path
import json


def prepare_chat(espclaw):
    source = espclaw / 'port/chat_root_ca.pem'
    text = source.read_text(encoding='ascii')
    assert text.count('BEGIN CERTIFICATE') > 50
    output = '/* Generated public CA material; see chat-root-ca.NOTICE. */\n'
    output += 'static const unsigned char chat_root_ca[] =\n'
    output += '\n'.join(json.dumps(line) for line in text.splitlines(keepends=True)) + ';\n'
    target = espclaw / 'port/chat_root_ca.inc'
    if not target.exists() or target.read_text(encoding='ascii') != output:
        target.write_text(output, encoding='ascii')


if __name__ == '__main__':
    prepare_chat(Path(__file__).resolve().parent.parent /
                 '04-v3-20260913/espdl-quickapp/overlay/apps/system/espclaw')
