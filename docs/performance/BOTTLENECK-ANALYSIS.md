# Native 60 FPS bottleneck analysis

**Measurement date:** 7 November 2026<br>
**Scene:** fixed-camera Shrine of Worship, uncapped native rendering<br>
**Frame generation:** off

## Finding

The current build does **not** sustain 60 native FPS in this scene: four-run medians
were 50.38 FPS at 1280×720 and 46.80 FPS at 1920×1080. Median per-window p95 frame
times were 22.90 ms and 24.55 ms, respectively, well above the 16.67 ms 60-FPS
budget. Lowering resolution helps, but does not approach that budget.

The measurements lean toward a substantial host renderer/command-processing cost,
with GPU work still material. They do not prove a single exclusive bottleneck: the
GPU-busy counter was around 70%, instrumented command-buffer spans account for only
part of the frame, and the detailed probes measurably perturb performance at 720p.
The appropriate next step is a narrow, correctness-checked CPU-side A/B—not a broad
rendering or synchronization rewrite.

## Test and measurement method

- Host: Ryzen 7 5800X, Radeon RX 6700 XT, RADV 26.2.6 on Arch Linux. The tested
  source build uses GCC 16.2.1, `RelWithDebInfo`, PGO and LTO.
- The same restored save, stationary Shrine view, scripted neutral input and
  benchmark-only input isolation were used throughout. The game was uncapped;
  presentation used Mailbox. Render resolution was 1920×1080 or 1280×720. FSR 3
  frame generation was disabled.
- Each main matrix cell has four valid runs. Settled data covers game timestamps
  25–65 seconds (nine five-second windows). Runs that stopped before those windows
  were complete were excluded and replaced.
- `SOTC_FRAME_TRACE` records individual flip intervals to CSV and the logger
  computes p50/p95/p99 per five-second window. The percentile table below is the
  median of the run-level medians of those window percentiles; it is **not** a
  single percentile pooled across every run.
- Detailed profiles enabled `SOTC_DRAW_SPLIT` and nonblocking Vulkan timestamp
  probes. Linux thread CPU time uses per-thread CPU clocks; a separate comparison
  against `/proc` task accounting agreed within about 1%. Timestamp queries used
  10 ns device resolution and 64 valid bits. These probes are diagnostic, not the
  source of the probe-free FPS baseline.

## Main results

| Render size | Detailed probes | Median FPS across four runs | Median per-window p50 / p95 / p99 (ms) |
| --- | --- | ---: | ---: |
| 1920×1080 | Off | **46.80** | 21.35 / 24.55 / 27.10 |
| 1920×1080 | On | 46.65 | 21.85 / 24.65 / 28.10 |
| 1280×720 | Off | **50.38** | 19.85 / 22.90 / 24.00 |
| 1280×720 | On | 46.60 | 21.40 / 24.05 / 26.50 |

FPS is the median of each run's settled-window median; individual run results varied
by several FPS. The 720p results show that reducing pixel count yields only a modest
gain in the unprobed baseline. This argues against a simple resolution-dominated
fill-rate limit, but does not rule out GPU work elsewhere in the pipeline.

### Probe overhead

A valid 720p on–off–off–on block compared the combined detailed probes with
trace-only controls. The two probe-on runs had a 43.10 FPS median; the two controls
had a 48.55 FPS median, about **11% lower FPS with probes enabled**. The per-run raw
interval percentiles moved in the same direction: median-of-run p50/p95/p99 was
22.41/26.05/28.19 ms with probes and 20.33/23.79/24.88 ms without. GPU-busy samples
were similar. A separate valid 1080p on–off–off–on block measured 46.70 FPS with
probes versus 48.90 FPS without (about **4.5% lower**); its median-of-run raw
p50/p95/p99 was 21.86/25.12/29.18 ms versus 20.38/23.97/26.63 ms. That estimate is
directional only: there were two runs per arm and the probe-on run medians were
44.7 and 48.7 FPS. The four-run main matrix also varied by resolution (7.5% lower
at 720p, under 1% at 1080p). Together, these blocks show that probes can materially
affect FPS, but the size varies by resolution and run; use probe-off numbers for FPS
conclusions and regard the detailed CPU/GPU breakdown as potentially perturbative.

Raw per-flip CSVs were retained for the 720p controls, both detailed-profile groups,
and the supplemental 1080p ABBA. The p95 values above come directly from frame
intervals, not an estimate from average FPS. The main comparison reports window
percentiles to avoid letting longer, faster windows dominate a pooled whole-run
percentile.

## What the profiles show

### Host CPU work

Across settled detailed-profile windows, measured thread CPU time was approximately:

| Resolution | Draw thread | Command processor | Vulkan recorder |
| --- | ---: | ---: | ---: |
| 1920×1080 | 17.3 ms/game frame | 15.4 ms/game frame | 17.6 ms/game frame |
| 1280×720 | 16.8 ms/game frame | 14.7 ms/game frame | 17.1 ms/game frame |

These are **separate threads running concurrently**; do not add them and interpret
the sum as frame latency. Their similar costs at the two render sizes support a
substantial host-side workload that is not proportional to pixel count.

Draw-thread elapsed-time subcounters further locate work, but are nested/inclusive
and should not be summed as independent costs. The counter labeled `obtain` was about
4.0 ms/frame inside roughly 9.5 ms/frame of resource binding. It is a wall-elapsed
timer, not thread CPU time or a range-lookup-only measurement: its scope begins before
`BufferCache::ObtainBuffer` and extends past the return through caller-side offset
alignment, descriptor bookkeeping, optional write invalidation, and
`Runtime::IsBufferAccessed`. Draw/indirect/dispatch work was also substantial.
Descriptor push recording was about 0.4 ms/frame, descriptor updates were near zero,
and `vkQueueSubmit` was about 0.6 ms/frame. Draw-thread waits for GPU, submit,
recorder and frame were near zero. The CP-to-draw queue reached its 512-entry
capacity in settled profile windows, indicating queue pressure, though this maximum
alone does not identify which thread sets the frame rate.

Guest-thread readback-wait telemetry was about 12.8 ms/frame in aggregate and mostly
attributed to GPU waits. It can overlap other guest and renderer work, so it is not
12.8 ms of serialized frame latency. It does not override the near-zero draw-thread
wait measurements.

### GPU work and synchronization

Vulkan timestamps summed to roughly 14–15 ms/game frame across instrumented primary
command-buffer spans, around 64–65% of the measurement window; no timestamp queries
were skipped in the settled profiles. This is **not complete whole-frame GPU time**:
upload command buffers and presentation/compositor work are not included, and the
sum cannot be treated as a per-frame critical-path duration.

The sampled GPU-busy counter was generally around 68–70%, rather than continuously
near 100%. It is a coarse utilization signal, not per-frame timing or proof of GPU
headroom. Taken with the weak resolution scaling and the host-thread costs, it
leans away from a fully saturated GPU as the only limiter. GPU execution still
occupies a meaningful portion of the measured window, so this is best described as
a **host-heavy or mixed limit**, not a proven CPU-only limit.

## `ObtainBuffer` phase-A review

The source path shows why the existing `obtain` figure cannot be attributed to key
lookup:

1. A configurable small-read-only streaming branch checks GPU-modified state, copies
   guest bytes to the stream buffer, commits it, and returns. Its default size
   threshold is zero, but it remains a distinct path when enabled.
2. The normal path computes the covered blocks and calls `GetArena`. The stable
   mapping case uses a few constant-time `address_space` table reads; missing or
   split mappings can create or merge arena objects and rewrite page-to-arena
   pointers.
3. `EnsureResident` handles overlapping pending releases, resident-range lookup and
   use stamping. On misses it can allocate/bind sparse memory and update the BDA
   page table. It already has a direct-mapped resident-range cache, keyed by first
   block and `resident_version` (64 slots by default, 1024 with the wide-cache
   setting); this is not an exact `(address, size, flags)` result cache.
4. `SynchronizeMemory` scans the memory tracker for CPU-dirty upload ranges. If any
   exist, it requests staging, copies guest bytes, flushes and records a Vulkan copy,
   and records the writes. Read-only texel buffers may additionally synchronize an
   aliased image.
5. Writable ranges additionally call `NoteHostWrite`, `AddGpuModified` (including
   in-flight-readback and clean-page tracking), and `RecordCurrentWrite`.

The existing profiles do **not** give separate timings for these stages, exact-range
key frequency (consecutive or within a frame), unique-key counts, or generation and
arena invalidation rates. None of those values can be inferred from the 4.0 ms
inclusive caller timer or from the resident cache's own hit path.

Source review identifies invalidation concerns for any future cache. Resident-range
additions, releases and cold evictions advance `resident_version`; `GetArena` can
rewrite `address_space` pointers during arena creation/merge, so a new cache of arena
pointers must explicitly account for those rewrites rather than assuming
`resident_version` alone covers every identity change. `OnMappingChanged` advances
`clean_map_generation`; `StreamGeneration` applies to transient stream-buffer
storage. A cached result must also exclude the stream-buffer return path. Regardless
of a lookup hit, `EnsureResident`, `SynchronizeMemory`, GPU-write tracking and the
caller-side access/barrier checks must continue to run.

### Phase-A execution status and Phase-B gate

The existing report contains a prior four-run probe-free 1080p reference (46.80 FPS;
per-window p50/p95/p99 of 21.35/24.55/27.10 ms). It is preserved reference data, not
a new baseline run for this follow-up. A fresh current-source build and run could not
be made in this environment: CMake, Ninja and Vulkan development headers are
unavailable. Therefore no claim is made that the current instrumentation was built
or linked here, and no new stage timings, exact-key repetition counts, or
invalidation frequencies are reported. Phase A is incomplete; Phase B has not
started, and no cache optimization has been implemented.

After a complete Linux build environment is available, add only opt-in aggregate
probes for the stages above and count exact-key calls, distinct keys and repeat hits
without per-call logging. Record arena/page-map and relevant generation changes
separately. Proceed to an opt-in exact-range lookup experiment only if those data
show both substantial reuse and material lookup cost. Verify every hit against the
existing path and conservatively invalidate on any backing-arena, mapping, or
generation change. Then compare default and experiment with four interleaved runs
per variant at 1080p, probe-free for FPS, and separate diagnostic runs. Require
correctness checks and repeatable improvement in native FPS and p50/p95/p99; reject
the experiment if lookup is not material, reuse is rare, or coherence cannot be
preserved. **No optimization is justified by the available measurements.**

## Limits and artifacts

This is one stationary Shrine scene on one Linux GPU/driver combination. It does
not establish performance in traversal, combat, colossus encounters, transitions,
or longer sessions. The utilization sample is not a substitute for per-frame GPU
execution time, and the detailed probes have measurable cost. Frame-generation
presentations are intentionally excluded: these figures are native/base game FPS.

Benchmark logs, raw traces, and process-sampling files remain local measurement
artifacts and are not part of this report. The AppImage was not used or modified by
these source-build measurements.
