#include "platform.h"

#ifdef _WIN32

    #include <windows.h>

[[gnu::dllimport]] BOOL WINAPI ProcessPrng(PBYTE pbData, SIZE_T cbData);

int fill_random(unsigned char *buf, const Py_ssize_t len) {
    return ProcessPrng((PBYTE)buf, (SIZE_T)len) ? 0 : -1;
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
