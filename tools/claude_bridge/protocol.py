"""Shared wire limits, validation and session lifecycle rules."""
import json
import re

VERSION = 2
LIMIT = 16384
TEXT_LIMIT = 4096
PROMPT_LIMIT = 3072
ACTIVE = frozenset(('starting', 'running', 'waiting', 'stopping'))
KEYS = frozenset(('enter', 'escape', 'up', 'down', 'tab', 'backspace'))
ID = re.compile(r'[A-Za-z0-9_-]{8,80}\Z')


class AlreadyAccepted(ValueError):
    """A durable receipt exists, so this request must never be executed again."""
    pass


def tail(value, size):
    return str(value).encode('utf-8', errors='replace')[-size:].decode('utf-8', errors='ignore')


def text(value, label='输入', maximum=PROMPT_LIMIT):
    if not isinstance(value, str) or not value.strip() or len(value.encode('utf-8')) > maximum:
        raise ValueError(f'{label}应为 1–{maximum} 字节')
    if any((ord(c) < 32 and c not in '\n\t') or 127 <= ord(c) <= 159 for c in value):
        raise ValueError(label + '包含终端控制字符')
    return value


def request_id(value):
    if not isinstance(value, str) or not ID.fullmatch(value): raise ValueError('提交编号无效')
    return value


def project_value(value):
    if not isinstance(value,str) or not value or len(value)>512 or any(ord(c)<32 for c in value):
        raise ValueError('请输入有效的本机项目目录')
    if value.startswith(('\\\\','//')): raise ValueError('不允许网络或设备项目路径')
    return value


def loads(body):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result: raise ValueError('JSON 字段重复')
            result[key] = value
        return result
    def invalid(_): raise ValueError('JSON 数字无效')
    value = json.loads(body, object_pairs_hook=pairs, parse_constant=invalid)
    def depth(item, level=0):
        if level > 16: raise ValueError('JSON 层级过深')
        if isinstance(item, dict):
            for child in item.values(): depth(child, level+1)
        elif isinstance(item, list):
            for child in item: depth(child, level+1)
    depth(value)
    return value


def redact(value, secrets=()):
    value = str(value)
    for secret in secrets:
        if secret and len(secret) >= 16: value = value.replace(secret, '[已隐藏凭据]')
    value = re.sub(r'(?i)(Bearer\s+)[A-Za-z0-9._~+/-]{16,}', r'\1[已隐藏凭据]', value)
    value = re.sub(r'(?i)((?:api[_-]?key|auth[_-]?token|access[_-]?token|password)["\s]*[:=]["\s]*)[^\s",}]{8,}', r'\1[已隐藏凭据]', value)
    value = re.sub(r'\bsk-[A-Za-z0-9_-]{16,}', '[已隐藏凭据]', value)
    return value


def finish(job, code, protocol):
    if job['state'] == 'stopping': job.update(state='stopped', detail='任务已停止')
    elif job.get('interactive') and code & 0xffffffff == 0xc000013a:
        job.update(state='stopped', detail='电脑终端已关闭')
    elif code == 0 and (job.get('result_state') == 'done' or protocol == 'text'):
        job.update(state='done', detail='终端已退出' if job.get('interactive') else '本轮完成')
    else: job.update(state='failed', detail=f'CLI 退出，代码 {code}' if not job.get('result_state') else '任务失败')
    if job.get('interactive') and job.get('last_result'):job['text']=job['last_result']
