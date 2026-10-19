"""Local CLI registry. Commands are argv lists; prompts are never shell code."""
import json
import os
from pathlib import Path
import shutil


def discover(claude, state):
    result = {'claude': {'name': 'Claude Code', 'command': [claude], 'protocol': 'claude', 'resume': True}}
    codex = shutil.which('codex.exe')
    if os.name == 'nt' and not codex:
        base = Path(os.environ.get('APPDATA', '')) / 'npm/node_modules/@openai/codex/node_modules/@openai'
        codex = next((str(p) for p in base.glob('codex-win*/vendor/*/bin/codex.exe')), None)
    if os.name != 'nt': codex = codex or shutil.which('codex')
    if codex: result['codex'] = {'name': 'Codex', 'command': [codex], 'protocol': 'codex', 'resume': True}
    config = state / 'clis.json'
    if config.exists():
        entries = json.loads(config.read_text(encoding='utf-8'))
        if not isinstance(entries, dict) or len(entries) > 16: raise ValueError('clis.json must contain at most 16 CLI entries')
        for key, value in entries.items():
            if not key or len(key) > 32 or not all(c.isalnum() or c in '-_' for c in key): raise ValueError('Invalid CLI id')
            if key in ('claude','codex'): raise ValueError('Builtin CLI identities cannot be overridden')
            if not isinstance(value,dict): raise ValueError('CLI entry must be an object')
            command = value.get('command')
            if not isinstance(command, list) or not command or any(not isinstance(a, str) or '\x00' in a for a in command): raise ValueError('CLI command must be an argv list')
            if len(command)>64 or sum(len(a.encode()) for a in command)>8192: raise ValueError('CLI command is too large')
            executable = shutil.which(command[0]) or command[0]
            if executable.startswith(('\\\\','//')): raise ValueError('CLI executable must be local')
            if not Path(executable).is_file() or Path(executable).suffix.lower() in ('.cmd', '.bat', '.ps1'): raise ValueError('Use a native executable, or explicit interpreter plus script')
            if value.get('protocol', 'text') != 'text': raise ValueError('Custom CLIs use text protocol')
            result[key] = {'name': ''.join(c for c in str(value.get('name',key)) if ord(c)>=32)[:40], 'command': [str(Path(executable).resolve()), *command[1:]], 'protocol': 'text', 'resume': False, 'input': bool(value.get('input', False))}
    return result


def codex_command(cli, job):
    command = cli['command'] + ['exec', '--json', '--skip-git-repo-check', '-c', 'approval_policy="never"', '-c', 'sandbox_mode="workspace-write"']
    if job['resume']: command += ['resume', job['session_id']]
    return command + ['-']


def public(registry):
    return [dict(id=key, name=value['name'], resume=value.get('resume', False), input=value.get('input', False)) for key, value in registry.items()]
