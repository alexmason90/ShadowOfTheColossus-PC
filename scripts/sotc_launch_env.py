"""Runtime defaults shared by the launcher and direct native benchmark runner.

These preserve the launcher's existing policy. Explicit environment overrides always win.
Setting SOTC_LINUX_PERF in a probe process alone cannot apply Python launcher defaults.
"""
import os


def linux_performance_defaults(env):
    if os.name == 'nt' or env.get('SOTC_LINUX_PERF', '1') != '1':
        return
    for key, value in {
        # The heap guard's two shortcuts (SOTC_GUARD_STALE, SOTC_GUARD_USER_POINTER) stay off,
        # as in the Windows launcher since 0.73: they spoilt the game's heap at the ending.
        'SOTC_RB_HOT': '1', 'SOTC_RELEASE_DEFER': '1',
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
    if os.name == 'nt':
        return  # the Windows launcher (game_environment) sets the ones below itself
    # Linux parity with the Windows launcher, which sets these for every player; they are not
    # performance switches, so SOTC_LINUX_PERF=0 keeps them.
    # 0.72.2 (T51): at the end of the game the game returns page pool pages under textures it
    # goes on drawing; a refresh then uploaded another texture's bytes into them (stretched
    # textures, flicker, the close between the ending and the intro).
    env.setdefault('SOTC_POOL_FREEZE', '1')
    # High quality textures (the Windows launcher's default "High"): the texture banks sampled
    # since the last report read as sampled, so the game streams its fine mips (blurry leaves and
    # rocks without), with the 1-tick linger. SOTC_LOD_FEEDBACK=0 / SOTC_LOD_LINGER=0 restore
    # the old behaviour for cards short of video memory.
    env.setdefault('SOTC_LOD_LINGER', '1')
    env.setdefault('SOTC_LOD_FEEDBACK', '1')
    # 0.73: High's finer mip (the Windows launcher's level for "High" without the light profile).
    env.setdefault('SOTC_LOD_LEVEL', '1')
