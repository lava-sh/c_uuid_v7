#ifdef _WIN32

    #include "platform.h"

    #include <windows.h>

uint64_t now_ms(void) {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    return (((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime) / 10'000ULL - 11'644'473'600'000ULL;
}

#endif
