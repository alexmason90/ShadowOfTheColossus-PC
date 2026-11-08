// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <setjmp.h>
#ifdef _WIN32
typedef unsigned long long sigjmp_buf[32]; // as gpu/shim/bbport_toggles.h
#endif
// Renderer tests have no guest process. Clock/host-thread services work; guest accesses abort.
extern "C" {
thread_local sigjmp_buf* runtime_fault_recover = nullptr;
uint32_t runtime_disabled_optimizations = 0;
uint64_t runtime_tsc_frequency() { return 1000000000; }
int runtime_file_translate(const char*, char*, size_t) { std::abort(); }
uint64_t runtime_memory_clamp(uintptr_t, uint64_t) { std::abort(); }
uint64_t runtime_process_time_us() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int runtime_memory_region(uintptr_t, uintptr_t*, uintptr_t*, int*) { std::abort(); }
void runtime_memory_set_gpu_hooks(void (*)(uintptr_t, uint64_t),
    void (*)(uintptr_t, uint64_t), void (*)(uintptr_t, uint64_t)) { std::abort(); }
void runtime_thread_attach_host(const char*) {}
void runtime_memory_gpu_protect(uintptr_t, uint64_t, int, int) { std::abort(); }
void runtime_restart() { std::abort(); }
uint64_t runtime_process_time_counter() { return runtime_process_time_us() * 1000; }
int32_t* runtime_errno() { std::abort(); }
int runtime_memory_write_backing(uintptr_t, const void*, uint64_t) { std::abort(); }

// Offline shader/renderer tests link the production GPU DSO but do not load a guest runtime.
// Guest-memory services remain fail-fast; optional telemetry and host-only controls are inert.
uint64_t runtime_memory_generation() { return 1; }
const unsigned char* runtime_memory_backing_pointer(uintptr_t) { return nullptr; }
const unsigned char* runtime_memory_backing_view(uint64_t* size) {
    if (size) *size = 0;
    return nullptr;
}
int runtime_memory_copy_out(uintptr_t, unsigned char*, uint64_t) { std::abort(); }
int runtime_memory_is_mapped(uintptr_t, uint64_t) { std::abort(); }
void runtime_memory_describe(uintptr_t, char* out, size_t capacity) {
    if (capacity) std::snprintf(out, capacity, "no guest memory in offline test");
}
void runtime_memory_cache_stats(uint64_t* misses, uint64_t* stale, uint64_t* fast) {
    *misses = *stale = *fast = 0;
}
uint64_t runtime_memory_clamp_gap_hits() { return 0; }
uint64_t runtime_memory_carve_splits() { return 0; }
void runtime_memory_carve_stats(uint64_t* mib, uint64_t* runs, double* split_ms,
                                double* worst_ms, double* other_ms) {
    *mib = *runs = 0;
    *split_ms = *worst_ms = *other_ms = 0;
}
void runtime_memory_lock_sites(char* readers, char* writers, size_t capacity) {
    if (capacity) readers[0] = writers[0] = '\0';
}
void runtime_memory_lock_stats(uint64_t* writes, double* wait_ms, double* hold_ms,
                               double* worst_ms) {
    *writes = 0;
    *wait_ms = *hold_ms = *worst_ms = 0;
}
void runtime_memory_lock_stats2(double* worst_hold_ms, uint64_t* read_waits,
                                double* read_wait_ms) {
    *worst_hold_ms = *read_wait_ms = 0;
    *read_waits = 0;
}
uint64_t runtime_file_read_ns_take() { return 0; }
uint64_t runtime_read_tsc() { return runtime_process_time_counter(); }
uintptr_t runtime_sotc_heap() { return 0; }
uintptr_t runtime_sotc_heap_table() { return 0; }
int runtime_sotc_neo() { return 0; }
int runtime_savedata_mounted() { return 0; }
void runtime_audio_set_volume_percent(int) {}
int runtime_audio_volume_percent() { return 0; }
int runtime_rumble_enabled() { return 0; }
void runtime_rumble_set(int) {}
void runtime_pool_release_dump(uintptr_t, uintptr_t) {}
void runtime_write_fault_dump(uintptr_t, uint64_t) {}
void sotc_event(const char*, const char*, ...) {}
}
