#ifndef _WIN32

    #include "platform.h"

    #include <time.h>

uint64_t now_ms(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 0};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1'000ULL + (uint64_t)ts.tv_nsec / 1'000'000ULL;
}

#endif
