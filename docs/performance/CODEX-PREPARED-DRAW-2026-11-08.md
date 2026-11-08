# Prepared-draw follow-up — 8 November 2026

One additional gameplay smoke test was run, before rebuilding or changing the tested binary.
The archived corrected harness, B-01 inputs and settings, probe (`8f248bad…`), and experimental
renderer (`7ebb6b6a…`) matched the previous two runs. The only feature-setting change from
ON+VERIFY was `SOTC_PREPARED_DRAW_VERIFY=0`; prepared draws remained enabled. Fresh isolated
saves were used in `/tmp/opencode/sotc-shrine-smoke-20261108/runs/codex-prepared-noverify`.
Measurement was 40 seconds, outer timeout 330 seconds, probe timeout zero, screenshots zero.
Recorder batching/spin/sleep remained 1/250/500. No additional gameplay runs were launched.

| Existing binary, 1080p Shrine | Average FPS | Minimum FPS | Settled samples | Commands/frame |
| --- | ---: | ---: | ---: | ---: |
| OFF (Luna) | 38.5 | 37.3 | 9 | 5,872 |
| ON+VERIFY (Luna) | 36.0 | 31.3 | 9 | 5,877 |
| ON, verification OFF (Codex) | 36.2 | 35.3 | 9 | 5,875 |

The new run had zero correctness markers or fallbacks. Its mismatch counter is **inactive**
without verification, so zero does not establish descriptor equivalence. Settled captures averaged
169,260/s; FastBind was 3,815,360 / 7,120,739 = 53.58%. These single runs provide no evidence of
an FPS gain: verification OFF is approximately 6% below OFF and approximately 0.6% above
ON+VERIFY. Run variance and temporal drift remain uncontrolled.

Raw log and summary are preserved outside the source tree in
`../performance/bottleneck-measurements/codex-prepared-noverify-20261108/`.

## Verification cost and scoped change

`SOTC_PREPARED_DRAW_VERIFY` is enabled when its first character is `1`. For every prepared
stage reaching `BindBuffers`, it walks all buffer bindings, calls `GetSharp(stage)` for guest
buffers (or constructs zero for special buffers), and compares descriptors. On the first mismatch
it increments mismatch/report counters, logs at most 20 reports, and falls back for that stage.
Successful verification then uses the snapshot in the ordinary binding loop. FastBind hits never
reach this loop. Disabling verification removes this extra decode/compare pass; snapshot capture,
layout validation, diagnostics, and resource binding still run.

The producer still captures every eligible draw even when FastBind later skips resource binding.
The new run reports roughly 0.09–0.10 us per capture, about 16 ms of instrumented producer time
per wall-second. This excludes the later layout validation and does not explain the entire FPS
difference. Copying, descriptor preparation, clocks and cross-thread atomic diagnostics remain
costs. No larger architecture or cache was added on this evidence.

The scoped optimisation validates snapshots **only when binding consumes them**. Selection now
walks the pipeline stages directly once, skipping null slots and checking active-stage bounds,
identity, order and buffer count. It avoids constructing an intermediate signature array, the
second traversal of that array, and duplicate per-stage identity/count checks in `BindBuffers`.
FastBind returns before this validation. Rejected consumed snapshots still use ordinary decoding;
verification and its mismatch fallback remain intact. Ownership stays in BackOp, and all cache,
resource and Vulkan operations remain on their existing threads. The feature remains opt-in.

`FastBind skips` now counts incoming snapshots bypassed without inspecting them; it does not
claim those unused snapshots passed validation. Fallbacks count rejected snapshots when consumed.

Expected benefit: less draw-thread validation work, especially with the observed ~54% FastBind
hit rate. This does not remove producer capture cost and is not a demonstrated FPS improvement.
The changed binary has not been run in gameplay; visual equivalence, transitions and long-session
stability remain unverified. Further gameplay benchmarks require user approval.

## Offline validation

A fresh RelWithDebInfo build with PGO off and LTO off completed in
`/tmp/codex-sotc-build-20261108`; no previous binaries were overwritten. The focused
prepared-draw test also passed with AddressSanitizer and UndefinedBehaviorSanitizer.
Tests cover opt-in gating, invalid/null snapshots, pipeline/stage/order/layout mismatches,
null pipeline slots, too many/few active stages, maximum-stage bounds, zero-buffer stages,
and descriptor value ownership across moves. Full offline test results are recorded below.

Passed from the fresh build: `pixel-format-test`, `prepared-draw-test`,
`upscaler-support-test`, and `barycentric-gs-test`. Both emitted SPIR-V modules passed
`spirv-val --target-env vulkan1.3`. `git diff --check` passed. AppImage SHA-256 remains
`66aa48b707d8cbb7af3283dba04f5c0286d5f53ee23af643f5a37440fba74c13`; the preserved
experimental renderer remains `7ebb6b6a…`. No commit or push was made.
