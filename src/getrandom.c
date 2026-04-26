#include "platform.h"

#ifdef _WIN32

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

int fill_random(unsigned char *buf, const Py_ssize_t len) {
    arc4random_buf(buf, (size_t)len);
    return 0;
}

#else

    #include <errno.h>
    #include <sys/random.h>

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
