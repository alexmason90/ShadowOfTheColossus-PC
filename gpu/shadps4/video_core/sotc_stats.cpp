// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: thread CPU time for the SOTC_DRAW_SPLIT lines (sotc_stats.h).
#include "video_core/sotc_stats.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <pthread.h>
#include <time.h>
#include <sys/resource.h>
#endif

namespace SotcStats {

#ifdef _WIN32
static std::atomic<HANDLE> split_threads[SplitThreads]{};

void RegisterSplitThread(SplitThread which) {
    if (!kDrawSplit) {
        return;
    }
    HANDLE handle{};
    if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle,
                        THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0)) {
        split_threads[which].store(handle, std::memory_order_release);
    }
}

uint64_t SplitThreadCycles(SplitThread which) {
    // The thread cycle time counts reference (TSC) ticks on current Windows, the unit of Ticks().
    const HANDLE handle = split_threads[which].load(std::memory_order_acquire);
    ULONG64 cycles = 0;
    if (!handle || !QueryThreadCycleTime(handle, &cycles)) {
        return 0;
    }
    return cycles;
}

uint64_t SplitProcessFaults() {
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    return K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))
               ? counters.PageFaultCount
               : 0;
}
#elif defined(__linux__)
static std::atomic<clockid_t> split_clocks[SplitThreads]{};
void RegisterSplitThread(SplitThread which) {
    if (!kDrawSplit) return;
    clockid_t id{};
    if (pthread_getcpuclockid(pthread_self(), &id) == 0)
        split_clocks[which].store(id, std::memory_order_release);
}
uint64_t SplitThreadCycles(SplitThread which) {
    const clockid_t id = split_clocks[which].load(std::memory_order_acquire);
    timespec time{};
    if (!id || clock_gettime(id, &time) != 0) return 0;
    const double ms = double(time.tv_sec) * 1000.0 + double(time.tv_nsec) / 1e6;
    return static_cast<uint64_t>(ms * TicksPerMs());
}
uint64_t SplitProcessFaults() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    return uint64_t(usage.ru_minflt) + uint64_t(usage.ru_majflt);
}
#else
void RegisterSplitThread(SplitThread) {}

uint64_t SplitThreadCycles(SplitThread) {
    return 0;
}

uint64_t SplitProcessFaults() {
    return 0;
}
#endif

void LogSystemMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        std::fprintf(stderr,
                     "SOTC system memory: commit %llu of %llu MiB in use, RAM %llu of %llu MiB free\n",
                     static_cast<unsigned long long>((status.ullTotalPageFile - status.ullAvailPageFile) >> 20),
                     static_cast<unsigned long long>(status.ullTotalPageFile >> 20),
                     static_cast<unsigned long long>(status.ullAvailPhys >> 20),
                     static_cast<unsigned long long>(status.ullTotalPhys >> 20));
    }
#endif
}

} // namespace SotcStats
