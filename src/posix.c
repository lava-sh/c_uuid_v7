#ifndef _WIN32

    #include "platform.h"

    #include <fcntl.h>
    #include <time.h>
    #include <unistd.h>

    #if defined(__linux__)
        #include <sys/random.h>
    #endif

uint64_t now_ms(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 0};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1'000ULL + (uint64_t)ts.tv_nsec / 1'000'000ULL;
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
    if (fd < 0) {
        return -1;
    }

    offset = 0;
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
