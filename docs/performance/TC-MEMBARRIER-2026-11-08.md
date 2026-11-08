# Texture cache lock: membarrier on Linux — 8 November 2026

## Finding

At the 1080p B-01 Shrine the draw thread (`shadPS4:GpuDraw`) runs at 92–94% of a core inside the
measured window, the only renderer thread that is close to saturated (per-thread CPU from
`/proc`, no profiler attached). In the peak-clock `perf` profile, 12% of its samples are
`Rasterizer::BindTextures` self time, and 73% of those land on one instruction right after the
return from `TextureCache::BatchLock()` (~9% of the thread). `cycles:u` sampling is not precise on
this CPU (precise sampling needs a system setting), so the cost is attributed a few instructions
late; the candidate is the `lock orq` full fence in `ReentrantMutex::FastTryAcquire`.

The texture cache lock is biased to the thread that does the binding passes (the draw thread).
On Windows its fast path is a plain store and load: the other threads run
`FlushProcessWriteBuffers` instead. On Linux the fast path kept a full fence, which drains the
draw thread's store buffer on each of its ~15,000 batch holds per frame.

## Change

`ReentrantMutex::AsymmetricBarrier` uses `membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED)` on Linux
(registered once; Linux 4.14+), the same guarantee as `FlushProcessWriteBuffers`, and the fast path
then needs only a compiler fence. Without membarrier support, or with `SOTC_TC_MEMBARRIER=0`, both
sides keep the full fence. The decision is made once and read by both sides. The slow side (guest
write faults, ~200 per frame) pays a system call instead.

`tests/test_texture_cache_lock.cpp` (`texture-cache-lock-test`): a biased owner doing 400,000
re-entrant passes against 3 threads using `lock()` and `try_lock()` on the slow path; a
non-atomic counter and an occupancy check catch any overlap. Passes in both modes, repeatedly.

## Result: +1.3 FPS (+2.3%), all four pairs positive

One renderer build for both arms (`71341fd2…`, GCC PGO+LTO with the reference profile renamed
to the new build directory; the header change shifts source locations, so 628 functions, including
the texture cache's, lost their profile and absolute FPS is below the reference build). Arms
differ only in `SOTC_TC_MEMBARRIER`. Peak clocks, CPU cooled below 50 °C before every run, fresh
saves and caches, base probe `b93e24bc…`. All 8 correctness gates passed.

| Run | Arm | Avg / min FPS | GpuDraw CPU |
|---|---|---:|---:|
| 01 | fence | 55.4 / 53.1 | 94% |
| 02 | membarrier | 57.1 / 55.4 | 92% |
| 03 | membarrier | 56.8 / 54.8 | 92% |
| 04 | fence | 55.6 / 54.4 | 93% |
| 05 | membarrier | 60.5 / 57.6 | 92% |
| 06 | fence | 58.9 / 55.6 | 92% |
| 07 | fence | 54.7 / 53.6 | 93% |
| 08 | membarrier | 55.4 / 54.2 | 92% |

Membarrier 57.5 vs fence 56.2 FPS. Adjacent pairs: +1.7, +1.2, +1.6, +0.7. Run-to-run spread
(54.7–60.5) is still larger than the effect, so the size is approximate; the direction held in
every pair.
