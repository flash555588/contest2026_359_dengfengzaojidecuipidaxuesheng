"""Native Windows console handles for a real interactive CLI, with bounded ESP mirroring."""
import ctypes
from ctypes import wintypes as w
import json
import msvcrt
import os
import subprocess
import sys
import threading
import time
import re
from protocol import text as valid_text, KEYS


class Coord(ctypes.Structure):
    _fields_ = [('x', w.SHORT), ('y', w.SHORT)]


class Rect(ctypes.Structure):
    _fields_ = [('left', w.SHORT), ('top', w.SHORT), ('right', w.SHORT), ('bottom', w.SHORT)]


class BufferInfo(ctypes.Structure):
    _fields_ = [('size', Coord), ('cursor', Coord), ('attributes', w.WORD), ('window', Rect), ('maximum', Coord)]


class KeyEvent(ctypes.Structure):
    _fields_ = [('down', w.BOOL), ('repeat', w.WORD), ('key', w.WORD), ('scan', w.WORD), ('char', w.WCHAR), ('control', w.DWORD)]


class EventData(ctypes.Union):
    _fields_ = [('key', KeyEvent), ('padding', ctypes.c_byte * 16)]


class InputRecord(ctypes.Structure):
    _fields_ = [('type', w.WORD), ('event', EventData)]


class Console:
    def __init__(self):
        self.kernel = k = ctypes.WinDLL('kernel32', use_last_error=True)
        k.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, ctypes.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
        k.CreateFileW.restype = w.HANDLE
        k.CloseHandle.argtypes = [w.HANDLE]
        k.GetConsoleScreenBufferInfo.argtypes = [w.HANDLE, ctypes.POINTER(BufferInfo)]
        k.ReadConsoleOutputCharacterW.argtypes = [w.HANDLE, w.LPWSTR, w.DWORD, Coord, ctypes.POINTER(w.DWORD)]
        k.WriteConsoleInputW.argtypes = [w.HANDLE, ctypes.POINTER(InputRecord), w.DWORD, ctypes.POINTER(w.DWORD)]
        k.GetConsoleMode.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
        k.SetConsoleMode.argtypes = [w.HANDLE, w.DWORD]
        self.input = self.open('CONIN$')

    def open(self, name):
        handle = self.kernel.CreateFileW(name, 0xc0000000, 3, None, 3, 0, None)
        if handle == ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())
        return handle

    def snapshot(self):
        handle = self.open('CONOUT$')
        try:
            info = BufferInfo()
            if not self.kernel.GetConsoleScreenBufferInfo(handle, ctypes.byref(info)):
                raise ctypes.WinError(ctypes.get_last_error())
            width = max(1, min(info.size.x, 512))
            height = min(info.window.bottom - info.window.top + 1, 80, 20000 // width)
            lines = []
            # Follow current output without changing the user's scroll position.
            top=max(0,info.cursor.y-height+1)
            for row in range(top, min(top+height,info.size.y)):
                text = ctypes.create_unicode_buffer(width + 1); read = w.DWORD()
                if self.kernel.ReadConsoleOutputCharacterW(handle, text, width, Coord(0, row), ctypes.byref(read)):
                    lines.append(text[:read.value].rstrip())
            value = re.sub(r'\n{3,}', '\n\n', '\n'.join(lines).strip()).encode('utf-8', errors='replace')
            return value[-4096:].decode('utf-8', errors='ignore')
        finally: self.kernel.CloseHandle(handle)

    def send_text(self, text):
        valid_text(text)
        # Multiline messages are bracketed paste, followed by a separate Enter.
        if '\n' in text: text = '\x1b[200~' + text + '\x1b[201~'
        data = (text + '\r').encode('utf-16-le')
        units = [int.from_bytes(data[i:i+2], 'little') for i in range(0, len(data), 2)]
        events = (InputRecord * (2 * len(units)))()
        for i, unit in enumerate(units):
            for offset, down in enumerate((True, False)):
                event = events[2*i + offset]; event.type = 1
                event.event.key = KeyEvent(down, 1, 13 if unit == 13 else 0, 0, chr(unit), 0)
        self.send_events(events)

    def send_key(self, key):
        if key not in KEYS: raise ValueError('Unsupported console key')
        code = {'enter':13,'escape':27,'up':38,'down':40,'tab':9,'backspace':8}[key]
        char = chr(code) if key in ('enter','escape','tab','backspace') else '\0'
        events = (InputRecord * 2)()
        for i, down in enumerate((True,False)):
            events[i].type=1; events[i].event.key=KeyEvent(down,1,code,0,char,0)
        self.send_events(events)

    def send_events(self, events):
        offset = 0
        while offset < len(events):
            written = w.DWORD()
            pointer = ctypes.cast(ctypes.byref(events, offset * ctypes.sizeof(InputRecord)), ctypes.POINTER(InputRecord))
            if not self.kernel.WriteConsoleInputW(self.input, pointer, len(events)-offset, ctypes.byref(written)) or not written.value:
                raise ctypes.WinError(ctypes.get_last_error())
            offset += written.value

    def close(self): self.kernel.CloseHandle(self.input)


def run(command):
    console = Console()
    # Pass actual console handles, not pipes: the CLI sees all three streams as
    # TTYs and renders its original interface, colors, menus and prompt editor.
    stdin = open('CONIN$', 'rb', buffering=0)
    stdout = open('CONOUT$', 'wb', buffering=0)
    mode = w.DWORD()
    handle = msvcrt.get_osfhandle(stdout.fileno())
    if console.kernel.GetConsoleMode(handle, ctypes.byref(mode)):
        console.kernel.SetConsoleMode(handle, mode.value | 4)
    process = subprocess.Popen(command, stdin=stdin, stdout=stdout, stderr=stdout)
    emit_lock=threading.Lock()
    def emit(event):
        with emit_lock:
            sys.stdout.write(json.dumps(event, ensure_ascii=True) + '\n'); sys.stdout.flush()
    def inputs():
        pending = bytearray()
        while process.poll() is None:
            data = os.read(sys.stdin.fileno(), 4096)
            if not data: break
            pending.extend(data)
            if len(pending) > 16384: pending.clear(); continue
            while b'\n' in pending:
                line, _, rest = pending.partition(b'\n'); pending[:] = rest
                packet = None
                try:
                    packet = json.loads(line)
                    if 'key' in packet and 'text' not in packet: console.send_key(packet['key'])
                    else: console.send_text(packet.get('text'))
                    emit({'type':'console_input_ack','request_id':packet.get('request_id'),'ok':True})
                except (ValueError,OSError,AttributeError,TypeError):
                    if isinstance(packet,dict):emit({'type':'console_input_ack','request_id':packet.get('request_id'),'ok':False})
    threading.Thread(target=inputs, daemon=True).start()
    previous = None
    heartbeat = 0
    try:
        emit({'type':'console_started', 'pid':process.pid, 'interactive':True})
        while True:
            healthy=True
            try: screen = console.snapshot()
            except OSError: screen='';healthy=False
            if healthy and screen != previous:
                emit({'type':'console_screen', 'text':screen}); previous = screen
            if time.monotonic()-heartbeat>=3:
                emit({'type':'console_heartbeat','ok':healthy});heartbeat=time.monotonic()
            code = process.poll()
            if code is not None:
                emit({'type':'console_exit','code':code}); return code
            time.sleep(.5)
    finally:
        if process.poll() is None: process.kill()
        process.wait(); stdin.close(); stdout.close(); console.close()
