#ifdef _WIN32

// clang-format off
    #include "platform.h"

    #pragma comment(lib, "advapi32.lib")
// clang-format on

uint64_t now_ms(void) {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    return (((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime) / 10'000ULL - 11'644'473'600'000ULL;
}

BOOLEAN WINAPI SystemFunction036(PVOID, ULONG);

int fill_random(unsigned char *buf, const Py_ssize_t len) {
    return SystemFunction036(buf, (ULONG)len) ? 0 : -1;
}

#endif
