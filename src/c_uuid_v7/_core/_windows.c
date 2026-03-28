#ifdef _WIN32

#include "_platform.h"

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

uint64_t epoch_base_ms = 0;
uint64_t interrupt_base_ms = 0;

uint64_t system_ms(void) {
    FILETIME ft;
    ULARGE_INTEGER ticks;

    GetSystemTimePreciseAsFileTime(&ft);
    ticks.QuadPart = (uint64_t)ft.dwHighDateTime << 32 | (uint64_t)ft.dwLowDateTime;
    return (ticks.QuadPart - 116444736000000000ULL) / 10000ULL;
}

int fill_random(unsigned char *buf, const Py_ssize_t len) {
    const NTSTATUS status = BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return status >= 0 ? 0 : -1;
}

void platform_seeded(void) {
    ULONGLONG interrupt_time = 0;

    epoch_base_ms = system_ms();
    QueryUnbiasedInterruptTime(&interrupt_time);
    interrupt_base_ms = interrupt_time / 10000ULL;
}

#endif
