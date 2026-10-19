"""Run one CLI in its own Windows console while relaying its protocol to ESP."""
import argparse
import ctypes
import json
import os
import subprocess
import sys
import threading


def show_event(event, write):
    kind = event.get('type')
    if kind == 'stream_event':
        value = event.get('event', {})
        delta = value.get('delta', {})
        if delta.get('type') == 'text_delta': write(str(delta.get('text', '')))
        block = value.get('content_block', {})
        if block.get('type') == 'tool_use': write('\n[工具] ' + str(block.get('name', '')) + '\n')
    elif kind == 'system' and event.get('subtype') == 'api_retry':
        write('\n[重试] ' + str(event.get('error', '')) + ' / ' + str(event.get('attempt', '')) + '\n')
    elif kind == 'user':
        for block in event.get('message', {}).get('content', []):
            if not isinstance(block, dict) or block.get('type') != 'tool_result': continue
            content = block.get('content', '')
            if isinstance(content, list):
                content = '\n'.join(str(part.get('text', '')) for part in content if isinstance(part, dict))
            write('\n[工具失败]\n' if block.get('is_error') else '\n[工具输出]\n')
            write(str(content) + '\n')
    elif kind in ('item.started', 'item.updated', 'item.completed'):
        item = event.get('item', {})
        if item.get('type') == 'agent_message': write(str(item.get('text', '')) + '\n')
        else:
            write('\n[' + str(item.get('type', '过程')) + '] ' + str(item.get('command', '')) + '\n')
            if item.get('aggregated_output'): write(str(item['aggregated_output']) + '\n')
    elif kind == 'result':
        write('\n[结果] ' + str(event.get('result', '')) + '\n')
        for error in event.get('errors', []): write(str(error) + '\n')
    elif kind in ('error', 'turn.failed'):
        write('\n[错误] ' + str(event.get('error', event.get('message', ''))) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--protocol', choices=('claude', 'codex', 'text', 'interactive'), required=True)
    parser.add_argument('--title', required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command: parser.error('Missing CLI executable')
    console = None
    if os.name == 'nt':
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.SetConsoleTitleW.argtypes = [ctypes.c_wchar_p]
        kernel.SetConsoleTitleW(args.title)
        kernel.SetConsoleOutputCP(65001)
        try: console = open('CONOUT$', 'w', encoding='utf-8', errors='replace', buffering=1)
        except OSError: pass
    lock = threading.Lock()
    def write(text):
        if console:
            with lock:
                try: console.write(text); console.flush()
                except OSError: pass
    if args.protocol != 'interactive':
        write(args.title + '\n实际程序：' + command[0] + '\n项目：' + os.getcwd() + '\n从 ESP 提交任务；需要操作确认时请在 ESP 上处理。\n\n')
    # The bridge assigns this worker to its Windows Job Object before releasing
    # this gate, so even a quickly spawned CLI belongs to the owned process tree.
    if os.read(sys.stdin.fileno(), 1) != b'\0': return 2
    if args.protocol == 'interactive':
        from native_console import run
        try: return run(command)
        finally:
            if console: console.close()
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    def input_pipe():
        try:
            while True:
                data = os.read(sys.stdin.fileno(), 4096)
                if not data: break
                write(data.decode('utf-8', errors='replace'))
                process.stdin.write(data); process.stdin.flush()
        except (OSError, ValueError): pass
        finally:
            try: process.stdin.close()
            except OSError: pass
    def errors():
        while True:
            data = process.stderr.read1(8192)
            if not data: break
            sys.stderr.buffer.write(data); sys.stderr.buffer.flush()
            write(data.decode('utf-8', errors='replace'))
    threading.Thread(target=input_pipe, daemon=True).start()
    error_thread = threading.Thread(target=errors, daemon=True); error_thread.start()
    try:
        while True:
            data = process.stdout.read1(4096) if args.protocol == 'text' else process.stdout.readline(1024 * 1024 + 1)
            if not data: break
            sys.stdout.buffer.write(data); sys.stdout.buffer.flush()
            if args.protocol == 'text': write(data.decode('utf-8', errors='replace'))
            else:
                if len(data) > 1024 * 1024: process.kill(); return 1
                try: event = json.loads(data)
                except (ValueError, UnicodeError): continue
                if isinstance(event, dict): show_event(event, write)
        code = process.wait()
        error_thread.join(timeout=2)
        write('\n[CLI 已退出，代码 ' + str(code) + ']\n完整结果仍可在 ESP 中查看。\n')
        return code
    except (KeyboardInterrupt, BrokenPipeError):
        process.terminate()
        return 130
    finally:
        if process.poll() is None: process.kill()
        process.wait()
        process.stdout.close(); process.stderr.close()
        if console: console.close()


if __name__ == '__main__': sys.exit(main())
