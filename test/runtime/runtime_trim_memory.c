/* Direct runtime checks: actually decommit unused Windows pages, keep live
   partial pages and other stacks, and recommit through the fault handler.
   This runs beside the Goose fixture in both CI profiles. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#define GS_NEED_THREADS 1
#define GS_STACK_RESERVE (64u << 20)
#define GS_STACK_GAP (64u << 10)
#include "../../src/runtime/runtime.h"
#include "../../src/runtime/runtime_impl.h"
#include "../../src/runtime/runtime_threads.h"

static void check(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "trim memory check failed: %s\n", what); exit(2); }
}

int main(int argc, char **argv) {
    gs_rt_init(argc, argv, 2, 3, 0);
    uint8_t *base = GS(0)->top;
    uint8_t *other = GS(1)->top;
    gs_stack global; gs_stack_init(&global);
    uint8_t *global_base = global.top;
    global.top += 8197; memset(global_base, 37, 8197);
    GS(1)->top += 8197; memset(other, 61, 8197);
    size_t keeps[] = { 0, 1, 4095, 4096, 4097, 8197 };
    for (size_t k = 0; k < sizeof keeps / sizeof *keeps; k++) {
        size_t count = 16u << 20, keep = keeps[k];
        GS(0)->top = base + count;
        memset(base, 85, count);
        check(gs_trim_memory(), "trim with a live large value");
        check(base[0] == 85 && base[count - 1] == 85, "large live value survives");
        GS(0)->top = base + keep;
        check(gs_trim_memory(), "trim after shrink");
        check(gs_trim_memory(), "repeat trim is harmless");
#ifdef _WIN32
        MEMORY_BASIC_INFORMATION info;
        size_t start = (keep + gs_page_size - 1) / gs_page_size * gs_page_size;
        check(VirtualQuery(base + start, &info, sizeof info) != 0, "query discarded page");
        check(info.State == MEM_RESERVE, "unused page really decommitted");
        check(VirtualQuery(base + GS_STACK_RESERVE, &info, sizeof info) != 0 &&
              info.State == MEM_RESERVE, "guard gap remains uncommitted");
#endif
        for (size_t i = 0; i < keep; i++) check(base[i] == 85, "live prefix survives");
        for (size_t i = 0; i < 8197; i++) {
            check(other[i] == 61, "other local stack survives");
            check(global_base[i] == 37, "global stack survives");
        }
        GS(0)->top = base + count;
        memset(base + keep, 93, count - keep);
        for (size_t i = keep; i < count; i++) check(base[i] == 93, "regrowth recommits");
    }
    /* As a thread program's end does: its stack block, then its regions. */
    gs_thread_release();
    gs_release_regions();
    puts("runtime trim memory ok");
    return 0;
}
