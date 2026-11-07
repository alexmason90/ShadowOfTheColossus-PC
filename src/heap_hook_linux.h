/* SPDX-License-Identifier: GPL-2.0-or-later
 * Linux SIGTRAP counterpart of the Windows BPE heap discovery hook in probe.c.
 * Included after image/page_size are defined. The loader installs it while the image
 * is writable; afterwards only the one code page is temporarily made writable.
 */
#define LINUX_HEAP_FREE_OFFSET 0x495e0
#define LINUX_HEAP_TABLE_OFFSET 0x2dfaab0
static unsigned char linux_heap_original;
static unsigned linux_heap_calls, linux_heap_done;
static _Thread_local int linux_heap_rearm;
static struct { uintptr_t heap; unsigned count; } linux_heaps[8];
static uintptr_t linux_heap_chosen;
static void linux_heap_write(unsigned char value) {
    unsigned char *at = image + LINUX_HEAP_FREE_OFFSET;
    void *page = (void *)((uintptr_t)at & ~(uintptr_t)(page_size - 1));
    if (!mprotect(page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        __atomic_store_n(at, value, __ATOMIC_RELEASE);
        mprotect(page, page_size, PROT_READ | PROT_EXEC);
    }
}
static void install_heap_hook(void) {
    static const unsigned char expected[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56};
    const char *value = getenv("SOTC_HEAP_GUARD");
    if ((value && value[0]=='0') || memcmp(image+LINUX_HEAP_FREE_OFFSET, expected, sizeof expected)) return;
    linux_heap_original = image[LINUX_HEAP_FREE_OFFSET];
    image[LINUX_HEAP_FREE_OFFSET] = 0xcc;
}
uintptr_t runtime_sotc_heap(void) { return __atomic_load_n(&linux_heap_chosen, __ATOMIC_ACQUIRE); }
uintptr_t runtime_sotc_heap_table(void) {
    return runtime_sotc_heap() ? (uintptr_t)(image+LINUX_HEAP_TABLE_OFFSET) : 0;
}
static int linux_heap_trap(siginfo_t *info, ucontext_t *context) {
    greg_t *regs = context->uc_mcontext.gregs;
    if (info->si_code == TRAP_TRACE) {
        if (!linux_heap_rearm) return 0;
        linux_heap_rearm = 0;
        if (!__atomic_load_n(&linux_heap_done, __ATOMIC_ACQUIRE)) linux_heap_write(0xcc);
        regs[REG_EFL] &= ~(greg_t)0x100;
        return 1;
    }
    /* INT3 saves the address after the breakpoint on Linux. */
    if (!image || (uintptr_t)regs[REG_RIP] != (uintptr_t)(image+LINUX_HEAP_FREE_OFFSET+1)) return 0;
    regs[REG_RIP]--;
    if (__atomic_load_n(image+LINUX_HEAP_FREE_OFFSET, __ATOMIC_ACQUIRE) != 0xcc) return 1;
    const uintptr_t heap = (uintptr_t)regs[REG_RDI];
    for (unsigned i=0; i<8; ++i) {
        uintptr_t seen = __atomic_load_n(&linux_heaps[i].heap, __ATOMIC_RELAXED);
        if (!seen) {
            uintptr_t empty = 0;
            __atomic_compare_exchange_n(&linux_heaps[i].heap, &empty, heap, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
            seen = __atomic_load_n(&linux_heaps[i].heap, __ATOMIC_RELAXED);
        }
        if (seen == heap) { __atomic_add_fetch(&linux_heaps[i].count, 1, __ATOMIC_RELAXED); break; }
    }
    linux_heap_write(linux_heap_original);
    if (__atomic_add_fetch(&linux_heap_calls, 1, __ATOMIC_RELAXED) >= 2000) {
        if (!__atomic_exchange_n(&linux_heap_done, 1, __ATOMIC_ACQ_REL)) {
            uintptr_t best = 0;
            unsigned best_count = 0;
            for (unsigned i=0; i<8; ++i) {
                const uintptr_t candidate = __atomic_load_n(&linux_heaps[i].heap, __ATOMIC_RELAXED);
                uint64_t size = 0;
                const unsigned char *backing = candidate ? runtime_memory_backing_pointer(candidate+0x30) : NULL;
                if (backing) memcpy(&size, backing, sizeof size);
                else if (candidate-(uintptr_t)image < 0x20000000) memcpy(&size, (void *)(candidate+0x30), sizeof size);
                const unsigned count = __atomic_load_n(&linux_heaps[i].count, __ATOMIC_RELAXED);
                if (size >= 0x1000000 && size < 0x100000000ull && count > best_count) { best=candidate; best_count=count; }
            }
            __atomic_store_n(&linux_heap_chosen, best, __ATOMIC_RELEASE);
        }
    } else {
        linux_heap_rearm = 1;
        regs[REG_EFL] |= 0x100;
    }
    return 1;
}
