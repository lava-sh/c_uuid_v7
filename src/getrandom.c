#include "platform.h"

#ifdef _WIN32

    #if _WIN32_WINNT >= 0x0A00

[[gnu::dllimport]]
BOOL WINAPI ProcessPrng(PBYTE pbData, SIZE_T cbData);

int fill_random(unsigned char *buf, const Py_ssize_t len) {
    return ProcessPrng((PBYTE)buf, (SIZE_T)len) ? 0 : -1;
}

    #else

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

    #endif

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
