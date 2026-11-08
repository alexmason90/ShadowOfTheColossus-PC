# GPU clock policy: a measurable native FPS improvement

Fixed 1080p B-01 Shrine, isolated saves, warm identical shader caches, frame generation off,
prepared draws off, pipeline memo off, recorder clock batch/spin/sleep = 1/250/500.
Every run loaded optimized renderer SHA-256
`1c52fc719f468cf7f8be9f45f31b2a24d04b9571eb84dff1fb73067048890534` (checked in `/proc`).

| Run | GPU policy | Average / minimum FPS | p50 / p95 / p99 ms | Worst ms | >50 ms |
| --- | --- | --- | --- | --- | --- |
| A1 | Automatic | 47.8 / 45.6 | 21.5 / 23.0 / 26.0 | 33.3 | 0 |
| B1 | Peak | 56.5 / 52.1 | 17.7 / 19.9 / 20.9 | 23.1 | 0 |
| B2 | Peak | 55.1 / 53.0 | 18.6 / 20.0 / 20.9 | 26.1 | 0 |
| A2 | Automatic | 45.6 / 43.4 | 21.8 / 25.1 / 28.2 | 72.0 | 1 |

Mean automatic 46.7 versus peak 55.8 FPS: **+19.5%**. Both peak runs exceeded both controls.
This is preliminary evidence from two observations per policy, not a universal gain or sustained
60 FPS. Automatic controls drifted down 2.2 FPS; peak runs differed by 1.4 FPS. Percentiles are
medians of the nine settled five-second window percentiles, not pooled per-frame percentiles.
All four correctness gates passed, with zero settled shader compilations and 5864–5878 commands/frame.
Automatic GPU clocks averaged 1.05–1.27 GHz; peak stayed at 2.61 GHz. Maximum reported GPU sensor
temperature was 65 C. Automatic policy was verified before and after every launch.

`tools/sotc_gpu_performance.py` is an explicit opt-in command wrapper. It holds an AMDGPU context
and requests the documented PEAK stable pstate through libdrm. There are no sysfs writes, voltage
changes, overclocking, power-limit changes or tracing. The kernel releases the lease when its
context closes. Existing non-automatic GPU policies and profiling leases are refused. Ten tests
cover refusal/failure paths, ownership, cleanup, command arguments and exit
codes; actual GPU checks also covered normal failure and SIGTERM with restoration to automatic.
The selected GPU's clocks are affected while the command runs; this is not a per-process frequency.

Example from the repository root (select the render node used by the renderer):

```sh
python tools/sotc_gpu_performance.py --device /dev/dri/renderD128 -- python sotc.py
```

This wrapper does not choose or install a renderer library. Keep the intended optimized library
selection explicit; the benchmark wrappers record and verify it. Nothing was enabled by default.

A separate hardware-trace diagnostic used RADV RGP, which itself requests peak clocks. It must
not be treated as an unprofiled FPS control. The captured frame had 33 command-buffer records,
7.28 ms of recorded GPU spans and 4.24 ms of inter-buffer gaps over an 11.52 ms captured span.
The largest 1.95 ms gap included 1.86 ms before the CPU submitted the next buffer (calibrated CPU/GPU
timestamps). This supports remaining CPU submission latency, but one traced span is not a complete
whole-frame timing breakdown. Installed RADV lacks u_trace support; the initial attempted u_trace
launch produced no trace and was excluded from the clock comparison.

Raw runs, loaded-library maps, host samples, RGP capture and extracted queue events are preserved at
`/home/hoss/.cache/codex-sotc-gpu-timeline-20261108/` (`clock-abba/results.json` for the table).
API references: [Mesa profiling pstate](https://docs.mesa3d.org/envvars.html#envvar-RADV_PROFILE_PSTATE),
[kernel context ownership/restoration](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/amdgpu_ctx.c),
[Mesa RGP record definitions](https://github.com/chaotic-cx/mesa-mirror/blob/main/src/amd/common/ac_rgp.c).

## CPU-tuned LTO comparison

One separate variant relinked the exact 166 preserved PGO-trained object/archive inputs with
GCC 16.2.1, `-O3 -march=znver3 -mtune=znver3 -flto=2`. These are **LTO-stage** changes to the
existing O2 objects, not a fresh O3 source build. GCC's verbose log confirms these options reached
all 97 LTRANS invocations. No compiler warnings occurred; strict missing-profile/coverage options
remained enabled. Existing profile annotations were reused, without recompiling source or changing
the 193-file training profile set. The original library and all input objects stayed unchanged.

All arms below used peak clocks, with tracing off and the same workload/settings as above.

| Run | Library | Average / minimum FPS | p50 / p95 / p99 ms | Worst ms | >50 ms |
| --- | --- | --- | --- | --- | --- |
| A1 | Reference | 55.8 / 49.6 | 17.7 / 19.8 / 19.9 | 27.0 | 0 |
| B1 | Tuned | 58.2 / 55.9 | 17.4 / 19.8 / 20.8 | 24.8 | 0 |
| B2 | Tuned | 53.8 / 52.1 | 18.8 / 20.8 / 21.9 | 26.0 | 0 |
| A2 | Reference | 57.0 / 47.9 | 17.7 / 19.8 / 20.8 | 41.3 | 0 |

Reference mean **56.4**, tuned **56.0 FPS (-0.7%)**. The tuned observations varied by 4.4 FPS;
there is no established benefit, so retain the reference library as the preferred build. The
58.2-FPS run included a 60.2-FPS window; neither build demonstrated sustained 60 FPS.
All gates passed, zero settled shader compilations, 5873–5877 commands/frame. Native
prepared-draw/stage-bind/pixel-format and shader-translation tests passed; all five emitted SPIR-V
modules passed `spirv-val --target-env vulkan1.3`. The new library also passed a loader smoke.
These shader tests exercise unchanged shader code in their existing dedicated test executables;
the relinked library itself was checked by loading it and the gameplay runs.

Variant: `/home/hoss/.cache/codex-sotc-gpu-timeline-20261108/cpu-tuned-lto/libbbgpu.so`, SHA-256
`70f1834693c4cdc5da39a58efe0c3d08a9f459347eed6e9ac0d871df3bf13c2c`.
Exact link command, input hashes, logs and offline outputs are beside it. Per-run FastBind counters,
host samples and verified library mappings are in `tuned-abba/results.json` and its run directories.

Decision: retain the opt-in GPU policy wrapper with the existing reference PGO/LTO build; keep
prepared draws and the unsuccessful empty-stage experiment off. No new renderer architecture was
introduced. Remaining CPU submission gaps, not a higher GPU-busy percentage, are the next measured
obstacle. This phase made meaningful progress but did not achieve the native 60-FPS objective.
