"""Regression tests for the read-only AppImage launch path; no game assets needed."""
from paths import ROOT
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('sotc', ROOT / 'sotc.py')
sotc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sotc)


@unittest.skipIf(os.name == 'nt', 'Linux launch paths')
class LinuxLaunchTests(unittest.TestCase):
    def test_native_executable_and_xdg_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'bin').mkdir()
            native = root / 'bin/bb-probe'
            native.touch()
            (root / 'bin/bb-probe.exe').touch()
            with patch.object(sotc, 'PORT', root), patch.dict(os.environ, {
                'XDG_CONFIG_HOME': str(root / 'config'), 'XDG_DATA_HOME': str(root / 'data')
            }, clear=True):
                self.assertEqual(sotc.find_probe(), native)
                self.assertEqual(sotc.data_dir(), root / 'data/sotc')
                self.assertEqual(sotc.config_file(), root / 'config/sotc/sotc.ini')
                self.assertEqual(sotc.no_console(), 0)

    def test_performance_defaults_respect_overrides_and_opt_out(self):
        env={'SOTC_RB_HOT':'0'}
        sotc.linux_performance_defaults(env)
        self.assertEqual(env['SOTC_RB_HOT'],'0')
        self.assertEqual(env['SOTC_RELEASE_DEFER'],'1')
        disabled={'SOTC_LINUX_PERF':'0'}
        sotc.linux_performance_defaults(disabled)
        self.assertEqual(disabled,{'SOTC_LINUX_PERF':'0'})

    def test_runtime_defaults_match_windows_launcher(self):
        # The pool freeze (end-game textures) and the texture streaming feedback are set by the
        # Windows launcher for every player; the performance opt-out must not drop them.
        env = {'SOTC_LINUX_PERF': '0'}
        sotc.native_runtime_defaults(env)
        for key in ('SOTC_POOL_FREEZE', 'SOTC_LOD_LINGER', 'SOTC_LOD_FEEDBACK'):
            self.assertEqual(env[key], '1', key)
        self.assertNotIn('SOTC_RB_HOT', env)
        overridden = {'SOTC_LOD_FEEDBACK': '0', 'SOTC_POOL_FREEZE': '0'}
        sotc.native_runtime_defaults(overridden)
        self.assertEqual(overridden['SOTC_LOD_FEEDBACK'], '0')
        self.assertEqual(overridden['SOTC_POOL_FREEZE'], '0')

    def test_script_fingerprint_survives_appimage_remounts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game = root / 'game'
            game.mkdir()
            (game / 'eboot.bin').write_bytes(b'fixture')
            fingerprints = []
            for mount in ('first-mount', 'second-mount'):
                scripts = root / mount / 'scripts'
                scripts.mkdir(parents=True)
                (scripts / 'prepare.py').write_text('same script')
                with patch.object(sotc, 'SCRIPTS', scripts):
                    fingerprints.append(sotc.fingerprint(game, []))
            self.assertEqual(*fingerprints)
            (scripts / 'prepare.py').write_text('new script!')
            with patch.object(sotc, 'SCRIPTS', scripts):
                self.assertNotEqual(fingerprints[0], sotc.fingerprint(game, []))

    def test_preparation_and_launch_never_write_into_application(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            app = root / 'app'
            (app / 'bin').mkdir(parents=True)
            (app / 'bin/bb-probe').touch()
            game = root / 'game'
            game.mkdir()
            (game / 'eboot.bin').write_bytes(b'test fixture, not a game')
            data = root / 'data'
            config = root / 'config.ini'
            config.write_text('SOTC_MODES=af16\n')
            def prepare(name, *args):
                out = Path(args[args.index('--out') + 1])
                for filename in ('boot-linked.bin', 'content.bin', 'sotc-cpu.bin', 'patches.bin'):
                    (out / filename).touch()
            previous = Path.cwd()
            app.chmod(0o555)
            try:
                with patch.object(sotc, 'PORT', app), patch.object(sotc, 'run_script', side_effect=prepare), \
                     patch.object(sotc.sys, 'argv', ['sotc.py', '--timeout', '1']), \
                     patch.object(sotc.sys.stdout, 'isatty', return_value=False), \
                     patch.object(sotc.subprocess, 'run') as run, patch.dict(os.environ, {
                         'SOTC_GAME_DIR': str(game), 'SOTC_USER_DIR': str(data),
                         'SOTC_CONFIG': str(config)
                     }, clear=True):
                    run.return_value.returncode = 7
                    self.assertEqual(sotc.main(), 7)
                    command = run.call_args.args[0]
                    self.assertEqual(command[0], str(app / 'bin/bb-probe'))
                    self.assertEqual(command[1], str(data / 'prepared/boot-linked.bin'))
                    self.assertEqual(command[-6:], ['--user', str(data), '--timeout', '0', '--timeout', '1'])
                    self.assertEqual(os.environ['BB_CONFIG'], str(config))
                    self.assertEqual(os.environ['BB_GPU_USER_DIR'], str(data))
                    self.assertEqual(os.environ['BB_DMEM_MB'], '9152')
                    self.assertEqual(os.environ['BB_VBLANK_HZ'], '120')
                    self.assertEqual(os.environ['SOTC_RELEASE_DEFER'], '1')
                    self.assertFalse((app / 'out').exists())
                    self.assertTrue((data / 'prepared/prepared.txt').exists())
            finally:
                os.chdir(previous)
                app.chmod(0o755)


if __name__ == '__main__':
    unittest.main()
