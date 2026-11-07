# Native Linux build and AppImage

This Linux port executes the guest's x86-64 code directly and renders with Vulkan.
Wine/Proton is not used. Early gameplay has been tested with a CUSA08809 dump on an RX 6700 XT using RADV.
The intro and playable shrine area rendered at about 29–30 FPS; full-game stability remains unverified.
Supported dump versions are the same as upstream: CUSA08809 EU 1.01 or CUSA08034 US 1.00.

## Build

Clone recursively, or run `git submodule update --init --recursive`.
Use a recent GCC/Clang with C++23, CMake >= 3.24, Ninja and pkg-config. Dependencies:
Vulkan headers/loader, SDL3, FFmpeg, Boost headers, fmt, robin-map, xxhash,
Vulkan Memory Allocator headers, glslang, Zydis, X11 development files.
Magic Enum, miniz, Xbyak and LibAtrac9 are provided by the submodules.
`shell.nix` provides a build environment on Nix systems.
Python 3 with Tk is needed for the Linux launcher.

```sh
bash build.sh --test
python3 launcher/sotc_launcher_linux.py
```

`BB_JOBS=2` is the default Linux GPU compiler parallelism; increase it if RAM permits.
`BB_LTO=OFF` disables LTO for faster development builds. GCC LTO uses two link
workers by default (`-DBB_LTO_JOBS=N` in CMake changes this).

For local profile-guided optimization, build with `BB_PGO=generate BB_LTO=OFF`,
play representative areas for at least a minute (profiles are written every 30 seconds),
then rebuild in the same directory with `BB_PGO=use BB_LTO=ON`. Keep `pgo/` private
and retrain after significant source changes. `BB_PGO=off` disables profile use.
The packaged build records its settings in `usr/share/sotc/BUILD-OPTIMIZATION.txt`.

This version selectively ports submit-rate pacing from PR #351, partial dirty readback
completion and bounded retries from PR #349, and the flat-buffer ownership transfer
from PR #308. These are enabled by default, with independent rollback switches:
`SOTC_FLIP_PACER=0` (also `SOTC_FLIP_SUBTICKS=1` for the original timer frequency),
`SOTC_RB_PART=0 SOTC_RB_REQUEUE=0`, and `SOTC_FLAT_SWAP=0`.
The legacy `SOTC_RB_DIRTY_RETRY` setting takes precedence over `SOTC_RB_REQUEUE`.
The larger draw-replay/depth-pass caches from PR #308 are not included.

Choose the folder containing your decrypted `eboot.bin` in the launcher. Sound is
optional and requires your own decrypted `libSceNgs2.sprx` and `libSceUlt.sprx`.
The package contains no game dump or console firmware.

For terminal launch:

```sh
SOTC_GAME_DIR=/path/to/CUSA08809 bash run.sh
# Existing prebuilt tree (no compiler needed):
BB_PREBUILT=1 SOTC_GAME_DIR=/path/to/CUSA08809 bash run.sh
```

## Package

Download linuxdeploy's x86_64 AppImage from its official GitHub releases and make it
executable. FUSE is optional: you can extract it with `--appimage-extract` and use
its extracted `AppRun` instead.

```sh
LINUXDEPLOY=/absolute/path/to/linuxdeploy-x86_64.AppImage bash packaging/appimage.sh
chmod +x dist/SotC-x86_64.AppImage
./dist/SotC-x86_64.AppImage
```

The AppImage bundles the native runtime, its library dependencies, Python, Tcl/Tk,
and preparation scripts. It uses the host's Vulkan graphics driver. Build it on
the oldest distribution you intend to support: linuxdeploy deliberately excludes
glibc and graphics drivers. A build made on a recent Arch installation is **not**
a universal Ubuntu/Steam Deck release. The build's glibc version is recorded in
`usr/share/sotc/BUILD-GLIBC.txt`; the executable's symbol requirements determine
its actual minimum. The bundled AppDir can be tested without packaging by passing
`--appdir-only` to `packaging/appimage.sh` and running `out/SotC.AppDir/AppRun`.

If FUSE is unavailable, run `./SotC-x86_64.AppImage --appimage-extract-and-run`.
`--vulkan-test` checks command submission and GPU readback without a game dump.
`--help` shows paths; `--play` starts using saved settings; `--run` passes the
remaining arguments directly to `sotc.py`. Use `--play` as a Steam launch argument
if you want to skip the launcher. Select native Linux execution in Steam.

## Data and settings

- Settings: `${XDG_CONFIG_HOME:-~/.config}/sotc/sotc.ini` (`SOTC_CONFIG` overrides).
- Saves, shaders and logs: `${XDG_DATA_HOME:-~/.local/share}/sotc` (`SOTC_USER_DIR` overrides).
- Generated game images: `<data>/prepared`. These are private and never packaged.

The loader reserves the executable below 2 GiB for the game’s 31-bit static pointers.
Game asset paths resolve differing filename casing on Linux, including update overlays;
writable save mounts keep their normal case semantics. The dump is not renamed or edited.
The “Optimize GPU readbacks and memory reuse” option enables upstream readback and
heap-guard fast paths and mailbox presentation; explicit environment/INI overrides
win. Set SOTC_LINUX_PERF=0 to disable these defaults. In-game settings saves preserve
launcher startup options and key/pad mappings in the shared INI.
The AppImage can remain read-only. Advanced `SOTC_*` settings and key/pad mappings
can be added to the INI. Environment variables take precedence over the INI.

## Current limits

Native FSR 3 frame generation is available as an experimental launcher checkbox
and through F8. With a 30 FPS base, tests on the RX 6700 XT reached about 59–60
presentations/s in the shrine area. The normal FPS counter reports base game FPS;
the performance overlay also reports generated frames. This does not turn 30 FPS
simulation/input into native 60 FPS responsiveness. DLSS and the Windows AMD FSR SDK
upscaler (FSR 4) remain unavailable.
The Linux launcher provides dump/module selection, 30/60/unlimited FPS, FSR 3 frame
generation, optional readback/memory optimizations, logs and Stop; the
Windows launcher's updater, save manager, hardware wizard and tray are not ported.
DLC discovery and the Windows page-pool diagnostic trace hooks are also unavailable.
In-game settings remain available through F1. Graphics correctness and game
completion require testing with your dump and GPU; startup tests alone do not
establish playability.

The source and GPL license must accompany binary distribution (or be offered in
accordance with GPL). Include the exact matching modified source and submodule
revisions, along with applicable bundled-library licenses.
