"""No GPU required: exercise the actual libdrm lease wrapper's ownership policy."""
import ctypes
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'gpu_performance', Path(__file__).resolve().parents[1] / 'tools/sotc_gpu_performance.py')
gpu = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gpu)


class Fn:
    def __init__(self, callback):
        self.callback = callback

    def __call__(self, *args):
        return self.callback(*args)


class Driver:
    def __init__(self, *, initial=0, fail=None, verify=4):
        self.calls = []
        self.state, self.fail, self.verify = initial, fail, verify
        for name in ('amdgpu_device_initialize', 'amdgpu_cs_ctx_create',
                     'amdgpu_cs_ctx_stable_pstate', 'amdgpu_cs_ctx_free',
                     'amdgpu_device_deinitialize'):
            setattr(self, name, Fn(lambda *args, name=name: self.call(name, args)))

    def call(self, name, args):
        self.calls.append((name, args[1:3] if name.endswith('stable_pstate') else ()))
        if name == self.fail or (self.fail == 'set' and args[1:2] == (6,)):
            return -16  # EBUSY: another context may own the profiling lease.
        if name == 'amdgpu_device_initialize':
            ctypes.cast(args[3], ctypes.POINTER(ctypes.c_void_p))[0] = 11
        elif name == 'amdgpu_cs_ctx_create':
            ctypes.cast(args[1], ctypes.POINTER(ctypes.c_void_p))[0] = 22
        elif name == 'amdgpu_cs_ctx_stable_pstate':
            if args[1] == 5:
                ctypes.cast(args[3], ctypes.POINTER(ctypes.c_uint32))[0] = self.state
            else:
                self.state = self.verify
        return 0


class LeaseTests(unittest.TestCase):
    def setUp(self):
        policy = patch.object(gpu, 'read_gpu_policy', return_value='auto')
        self.policy = policy.start()
        self.addCleanup(policy.stop)

    def lease(self, driver):
        return gpu.PeakClockLease('/dev/null', library=driver)

    def test_peak_verified_and_resources_released_once(self):
        driver = Driver()
        lease = self.lease(driver)
        with lease:
            self.assertTrue(lease.context)
            self.assertEqual(driver.state, 4)
        lease.close()
        names = [x[0] for x in driver.calls]
        self.assertEqual(names.count('amdgpu_cs_ctx_free'), 1)
        self.assertEqual(names[-2:], ['amdgpu_cs_ctx_free', 'amdgpu_device_deinitialize'])
        self.assertIsNone(lease.fd)
        self.assertFalse(lease.device)
        # Only PEAK is set. The kernel must restore its saved state on context free.
        self.assertEqual([a for n, a in driver.calls if n.endswith('stable_pstate')],
                         [(5, 0), (6, 4), (5, 0)])

    def test_existing_profiling_state_is_not_changed(self):
        driver = Driver(initial=1)
        with self.assertRaisesRegex(RuntimeError, 'already has stable pstate'):
            with self.lease(driver):
                self.fail('must not launch')
        self.assertFalse(any(a == (6, 4) for _, a in driver.calls))
        self.assertEqual(driver.calls[-2][0], 'amdgpu_cs_ctx_free')

    def test_manual_or_preexisting_power_policy_is_not_changed(self):
        for policy in ('high', 'low', 'manual', 'profile_peak', 'profile_standard'):
            with self.subTest(policy=policy):
                self.policy.return_value = policy
                driver = Driver()
                with self.assertRaisesRegex(RuntimeError, 'leaving it alone'):
                    with self.lease(driver):
                        self.fail('must not launch')
                self.assertEqual(driver.calls, [])

    def test_driver_failure_does_not_launch_or_leak(self):
        for failure in ('amdgpu_device_initialize', 'amdgpu_cs_ctx_create',
                        'amdgpu_cs_ctx_stable_pstate', 'set'):
            with self.subTest(failure=failure):
                driver = Driver(fail=failure)
                lease = self.lease(driver)
                with self.assertRaises(OSError):
                    with lease:
                        self.fail('must not launch')
                self.assertIsNone(lease.fd)
                self.assertFalse(lease.device)

    def test_driver_must_confirm_requested_state(self):
        driver = Driver(verify=0)
        with self.assertRaisesRegex(RuntimeError, 'did not enter peak'):
            with self.lease(driver):
                self.fail('must not launch')
        self.assertEqual(driver.calls[-2][0], 'amdgpu_cs_ctx_free')

    def test_workload_exception_releases_lease(self):
        driver = Driver()
        with self.assertRaisesRegex(ValueError, 'workload'):
            with self.lease(driver):
                raise ValueError('workload failed')
        self.assertEqual(driver.calls[-2][0], 'amdgpu_cs_ctx_free')

    def test_bad_device_does_not_run_workload(self):
        with patch.object(gpu, 'run_command') as run:
            self.assertEqual(gpu.main(['--device', '/nonexistent/render', '--', 'game']), 1)
            run.assert_not_called()

    def test_main_forwards_arguments_and_exit_code(self):
        with patch.object(gpu, 'PeakClockLease') as lease, \
             patch.object(gpu, 'run_command', return_value=7) as run:
            self.assertEqual(gpu.main(['--device', '/dev/null', '--', 'game', '--arg=x']), 7)
            run.assert_called_once_with(['game', '--arg=x'])
            lease.return_value.__exit__.assert_called_once()

    def test_child_exit_code_is_observed(self):
        self.assertEqual(gpu.run_command([sys.executable, '-c', 'raise SystemExit(7)']), 7)

    def test_signalled_child_exit_code(self):
        self.assertEqual(gpu.run_command([
            sys.executable, '-c', 'import os, signal; os.kill(os.getpid(), signal.SIGTERM)']), 143)


if __name__ == '__main__':
    unittest.main()
