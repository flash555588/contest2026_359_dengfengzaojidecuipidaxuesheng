"""Read-only lifecycle observations from the owned Claude session."""
import hmac
import time
from protocol import ACTIVE, tail

EVENTS = ('UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'PostToolUseFailure',
          'Stop', 'StopFailure', 'Notification', 'SessionEnd')


def hook_settings(port, ident, key):
    def hook(event, timeout):
        return {'type':'http', 'url':f'http://127.0.0.1:{port}/internal/{event}/{ident}',
                'headers':{'Authorization':'Bearer '+key}, 'timeout':timeout}
    hooks = {event:[{'hooks':[hook('observe', 2)]}] for event in EVENTS}
    hooks['PermissionRequest'] = [{'matcher':'.*','hooks':[hook('permission', 300)]}]
    return {'hooks':hooks}


class SessionMonitor:
    def observe(self, ident, key, data):
        with self.lock:
            expected = self.hook_keys.get(ident)
            if not expected or not hmac.compare_digest(key, expected): raise PermissionError()
            job = self.jobs[ident]
            if data.get('session_id') != job['session_id']: raise PermissionError('会话不匹配')
            event = data.get('hook_event_name')
            if event not in EVENTS: raise ValueError('监听事件无效')
            # Child agents cannot mark the main conversation complete or failed.
            if data.get('agent_id') or job['state'] not in ACTIVE or job['state']=='stopping': return {}
            now = time.time()
            job['observed_at'] = now
            if event == 'UserPromptSubmit':
                job.update(activity='thinking', detail='正在处理本轮任务', turn_started=now)
                job.pop('result_state', None)
                job.pop('last_result', None)
            elif event in ('PreToolUse','PostToolUse','PostToolUseFailure'):
                tool = tail(self.safe(data.get('tool_name','工具')), 80)
                if event == 'PreToolUse':
                    job.update(activity='tool', detail='正在执行 '+tool)
                    self.note(job, '调用工具：'+tool)
                else:
                    job.update(activity='thinking', detail='工具已返回，继续处理' if event=='PostToolUse' else '工具执行失败，等待 Claude 处理')
                    self.note(job, ('工具已返回：' if event=='PostToolUse' else '工具失败：')+tool)
            elif event == 'Stop':
                background = bool(data.get('background_tasks') or data.get('session_crons'))
                job.update(activity='background' if background else 'idle',
                           detail='本轮回复已完成，仍有后台工作' if background else '本轮完成；终端保留，可继续输入',
                           result_state='done', turn_finished=now)
                message = data.get('last_assistant_message')
                if isinstance(message,str) and message.strip():
                    job['last_result']=tail(self.safe(message),4096);job['text']=job['last_result']
                self.note(job, job['detail'])
            elif event == 'StopFailure':
                kind = str(data.get('error','unknown'))
                reason = {'model_not_found':'模型不存在或无权访问','authentication_failed':'模型认证失败',
                          'rate_limit':'模型服务限流','billing_error':'模型账户计费异常',
                          'invalid_request':'模型请求被拒绝','server_error':'模型服务错误',
                          'overloaded':'模型服务繁忙'}.get(kind,'模型本轮执行失败')
                job.update(activity='error', detail=reason+'；可在电脑处理后继续输入',
                           result_state='failed', turn_finished=now)
                message=data.get('last_assistant_message') or data.get('error_details') or reason
                job['text']=tail(self.safe(message),4096)
                job['last_result']=job['text']
                self.note(job, reason)
            elif event == 'Notification' and data.get('notification_type')=='idle_prompt':
                if job.get('activity') not in ('idle','error'):
                    job.update(activity='idle',detail='Claude 正在等待输入')
            elif event == 'SessionEnd':
                job['session_end_reason']=tail(self.safe(data.get('reason','')),120)
            # Observations never return an allow/block decision or execute tools.
            self.save()
            return {}
