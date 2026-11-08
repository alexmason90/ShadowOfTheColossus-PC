# Native launch configuration correction

The 2026-11-08 PGO ABBA bypassed `sotc.py` and executed `bb-probe` directly. Setting
`SOTC_LINUX_PERF=1` did not apply the Python launcher's performance defaults. Historical runs
used 9152 MiB guest direct memory and hot readbacks; that ABBA used 5056 MiB and no hot
readbacks. Historical runs also loaded shader/driver caches; the ABBA used empty isolated roots.
The ABBA compares its own two configurations but does not measure a source regression against
the historical 51.4 FPS observation.

`scripts/sotc_launch_env.py` now owns the existing launcher defaults. Both `sotc.py` and the
repository Shrine runner apply them. Explicit `--env` overrides in the runner win, including
`SOTC_LINUX_PERF=0` or individual fast-path opt-outs. This shares the existing policy; it does
not change the renderer implementations of those flags. In particular, this is not evidence
that historical readback/heap-guard correctness concerns are resolved for every workload.

The repository runner includes the previously corrected B-01 gate and timeout behavior. B-01
now requires an explicit run root outside the repository; saves and outputs remain under that
root's `out/`. The prior archived harness and all prior measurement roots are preserved.
Before launch the runner prints the effective guest-memory, readback, prepared-draw and
recorder settings. Reference mode starts with prepared draws and pipeline-key memoisation off.

Run shape (a proposal, not a new measurement):

```sh
LD_LIBRARY_PATH=/path/to/optimized-build:/path/to/deps/usr/lib \
  python3 tools/sotc_bench_shrine.py --reference-b01 \
  --run-root /path/to/fresh-isolated-root --measure 40 --timeout 330 --shots 0
```

The run root must contain `reference-b01/`, `out/bb-probe.exe`, and the isolated
`out/save-template/CUSA08809/`. Copy cache templates into `out/user/` before launch when a warm
comparison is intended. Use identical cache copies for each arm and record their source; do not
silently mix historical shaders with newly generated translation results.

Offline checks cover launcher/probe environment equivalence, explicit overrides, Linux opt-out,
Windows defaults, actual mocked probe launch with isolated save restoration, rejection of an
incomplete measurement, and preservation of fatal shader/runtime/Vulkan gate markers.
There is no new FPS measurement or 60 FPS claim from this patch.
