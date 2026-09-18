/* Platform abstraction. Desktop and Android each provide an implementation.
 * The engine and renderer call only through this. */
#ifndef HTA_PLATFORM_H
#define HTA_PLATFORM_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void     hta_log(const char *fmt, ...);
double   hta_time_seconds(void);

/* Stage-2 probe (investigation §5.1): can we map Halo's tag cache at its
 * hardcoded 32-bit base address 0x40440000 inside a 64-bit process?
 * Returns true if a MAP_FIXED mapping at that exact address succeeded. */
bool     hta_probe_fixed_map(uint64_t addr, size_t len);

#endif
