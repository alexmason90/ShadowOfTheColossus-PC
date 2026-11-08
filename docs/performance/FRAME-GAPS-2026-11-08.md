# Where the frame waits — 8 November 2026

Renderer `17df53f2…` (c58094f + 706e8ac, fresh profile), 1080p B-01 Shrine, peak clocks.

## Breakdown (`SOTC_DRAW_SPLIT=1 SOTC_CMD_STATS=1`, counting only)

Per frame at ~57 FPS:

- Draw thread: 16.2 ms CPU, of which idle 1.5 ms and idle spin 0.7 ms. Its own waits (GPU,
  submit, recorder, frame) are all 0.0 ms. Binds 9.5 ms (buffer obtain 3.7, clamp 0.5, texture
  descriptors 0.5), descriptor push 0.4, `vkQueueSubmit` 0.6, guest readback begin 0.2 + apply 0.5.
- Command processor 15.3 ms CPU, recorder 16.1 ms (mostly its empty-queue spin). The command
  processor waited for the draw thread 0–1 times per second; the draw queue peaked at its 512 limit.
- Texture cache lock: 15,570 holds per frame, 0 contended.
- **Guest readbacks: ~14 per frame, 6.7 ms of guest-thread waiting per frame** (summed over
  threads): GPU 4.7, apply round trip 1.1, queue 0.6, submit 0.3.

The game's frame work runs on its 4 `BPE JobWorkerThread`s (86–87% CPU each); the main and
"Shadow Game Update" threads use ~9% each. The readbacks are the job workers reading the
visibility bitmap that compute shaders rewrite every frame (ROADMAP T48: eboot 0x1a6d60; 84% of
the GPU wait is for data written in the current frame).

## What was ruled out

- **Wake-up latency of the GPU wait.** `SOTC_WAIT_SPIN_US=1000` (poll 1 ms before the kernel
  wait), 8 runs A-B-B-A + B-A-A-B, all gates passed: 56.9 vs 57.9 FPS with the default, workers
  +0.3 core. Not a gain; stays off.
- **Periodic submits / readbacks ahead.** Already on by default for AMD (`FlushPeriodic`,
  `SOTC_RB_AHEAD`), as is the early submit after a hot write (`SOTC_RB_EARLY`).
- **Applying readbacks on the guest thread** (to drop the 1.1 ms round trip). `FinishAsyncReadback`
  changes buffer cache state the draw thread owns without locks (in-flight list, staging pool,
  GPU-modified ranges, page protection); not safe to move.
- **`SOTC_VIS_DIRECT`** (no readback for the bitmap): ROADMAP T51 marks it "do not use", it
  crashed at the end of the game because early end-of-pipe labels let the game free memory the GPU
  still writes. Re-enabling it needs every label after a direct dispatch held until that dispatch
  is done, and a test of the full ending, which the Shrine save cannot cover.

## Conclusion

The idle time is not one avoidable stall. The draw thread is busy ~85–95% of every frame and has no
wait of its own; the remaining gap is the per-frame CPU → GPU → CPU dependency of the visibility
readbacks, which on PS4 is a short unified-memory read. Run-to-run spread (56.0–60.6 FPS for the
same build in this batch) is still larger than any single remaining item.
