"""Runtime defaults shared by the launcher and direct native benchmark runner.

These preserve the launcher's existing policy. Explicit environment overrides always win.
Setting SOTC_LINUX_PERF in a probe process alone cannot apply Python launcher defaults.
"""
import os


def linux_performance_defaults(env):
    if os.name == 'nt' or env.get('SOTC_LINUX_PERF', '1') != '1':
        return
    for key, value in {
        'SOTC_RB_HOT': '1', 'SOTC_GUARD_STALE': '1',
        'SOTC_GUARD_USER_POINTER': '1', 'SOTC_RELEASE_DEFER': '1',
        'SOTC_IMAGE_DEDICATED': '1', 'BB_PRESENT_MODE': 'Mailbox',
    }.items():
        env.setdefault(key, value)


def native_runtime_defaults(env):
    linux_performance_defaults(env)
    # The game's direct memory allocator (a 64 KiB page bitmap over all of it) filled up and read
    # past the bitmap (guest offset 0x1a371, read of -1): in PS4 Pro mode with 5696 MiB when a save
    # loaded, and in PS4 mode with 5056 MiB on loading the last colossus' area (user logs, 19
    # Oct; high quality textures keep more fine mips resident than on a PS4). Every mode gets the
    # 9152 MiB the wide render sizes already used.
    env.setdefault('BB_DMEM_MB', '9152')
    # The game waits for vblanks: at 60 Hz an uncapped frame (fps60, or F7 in game) still rounds
    # up to 30 FPS. With the cap, 120 Hz only shows a late frame sooner (and SOTC_FPS_LIMIT counts
    # in 120 Hz vblanks).
    env.setdefault('BB_VBLANK_HZ', '120')
