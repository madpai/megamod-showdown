/* Desktop/POSIX platform layer — the host-side counterpart of
 * platform_android.c. Same interface, so the engine and renderer are identical
 * on both. */
#include "platform.h"

#include <stdio.h>
#include <sys/mman.h>
#include <time.h>

void hta_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

double hta_time_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

bool hta_probe_fixed_map(uint64_t addr, size_t len)
{
    void *want = (void *)(uintptr_t)addr;
#ifdef MAP_FIXED_NOREPLACE
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE;
#else
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#endif
    void *got = mmap(want, len, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (got == MAP_FAILED) {
        hta_log("[probe] mmap at 0x%llx FAILED", (unsigned long long)addr);
        return false;
    }
    bool exact = (got == want);
    if (exact) ((volatile unsigned char *)got)[0] = 0xAB;
    hta_log("[probe] mmap at 0x%llx -> %p (%s)", (unsigned long long)addr, got,
            exact ? "EXACT" : "MOVED");
    munmap(got, len);
    return exact;
}
