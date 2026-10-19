"""Own exactly one task and its descendants."""
import ctypes
import os
import signal

class ProcessTree:
    """Own only this task's child tree; closing a Windows job kills its children."""
    def __init__(self, process):
        self.process, self.handle = process, None
        if os.name == 'nt':
            from ctypes import wintypes as w
            class BASIC(ctypes.Structure):
                _fields_ = [('process_time', ctypes.c_longlong), ('job_time', ctypes.c_longlong),
                            ('flags', w.DWORD), ('min_ws', ctypes.c_size_t), ('max_ws', ctypes.c_size_t),
                            ('active', w.DWORD), ('affinity', ctypes.c_size_t), ('priority', w.DWORD), ('scheduling', w.DWORD)]
            class IO(ctypes.Structure):
                _fields_ = [(name, ctypes.c_ulonglong) for name in ('read_ops','write_ops','other_ops','read_bytes','write_bytes','other_bytes')]
            class EXTENDED(ctypes.Structure):
                _fields_ = [('basic', BASIC), ('io', IO), ('process_memory', ctypes.c_size_t),
                            ('job_memory', ctypes.c_size_t), ('peak_process', ctypes.c_size_t), ('peak_job', ctypes.c_size_t)]
            k = ctypes.WinDLL('kernel32', use_last_error=True)
            k.CreateJobObjectW.argtypes = [ctypes.c_void_p, w.LPCWSTR]; k.CreateJobObjectW.restype = w.HANDLE
            k.SetInformationJobObject.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD]
            k.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
            k.TerminateJobObject.argtypes = [w.HANDLE, w.UINT]
            k.CloseHandle.argtypes = [w.HANDLE]
            handle = k.CreateJobObjectW(None, None)
            info = EXTENDED(); info.basic.flags = 0x2000  # KILL_ON_JOB_CLOSE
            if not handle or not k.SetInformationJobObject(handle, 9, ctypes.byref(info), ctypes.sizeof(info)) or not k.AssignProcessToJobObject(handle, int(process._handle)):
                if handle: k.CloseHandle(handle)
                process.kill(); raise OSError('Unable to own Claude process tree')
            self.kernel, self.handle = k, handle

    def stop(self):
        if self.handle:
            self.kernel.TerminateJobObject(self.handle, 143)
        else:
            try: os.killpg(self.process.pid, signal.SIGTERM)
            except ProcessLookupError: pass

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle); self.handle = None
