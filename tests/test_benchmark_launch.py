"""Offline launch-policy and correctness-gate regressions; never executes a game process."""
from paths import ROOT
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import sotc_launch_env

spec = importlib.util.spec_from_file_location('shrine_bench', ROOT / 'tools/sotc_bench_shrine.py')
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


class BenchmarkEnvironmentTests(unittest.TestCase):
    def test_reference_probe_receives_launcher_runtime_defaults(self):
        base = {'PATH': '/usr/bin'}
        with patch.object(os, 'name', 'posix'):
            env = bench.build_probe_environment(base, Path('out'), Path('/isolated/reference-b01'))
            launched = dict(base)
            sotc_launch_env.native_runtime_defaults(launched)
        for key in ('BB_DMEM_MB', 'BB_VBLANK_HZ', 'SOTC_RB_HOT', 'SOTC_GUARD_STALE',
                    'SOTC_GUARD_USER_POINTER', 'SOTC_RELEASE_DEFER', 'SOTC_IMAGE_DEDICATED',
                    'BB_PRESENT_MODE'):
            self.assertEqual(env[key], launched[key], key)
        self.assertEqual(env['BB_DMEM_MB'], '9152')
        self.assertEqual(base, {'PATH': '/usr/bin'})
        self.assertEqual(env['SOTC_PREPARED_DRAW'], '0')
        self.assertEqual(env['SOTC_PREPARED_DRAW_LAZY'], '0')
        self.assertEqual(env['SOTC_PREPARED_DRAW_LAZY_DIRECT'], '0')
        self.assertEqual(env['SOTC_PREPARED_DRAW_VERIFY'], '0')
        self.assertEqual(env['SOTC_PIPE_KEY_MEMO'], '0')
        self.assertEqual(env['SOTC_FRAMEGEN'], 'off')
        self.assertEqual(env['SOTC_REC_CLOCK_BATCH'], '1')
        self.assertEqual(env['SOTC_REC_SPIN_US'], '250')
        self.assertEqual(env['SOTC_REC_SLEEP_US'], '500')

    def test_explicit_overrides_survive_defaults(self):
        overrides = ('BB_DMEM_MB=5056', 'SOTC_RB_HOT=0', 'SOTC_GUARD_STALE=0',
                     'SOTC_GUARD_USER_POINTER=0', 'SOTC_RELEASE_DEFER=0',
                     'SOTC_IMAGE_DEDICATED=0', 'SOTC_PREPARED_DRAW=1',
                     'SOTC_PREPARED_DRAW_LAZY=1', 'SOTC_PREPARED_DRAW_LAZY_DIRECT=1')
        env = bench.build_probe_environment({}, Path('out'), Path('/isolated/reference'), overrides)
        for item in overrides:
            key, value = item.split('=', 1)
            self.assertEqual(env[key], value)

    def test_linux_perf_opt_out_retains_guest_memory_and_vblank_defaults(self):
        env = bench.build_probe_environment({}, Path('out'), Path('/isolated/reference'),
                                             ('SOTC_LINUX_PERF=0',))
        self.assertNotIn('SOTC_RB_HOT', env)
        self.assertNotIn('SOTC_RELEASE_DEFER', env)
        self.assertEqual(env['BB_DMEM_MB'], '9152')
        self.assertEqual(env['BB_VBLANK_HZ'], '120')

    def test_windows_runtime_defaults_preserve_existing_policy(self):
        env = {}
        with patch.object(os, 'name', 'nt'):
            sotc_launch_env.native_runtime_defaults(env)
        self.assertEqual(env, {'BB_DMEM_MB': '9152', 'BB_VBLANK_HZ': '120'})

    def test_b01_requires_an_explicit_isolated_root_before_launch(self):
        for args in ([], ['--run-root', str(ROOT)], ['--run-root', str(ROOT / 'out')]):
            with self.subTest(args=args), \
                 patch.object(bench.sys, 'argv', ['bench', '--reference-b01', *args]), \
                 patch.object(bench.subprocess, 'Popen') as popen:
                with self.assertRaises(SystemExit) as result:
                    bench.main()
                self.assertEqual(result.exception.code, 2)
                popen.assert_not_called()

    def test_actual_probe_launch_uses_isolated_paths_and_shared_defaults(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            reference = root / 'reference-b01'
            reference.mkdir()
            (reference / 'neutral.pad').write_text('lx=128 ly=128 rx=128 ry=128\n')
            template = root / 'out/save-template/CUSA08809'
            template.mkdir(parents=True)
            (template / 'fixture-save').write_bytes(b'offline fixture')
            process = Mock(returncode=0)
            process.poll.return_value = 0  # Deliberate early exit must be rejected.
            previous = Path.cwd()
            try:
                with patch.object(bench.sys, 'argv', ['bench', '--reference-b01', '--run-root', str(root)]), \
                     patch.object(bench, 'game_serial', return_value='CUSA08809'), \
                     patch.object(bench.subprocess, 'Popen', return_value=process) as popen, \
                     patch.object(os, 'name', 'posix'):
                    self.assertEqual(bench.main(), 1)
                    self.assertEqual(Path.cwd(), root)
                    env = popen.call_args.kwargs['env']
                    self.assertEqual(env['BB_DMEM_MB'], '9152')
                    self.assertEqual(env['SOTC_RELEASE_DEFER'], '1')
                    self.assertEqual(env['BB_CONFIG'], str(reference / 'recclock-1080-B-01.ini'))
                    self.assertEqual(popen.call_args.args[0][-2:], ['--timeout', '0'])
                    self.assertEqual((root / 'out/user/savedata/1/CUSA08809/fixture-save').read_bytes(),
                                     b'offline fixture')
            finally:
                os.chdir(previous)


class CorrectnessGateTests(unittest.TestCase):
    def test_only_exact_known_stack_exclusion_is_nonfatal(self):
        benign = ('GPU <Error> page_manager.cpp:123 Protect: Not protecting guest stack pages '
                  'in 0x1234+0x1000\n')
        self.assertIsNone(bench.find_correctness_failure(benign))
        self.assertIsNotNone(bench.find_correctness_failure(benign + '<Critical> Unreachable code'))
        self.assertIsNotNone(bench.find_correctness_failure(benign.replace('<Error>', '<Critical>')))

    def test_fatal_runtime_and_shader_markers_are_preserved(self):
        for marker in ('<Critical> Unreachable code in resource_patching_pass.cpp',
                       'Assertion Failed', 'assertion failed', 'VK_ERROR_DEVICE_LOST',
                       'Fault at 0x1234', 'STOP: shader error',
                       'Prepared draw descriptor mismatch: shader=0x1234'):
            with self.subTest(marker=marker):
                self.assertIsNotNone(bench.find_correctness_failure(marker))


if __name__ == '__main__':
    unittest.main()
