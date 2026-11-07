# Performance handoff for the next reviewer

**Snapshot:** 7 November 2026  
**Branch:** `perf-pacer-eval`  
**Purpose:** Continue native-FPS work from measured evidence; do not treat generated
presentation FPS as game/base FPS.

## Executive summary

The current native Linux build reached a **51.40 FPS median** in a fixed early-game Shrine
scene, measured from the current source library with PGO+LTO. The previous packaged PGO+LTO
AppImage measured **37.75 FPS** in a four-run interleaved comparison (**+36.2% source-side
delta**). A separate same-source four-run-per-variant ablation measured **50.10 FPS with
PGO+LTO vs 45.75 without (+9.5%)**. These are native/base game rates with frame generation
off. The current packaged AppImage was separately verified at **30.0 base FPS and 60.0
presentations/s** with FSR 3 generation enabled.

The source changes and release commit are already on GitHub. This review made no source-code
changes; this handoff document is the only new repository file. The main unresolved technical
question is how much of the remaining ~50 FPS ceiling is host draw/submit work versus GPU
execution: useful host-side counters exist, but there is no direct GPU-utilization or per-frame
GPU-time measurement. The logger reports p50 and p99 frame times, **not p95**; no valid p95 can
be reconstructed from the retained samples.

## 1. Implemented work and what changed since the prior checkpoint

The prior Codex checkpoint already contained the source implementation and final PGO/LTO
binary. Work since that checkpoint primarily completed measurement, packaging, and release;
it did not add new C++ renderer code.

### Source implementation present in the pushed tree

- **Native Linux port:** launcher/AppImage path, Linux memory/heap and signal handling, and
  native Vulkan presentation. Native FSR 3 frame generation is included.
- **PR #351, selective:** finer flip timing and submit-rate pacing. The core pacer is in the
  source; alternate limiter experiments remain opt-in.
- **PR #349, adapted:** per-item dirty readback tracking, partial completion, and bounded
  retries on the owning guest thread. Existing safeguards for late guest writes remain.
- **PR #308, selective:** flat-buffer ownership transfer plus DrawReplay, depth/G-buffer
  render-state reuse, and associated draw-thread/binding fast paths. This is not the whole PR;
  byte-range readback intervals and the full stacked branch are not included.
- **PR #313, selective:** stale read-fault page cleanup.
- **Build tuning:** a fresh GCC 16.2.1 PGO profile and LTO build. No profile data is committed.
- **Diagnostics/safety:** frame/readback/VRAM counters and comparison/verification paths for
  DrawReplay, register transport, depth/G-buffer, and texture binding.

### Work completed after the checkpoint

- Resumed and completed the interrupted `final01–08` interleaved comparison; the incomplete
  server-restart run was rerun rather than counted.
- Added a separate `compiler01–08` comparison to isolate PGO/LTO on the same source.
- Packaged and validated the new AppImage; regenerated the source tarball, upstream-based
  patch, benchmark JSON, performance docs, and checksum manifest. The patch reverse-checks
  against the current tree and all output checksums verify.
- Published prerelease `linux-native-fps-2026.11.07` at commit `3ceb073`; the AppImage and
  SHA-256 sidecar are release assets.

## 2. Measured native-FPS improvements

All percentages below are **not additive**. Some are cumulative source comparisons; only the
explicit same-binary or same-source ablations isolate a category.

| Experiment | Result | Interpretation / confidence |
| --- | --- | --- |
| Current source vs previous packaged AppImage; 4 runs each, interleaved | Current **51.40 FPS** (run medians 49.90–53.50), prior package **37.75 FPS** (35.90–40.10): **+36.2%**. Group median per-window p50/p99: **19.75/23.15 ms** vs **26.55/32.30 ms**. | Strong evidence of a local cumulative source-side gain. Both use GCC 16.2.1 PGO+LTO, but this does not attribute the gain to any single PR/fast path. |
| Same-source compiler ablation; 4 runs each | PGO+LTO **50.10 FPS** vs PGO/LTO off **45.75 FPS**: **+9.5%**. p50/p99 **20.30/24.05 ms** vs **21.90/27.90 ms**. | Strongest isolated compiler result in the final tests. Source and scene were held constant. |
| Same-binary core renderer ABBA; PGO/LTO off, selected depth/G-buffer/binding paths off vs on | Off run means **30.72, 31.33 FPS**; on **44.00, 47.16 FPS**. Medians **31.03 vs 45.58 FPS (+46.9%)**. Median-window p99 **36.45 vs 27.65 ms**; p50 **32.35 vs 21.40 ms**. | Promising repeated evidence for the selected core paths as a group. Only two runs per side; not a per-feature attribution and not directly comparable to the final PGO/LTO result. |
| DrawReplay stage A/B, one run per setting | `replay-core` **42.22 FPS**, p99 30.2 ms; `replay-only` **47.18 FPS**, p99 26.1 ms (**+11.7%**). | Suggestive single-run evidence for DrawReplay. Not replicated as a standalone ABBA. |
| Earlier combined before/after measurement | Old build **33.24/33.53 FPS**, p99 41.6 ms; optimized build **39.93/41.43 FPS**, p99 31.2/30.3 ms; diagnostic rollback **38.27 FPS**, p99 33.4 ms. | About +22% in the earlier two-run comparison, combining compiler and source changes. The rollback was not a pure compiler ablation. Do not substitute these values for the later four-run final ABBA. |

The final AppImage comparison is not a replication of the earlier `40.7 FPS` package label:
the exact prior AppImage measured 35.90–40.10 FPS in the later controlled ABBA (37.75 median).
Use the later interleaved result for the current source-vs-package claim; retain the older
result as an earlier measurement with different run conditions/variance, not as the baseline
for the final build.

### Frame-time reporting details

The engine emits per-window `SOTC FRAMES p50 ... p99 ...` summaries. For the final comparison,
the table above is the median across variants of each run's median over the nine settled
five-second windows. It is **not** a pooled whole-run percentile.

| Current source, 4-run group | p50 | p95 | p99 |
| --- | ---: | ---: | ---: |
| Median of per-window frame-time percentiles | **19.75 ms** | **Not available** | **23.15 ms** |

The logger does not emit p95 and the benchmark retained nine aggregate windows per run, not a
raw frame-time histogram. Do not estimate p95 from p50/p99 or from the inverse of average FPS.
Instrument and retain raw frame intervals if p95 is needed in the next comparison.

## 3. Test scene, controls, build, and verification

### Main FPS scene and harness

- Ryzen 7 5800X (8 cores / 16 threads), Radeon RX 6700 XT, RADV 26.2.6, Arch Linux,
  glibc 2.44.
- Stationary, fixed-camera view outside the Shrine of Worship; 1920×1080, FSR 3 native AA,
  AF16, motion blur off, Mailbox presentation, game FPS uncapped, frame generation off.
- Same isolated save restored before runs; scripted neutral input; physical keyboard/mouse
  blocked by a benchmark-only shim. No compilation during measured runs.
- Settled windows at game timestamps 25–65 seconds: nine approximately five-second samples
  per run. About **5,864 command-processor commands/frame** in both comparison variants.
- The final ABBA order was `new, oldapp, oldapp, new, oldapp, new, new, oldapp`. The compiler
  ablation was separately interleaved `new/dev/dev/new/dev/new/new/dev`.
- `SOTC FPS` is native/base game FPS. FSR-generated presentation rate is logged separately.

### Build configuration

- GCC **16.2.1**, `RelWithDebInfo`, upstream `-O2`, `BB_PGO=use`, `BB_LTO=ON`, two LTO jobs.
- PGO was freshly regenerated rather than mixed with the old profile: 189 `.gcda` files,
  approximately 3.5 MiB; training included startup/shrine/walk (150 s) and an FSR frame-gen
  run (120 s). This is still a narrow profile, not broad full-game training.
- The previous AppImage was also built with PGO+LTO, making the final A/B source-side rather
  than compiler-setting-only. The dev comparison used the same current source with PGO off and
  LTO off.
- AppImage was built on glibc 2.44; portability to older distributions/Steam Deck is not
  established.

### Current package and correctness checks

- Final renderer verification ran about 110 s with zero reported mismatches: 11.53 M
  DrawReplay key/hash comparisons, 17.83 M eligibility checks, 61.87 M register chunks,
  17.63 M depth-prep / 12.46 M begin-render / 14.35 M dynamic-state comparisons, 5.13 M
  G-buffer checks, and 23.63 M texture-bind-fast-path checks. All reported `bad`/difference
  counts were zero. These are verification-mode runs, not FPS samples.
- Seven native checks passed; the recorded selected Python suite ran 48 tests and passed.
- Packaged AppImage Vulkan submission and 4096-byte readback passed; GUI startup stayed
  running; packaged gameplay with FSR 3 ran at a 30 FPS target. Nine settled samples were
  **30.0 base FPS / 60.0 presentations per second**, with 1,800 generated frames logged.
- Steady-scene VRAM use was about **2.9–3.0 GiB**, with zero emergency texture collections.
  The Vulkan-reported budget moved between about 7.7 and 10.7 GiB as other GPU clients
  changed; it was not binding in these runs.
- Published build: release tag `linux-native-fps-2026.11.07`, source commit `3ceb073`,
  AppImage SHA-256 `66aa48b707d8cbb7af3283dba04f5c0286d5f53ee23af643f5a37440fba74c13`.

## 4. Failed, regressed, or inconclusive experiments

| Experiment | Observation | Decision |
| --- | --- | --- |
| `SOTC_FLIP_MAX_HOLD_US=1000` | One exploratory run looked ~7% faster, but the later ABBA gave old pacing **46.50 FPS** (3 valid runs) vs hold-1000 **41.45 FPS** (4 runs), with p99 **27.10 vs 32.85 ms**. One old-pacer run had no settled windows and was excluded. | Rejected as default; contradictory initial signal did not replicate. |
| Physical-core affinity (`all` vs CPUs 0–7) | Early single runs: 44.54 vs 47.31 FPS looked positive. Eight-run affinity ABBA: all-CPU group median **45.60**, physical-only **44.70 FPS**. | No repeatable gain; do not enable affinity by default. |
| `SOTC_CACHE_WIDE=1`, ahead-page sizes 63/255, `SOTC_RB_STREAM=1`, hold-0 and wall-limit-60 | One-run medians were respectively 44.0, 47.1, 43.1, 46.0, 46.2 and 46.3 FPS against a ~45.6 FPS one-run default. | Exploratory only, no robust winner; remain opt-in. |
| Draw fixed-cost reuse experiment | One run measured 43.56 FPS vs 44.54 for the tuning reference. | No gain; broad fixed-cost path remains disabled. |
| Disable hot-readback optimization | One run fell to **37.58 FPS**, p99 36.5 ms vs tuning reference 44.54 FPS, p99 27.1 ms. | Regression; preserve hot-readback path. |
| Wall-deadline limiter | At 30 FPS, grid and wall modes both reached 30.0 FPS with about 33.3 ms p50 / 33.5 ms p99. A single 45-FPS test was noisy (wall ~43.45 median vs grid ~45.85). | Correct at the 30-FPS target; no performance win and no repeated 45-FPS evidence. Keep opt-in. |
| Coarse/fine pipeline memo and command-processor fast paths | Single-stage results: coarse 46.59 FPS, fine 46.20; command fast paths showed no clear additional gain. | Coarse memo remains opt-in; bit-20 command fast path stays off by default. |

Other caveats:

- One early pacer trial stalled after its initial report. It did not reproduce in verifier or
  later clean runs; cause remains unknown. Do not silently count it as a valid benchmark.
- A `tune-spin0` run produced a watchdog stack dump **after** its measurement. This was not
  established as a gameplay crash. Benchmark harness stops by external deadline; those
  intentional SIGTERM exits are not crashes. A prior server restart also interrupted one
  final-ABBA trial; it was rerun cleanly.
- No confirmed source correctness mismatch remains in the checked paths. “Verified” applies
  only to the listed checks and scene; it is not a claim of full-game stability.

## 5. Bottleneck evidence: measured vs hypothesized

### Measured signals

- Final source reports about **15.79 ms/frame draw-busy time** (median across the four run
  logs) and **11.23 ms/frame command-parse time**; prior package reports about 20.34 and
  11.47 ms/frame respectively. These are `SOTC threads per frame` counters. Draw-busy is
  elapsed interval minus measured draw-idle, **not direct per-thread CPU-cycle time**; parse
  timing is also instrumented. Treat them as host-side activity proxies, not proof of CPU
  saturation. They do show the draw path became materially shorter while parse stayed similar.
- `SOTC CP waits for the draw thread` was around 0–1 waits/s in the final logs, not a sustained
  queue-starvation signal.
- In settled current-source windows, shader/pipeline compilation and synchronous readback
  costs were near zero; reported readback overhead was typically **1–3 ms/s**. The earlier
  before/after build comparison showed synchronous draw-thread readback telemetry fall from
  about 71 to 2 ms/s, but that does not mean all GPU waits vanished.
- Current command counts stayed effectively constant, and VRAM use remained far below the
  reported budget with no emergency collections. CPU submission/renderer work, not VRAM
  capacity, is the stronger measured target in this scene.
- An older instrumented profile found roughly 12 ms/frame of inclusive binding work
  (buffers ~4.5 ms, textures ~5 ms) and ~11 ms/frame indirect-operation time. The profile
  had zero-valued thread CPU columns and overlapping inclusive timers; it is a lead for
  profiling, not a current additive cost model.

### Not established

- No GPU utilization counters or reliable per-frame GPU execution timestamps were captured
  for the final ABBA. The logs cannot establish whether the RX 6700 XT is GPU-bound, nor rule
  out GPU limits after CPU work is reduced.
- The “draw busy” metric is not a CPU-clock measurement; the opt-in draw-split CPU report had
  earlier zero counters. Revalidate the Linux thread CPU-clock instrumentation before using it
  to claim a specific CPU bottleneck.
- FSR 3 presentation rate is not native FPS and cannot be used to infer input/simulation
  latency or GPU headroom.

**Working hypothesis:** the remaining repeatable ceiling is primarily host draw-thread/render
state/binding work, with command parsing also significant. This is supported by the active-time
proxy and the historical binding profile, but GPU-bound status remains unknown. Treat this as
a hypothesis until CPU-clock and GPU-time measurements agree.

## 6. Outstanding work, bugs, and unverified changes

- No p95 data or retained raw frame-time distribution. Add a ring-buffer/histogram or export
  raw frame intervals, then compute p50/p95/p99 over the same settled windows.
- No colossus/boss, long-session, scene-transition, save-load stress, or full-game completion
  coverage for the final package. #349 partial readback retries and #313 stale-read-fault
  cleanup need extended correctness/stability coverage in streaming/boss workloads.
- #308 byte-range readback intervals are not ported; compare selectively with the current
  #349 implementation only with memory-ordering tests and telemetry.
- #303's inline descriptor storage / larger slot cache and related allocation paths remain
  absent. The older profile suggests bindings are worth revisiting, but current costs need a
  fresh profile first.
- Several capacity/pacing experiments remain opt-in and lack replicated ABBA evidence:
  wide cache, ahead-page variants, readback streaming, wall limiter, coarse pipeline memo.
- Fresh PGO profile covered a short training set, not all traversal/combat scenes. Broader
  representative PGO training may improve robustness, but the +9.5% compiler gain is the only
  measured claim.
- AppImage portability is limited to glibc 2.44+ until rebuilt and tested on an older target.
- First PGO/LTO AppImage was tested with FG30; do not assume 60-target pacing or performance
  on the current package without a new test. Do not promise 60 native FPS from the 60
  presentations/s FG result.

## 7. GitHub, local-only artifacts, and working-tree state

- `1fe46aa` is the upstream v0.72.2 base.
- `a54a8c1` (`WIP: checkpoint native Linux port and FPS optimisations`) is the main native
  port/optimization commit, 67 files changed, and is on GitHub `origin/linux-native-fps`.
- `3ceb073` (`Point Linux branch to backed-up FSR submodule`) is the branch tip and is also
  on `origin/linux-native-fps`. Local `perf-pacer-eval` and remote `linux-native-fps` point to
  the same commit. `origin/main` remains at `1fe46aa`.
- Public prerelease `linux-native-fps-2026.11.07` points at `3ceb073` and carries the
  validated AppImage plus checksum sidecar:
  https://github.com/alexmason90/ShadowOfTheColossus-PC/releases/tag/linux-native-fps-2026.11.07
- The AppImage is a release asset, **not a Git blob**. The generated source tarball, patch,
  updated `outputs/` report files, raw benchmark logs, PGO profile, built libraries, backups,
  and experiment scripts remain local workspace artifacts unless separately committed or
  attached. The source repository was clean before this handoff was added.
- This handoff is intentionally not committed or pushed. After creation, expect only
  `?? docs/performance/LUNA-HANDOFF.md` in Git status; there should be no modified source.

## 8. Five recommended next steps, ranked

1. **Measure actual host CPU vs GPU time before choosing the next code path.** Fix/validate
   Linux per-thread CPU-clock reporting and add Vulkan GPU timestamp/utilization sampling to
   the same fixed scene. Current draw-busy (~15.8 ms/frame) is a strong clue but not a CPU
   clock; GPU saturation is unknown. Highest information value, no direct FPS gain by itself.
2. **If host draw work is limiting, isolate the remaining binding/descriptor cost and test
   #303-sized pieces.** Use a fresh current-build profile, then small opt-in changes and a
   minimum 4-run interleaved A/B with image/correctness checks. The historical ~12 ms/frame
   binding profile and core-cache ABBA make this the strongest unimplemented draw-side lead;
   expected benefit is moderate but unmeasured.
3. **Repeat a clean DrawReplay/depth/G-buffer subfeature ablation on the final PGO build.**
   Existing core ABBA and one-run DrawReplay tests are positive but do not isolate every path
   or prove behavior in other scenes. Record base FPS, percentiles, thread activity and
   GPU time; retain only repeatable wins.
4. **Train a broader PGO profile and repeat the same-source compiler ABBA.** Current +9.5%
   PGO/LTO benefit is robust, while the training set is narrow. Add representative traversal
   and combat only after correctness/stability coverage; expected upside is low-to-moderate,
   with less code risk than new fast paths.
5. **Stress #349/#313, then evaluate #308 byte-range readbacks for tail latency.** Current
   readback time is already low in the steady shrine scene, so average-FPS upside is
   uncertain; the likely value is fewer stale retries/hitches in streaming and boss scenes.
   This is higher correctness risk and should not ship as a default without stress tests.

## 9. Reproduction/reference artifacts

Primary local records (paths relative to the workspace):

- `work/optimization/final01-new.log` … `final08-oldapp.log` and
  `work/renderer-port/final-abba-status.log` — final source-vs-package ABBA.
- `work/optimization/compiler01-new.log` … `compiler08-dev.log` and
  `work/renderer-port/compiler-abba-status.log` — same-source PGO/LTO ablation.
- `work/renderer-port/final-summary.py` — settled-window summary; `final-summary.py final`
  and `final-summary.py compiler` reproduce the medians.
- `work/renderer-port/core-results.json`, `work/optimization/replay-*.log`,
  `work/optimization/tune-*.log`, `work/optimization/abba-*.log`, and
  `work/optimization/aff*.log` — earlier feature/tuning experiments.
- `work/optimization/final-verify.log`, `work/renderer-port/final-native-test.log`,
  `work/renderer-port/pgo-final-build.log`, and the source commit history — correctness,
  build, and implementation record.
- `outputs/PERFORMANCE.md`, `outputs/benchmark-results.json`, and
  `outputs/SHA256SUMS` — user-facing summary, machine-readable results, and artifact hashes.

Do not use early single-run FPS values as final claims when a later ABBA exists. Preserve the
current AppImage and saved data; never include game content, firmware, or private filesystem
paths in commits, reports, or release assets.
