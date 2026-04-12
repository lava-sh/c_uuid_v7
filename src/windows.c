#ifdef _WIN32

// clang-format off
#include "platform.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "Mincore.lib")
// clang-format on

uint64_t epoch_base_ms = 0;
uint64_t tick_base_ms = 0;

uint64_t system_ms(void) {
    FILETIME ft;
    ULARGE_INTEGER ticks;

    GetSystemTimePreciseAsFileTime(&ft);
    ticks.QuadPart = (uint64_t)ft.dwHighDateTime << 32 | (uint64_t)ft.dwLowDateTime;
    return (ticks.QuadPart - 116444736000000000ULL) / 10000ULL;
}

// RtlGenRandom (advapi32.dll), exported as SystemFunction036
BOOLEAN WINAPI SystemFunction036(PVOID, ULONG);

int fill_random(unsigned char *buf, const Py_ssize_t len) {
    return SystemFunction036(buf, (ULONG)len) ? 0 : -1;
}

void platform_seeded(void) {
    ULONGLONG interrupt_time = 0;

    epoch_base_ms = system_ms();
    QueryInterruptTime(&interrupt_time);
    tick_base_ms = interrupt_time / 10000ULL;
}

#endif
