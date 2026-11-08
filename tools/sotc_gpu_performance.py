#!/usr/bin/env python3
"""Opt-in AMDGPU peak-clock lease for one command; no tracing or sysfs writes.

Example: python tools/sotc_gpu_performance.py --device /dev/dri/renderD128 -- python sotc.py
The kernel restores the previous power state when the owning DRM context is freed,
including when this process dies. This changes clocks for the whole selected GPU
while the command runs. It does not change voltage, overclock or power limits.
"""
import argparse
import ctypes
import os
from pathlib import Path
import signal
import subprocess
import sys


def read_gpu_policy(device):
    node = Path(device).resolve(strict=True).name
    if not node.startswith('renderD') or not node[7:].isdigit():
        raise RuntimeError('select an AMDGPU render node, e.g. /dev/dri/renderD128')
    return (Path('/sys/class/drm') / node / 'device/power_dpm_force_performance_level').read_text().strip()


class PeakClockLease:
    """Own the libdrm device/context until the workload exits. Fail closed."""

    def __init__(self, device, *, library=None):
        self.path = device
        self.lib = library
        self.fd = None
        self.device = ctypes.c_void_p()
        self.context = ctypes.c_void_p()

    @staticmethod
    def check(result, operation):
        if result:
            raise OSError(abs(result), f'{operation}: {os.strerror(abs(result))}')

    def __enter__(self):
        try:
            # GET_STABLE_PSTATE maps high/low/manual policies to NONE, just like auto.
            # Require auto explicitly so releasing this lease cannot discard a user's
            # manually selected policy on kernels that only save stable pstates.
            policy = read_gpu_policy(self.path)
            if policy != 'auto':
                raise RuntimeError(f'GPU policy is {policy}, not auto; leaving it alone')
            if self.lib is None:
                self.lib = ctypes.CDLL('libdrm_amdgpu.so.1')
            ptr, u32 = ctypes.c_void_p, ctypes.c_uint32
            signatures = {
                'amdgpu_device_initialize': [ctypes.c_int, ctypes.POINTER(u32),
                                             ctypes.POINTER(u32), ctypes.POINTER(ptr)],
                'amdgpu_cs_ctx_create': [ptr, ctypes.POINTER(ptr)],
                'amdgpu_cs_ctx_stable_pstate': [ptr, u32, u32, ctypes.POINTER(u32)],
                'amdgpu_cs_ctx_free': [ptr],
                'amdgpu_device_deinitialize': [ptr],
            }
            for name, args in signatures.items():
                fn = getattr(self.lib, name)
                fn.argtypes, fn.restype = args, ctypes.c_int
            self.fd = os.open(self.path, os.O_RDWR | os.O_CLOEXEC)
            major, minor, state = u32(), u32(), u32()
            self.check(self.lib.amdgpu_device_initialize(
                self.fd, ctypes.byref(major), ctypes.byref(minor), ctypes.byref(self.device)),
                'AMDGPU device initialization')
            self.check(self.lib.amdgpu_cs_ctx_create(self.device, ctypes.byref(self.context)),
                       'AMDGPU context creation')
            # AMDGPU_CTX_OP_GET_STABLE_PSTATE=5; NONE=0. Do not take over an
            # existing profiling lease. The kernel also rejects competing owners.
            self.check(self.lib.amdgpu_cs_ctx_stable_pstate(
                self.context, 5, 0, ctypes.byref(state)), 'query stable pstate')
            if state.value != 0:
                raise RuntimeError(f'GPU already has stable pstate {state.value}; leaving it alone')
            # AMDGPU_CTX_OP_SET_STABLE_PSTATE=6; PEAK=4 (amdgpu_drm.h).
            self.check(self.lib.amdgpu_cs_ctx_stable_pstate(self.context, 6, 4, None),
                       'acquire peak-clock lease')
            self.check(self.lib.amdgpu_cs_ctx_stable_pstate(
                self.context, 5, 0, ctypes.byref(state)), 'verify stable pstate')
            if state.value != 4:
                raise RuntimeError(f'GPU did not enter peak pstate (reported {state.value})')
            return self
        except BaseException:
            self.close()
            raise

    def close(self):
        # Freeing the owning context restores the saved state in amdgpu_ctx_fini.
        # Do not explicitly SET NONE: that would discard the kernel's saved state.
        try:
            if self.context:
                self.check(self.lib.amdgpu_cs_ctx_free(self.context), 'release GPU context')
                self.context = ctypes.c_void_p()
        finally:
            try:
                if self.device:
                    self.check(self.lib.amdgpu_device_deinitialize(self.device), 'release GPU device')
                    self.device = ctypes.c_void_p()
            finally:
                if self.fd is not None:
                    os.close(self.fd)
                    self.fd = None

    def __exit__(self, *exc):
        self.close()


def run_command(command):
    child = subprocess.Popen(command, start_new_session=True)
    old_handlers = {}

    def forward(signum, frame):
        try:
            os.killpg(child.pid, signum)
        except ProcessLookupError:
            pass

    try:
        for sig in (signal.SIGINT, signal.SIGTERM):
            old_handlers[sig] = signal.signal(sig, forward)
        result = child.wait()
        return result if result >= 0 else 128 - result
    finally:
        for sig, handler in old_handlers.items():
            signal.signal(sig, handler)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True, help='AMDGPU render node used by the game')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('a command is required after --')
    try:
        with PeakClockLease(args.device):
            print(f'SOTC GPU PERFORMANCE: peak-clock lease on {args.device}; no tracing added', flush=True)
            return run_command(command)
    except (OSError, RuntimeError) as exc:
        print(f'SOTC GPU PERFORMANCE: failed: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
