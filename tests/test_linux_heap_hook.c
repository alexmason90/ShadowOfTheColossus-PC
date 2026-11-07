#define _GNU_SOURCE
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
static unsigned char *image;
static size_t page_size;
static const unsigned char *runtime_memory_backing_pointer(uintptr_t address) { (void)address; return NULL; }
#include "heap_hook_linux.h"
static void trap(int sig, siginfo_t *info, void *context) {
    if (sig != SIGTRAP || !linux_heap_trap(info, context)) _exit(99);
}
int main(void) {
    page_size=(size_t)sysconf(_SC_PAGESIZE);
    image=mmap(NULL,0x50000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(image!=MAP_FAILED);
    const unsigned char function[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x5e,0x41,0x5f,0xc9,0xc3};
    memcpy(image+LINUX_HEAP_FREE_OFFSET,function,sizeof function);
    setenv("SOTC_HEAP_GUARD","0",1);
    install_heap_hook();
    assert(image[LINUX_HEAP_FREE_OFFSET]==0x55);
    unsetenv("SOTC_HEAP_GUARD");
    image[LINUX_HEAP_FREE_OFFSET]=0x90;
    install_heap_hook();
    assert(image[LINUX_HEAP_FREE_OFFSET]==0x90);
    image[LINUX_HEAP_FREE_OFFSET]=0x55;
    unsigned char *heap=image+0x1000;
    uint64_t size=0x2000000;
    memcpy(heap+0x30,&size,sizeof size);
    struct sigaction action={0};
    action.sa_sigaction=trap;
    action.sa_flags=SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    assert(!sigaction(SIGTRAP,&action,NULL));
    install_heap_hook();
    assert(!mprotect(image,0x50000,PROT_READ|PROT_EXEC));
    void (*entry)(void *)=(void (*)(void *))(image+LINUX_HEAP_FREE_OFFSET);
    entry(heap);
    assert(image[LINUX_HEAP_FREE_OFFSET]==0xcc && !linux_heap_rearm);
    for (unsigned i=1; i<2100; ++i) entry(heap);
    assert(linux_heap_calls==2000);
    assert(image[LINUX_HEAP_FREE_OFFSET]==0x55);
    assert(runtime_sotc_heap()==(uintptr_t)heap);
    assert(runtime_sotc_heap_table()==(uintptr_t)(image+LINUX_HEAP_TABLE_OFFSET));
    puts("PASS: Linux heap discovery, breakpoint rearm and removal, disabled and unknown code");
    munmap(image,0x50000);
    return 0;
}
