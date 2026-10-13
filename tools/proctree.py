"""Start a process inside a Windows job object so its whole tree can be killed and reaped.

``taskkill /T`` walks parent links once; a descendant spawned after that snapshot, or one
whose parent already exited, survives and keeps files open. A job object collects every
descendant atomically (children inherit the job), ``TerminateJobObject`` kills them all,
and the job's active-process count says when the last one is really gone.
"""
from __future__ import annotations

import subprocess
import sys
import time

CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)

if sys.platform == "win32":
    import ctypes
    from ctypes import wintypes

    _k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _k32.CreateJobObjectW.restype = wintypes.HANDLE
    _k32.CreateJobObjectW.argtypes = [wintypes.LPVOID, wintypes.LPCWSTR]
    _k32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD]
    _k32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    _k32.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
    _k32.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD,
                                               wintypes.LPVOID]
    _k32.CloseHandle.argtypes = [wintypes.HANDLE]

    class _BasicLimit(ctypes.Structure):
        _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                    ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                    ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                    ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD),
                    ("SchedulingClass", wintypes.DWORD)]

    class _IoCounters(ctypes.Structure):
        _fields_ = [(n, ctypes.c_uint64) for n in ("a", "b", "c", "d", "e", "f")]

    class _ExtendedLimit(ctypes.Structure):
        _fields_ = [("BasicLimitInformation", _BasicLimit), ("IoInfo", _IoCounters),
                    ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                    ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]

    class _Accounting(ctypes.Structure):
        _fields_ = [("TotalUserTime", ctypes.c_int64), ("TotalKernelTime", ctypes.c_int64),
                    ("ThisPeriodTotalUserTime", ctypes.c_int64), ("ThisPeriodTotalKernelTime", ctypes.c_int64),
                    ("TotalPageFaultCount", wintypes.DWORD), ("TotalProcesses", wintypes.DWORD),
                    ("ActiveProcesses", wintypes.DWORD), ("TotalTerminatedProcesses", wintypes.DWORD)]


class Tree:
    """A child process and all its descendants."""

    def __init__(self, argv, **kwargs):
        self.job = None
        if sys.platform == "win32":
            kwargs["creationflags"] = kwargs.get("creationflags", 0) | CREATE_NO_WINDOW
            job = _k32.CreateJobObjectW(None, None)
            if job:
                info = _ExtendedLimit()
                info.BasicLimitInformation.LimitFlags = 0x2000  # KILL_ON_JOB_CLOSE: nothing outlives us
                if _k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
                    self.job = job
                else:
                    _k32.CloseHandle(job)
        self.proc = subprocess.Popen(argv, **kwargs)
        if self.job is not None and not _k32.AssignProcessToJobObject(self.job, int(self.proc._handle)):
            # Cannot join (already in a non-nestable job): fall back to taskkill below.
            _k32.CloseHandle(self.job)
            self.job = None

    def active(self) -> int:
        if self.job is None:
            return 0
        info = _Accounting()
        if not _k32.QueryInformationJobObject(self.job, 1, ctypes.byref(info), ctypes.sizeof(info), None):
            return 0
        return info.ActiveProcesses

    def kill(self, timeout=30.0):
        """Kill every process in the tree and wait until all of them have exited."""
        if self.job is not None:
            _k32.TerminateJobObject(self.job, 1)
        elif self.proc.poll() is None:
            if sys.platform == "win32":
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(self.proc.pid)], capture_output=True,
                               creationflags=CREATE_NO_WINDOW)
            else:
                self.proc.kill()
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        deadline = time.monotonic() + timeout
        while self.active() and time.monotonic() < deadline:
            time.sleep(0.02)

    def close(self):
        """Release the job handle; unfinished descendants (there should be none) die with it."""
        if self.job is not None:
            _k32.CloseHandle(self.job)
            self.job = None
