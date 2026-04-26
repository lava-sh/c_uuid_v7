#include "getrandom.h"

#ifdef _WIN32

uint64_t now_ms(void) {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    return (((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime) / 10'000ULL - 11'644'473'600'000ULL;
}

[[gnu::dllimport]]
BOOLEAN WINAPI SystemFunction036(PVOID buf, ULONG len);

int fill_random(unsigned char *buf, const Py_ssize_t len) {
    Py_ssize_t offset = 0;
    while (offset < len) {
        const ULONG chunk = (len - offset) > 0x7FFFFFFF ? 0x7FFFFFFFUL : (ULONG)(len - offset);
        if (!SystemFunction036(buf + offset, chunk)) {
            return -1;
        }
        offset += (Py_ssize_t)chunk;
    }
    return 0;
}

#elif defined(__APPLE__)

    #include <stdlib.h>
    #include <time.h>

uint64_t now_ms(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 0};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1'000ULL + (uint64_t)ts.tv_nsec / 1'000'000ULL;
}

int fill_random(unsigned char *buf, const Py_ssize_t len) {
    arc4random_buf(buf, (size_t)len);
    return 0;
}

#else

    #include <errno.h>
    #include <sys/random.h>
    #include <time.h>

uint64_t now_ms(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 0};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1'000ULL + (uint64_t)ts.tv_nsec / 1'000'000ULL;
}

int fill_random(unsigned char *buf, Py_ssize_t len) {
    while (len > 0) {
        const ssize_t n = getrandom(buf, (size_t)len, 0);
        if (n > 0) {
            buf += n;
            len -= n;
        } else if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

#endif
