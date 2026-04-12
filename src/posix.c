#ifndef _WIN32

// clang-format off
#include "platform.h"

#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
    #include <sys/random.h>
#endif
// clang-format on

uint64_t system_ms(void) {
    #if defined(CLOCK_REALTIME)
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
    }
    #endif

    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
}

int fill_random(unsigned char *buf, const Py_ssize_t len) {
    Py_ssize_t offset = 0;

    #if defined(__linux__)
    while (offset < len) {
        const ssize_t rc = getrandom(buf + offset, (size_t)(len - offset), 0);
        if (rc < 0) {
            break;
        }
        offset += (Py_ssize_t)rc;
    }
    if (offset == len) {
        return 0;
    }
    #endif

    const int fd = open("/dev/urandom", O_RDONLY);
    offset = 0;

    if (fd < 0) {
        return -1;
    }

    while (offset < len) {
        const ssize_t rc = read(fd, buf + offset, (size_t)(len - offset));
        if (rc <= 0) {
            close(fd);
            return -1;
        }
        offset += (Py_ssize_t)rc;
    }

    close(fd);
    return 0;
}

#endif
