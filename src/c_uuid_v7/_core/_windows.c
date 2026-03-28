#ifdef _WIN32

#include "_platform.h"

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

uint64_t epoch_base_ms = 0;
uint64_t tick_base_ms = 0;
#if PY_VERSION_HEX < 0x030E0000
VOID(WINAPI *query_interrupt_time_ptr)(PULONGLONG) = NULL;
#endif

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
    epoch_base_ms = system_ms();
#if PY_VERSION_HEX >= 0x030E0000
    /* Python 3.14+ requires Windows 10+, where QueryInterruptTime is available. */
    {
        ULONGLONG interrupt_time = 0;

        QueryInterruptTime(&interrupt_time);
        tick_base_ms = interrupt_time / 10000ULL;
    }
#else
    /* Python 3.10-3.13 still support Windows 8.1, so QueryInterruptTime may be absent. */
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");

    query_interrupt_time_ptr = NULL;
    if (kernel32 != NULL) {
        query_interrupt_time_ptr =
            (VOID(WINAPI *)(PULONGLONG))GetProcAddress(kernel32, "QueryInterruptTime");
    }

    if (query_interrupt_time_ptr != NULL) {
        ULONGLONG interrupt_time = 0;

        query_interrupt_time_ptr(&interrupt_time);
        tick_base_ms = interrupt_time / 10000ULL;
        return;
    }

    tick_base_ms = GetTickCount64();
#endif
}

#endif
