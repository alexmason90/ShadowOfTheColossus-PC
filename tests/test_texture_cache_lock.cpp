// SPDX-License-Identifier: GPL-2.0-or-later
// Mutual exclusion of the texture cache lock (VideoCore::ReentrantMutex): one biased owner thread
// (the draw thread in the game) holding it in short re-entrant passes, and other threads (guest
// write faults) taking the announcing slow path, with both lock() and try_lock(). A non-atomic
// counter is only exact if no two threads ever hold the lock at once. Run it with the default
// settings (membarrier on Linux: the owner skips its fence) and with SOTC_TC_MEMBARRIER=0.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "video_core/texture_cache/texture_cache.h"

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);          \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

VideoCore::ReentrantMutex mutex;
volatile unsigned long counter;
std::atomic<int> inside{0};

void Critical() {
    CHECK(inside.fetch_add(1, std::memory_order_relaxed) == 0);
    const unsigned long v = counter;
    counter = v + 1;
    inside.fetch_sub(1, std::memory_order_relaxed);
}

} // namespace

int main() {
    constexpr int OwnerPasses = 400000;
    constexpr int OtherThreads = 3;
    constexpr int OtherIters = 20000;
    std::atomic<bool> owner_ready{false};
    unsigned long expected = 0;
    unsigned long other_tries = 0;
    std::atomic<unsigned long> try_successes{0};

    std::thread owner([&] {
        mutex.SetFastThread();
        owner_ready.store(true, std::memory_order_release);
        for (int i = 0; i < OwnerPasses; ++i) {
            std::unique_lock outer{mutex};
            CHECK(mutex.HeldByThisThread());
            Critical();
            {
                std::unique_lock inner{mutex}; // re-entrant, like FindImage inside a batch hold
                Critical();
            }
        }
    });
    while (!owner_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::vector<std::thread> others;
    for (int t = 0; t < OtherThreads; ++t) {
        others.emplace_back([&, t] {
            for (int i = 0; i < OtherIters; ++i) {
                if ((i + t) % 4 == 0) {
                    if (mutex.try_lock()) {
                        Critical();
                        mutex.unlock();
                        try_successes.fetch_add(1, std::memory_order_relaxed);
                    }
                } else {
                    std::unique_lock lk{mutex};
                    Critical();
                }
            }
        });
    }
    owner.join();
    for (auto& t : others) {
        t.join();
    }
    for (int t = 0; t < OtherThreads; ++t) {
        for (int i = 0; i < OtherIters; ++i) {
            if ((i + t) % 4 == 0) {
                ++other_tries;
            } else {
                ++expected;
            }
        }
    }
    expected += 2ul * OwnerPasses + try_successes.load();
    CHECK(counter == expected);
    CHECK(!mutex.HeldByThisThread());
    std::printf("PASS: texture cache lock, %lu holds (%lu of %lu try_locks taken)\n", expected,
                try_successes.load(), other_tries);
    return 0;
}
