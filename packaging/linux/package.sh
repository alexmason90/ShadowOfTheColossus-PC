#!/usr/bin/env bash
# Generic FHS AppImage. Build on the oldest Linux distribution you intend to support.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
[[ $(uname -m) == x86_64 ]] || { echo 'Requires x86_64 Linux' >&2; exit 1; }
for file in out/bb-probe out/gpu/libbbgpu.so out/bb-gpu-capabilities; do
    [[ -f $file ]] || { echo "Missing $file; run bash build.sh first" >&2; exit 1; }
done
: "${LINUXDEPLOY:?Set LINUXDEPLOY to the linuxdeploy x86_64 executable (or extracted AppRun)}"
python=${PYTHON:-python3}
appdir="$PWD/out/SotC.AppDir"
rm -rf -- "$appdir"
mkdir -p "$appdir/usr/share/sotc/bin/gpu" "$appdir/usr/bin" "$appdir/usr/lib"
cp sotc.py LICENSE "$appdir/usr/share/sotc/"
cp -r scripts "$appdir/usr/share/sotc/"
mkdir -p "$appdir/usr/share/sotc/launcher"
cp launcher/sotc_launcher_linux.py "$appdir/usr/share/sotc/launcher/"
install -m755 out/bb-probe out/bb-gpu-capabilities "$appdir/usr/share/sotc/bin/"
install -m755 out/gpu/libbbgpu.so "$appdir/usr/share/sotc/bin/gpu/"
# Copy only the Python standard library, not unrelated host site-packages.
"$python" - "$appdir" <<'PY'
from pathlib import Path
import shutil
import sys
import sysconfig
import tkinter
root = Path(sys.argv[1]) / 'usr'
shutil.copy2(sys.executable, root / 'bin/python3')
stdlib = Path(sysconfig.get_path('stdlib'))
shutil.copytree(stdlib, root / 'lib' / stdlib.name,
                ignore=shutil.ignore_patterns('site-packages', '__pycache__', 'test', 'tests', 'idlelib', 'ensurepip', '_test*.so', 'xx*.so', 'config-*'))
libpython = Path(sysconfig.get_config_var('LIBDIR')) / sysconfig.get_config_var('LDLIBRARY')
shutil.copy2(libpython.resolve(), root / 'lib' / libpython.name)
# Tcl and Tk data are needed by _tkinter but are not ELF dependencies.
tcl = Path(tkinter.Tcl().eval('info library'))
target = root / 'share/tcltk'
target.mkdir(parents=True)
shutil.copytree(tcl, target / 'tcl8.6')
import os
tk = Path(os.environ['TK_LIBRARY']) if os.environ.get('TK_LIBRARY') else tcl.parent / 'tk8.6'
if not tk.is_dir():
    tk = Path('/usr/share/tk8.6')
shutil.copytree(tk, target / 'tk8.6')
PY
# Include installed library notices on Arch; additional roots can be supplied by other builders.
mkdir -p "$appdir/usr/share/licenses/sotc"
cp LICENSE "$appdir/usr/share/licenses/sotc/"
IFS=: read -r -a license_dirs <<< "/usr/share/licenses:${SOTC_LICENSE_DIRS:-}"
for directory in "${license_dirs[@]}"; do
    if [[ -d $directory ]]; then cp -r "$directory"/. "$appdir/usr/share/licenses/"; fi
done
# linuxdeploy follows DT_NEEDED recursively. Include dlopened Python extensions explicitly.
args=(--appdir "$appdir" --executable "$appdir/usr/bin/python3"
      --executable "$appdir/usr/share/sotc/bin/bb-probe"
      --executable "$appdir/usr/share/sotc/bin/bb-gpu-capabilities"
      --library "$appdir/usr/share/sotc/bin/gpu/libbbgpu.so"
      --desktop-file packaging/linux/sotc.desktop --icon-file launcher/sotc.png
      --custom-apprun packaging/linux/AppRun)
while IFS= read -r -d '' library; do args+=(--library "$library"); done < <(find "$appdir/usr/lib" -name '*.so*' -type f -print0)
export APPIMAGE_EXTRACT_AND_RUN=1
"$LINUXDEPLOY" "${args[@]}"
# Use linuxdeploy's relocated copies; remove the original copies with build-tree RUNPATHs.
for name in bb-probe bb-gpu-capabilities; do
    rm "$appdir/usr/share/sotc/bin/$name"
    ln -s "../../../bin/$name" "$appdir/usr/share/sotc/bin/$name"
done
rm "$appdir/usr/share/sotc/bin/gpu/libbbgpu.so"
ln -s ../../../../lib/libbbgpu.so "$appdir/usr/share/sotc/bin/gpu/libbbgpu.so"
# Reject accidental proprietary input and anything copied from the user's game.
if find "$appdir" -type f \( -iname '*.sprx' -o -iname '*.prx' -o -iname 'eboot.bin' \) -print -quit | grep -q .; then
    echo 'Refusing to package game or firmware files' >&2; exit 1
fi
getconf GNU_LIBC_VERSION > "$appdir/usr/share/sotc/BUILD-GLIBC.txt"
# Record the optimization settings without the builder's private profile paths.
awk '/^(BB_LTO|BB_LTO_JOBS|BB_PGO|CMAKE_BUILD_TYPE):/ { print }' out/gpu/CMakeCache.txt > "$appdir/usr/share/sotc/BUILD-OPTIMIZATION.txt"
if [[ ${1:-} != --appdir-only ]]; then
    mkdir -p dist
    export OUTPUT="$PWD/dist/SotC-x86_64.AppImage"
    LD_LIBRARY_PATH="$appdir/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$LINUXDEPLOY" --appdir "$appdir" --output appimage
    echo "Built $OUTPUT (requires $(getconf GNU_LIBC_VERSION) or newer)"
fi
