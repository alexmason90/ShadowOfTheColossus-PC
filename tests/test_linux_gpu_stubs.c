/* GPU library's loader hooks are absent in runtime unit-test executables. */
#include <stdint.h>
uintptr_t runtime_sotc_heap(void) { return 0; }
uintptr_t runtime_sotc_heap_table(void) { return 0; }
